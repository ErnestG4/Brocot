"""ARS arithmetic atlas of the preset library.

Tags every factory preset with its ARS arithmetic fingerprint (dominant
Ramanujan q, p-adic prime, partial-spacing class) and reports the
distribution — an arithmetic map of the sounds, complementing the spectral
feature tags.

Caveats (measured): the p-adic / RF readouts are clean for low-prime and
moderately structured spectra but noisy for sparse high-prime combs; the
spacing class is only meaningful for *dense, irregular* partial fields (the
textured-smooth carpets) — simple harmonic combs read a degenerate 'GUE' and
sparse ones read 'sparse'. So the atlas is most informative across the dense /
inharmonic part of the library.

Run: python -m phase3.ars_atlas   (writes derived/ars_tags.jsonl)
"""

from __future__ import annotations

import json
import os
import time
from collections import Counter
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

os.environ.setdefault("OMP_NUM_THREADS", "1")

import numpy as np

from phase3.ars_fingerprint import ars_fingerprint
from phase3.build_pull_index import _amplitude_vector, _config_from_preset

FACTORY = Path("resources/presets/factory")
OUT = Path("phase3/derived/ars_tags.jsonl")
Q_MAX = 64


def _one(path_str: str):
    path = Path(path_str)
    try:
        doc = json.loads(path.read_text())
    except Exception:
        return None
    cfg = _config_from_preset(doc)
    if cfg.num_enabled() == 0:
        return None
    amps = _amplitude_vector(cfg, Q_MAX)        # L1-normalised integer grid
    if amps.sum() <= 0:
        return None
    fp = ars_fingerprint(amps, Q_MAX)
    return {
        "preset": str(path.relative_to(FACTORY)),
        "n_ops": cfg.num_enabled(),
        "rf_top_q": fp.rf_top_q,
        "padic_prime": fp.padic_prime,
        "padic_strength": fp.padic_strength,
        "spacing": fp.spacing_class,
        "tag": fp.describe(),
    }


def main():
    paths = [str(p) for p in sorted(FACTORY.rglob("*.brocot"))]
    print(f"ARS-tagging {len(paths)} presets (q_max={Q_MAX}) …")
    t0 = time.time()
    recs = []
    with ProcessPoolExecutor(max_workers=10) as ex:
        for i, r in enumerate(ex.map(_one, paths, chunksize=16)):
            if r is not None:
                recs.append(r)
            if (i + 1) % 1000 == 0:
                print(f"  {i+1}/{len(paths)} ({time.time()-t0:.0f}s)")
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("w") as f:
        for r in recs:
            f.write(json.dumps(r) + "\n")

    print(f"\ndone: {len(recs)} presets in {time.time()-t0:.0f}s -> {OUT}\n")
    rfq = Counter(r["rf_top_q"] for r in recs)
    pp = Counter(r["padic_prime"] for r in recs)
    sp = Counter(r["spacing"] for r in recs)
    n = len(recs)
    print("dominant Ramanujan q:  " +
          "  ".join(f"q{k}:{v}" for k, v in sorted(rfq.items(), key=lambda x:-x[1])[:8]))
    print("dominant p-adic prime: " +
          "  ".join(f"p{k}:{v}" for k, v in sorted(pp.items())))
    print("spacing class:         " +
          "  ".join(f"{k}:{v} ({100*v/n:.0f}%)" for k, v in sp.most_common()))


if __name__ == "__main__":
    main()
