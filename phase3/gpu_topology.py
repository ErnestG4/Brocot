"""Multi-topology FM exploration — is it worth teaching the engine new
"program sets" (series / feedback), or is parallel FM enough?

The current JUCE engine is parallel-only (modulators sum into the carrier;
none modulate each other). DX7/SY99 timbres use *series* chains and *feedback*.
Before investing in a synth-side topology change, measure the payoff: render
spectra from series and feedback topologies and ask how far they fall from
everything parallel FM can reach (the 10M parallel GPU bank). If they sit far
outside it, the program sets unlock genuinely new timbres.

All renderers are differentiable and vectorised over time (series = nested
sins; feedback ≈ a few fixed-point iterations of self-modulation).

Run under phase3/.venv-gpu:
    phase3/.venv-gpu/bin/python -m phase3.gpu_topology
"""

from __future__ import annotations

import time

import numpy as np
import torch

from phase3.gpu_bank import NPZ as BANK_NPZ, Q_MAX, N_FFT
from phase3.gpu_fm import (DEVICE, F_CARRIER, SAMPLE_RATE, fingerprint_batch,
                           interp_matrix, render_batch)


def render_series(log_ratio, depth, detune, t, f_carrier=F_CARRIER):
    """Series chain op0→op1→…→op_{K-1}→carrier (op0 deepest). depth[k] is the
    index by which op[k]'s output modulates the next stage."""
    ratio = torch.exp(log_ratio) * torch.pow(2.0, detune / 1200.0)
    two_pi = 2.0 * np.pi
    B, K = log_ratio.shape
    out = torch.sin(two_pi * (f_carrier * ratio[:, 0]).unsqueeze(1) * t)
    for k in range(1, K):
        fk = (f_carrier * ratio[:, k]).unsqueeze(1)
        out = torch.sin(two_pi * fk * t + depth[:, k - 1].unsqueeze(1) * out)
    return torch.sin(two_pi * f_carrier * t + depth[:, K - 1].unsqueeze(1) * out)


def render_feedback(log_ratio, depth, detune, t, f_carrier=F_CARRIER, iters=4):
    """op0 with self-feedback (≈ fixed-point unroll) into the carrier, plus
    ops 1..K-1 parallel. Feedback FM → sawtooth-like brightness."""
    ratio = torch.exp(log_ratio) * torch.pow(2.0, detune / 1200.0)
    two_pi = 2.0 * np.pi
    B, K = log_ratio.shape
    base = two_pi * (f_carrier * ratio[:, 0]).unsqueeze(1) * t
    beta = depth[:, 0].unsqueeze(1)
    out = torch.sin(base)
    for _ in range(iters):
        out = torch.sin(base + beta * out)
    phase = two_pi * f_carrier * t + out
    for k in range(1, K):
        fk = (f_carrier * ratio[:, k]).unsqueeze(1)
        phase = phase + depth[:, k].unsqueeze(1) * torch.sin(two_pi * fk * t)
    return torch.sin(phase)


def _gen_targets(render_fn, rvt, n, K, t, W, win, g, chunk=40_000):
    """Render+fingerprint n configs of a topology, chunked so the (chunk, T)
    render transient stays within VRAM. Returns (n, q_max) fingerprints."""
    outs = []
    for s in range(0, n, chunk):
        b = min(chunk, n - s)
        ri = torch.randint(0, rvt.numel(), (b, K), generator=g, device=DEVICE)
        keepn = torch.randint(1, K + 1, (b, 1), generator=g, device=DEVICE)
        active = (torch.arange(K, device=DEVICE).unsqueeze(0) < keepn).float()
        dp = torch.empty(b, K, device=DEVICE).uniform_(0.3, 5.0, generator=g) * active
        x = render_fn(torch.log(rvt[ri]), dp, torch.zeros(b, K, device=DEVICE), t)
        outs.append(fingerprint_batch(x, W, win))
    return torch.cat(outs)


def _min_l1_to_bank(targets, bank, chunk=1_000_000):
    out = []
    for i in range(targets.shape[0]):
        p = targets[i].unsqueeze(0)
        best = 1e9
        for c in range(0, bank.shape[0], chunk):
            best = min(best, float((bank[c:c + chunk] - p).abs().sum(1).min()))
        out.append(best)
    return np.array(out)


def feedback_value(n_targets=400, n_cand=500_000, K=6, seed=11):
    """Concrete payoff: take feedback-rich targets and ask whether a PARALLEL
    candidate pool can match them vs a FEEDBACK candidate pool. If feedback
    candidates match them far better, feedback is *necessary* for those sounds
    (not just a different route to the same spectrum)."""
    from phase3.coverage_bank import curated_ratios
    from phase3.config import path_ratio
    paths = curated_ratios(5)
    rvt = torch.from_numpy(np.array([path_ratio(p) for p in paths], np.float32)).to(DEVICE)
    t = torch.arange(N_FFT, device=DEVICE, dtype=torch.float32) / SAMPLE_RATE
    win = torch.hann_window(N_FFT, device=DEVICE)
    W = interp_matrix(N_FFT, SAMPLE_RATE, Q_MAX, F_CARRIER)
    g = torch.Generator(device=DEVICE).manual_seed(seed)

    tgts = _gen_targets(render_feedback, rvt, n_targets, K, t, W, win, g)
    par = _gen_targets(render_batch, rvt, n_cand, K, t, W, win, g)
    fb = _gen_targets(render_feedback, rvt, n_cand, K, t, W, win, g)

    def best_match(cands):
        out = []
        for i in range(tgts.shape[0]):
            out.append(float((cands - tgts[i].unsqueeze(0)).abs().sum(1).min()))
        return np.array(out)

    par_l1, fb_l1 = best_match(par), best_match(fb)
    print(f"feedback-rich targets ({n_targets}), best match from {n_cand:,} "
          f"candidates of each pool:")
    print(f"  parallel pool : median {np.median(par_l1):.4f}  p90 {np.percentile(par_l1,90):.4f}")
    print(f"  feedback pool : median {np.median(fb_l1):.4f}  p90 {np.percentile(fb_l1,90):.4f}")
    print(f"  feedback wins (lower L1) on {100*(fb_l1<par_l1).mean():.0f}% of targets; "
          f"median improvement {np.median(par_l1-fb_l1):+.4f}")


def main(n_targets=1000, K=6, seed=7):
    d = np.load(BANK_NPZ)
    bank = torch.from_numpy(d["amps"].astype(np.float32)).to(DEVICE)
    print(f"parallel bank: {bank.shape[0]:,} fingerprints")
    from phase3.coverage_bank import curated_ratios
    from phase3.config import path_ratio
    paths = curated_ratios(5)
    rvt = torch.from_numpy(np.array([path_ratio(p) for p in paths], np.float32)).to(DEVICE)
    t = torch.arange(N_FFT, device=DEVICE, dtype=torch.float32) / SAMPLE_RATE
    win = torch.hann_window(N_FFT, device=DEVICE)
    W = interp_matrix(N_FFT, SAMPLE_RATE, Q_MAX, F_CARRIER)
    g = torch.Generator(device=DEVICE).manual_seed(seed)

    print(f"\nHow far do {n_targets} targets from each topology fall from the "
          f"parallel bank (min L1)?\n")
    print(f"{'topology':<12}{'p50':>8}{'p90':>8}{'frac>0.15':>11}{'frac>0.30':>11}")
    for name, fn in [("parallel", render_batch), ("series", render_series),
                     ("feedback", render_feedback)]:
        t0 = time.time()
        tgts = _gen_targets(fn, rvt, n_targets, K, t, W, win, g)
        nn = _min_l1_to_bank(tgts, bank)
        print(f"{name:<12}{np.percentile(nn,50):>8.4f}{np.percentile(nn,90):>8.4f}"
              f"{(nn>0.15).mean():>11.2f}{(nn>0.30).mean():>11.2f}   ({time.time()-t0:.0f}s)")
    print("\nparallel ≈ self-coverage floor; series/feedback well above it ⇒ "
          "those program sets reach timbres parallel FM can't.")


if __name__ == "__main__":
    main()
