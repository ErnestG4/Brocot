# GPU FM — differentiable renderer & matcher (`gpu_fm.py`)

Runs on the 4090 via torch+CUDA in `phase3/.venv-gpu/` (gitignored; rebuild:
`python3 -m venv --system-site-packages phase3/.venv-gpu && phase3/.venv-gpu/bin/pip install torch`).

```sh
phase3/.venv-gpu/bin/python -m phase3.gpu_fm     # self-test
```

## What works (and the key finding)

The renderer `x(t)=sin(2π f_c t + Σ_k I_k sin(2π r_k f_c t))` and the
integer-grid binning fingerprint are fully differentiable and batched. Tested:

- **Gradient nails depths/detune.** With ratios fixed, Adam through the
  renderer recovers a target's depths to L1 ≈ 0.008. Amplitude/index loss is
  smooth and well-behaved.
- **Gradient does *not* solve ratios.** Spectral-loss gradients w.r.t.
  oscillator *frequency* are oscillatory and non-informative (moving a ratio
  slides partials across bins) — the classic DDSP frequency-inference problem.
  Pure gradient descent on log-ratio stalls (~0.14 L1, wrong ratios).

So the matcher is a **hybrid**: GPU brute-force over discrete Stern-Brocot
ratios (the part gradients can't do) + gradient polish of depths & detune (the
part they do beautifully). `match_target` / `match_to_config`: ~3-4 s for
600k candidates × 250 polish steps, matching a target to L1 ≈ 0.02-0.05.

## The coherence rule (important)

The matcher scores in its **own** integer-grid binning fingerprint space. A
target must be extracted in that *same* space — `gpu_fm.audio_to_fingerprint`,
**not** the Welch-based `audio_spectrum.audio_to_spectrum`. Mixing them looks
like a good match on one metric but renders poorly on the other (we measured a
0.04 "match" reading 0.42 in the other space). Extract and score in one space.

## The 10M GPU bank (`gpu_bank.py`)

The 4090 renders + FFT-fingerprints **~840,000 configs/second**, so a
10-million-entry bank builds in **14 s** (1.46 GB, half-precision). Coverage
in this space is near-saturated:

```
held-out NN L1:  p50 0.030  p90 0.077  p95 0.089  p99 0.117  max 0.21
within 0.05: 70%     within 0.10: 97%
```

vs the 175k CPU bank's 55% within 0.10 — ~3× tighter. Almost any target has a
bank entry within 0.1 L1, retrieved in milliseconds (GPU L1, chunked to stay
within VRAM). This is the "ready for any input sound" table.

## Why not just gradient-match everything?

Because of the ratio-gradient problem above. The discrete Stern-Brocot lattice
is exactly the structure that makes brute-force ratio search cheap and
gradient depth-polish exact — the math-is-the-interface idea paying off on the
GPU. The bank warm-start (CPU, `coverage_bank.py`) remains a strong, proven
alternative; the GPU matcher is a fast no-bank option for arbitrary targets.
