"""Generate the 'Analog' factory pack — hand-designed patches that show off
per-operator detune (the new dimension): the cents between exact Stern-Brocot
ratios where beating, chorus and drift live.

These are demonstrations, not search matches — curated detune spreads chosen
to sound good and to exercise the full detune path (writer emits "detune",
the engine plays it). Easily reverted: it's just a factory folder.

Run: python -m phase3.generate_analog [--out DIR]
"""

from __future__ import annotations

import argparse
import json
from datetime import datetime, timezone
from pathlib import Path

from phase3.brocot_writer import _slug, result_to_brocot_json
from phase3.config import Config, ModConfig
from phase3.search import SearchResult


def _cfg(ops):
    """ops: list of (path, depth, detune_cents)."""
    mods = [ModConfig(path=p, depth=d, enabled=True, detune_cents=c)
            for p, d, c in ops]
    mods += [ModConfig() for _ in range(64 - len(mods))]
    return Config(mods=mods, f_carrier=220.0)


# name, description, ops[(path, depth, detune_cents)]
PATCHES = [
    ("Fat Unison",
     "Three 1/1 operators detuned ±7 cents — classic chorused unison fatness.",
     [("", 2.0, -7.0), ("", 2.0, 0.0), ("", 2.0, 7.0)]),
    ("Slow Beating",
     "Two operators near 2/1, +4 cents apart — a slow ~0.5 Hz beat at A3.",
     [("R", 3.054, 0.0), ("R", 3.054, 4.0)]),
    ("Shimmer Stack",
     "Harmonic stack 1·2·3·4 with small alternating detunes — a living shimmer.",
     [("", 1.84, -3.0), ("R", 1.5, 4.0), ("RL", 1.2, -5.0), ("RR", 0.9, 6.0)]),
    ("Drift Pad",
     "Low-index cluster with gentle ±2-5 cent spread — slow evolving pad.",
     [("", 0.7, -2.0), ("R", 0.6, 3.0), ("RLL", 0.5, -4.0),
      ("L", 0.55, 5.0), ("RR", 0.45, -3.0)]),
    ("Wide Octaves",
     "Octave-stacked operators (1·2·4) pulled a few cents wide — big and airy.",
     [("", 1.84, -6.0), ("R", 1.6, 0.0), ("RR", 1.2, 8.0)]),
    ("Detuned Fifths",
     "Perfect-fifth ratios (3/2) detuned into a soft chorus.",
     [("RL", 2.405, -5.0), ("RL", 2.405, 5.0), ("", 1.5, 0.0)]),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path,
                    default=Path("resources/presets/factory/analog"))
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    ts = datetime.now(timezone.utc).isoformat()

    for name, desc, ops in PATCHES:
        cfg = _cfg(ops)
        res = SearchResult(config=cfg, objective=0.0, family="analog",
                           seed=0, phase1_objective=0.0)
        doc = result_to_brocot_json(res, name=name, timestamp=ts)
        doc["provenance"]["kind"] = "hand-authored"
        doc["provenance"]["author"] = "Brocot (detune demo)"
        doc["provenance"]["category"] = "Analog"
        doc["provenance"]["description"] = desc
        # Drop the search block — these aren't search results.
        doc["provenance"].pop("search", None)
        path = args.out / f"{_slug(name)}.brocot"
        path.write_text(json.dumps(doc, ensure_ascii=False, indent=1) + "\n")
        det = [c for _, _, c in ops]
        print(f"  {name:<16} {len(ops)} ops, detune {det} -> {path.name}")

    print(f"\nwrote {len(PATCHES)} analog presets to {args.out}")


if __name__ == "__main__":
    main()
