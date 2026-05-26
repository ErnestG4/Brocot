"""GPU landscape — classify the 10M fingerprint bank into the factory's timbre
families, the substrate for pre-plotted "settings-sweep" paths toward any family.

The idea (Will): the synth should do NO real-time spectral analysis. Instead we
pre-plot the landscape offline: every dense bank config is labelled with the
family it belongs to, so each family has a cloud of reachable settings around a
known attractor, and the runtime just follows a parameter morph toward it.

This module is the foundation:
  1. GPU-render the factory exemplars into the *same* q_max=64 integer-grid
     fingerprint space as gpu_bank (render → FFT → binning), so distances are
     coherent with the bank (mixing the analytic-predict q_max=32 space and the
     GPU-render space gives fake matches — see gpu_fm coherence rule).
  2. Per-family centroid = mean fingerprint of that family's exemplars.
  3. Classify each of the 10M bank rows to its nearest family centroid (GPU L1,
     chunked). Report coverage + separation quality.

    classify: phase3/.venv-gpu/bin/python -m phase3.gpu_landscape classify

Run under phase3/.venv-gpu (torch). Reads derived/gpu_bank.npz + the factory
preset tree; writes derived/landscape_labels.npz.
"""

from __future__ import annotations

import argparse
import json
import time
from collections import Counter
from pathlib import Path

import numpy as np
import torch

from phase3.config import path_ratio
from phase3.gpu_bank import NPZ as BANK_NPZ
from phase3.gpu_fm import (DEVICE, F_CARRIER, SAMPLE_RATE, fingerprint_batch,
                           interp_matrix, render_batch)

Q_MAX = 64                 # must match gpu_bank
N_FFT = 4096               # must match gpu_bank's render/fingerprint window
MAX_OPS = 64               # kNumOperators
FACTORY = Path("resources/presets/factory")
OUT = Path("phase3/derived/landscape_labels.npz")


def _preset_arrays(doc: dict):
    """(ratios, depths, detunes) float arrays of length MAX_OPS for a .brocot.
    Disabled / absent ops contribute nothing (depth 0, ratio 1)."""
    ratios = np.ones(MAX_OPS, np.float32)
    depths = np.zeros(MAX_OPS, np.float32)
    detune = np.zeros(MAX_OPS, np.float32)
    ops = (doc.get("engine", {}) or {}).get("operators", []) or []
    for i, o in enumerate(ops[:MAX_OPS]):
        if not o.get("enabled", False):
            continue
        p = o.get("path", "") or ""
        try:
            ratios[i] = float(path_ratio(p)) if p else 1.0
        except Exception:
            ratios[i] = 1.0
        depths[i] = float(o.get("depth", 0.0) or 0.0)
        detune[i] = float(o.get("detune", 0.0) or 0.0)
    return ratios, depths, detune


def _category(doc: dict) -> str:
    prov = doc.get("provenance", {}) or {}
    cat = (prov.get("category") or "").strip()
    if cat:
        return cat
    kind = (prov.get("kind") or "").strip()
    return kind.replace("-", " ").title() if kind else "Hand-authored"


def _load_factory():
    """Return (categories list, ratios, depths, detunes) stacked over presets."""
    cats, R, D, T = [], [], [], []
    for f in sorted(FACTORY.rglob("*.brocot")):
        try:
            doc = json.loads(f.read_text())
        except json.JSONDecodeError:
            continue
        r, d, t = _preset_arrays(doc)
        cats.append(_category(doc)); R.append(r); D.append(d); T.append(t)
    return cats, np.stack(R), np.stack(D), np.stack(T)


def _fingerprint(ratios, depths, detunes, W, t, win, chunk=400):
    """GPU render+fingerprint stacked preset params → (N, Q_MAX)."""
    out = []
    n = ratios.shape[0]
    for c in range(0, n, chunk):
        lr = torch.log(torch.from_numpy(ratios[c:c + chunk]).to(DEVICE))
        dp = torch.from_numpy(depths[c:c + chunk]).to(DEVICE)
        dt = torch.from_numpy(detunes[c:c + chunk]).to(DEVICE)
        fp = fingerprint_batch(render_batch(lr, dp, dt, t, F_CARRIER), W, win)
        out.append(fp.cpu())
    return torch.cat(out).to(DEVICE)


def classify():
    t0 = time.time()
    t = torch.arange(N_FFT, device=DEVICE, dtype=torch.float32) / SAMPLE_RATE
    win = torch.hann_window(N_FFT, device=DEVICE)
    W = interp_matrix(N_FFT, SAMPLE_RATE, Q_MAX, F_CARRIER)

    print("Rendering + fingerprinting factory exemplars on GPU ...")
    cats, R, D, T = _load_factory()
    fps = _fingerprint(R, D, T, W, t, win)              # (Npreset, Q_MAX)
    print(f"  {fps.shape[0]} exemplars fingerprinted ({time.time()-t0:.1f}s)")

    # Per-family centroid (mean fingerprint, re-normalised).
    fam_names = sorted(set(cats))
    fam_idx = {name: i for i, name in enumerate(fam_names)}
    cat_i = np.array([fam_idx[c] for c in cats])
    F = len(fam_names)
    centroids = torch.zeros(F, Q_MAX, device=DEVICE)
    for i in range(F):
        sel = fps[torch.from_numpy(np.where(cat_i == i)[0]).to(DEVICE)]
        centroids[i] = sel.mean(0)
    centroids = centroids / centroids.sum(1, keepdim=True).clamp(min=1e-12)
    print(f"  {F} family centroids")

    # Load the 10M bank and classify each row to nearest centroid (chunked L1).
    print("Classifying 10M bank rows to nearest family centroid ...")
    d = np.load(BANK_NPZ)
    amps = torch.from_numpy(d["amps"]).to(DEVICE).float()    # (N, Q_MAX)
    N = amps.shape[0]
    labels = np.empty(N, np.int16)
    dists = np.empty(N, np.float32)
    CH = 200_000
    for c in range(0, N, CH):
        block = amps[c:c + CH]                                # (b, Q)
        # (b, F) L1 distances to each centroid; argmin → family.
        dmat = (block.unsqueeze(1) - centroids.unsqueeze(0)).abs().sum(2)
        best = dmat.min(1)
        labels[c:c + CH] = best.indices.cpu().numpy().astype(np.int16)
        dists[c:c + CH] = best.values.cpu().numpy()

    counts = Counter(labels.tolist())
    print(f"\nClassified {N:,} configs into {F} families "
          f"({time.time()-t0:.0f}s total):\n")
    print(f"  {'family':<34s}{'bank configs':>13s}{'%':>7s}{'med L1':>8s}")
    print(f"  {'-'*34}{'-'*13:>13s}{'-'*6:>7s}{'-'*7:>8s}")
    for i in sorted(range(F), key=lambda i: -counts.get(i, 0)):
        sel = labels == i
        n = int(sel.sum())
        med = float(np.median(dists[sel])) if n else 0.0
        print(f"  {fam_names[i]:<34s}{n:>13,d}{100*n/N:>6.1f}%{med:>8.3f}")

    OUT.parent.mkdir(parents=True, exist_ok=True)
    np.savez(OUT, labels=labels, dists=dists,
             family_names=np.array(fam_names),
             centroids=centroids.cpu().numpy())
    print(f"\nWrote {OUT} ({OUT.stat().st_size/1e6:.1f} MB)")
    print(f"overall median nearest-centroid L1: {np.median(dists):.3f}  "
          f"p90 {np.percentile(dists,90):.3f}")


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("classify")
    a = ap.parse_args()
    if a.cmd == "classify":
        classify()


if __name__ == "__main__":
    main()
