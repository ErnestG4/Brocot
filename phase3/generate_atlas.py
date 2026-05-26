"""Generate the 'Atlas' factory pack — a diverse tour of FM space.

Farthest-point sampling over the coverage bank picks fingerprints that are
maximally different from each other, so the pack is a spread of distinct
timbres rather than near-duplicates. A 'press next and explore' set for
learning what the synth can do. Each preset is named by its spectral
character. Easily reverted (factory folder).

Run: python -m phase3.generate_atlas [--n 24] [--out DIR]
"""

from __future__ import annotations

import argparse
import json
from datetime import datetime, timezone
from pathlib import Path

import numpy as np

from phase3.brocot_writer import _slug, result_to_brocot_json
from phase3.coverage_bank import config_from_entry, load_bank
from phase3.family_distance import quantize_to_integer_grid
from phase3.search import SearchResult
from phase3.spectral_features import spectral_features


def farthest_point_indices(amps: np.ndarray, n: int, seed: int = 0) -> list[int]:
    """Greedy k-center: repeatedly take the row farthest (max-min L1) from the
    set chosen so far — a maximally-spread sample of the fingerprint space."""
    rng = np.random.default_rng(seed)
    first = int(rng.integers(amps.shape[0]))
    chosen = [first]
    mind = np.abs(amps - amps[first]).sum(axis=1)
    for _ in range(n - 1):
        i = int(np.argmax(mind))
        chosen.append(i)
        mind = np.minimum(mind, np.abs(amps - amps[i]).sum(axis=1))
    return chosen


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=24)
    ap.add_argument("--out", type=Path,
                    default=Path("resources/presets/factory/atlas"))
    args = ap.parse_args()

    amps, configs = load_bank()

    # Musicality pre-filter: keep configs whose effective partial count is in a
    # sane range (L1-normalised ⇒ eff = 1/Σa²), so the tour isn't all single
    # spikes or maximal noise.
    sumsq = (amps.astype(np.float64) ** 2).sum(axis=1)
    eff = 1.0 / np.maximum(sumsq, 1e-12)
    keep = np.nonzero((eff >= 2.0) & (eff <= 28.0))[0]
    pool = amps[keep]
    print(f"pool: {len(keep)}/{amps.shape[0]} configs (effective count 2-28)")

    sel_local = farthest_point_indices(pool, args.n)
    sel = [int(keep[i]) for i in sel_local]

    args.out.mkdir(parents=True, exist_ok=True)
    ts = datetime.now(timezone.utc).isoformat()
    for rank, gi in enumerate(sel, start=1):
        cfg = config_from_entry(configs[gi])
        feats = spectral_features(
            quantize_to_integer_grid(cfg.predict(), 220.0, 64))
        tags = feats.describe()
        name = f"Atlas {rank:02d} · {tags.split(' · ')[0].capitalize()}"
        res = SearchResult(config=cfg, objective=0.0, family="atlas",
                           seed=0, phase1_objective=0.0)
        doc = result_to_brocot_json(res, name=name, timestamp=ts)
        doc["provenance"]["kind"] = "atlas"
        doc["provenance"]["author"] = "Brocot (space tour)"
        doc["provenance"]["category"] = "Atlas"
        doc["provenance"]["description"] = (
            f"A distinct corner of FM space — spectral character: {tags}.")
        doc["provenance"]["spectral_tags"] = tags
        doc["provenance"].pop("search", None)
        (args.out / f"atlas_{rank:02d}.brocot").write_text(
            json.dumps(doc, ensure_ascii=False, indent=1) + "\n")
        print(f"  Atlas {rank:02d}  ops={cfg.num_enabled()}  [{tags}]")

    print(f"\nwrote {len(sel)} atlas presets to {args.out}")


if __name__ == "__main__":
    main()
