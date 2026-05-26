"""R1(B) — predict-vs-render fold gap across registers.

The analytic predictor *culls* partials above f_max=20 kHz; the real
(time-domain) synth *folds* sidebands above Nyquist back into the audible
band as aliasing. That divergence is what makes Mode B prediction least
trustworthy up high — and real ARP/SY99 recordings have genuine HF content
that will fold when played in the treble.

For a sample of presets, at bass/mid/treble carriers, compare the analytic
fingerprint to the measured fingerprint of the rendered audio (both
normalised, same smoothed integer grid). If the gap grows with carrier,
that's folding; if it's flat, it's just extraction noise.

Run: python -m phase3.register_fold_gap
"""

from __future__ import annotations

import json
import os
import time
from pathlib import Path

import numpy as np

os.environ.setdefault("OMP_NUM_THREADS", "1")

from phase3.audio_spectrum import audio_to_spectrum, render_fm_audio  # noqa: E402
from phase3.build_pull_index import _config_from_preset  # noqa: E402
from phase3.family_distance import _kl_divergence, quantize_to_integer_grid  # noqa: E402
from phase3.partial_prediction import predict_partials  # noqa: E402

FACTORY = Path("resources/presets/factory")
CARRIERS = {"bass": 55.0, "mid": 220.0, "treble": 880.0}
SAMPLE_EVERY = 140          # ~30 presets across the corpus


def _gap(ratios, depths, fc):
    q_max = max(8, int(20000.0 / fc))           # audible partials at this fc
    sp = predict_partials(ratios, depths, f_carrier=fc,
                          threshold_db=-40.0, eps=1e-3, max_partials=4000)
    ana = quantize_to_integer_grid(sp, fc, q_max)
    ana = ana / ana.sum() if ana.sum() > 0 else ana
    audio = render_fm_audio(ratios, depths, f_carrier=fc, duration_s=1.5)
    meas = audio_to_spectrum(audio, q_max=q_max, f_carrier=fc, sample_rate=48000)
    meas = meas / meas.sum() if meas.sum() > 0 else meas
    return float(np.abs(ana - meas).sum()), _kl_divergence(meas, ana)


def main():
    paths = sorted(FACTORY.rglob("*.brocot"))[::SAMPLE_EVERY]
    print(f"fold gap over {len(paths)} sampled presets @ {CARRIERS} …\n")
    t0 = time.time()
    rows = {name: [] for name in CARRIERS}
    for p in paths:
        try:
            doc = json.loads(p.read_text())
        except Exception:
            continue
        rs, ds = _config_from_preset(doc).ratios_and_depths()
        if not rs:
            continue
        for name, fc in CARRIERS.items():
            l1, kl = _gap(rs, ds, fc)
            rows[name].append((l1, kl))

    print(f"{'register':<10}{'gap L1 med':>12}{'gap L1 p90':>12}"
          f"{'gap KL med':>12}{'gap KL p90':>12}")
    for name in CARRIERS:
        arr = np.array(rows[name])
        l1, kl = arr[:, 0], arr[:, 1]
        print(f"{name:<10}{np.median(l1):>12.4f}{np.percentile(l1,90):>12.4f}"
              f"{np.median(kl):>12.4f}{np.percentile(kl,90):>12.4f}")
    print(f"\n({time.time()-t0:.0f}s)  Growing gap toward treble ⇒ aliasing/fold "
          f"the analytic model misses.")


if __name__ == "__main__":
    main()
