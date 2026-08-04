"""Mod-map feasibility probe — can TRAJECTORY fingerprints see modulation?

The mod map has been deferred on one open question: the bank fingerprint is
a single 93 ms window, so time-varying modulation (LFO on a depth, filter
sweeps, tremolo) is invisible to it. The cheapest extension that stays
inside the project's L1-fingerprint mathematics is the **trajectory
fingerprint**: render K frames at staggered LFO phases and concatenate the
per-frame 64-bin fingerprints (K·64 dims, still L1-comparable).

This probe asks, falsifiably: with one depth-LFO per config,
  P1  do trajectory fingerprints separate modulated from static configs
      (frame-to-frame variation >> the static jitter floor)?
  P2  can they RANK modulation rate (slow/med/fast 3-NN classification)
      where the static fingerprint is blind by construction?
  P3  does the static 1-frame part still retrieve the underlying timbre
      (nearest-neighbour family agreement unchanged vs unmodulated)?

Frames are exact static renders at sampled LFO phases — valid while the
LFO is slow against the 93 ms window (rate << ~10 Hz), which is the
musically-typical tremolo/wobble regime the mod map targets.

Run: phase3/.venv-gpu/bin/python -m phase3.modmap_probe
"""

from __future__ import annotations

import json
import time

import numpy as np
import torch

from phase3.gpu_fm import (DEVICE, F_CARRIER, SAMPLE_RATE, fingerprint_batch,
                           interp_matrix, render_batch)
from phase3.gpu_bank import N_FFT, Q_MAX
from phase3.gpu_jumps import DERIVED

N_CFG = 8000
N_OPS = 6
K_FRAMES = 4          # trajectory frames across one LFO period
RATE_CLASSES = [(0.3, 0.8), (1.0, 2.5), (3.0, 7.0)]   # slow / med / fast Hz


def _sample_configs(rng):
    """Random 6-op configs in the bank's style + one depth-LFO each."""
    from phase3.coverage_bank import curated_ratios
    from phase3.config import path_ratio
    ratios = np.array([path_ratio(p) for p in curated_ratios(5)], np.float64)
    idx = rng.integers(0, len(ratios), (N_CFG, N_OPS))
    log_ratio = np.log(ratios[idx])
    n_on = rng.integers(1, N_OPS + 1, N_CFG)
    on = np.arange(N_OPS)[None, :] < n_on[:, None]
    dmax = np.clip(2.0 * 12.0 / np.maximum(n_on, 1), None, 5.0)
    depth = rng.uniform(0.1, 1.0, (N_CFG, N_OPS)) * dmax[:, None] * on
    fb = rng.uniform(0.0, 0.7, N_CFG) * (rng.random(N_CFG) < 0.3)

    rate_cls = rng.integers(0, len(RATE_CLASSES), N_CFG)
    lo = np.array([RATE_CLASSES[c][0] for c in rate_cls])
    hi = np.array([RATE_CLASSES[c][1] for c in rate_cls])
    lfo_rate = rng.uniform(lo, hi)
    lfo_depth = rng.uniform(0.3, 1.0, N_CFG)      # fraction of target depth
    tgt_op = np.array([rng.integers(0, max(1, n)) for n in n_on])
    return (log_ratio, depth, fb, rate_cls, lfo_rate, lfo_depth, tgt_op)


def _render_frames(log_ratio, depth, fb, lfo_depth, tgt_op, phases):
    """(len(phases), N_CFG, Q_MAX) fingerprints; LFO scales the target op's
    depth by 1 + lfo_depth·sin(phase) (clamped ≥ 0)."""
    t = torch.arange(N_FFT, device=DEVICE, dtype=torch.float32) / SAMPLE_RATE
    win = torch.hann_window(N_FFT, device=DEVICE)
    W = interp_matrix(N_FFT, SAMPLE_RATE, Q_MAX, F_CARRIER)
    lr = torch.as_tensor(log_ratio, dtype=torch.float32, device=DEVICE)
    beta = torch.as_tensor(fb, dtype=torch.float32, device=DEVICE) * float(np.pi)
    det = torch.zeros_like(lr)
    out = []
    rows = np.arange(N_CFG)
    for ph in phases:
        d = depth.copy()
        scale = np.clip(1.0 + lfo_depth * np.sin(ph), 0.0, None)
        d[rows, tgt_op] *= scale
        dp = torch.as_tensor(d, dtype=torch.float32, device=DEVICE)
        fp = fingerprint_batch(render_batch(lr, dp, det, t, F_CARRIER,
                                            feedback=beta), W, win)
        out.append(fp.float())
    return torch.stack(out)


def _knn_acc(fps: torch.Tensor, labels: np.ndarray, k: int = 3) -> float:
    """Leave-one-out k-NN label accuracy under L1."""
    n = fps.shape[0]
    lab = torch.as_tensor(labels, device=DEVICE)
    hits = 0
    for c in range(0, n, 1024):
        block = fps[c:c + 1024]
        d = torch.cdist(block, fps, p=1)
        d[torch.arange(block.shape[0]), torch.arange(c, c + block.shape[0])] = 1e9
        _, idx = torch.topk(d, k, largest=False)
        pred = torch.mode(lab[idx], dim=1).values
        hits += int((pred == lab[c:c + block.shape[0]]).sum())
    return hits / n


def main() -> None:
    t0 = time.time()
    rng = np.random.default_rng(0)
    log_ratio, depth, fb, rate_cls, lfo_rate, lfo_depth, tgt_op = \
        _sample_configs(rng)

    # Trajectory frames at K staggered phases of each config's own LFO cycle
    # (phase-sampling is rate-free; RATE enters through the frame SPACING in
    # real time — model both: fixed-phase frames measure modulation DEPTH
    # separability, fixed-TIME frames at dt=120 ms measure rate separability).
    phase_grid = np.linspace(0.0, 2.0 * np.pi, K_FRAMES, endpoint=False)
    fp_phase = _render_frames(log_ratio, depth, fb, lfo_depth, tgt_op,
                              phase_grid)
    dt = 0.120
    fp_time = torch.stack([
        _render_frames(log_ratio, depth, fb, lfo_depth, tgt_op,
                       [2.0 * np.pi * lfo_rate * (k * dt)])[0]
        for k in range(K_FRAMES)])

    static_fp = fp_phase[0]                                # phase-0 frame
    traj_time = fp_time.permute(1, 0, 2).reshape(N_CFG, -1)

    # P1: modulation visibility — frame-to-frame variation vs a re-render
    # jitter floor (two identical renders differ only by numerical noise).
    var_mod = (fp_phase[1:] - fp_phase[:-1]).abs().sum(-1).mean(0)
    refloor = (_render_frames(log_ratio, depth, fb, lfo_depth * 0, tgt_op,
                              [0.0])[0] - static_fp).abs().sum(-1)
    p1 = {"median_frame_variation": round(float(var_mod.median()), 4),
          "median_rerender_floor": round(float(refloor.median()), 6),
          "configs_above_10x_floor":
              round(float((var_mod > 10 * refloor.clamp(min=1e-6)).float()
                          .mean()), 3)}

    # P2: rate-class 3-NN — time-spaced trajectory vs static (blind).
    p2 = {"traj_time_acc": round(_knn_acc(traj_time, rate_cls), 3),
          "static_acc": round(_knn_acc(static_fp, rate_cls), 3),
          "chance": round(1.0 / len(RATE_CLASSES), 3)}

    # P3: does the trajectory's first frame still retrieve base timbre?
    # 1-NN of static frame among UNMODULATED renders of the same configs.
    fp_unmod = _render_frames(log_ratio, depth, fb, lfo_depth * 0, tgt_op,
                              [0.0])[0]
    d = torch.cdist(static_fp[:2000], fp_unmod, p=1)
    nn = d.argmin(1).cpu().numpy()
    p3 = {"static_frame_self_retrieval@1":
              round(float((nn == np.arange(2000)).mean()), 3)}

    out = {"n_cfg": N_CFG, "k_frames": K_FRAMES, "P1_visibility": p1,
           "P2_rate_knn": p2, "P3_timbre_retrieval": p3,
           "runtime_s": round(time.time() - t0, 1)}
    (DERIVED / "modmap_probe.json").write_text(json.dumps(out, indent=1) + "\n")
    print(json.dumps(out, indent=1))


if __name__ == "__main__":
    main()
