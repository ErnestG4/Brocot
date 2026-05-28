"""Massive GPU fingerprint bank — the headline precompute.

Batch-renders millions of FM configs on the 4090 and fingerprints them in the
matcher's integer-grid binning space (so retrieval is coherent with
gpu_fm.audio_to_fingerprint / match_to_config). Stored half-precision; a few
million entries fit in ~1 GB and retrieve in milliseconds via GPU L1.

    build:    phase3/.venv-gpu/bin/python -m phase3.gpu_bank build --n 5000000
    coverage: phase3/.venv-gpu/bin/python -m phase3.gpu_bank coverage

Run under phase3/.venv-gpu (torch). Artifacts gitignored (derived/).
"""

from __future__ import annotations

import argparse
import time
from pathlib import Path

import numpy as np
import torch

from phase3.gpu_fm import (DEVICE, F_CARRIER, SAMPLE_RATE, fingerprint_batch,
                           interp_matrix, render_batch)

NPZ = Path("phase3/derived/gpu_bank.npz")
Q_MAX = 64
N_FFT = 4096
N_OPS = 6


def _ratio_values():
    from phase3.coverage_bank import curated_ratios
    from phase3.config import path_ratio
    paths = curated_ratios(5)
    return paths, np.array([path_ratio(p) for p in paths], np.float32)


def bank_path(n_ops: int = 6) -> Path:
    """Per-depth bank file (n_ops=6 keeps the original unsuffixed path)."""
    return NPZ if n_ops == 6 else Path(f"phase3/derived/gpu_bank_{n_ops}.npz")


def build(n_total: int = 5_000_000, chunk: int = 50_000, seed: int = 0,
          n_ops: int = 6, out: Path = None):
    out = out or bank_path(n_ops)
    paths, rv = _ratio_values()
    rvt = torch.from_numpy(rv).to(DEVICE)
    R = rvt.numel()
    g = torch.Generator(device=DEVICE).manual_seed(seed)
    t = torch.arange(N_FFT, device=DEVICE, dtype=torch.float32) / SAMPLE_RATE
    win = torch.hann_window(N_FFT, device=DEVICE)
    W = interp_matrix(N_FFT, SAMPLE_RATE, Q_MAX, F_CARRIER)

    amps = np.empty((n_total, Q_MAX), dtype=np.float16)
    ridx = np.empty((n_total, n_ops), dtype=np.uint8)
    depths = np.empty((n_total, n_ops), dtype=np.float16)
    feedback = np.empty((n_total,), dtype=np.float16)
    t0 = time.time()
    done = 0
    while done < n_total:
        b = min(chunk, n_total - done)
        ri = torch.randint(0, R, (b, n_ops), generator=g, device=DEVICE)
        # Variable effective op-count: zero out a random subset of slots so the
        # bank spans 1..n_ops active operators (the depth-N map's space).
        keepn = torch.randint(1, n_ops + 1, (b, 1), generator=g, device=DEVICE)
        slot = torch.arange(n_ops, device=DEVICE).unsqueeze(0)
        active = (slot < keepn).float()
        dp = torch.empty(b, n_ops, device=DEVICE).uniform_(0.1, 5.0, generator=g) * active
        # Half the configs get op-0 self-feedback in (0, π) — the dimension
        # parallel FM can't reach (Square/Glass/Reed/Bell); rest stay parallel.
        fb = torch.empty(b, device=DEVICE).uniform_(0.0, float(np.pi), generator=g)
        fb *= (torch.rand(b, generator=g, device=DEVICE) < 0.5).float()
        fp = fingerprint_batch(
            render_batch(torch.log(rvt[ri]), dp, torch.zeros(b, n_ops, device=DEVICE),
                         t, F_CARRIER, feedback=fb), W, win)
        amps[done:done + b] = fp.half().cpu().numpy()
        ridx[done:done + b] = ri.byte().cpu().numpy()
        depths[done:done + b] = dp.half().cpu().numpy()
        feedback[done:done + b] = fb.half().cpu().numpy()
        done += b
        if done % 500_000 == 0:
            print(f"  {done:,}/{n_total:,} ({time.time()-t0:.0f}s, "
                  f"{done/(time.time()-t0):,.0f}/s)")

    out.parent.mkdir(parents=True, exist_ok=True)
    np.savez(out, amps=amps, ridx=ridx, depths=depths, feedback=feedback)
    print(f"\nGPU bank: {n_total:,} fingerprints @ {n_ops} ops, q_max={Q_MAX}  "
          f"({out.stat().st_size/1e9:.2f} GB) in {time.time()-t0:.0f}s -> {out}")


def load_to_gpu():
    d = np.load(NPZ)
    amps = torch.from_numpy(d["amps"]).to(DEVICE).float()   # (N, q_max)
    return amps, d["ridx"], d["depths"]


def retrieve(amps_gpu: torch.Tensor, target: np.ndarray, k: int = 10):
    """Top-k nearest (L1) bank rows to an L1-normalised target, on GPU."""
    t = torch.from_numpy(np.asarray(target, np.float32)[:amps_gpu.shape[1]]).to(DEVICE)
    t = t / torch.clamp(t.sum(), min=1e-12)
    d = (amps_gpu - t.unsqueeze(0)).abs().sum(dim=1)
    vals, idx = torch.topk(d, k, largest=False)
    return idx.cpu().numpy(), vals.cpu().numpy()


def coverage(n_probe: int = 1000, seed: int = 999):
    """Held-out random targets → nearest-neighbour distance in the bank."""
    amps_gpu, ridx, depths = load_to_gpu()
    print(f"GPU bank: {amps_gpu.shape[0]:,} fingerprints @ {amps_gpu.shape[1]}")
    paths, rv = _ratio_values()
    rvt = torch.from_numpy(rv).to(DEVICE)
    g = torch.Generator(device=DEVICE).manual_seed(seed)
    t = torch.arange(N_FFT, device=DEVICE, dtype=torch.float32) / SAMPLE_RATE
    win = torch.hann_window(N_FFT, device=DEVICE)
    W = interp_matrix(N_FFT, SAMPLE_RATE, Q_MAX, F_CARRIER)
    R = rvt.numel()
    # Generate all probe fingerprints first.
    ri = torch.randint(0, R, (n_probe, N_OPS), generator=g, device=DEVICE)
    keepn = torch.randint(1, N_OPS + 1, (n_probe, 1), generator=g, device=DEVICE)
    active = (torch.arange(N_OPS, device=DEVICE).unsqueeze(0) < keepn).float()
    dp = torch.empty(n_probe, N_OPS, device=DEVICE).uniform_(0.1, 5.0, generator=g) * active
    probes = fingerprint_batch(
        render_batch(torch.log(rvt[ri]), dp, torch.zeros(n_probe, N_OPS, device=DEVICE),
                     t, F_CARRIER), W, win)
    # Per-probe nearest L1 to the bank — broadcast over the 10M rows one probe
    # at a time (the (N, q_max) transient is reused; cdist(p=1) would OOM).
    nn = []
    N = amps_gpu.shape[0]
    CH = 1_000_000                       # 1M rows ⇒ ~256 MB transient
    for i in range(n_probe):
        p = probes[i].unsqueeze(0)
        best = 1e9
        for c in range(0, N, CH):
            d = (amps_gpu[c:c + CH] - p).abs().sum(dim=1)
            best = min(best, float(d.min()))
        nn.append(best)
    nn = np.array(nn)
    print("held-out NN L1 distance:")
    for p in (50, 90, 95, 99):
        print(f"  p{p}: {np.percentile(nn, p):.4f}")
    print(f"  max {nn.max():.4f}  mean {nn.mean():.4f}")
    print(f"  within 0.05: {(nn<0.05).mean()*100:.1f}%   "
          f"within 0.10: {(nn<0.10).mean()*100:.1f}%")


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build"); b.add_argument("--n", type=int, default=5_000_000)
    b.add_argument("--n-ops", type=int, default=6)
    c = sub.add_parser("coverage"); c.add_argument("--n-probe", type=int, default=1000)
    a = ap.parse_args()
    if a.cmd == "build":
        build(n_total=a.n, n_ops=a.n_ops)
    else:
        coverage(n_probe=a.n_probe)


if __name__ == "__main__":
    main()
