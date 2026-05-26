"""R1 — how much does an FM patch's timbre drift across the keyboard?

In the analytic model a partial sits at f_carrier·(1+Σ n_i r_i) with a
carrier-independent amplitude Π J_{n_i}(I_i). So the *only* thing that
changes a patch's integer-grid fingerprint with pitch is the f_min/f_max
cull: at a high carrier the top partials fall outside 20 kHz and vanish; at
a low carrier they survive. This script quantifies that — per patch, how
far the normalised fingerprint moves between bass / mid / treble carriers,
i.e. how much spectral mass is lost (analytically) or folds back as
aliasing (in the real synth) when the patch is transposed up.

(The real Web-Audio synth *folds* the >Nyquist sidebands rather than
culling them — that aliasing gap is measured separately in
register_fold_gap; here we measure the analytic cull, which bounds how
much content is "at risk" up high.)

Run: python -m phase3.register_drift
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

from phase3.build_pull_index import _config_from_preset  # noqa: E402
from phase3.family_distance import _kl_divergence, quantize_to_integer_grid  # noqa: E402
from phase3.partial_prediction import predict_partials  # noqa: E402

FACTORY = Path("resources/presets/factory")
OUT = Path("phase3/derived/register_drift.jsonl")

# Reference carriers: low bass A1, mid A3, treble A5.
CARRIERS = {"bass": 55.0, "mid": 220.0, "treble": 880.0}
Q_MAX = 384            # covers bass cutoff 20000/55 ≈ 363


def _fingerprint(ratios, depths, f_carrier) -> np.ndarray:
    sp = predict_partials(ratios, depths, f_carrier=f_carrier,
                          threshold_db=-40.0, eps=1e-3, max_partials=4000)
    a = quantize_to_integer_grid(sp, f_carrier, Q_MAX)
    s = a.sum()
    return a / s if s > 0 else a


def _one(path_str):
    path = Path(path_str)
    try:
        doc = json.loads(path.read_text())
    except Exception:
        return None
    cfg = _config_from_preset(doc)
    rs, ds = cfg.ratios_and_depths()
    n_ops = len(rs)
    if n_ops == 0:
        return None
    fp = {name: _fingerprint(rs, ds, fc) for name, fc in CARRIERS.items()}
    # Drift = L1 / KL between normalised fingerprints across registers.
    def l1(a, b): return float(np.abs(a - b).sum())
    # treble survival: fraction of the *mid* spectrum mass that still fits
    # below the treble cutoff (partial ≤ 20000/880 ≈ 22).
    cut = int(20000.0 / CARRIERS["treble"])
    mid = fp["mid"]
    treble_survival = float(mid[:cut].sum())   # mid is L1-normalised
    return {
        "preset": str(path.relative_to(FACTORY)),
        "n_ops": n_ops,
        "drift_bass_mid": round(l1(fp["bass"], fp["mid"]), 5),
        "drift_mid_treble": round(l1(fp["mid"], fp["treble"]), 5),
        "drift_bass_treble": round(l1(fp["bass"], fp["treble"]), 5),
        "kl_mid_treble": round(_kl_divergence(fp["treble"], fp["mid"]), 5),
        "treble_survival": round(treble_survival, 5),
    }


def main():
    paths = [str(p) for p in sorted(FACTORY.rglob("*.brocot"))]
    print(f"register drift over {len(paths)} presets @ {CARRIERS} …")
    t0 = time.time()
    recs = []
    with ProcessPoolExecutor(max_workers=10) as ex:
        for i, r in enumerate(ex.map(_one, paths, chunksize=16)):
            if r is not None:
                recs.append(r)
            if (i + 1) % 1000 == 0:
                print(f"  {i + 1}/{len(paths)} ({time.time() - t0:.0f}s)")
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("w") as f:
        for r in recs:
            f.write(json.dumps(r) + "\n")

    def col(k): return np.array([r[k] for r in recs])
    print(f"\ndone: {len(recs)} presets in {time.time() - t0:.0f}s -> {OUT}\n")
    print(f"{'metric':<20}{'median':>10}{'p50':>10}{'p90':>10}{'max':>10}")
    for k in ("drift_bass_mid", "drift_mid_treble", "drift_bass_treble",
              "kl_mid_treble", "treble_survival"):
        c = col(k)
        print(f"{k:<20}{np.median(c):>10.4f}{np.percentile(c,50):>10.4f}"
              f"{np.percentile(c,90):>10.4f}{c.max():>10.4f}")
    # How many patches "transpose cleanly" vs fragment up high?
    bt = col("drift_bass_treble")
    surv = col("treble_survival")
    print(f"\npatches with bass→treble drift < 0.10: "
          f"{(bt < 0.10).mean()*100:.1f}%   < 0.25: {(bt < 0.25).mean()*100:.1f}%")
    print(f"patches keeping ≥90% mass at treble: {(surv >= 0.90).mean()*100:.1f}%"
          f"   ≥99%: {(surv >= 0.99).mean()*100:.1f}%")


if __name__ == "__main__":
    main()
