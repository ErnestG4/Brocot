"""P1.4 — re-fingerprint the full preset corpus at q_max=2048.

For every .brocot preset, compute the L1-normalised integer-grid amplitude
vector (the "landscape position") at q_max=2048 with the new smoothed
projection, and compare it to the q_max=256 fingerprint. The question is
whether extending the grid *moves* a preset's landscape position or merely
adds high-partial resolution:

  * low_region_L1  — L1 distance between the q256 vector and the first 256
    bins of the q2048 vector (each renormalised). ~0 ⇒ landscape position
    is stable; the extra bins are pure resolution.
  * mass_above_256 / _1024 — fraction of spectral mass beyond those
    partials at q2048 (how much fine structure the bigger grid captures).

Writes a gitignored derived jsonl. Parallel across ~10 workers
(MKL/OMP threads pinned to 1 per worker — see workflow notes).

Run: python -m phase3.refingerprint_corpus
"""

from __future__ import annotations

import json
import os
import time
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import numpy as np

os.environ.setdefault("OMP_NUM_THREADS", "1")
os.environ.setdefault("MKL_NUM_THREADS", "1")
os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")

from phase3.build_pull_index import _amplitude_vector, _config_from_preset  # noqa: E402

FACTORY = Path("resources/presets/factory")
OUT = Path("phase3/derived/corpus_q2048.jsonl")   # gitignored


def _one(path_str: str) -> dict | None:
    path = Path(path_str)
    try:
        doc = json.loads(path.read_text())
    except Exception:
        return None
    cfg = _config_from_preset(doc)
    n_ops = sum(1 for m in cfg.mods if m.enabled)
    a256 = _amplitude_vector(cfg, 256)          # L1-normalised
    a2048 = _amplitude_vector(cfg, 2048)
    # Low-region comparison: renormalise both over bins 1..256.
    lo = a2048[:256].copy()
    s256, slo = a256.sum(), lo.sum()
    if slo > 0:
        lo /= slo
    a256n = a256 / s256 if s256 > 0 else a256
    low_l1 = float(np.abs(a256n - lo).sum())
    mass_above_256 = float(a2048[256:].sum())
    mass_above_1024 = float(a2048[1024:].sum())
    nz = int(np.count_nonzero(a2048))
    return {
        "preset": str(path.relative_to(FACTORY)),
        "n_ops": n_ops,
        "low_region_l1": round(low_l1, 6),
        "mass_above_256": round(mass_above_256, 6),
        "mass_above_1024": round(mass_above_1024, 6),
        "nonzero_bins_2048": nz,
    }


def main():
    paths = [str(p) for p in sorted(FACTORY.rglob("*.brocot"))]
    print(f"re-fingerprinting {len(paths)} presets at q_max=2048 "
          f"(compare vs q256)…")
    t0 = time.time()
    recs = []
    with ProcessPoolExecutor(max_workers=10) as ex:
        for i, r in enumerate(ex.map(_one, paths, chunksize=16)):
            if r is not None:
                recs.append(r)
            if (i + 1) % 500 == 0:
                print(f"  {i + 1}/{len(paths)}  ({time.time() - t0:.0f}s)")
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("w") as f:
        for r in recs:
            f.write(json.dumps(r) + "\n")

    low = np.array([r["low_region_l1"] for r in recs])
    m256 = np.array([r["mass_above_256"] for r in recs])
    m1024 = np.array([r["mass_above_1024"] for r in recs])
    print(f"\ndone: {len(recs)} presets in {time.time() - t0:.0f}s -> {OUT}")
    print("landscape stability (q256 vs q2048 low region):")
    print(f"  low_region_L1   median={np.median(low):.4f}  "
          f"p95={np.percentile(low, 95):.4f}  max={low.max():.4f}")
    print(f"  mass_above_256  median={np.median(m256):.4f}  "
          f"p95={np.percentile(m256, 95):.4f}  max={m256.max():.4f}")
    print(f"  mass_above_1024 median={np.median(m1024):.4f}  "
          f"p95={np.percentile(m1024, 95):.4f}  max={m1024.max():.4f}")
    n_moved = int((low > 0.05).sum())
    print(f"  presets whose landscape moved >0.05 L1: {n_moved}/{len(recs)} "
          f"({100 * n_moved / len(recs):.1f}%)")


if __name__ == "__main__":
    main()
