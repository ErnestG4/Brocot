"""Amortized inverse FM synthesizer — a neural net that maps a target
spectrum straight to FM parameters, trained on the 10M GPU bank.

The bank is labelled data: each row is (fingerprint → ratios, depths). We
train an MLP to invert it. The config is represented canonically as
presence + depth over the 63 curated Stern-Brocot ratios (permutation-
invariant; duplicate ratios sum their depths, which is exactly how two
same-ratio modulators combine in FM). So the net outputs:

    presence_logits (63)   — which ratios are active
    depth (63)             — the modulation index at each ratio

At inference a target spectrum → forward pass → params, instantly, no search.

    train: phase3/.venv-gpu/bin/python -m phase3.gpu_invnet train
    eval:  phase3/.venv-gpu/bin/python -m phase3.gpu_invnet eval

Run under phase3/.venv-gpu. Model saved to derived/invnet.pt (gitignored).
"""

from __future__ import annotations

import argparse
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn

from phase3.gpu_bank import NPZ as BANK_NPZ, Q_MAX, N_OPS
from phase3.gpu_fm import DEVICE

MODEL = Path("phase3/derived/invnet.pt")
N_RATIOS = 63        # len(curated_ratios(5))


def _bank_to_targets(ridx: np.ndarray, depths: np.ndarray):
    """(N,6) ratio indices + depths → (N,63) presence, (N,63) summed depth."""
    N = ridx.shape[0]
    ri = torch.from_numpy(ridx.astype(np.int64))
    dp = torch.from_numpy(depths.astype(np.float32))
    depth_vec = torch.zeros(N, N_RATIOS)
    depth_vec.scatter_add_(1, ri, dp)            # sum depths per ratio
    presence = (depth_vec > 1e-3).float()
    return presence, depth_vec


class InvNet(nn.Module):
    def __init__(self, q_max=Q_MAX, hidden=768, n_ratios=N_RATIOS):
        super().__init__()
        self.body = nn.Sequential(
            nn.Linear(q_max, hidden), nn.GELU(),
            nn.Linear(hidden, hidden), nn.GELU(),
            nn.Linear(hidden, hidden), nn.GELU(),
        )
        self.presence = nn.Linear(hidden, n_ratios)
        self.depth = nn.Linear(hidden, n_ratios)

    def forward(self, x):
        h = self.body(x)
        # softplus depth ≥ 0; presence as logits.
        return self.presence(h), torch.nn.functional.softplus(self.depth(h))


def train(epochs: int = 8, batch: int = 16384, lr: float = 2e-3,
          val_frac: float = 0.02):
    d = np.load(BANK_NPZ)
    amps = torch.from_numpy(d["amps"].astype(np.float32))      # (N, q_max), L1-normed
    presence, depth = _bank_to_targets(d["ridx"], d["depths"])
    N = amps.shape[0]
    nval = int(N * val_frac)
    print(f"training on {N-nval:,} examples, {nval:,} val")

    # Keep data on CPU pinned; move minibatches to GPU.
    perm = torch.randperm(N)
    tr, va = perm[nval:], perm[:nval]
    net = InvNet().to(DEVICE)
    opt = torch.optim.AdamW(net.parameters(), lr=lr)
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, epochs)
    # Heavy positive weighting — only ~6 of 63 ratios are active, so without
    # this the BCE collapses to "predict absent" (recall ~0).
    bce = nn.BCEWithLogitsLoss(pos_weight=torch.tensor(9.0, device=DEVICE))
    t0 = time.time()
    for ep in range(epochs):
        net.train()
        idx = tr[torch.randperm(tr.numel())]
        tot = 0.0
        for i in range(0, idx.numel(), batch):
            b = idx[i:i + batch]
            x = amps[b].to(DEVICE)
            pt = presence[b].to(DEVICE); dt = depth[b].to(DEVICE)
            pl, dp = net(x)
            # presence BCE + depth MSE masked to truly-present ratios.
            loss = bce(pl, pt) + (((dp - dt) ** 2) * pt).sum(1).mean()
            opt.zero_grad(); loss.backward(); opt.step()
            tot += float(loss)
        sched.step()
        # quick val: presence F1-ish + depth error
        net.eval()
        with torch.no_grad():
            xb = amps[va].to(DEVICE)
            pl, dp = net(xb)
            pred = (torch.sigmoid(pl) > 0.5).float()
            gt = presence[va].to(DEVICE)
            tp = (pred * gt).sum(); prec = tp / (pred.sum() + 1e-9); rec = tp / (gt.sum() + 1e-9)
        print(f"ep{ep+1}/{epochs} loss={tot/(idx.numel()//batch):.4f} "
              f"presence P={float(prec):.3f} R={float(rec):.3f} ({time.time()-t0:.0f}s)")

    MODEL.parent.mkdir(parents=True, exist_ok=True)
    torch.save(net.state_dict(), MODEL)
    print(f"saved {MODEL}")


def predict_config(net, target_amps, thresh: float = 0.5, top_k: int = 8):
    """Target spectrum → Config via one forward pass (+ threshold)."""
    from phase3.config import Config, ModConfig
    from phase3.coverage_bank import curated_ratios
    paths = curated_ratios(5)
    x = torch.from_numpy(np.asarray(target_amps, np.float32)[:Q_MAX]).to(DEVICE)
    x = x / torch.clamp(x.sum(), min=1e-12)
    with torch.no_grad():
        pl, dp = net(x.unsqueeze(0))
        prob = torch.sigmoid(pl)[0]; depth = dp[0]
    # Take the most-confident ratios: those above `thresh`, but always at
    # least a few (the net is under-confident on this multimodal problem),
    # capped at top_k by probability.
    present = torch.nonzero(prob > thresh).flatten()
    if present.numel() < 4:
        present = torch.topk(prob, 4).indices
    if present.numel() > top_k:
        present = present[torch.topk(prob[present], top_k).indices]
    mods = [ModConfig(path=paths[int(j)], depth=float(depth[int(j)]), enabled=True)
            for j in present if float(depth[int(j)]) > 0.02]
    return Config(mods=mods + [ModConfig() for _ in range(max(0, 64 - len(mods)))])


def evaluate(n_probe: int = 300, seed: int = 123):
    """Recovery: net-predicted config's fingerprint vs held-out random targets,
    in the coherent GPU space."""
    from phase3.gpu_fm import (interp_matrix, render_batch, fingerprint_batch,
                               SAMPLE_RATE, F_CARRIER, audio_to_fingerprint)
    from phase3.coverage_bank import curated_ratios
    from phase3.config import path_ratio
    net = InvNet().to(DEVICE); net.load_state_dict(torch.load(MODEL)); net.eval()
    paths = curated_ratios(5); rv = np.array([path_ratio(p) for p in paths], np.float32)
    rvt = torch.from_numpy(rv).to(DEVICE)
    n_fft = 4096
    t = torch.arange(n_fft, device=DEVICE, dtype=torch.float32) / SAMPLE_RATE
    win = torch.hann_window(n_fft, device=DEVICE)
    W = interp_matrix(n_fft, SAMPLE_RATE, Q_MAX, F_CARRIER)
    g = torch.Generator(device=DEVICE).manual_seed(seed)
    l1s = []
    t0 = time.time()
    for _ in range(n_probe):
        k = int(torch.randint(1, N_OPS + 1, (1,), generator=g, device=DEVICE))
        ri = torch.randint(0, rvt.numel(), (1, k), generator=g, device=DEVICE)
        dp = torch.empty(1, k, device=DEVICE).uniform_(0.1, 5.0, generator=g)
        tgt = fingerprint_batch(render_batch(torch.log(rvt[ri]), dp,
                                torch.zeros(1, k, device=DEVICE), t, F_CARRIER), W, win)[0]
        cfg = predict_config(net, tgt.cpu().numpy())
        # render predicted config, fingerprint, compare
        rs, ds = cfg.ratios_and_depths()
        if not rs:
            l1s.append(2.0); continue
        lr2 = torch.log(torch.tensor([rs], device=DEVICE))
        dp2 = torch.tensor([ds], device=DEVICE)
        fp = fingerprint_batch(render_batch(lr2, dp2, torch.zeros(1, len(rs), device=DEVICE),
                               t, F_CARRIER), W, win)[0]
        l1s.append(float((fp - tgt).abs().sum()))
    l1s = np.array(l1s)
    print(f"inverse-net recovery over {n_probe} targets ({1000*(time.time()-t0)/n_probe:.1f} ms/target):")
    for p in (50, 90, 95):
        print(f"  p{p}: {np.percentile(l1s, p):.4f}")
    print(f"  mean {l1s.mean():.4f}  within 0.15: {(l1s<0.15).mean()*100:.1f}%")


def main():
    ap = argparse.ArgumentParser(); sub = ap.add_subparsers(dest="cmd", required=True)
    tr = sub.add_parser("train"); tr.add_argument("--epochs", type=int, default=8)
    sub.add_parser("eval")
    a = ap.parse_args()
    if a.cmd == "train":
        train(epochs=a.epochs)
    else:
        evaluate()


if __name__ == "__main__":
    main()
