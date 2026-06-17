"""Measured family axis — the single source of truth for "where a sound sits".

Replaces the old provenance-string taxonomy (build_pull_index / gpu_landscape
keyed the family off `provenance.category`, which conflated three things:
real spectral families, op-count density buckets, and vanity labels). Empirically
those categories overlapped more than they separated (intra-spread > nearest-
neighbour distance), and the family-search exemplars were contaminated by the
entropy-unfair `family_distance` classifier (a "Square" exemplar classified as
Bell/Sawtooth far more often than Square).

The fix, validated in phase3/sep_probe*.py:
  - The 12 canonical spectral families (sawtooth/square/triangle/pulse/organ/
    bell/prime_2..13) DO have clean, separable realisations in the GPU
    render-fingerprint space — they just aren't the contaminated factory
    exemplars. MINING the 80M bank for the configs whose rendered fingerprint
    is closest to each ideal family target yields exemplars that separate at
    94% held-out recall (10/12 families >=98%; only the marginal primes 5/11
    stay soft).
  - So the family CENTROID is the mean fingerprint of each family's cleanest
    bank-mined realisations, not the mean of whatever carried that provenance
    string. Classification is fair L1 to these centroids in fingerprint space.
  - Density (op-count / n_partials) is an ORTHOGONAL axis, never a family.

These centroids are mined ONCE (from the richest 6-op bank, best reachability)
and reused to classify every per-depth / per-waveform bank and every factory
preset, so the family axis is stable across all maps: a Sawtooth output spectrum
is Sawtooth regardless of which depth map or operator waveform produced it.

Run standalone to (re)mine the cached centroids:
    phase3/.venv-gpu/bin/python -m phase3.family_axis mine
"""
from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import torch

from phase3.brocot_writer import _human_family_category
from phase3.families import default_families
from phase3.gpu_bank import bank_path
from phase3.gpu_fm import DEVICE

Q_MAX = 64
CENTROIDS_NPZ = Path("phase3/derived/family_centroids.npz")
DEFAULT_TOPK = 400          # mined exemplars averaged into each family centroid

# Canonical family display names — must stay byte-identical to the runtime's
# SteerTab kShapeCategories and to brocot_writer._human_family_category output.
CANON = [_human_family_category(f.name) for f in default_families()]


def ideal_targets(q_max: int = Q_MAX) -> np.ndarray:
    """The 12 analytic family target vectors, L1-normalised, length q_max.

    These are the IDEAL spectra (sawtooth = 1/n, square = odd 1/n, ...). FM can't
    realise them exactly; we use them only as the search target for mining the
    closest REACHABLE bank configs (whose mean becomes the usable centroid)."""
    C = np.zeros((len(CANON), q_max), np.float64)
    for i, fam in enumerate(default_families()):
        t = np.asarray(fam.target_amplitudes(q_max), np.float64)
        s = t.sum()
        C[i] = t / s if s > 0 else t
    return C


def mine_centroids(top_k: int = DEFAULT_TOPK, n_ops: int = 6,
                   chunk: int = 500_000, verbose: bool = True):
    """Mine clean family centroids from a bank by render-fingerprint proximity to
    the ideal targets. Returns (centroids (F,Q) float32, reachability (F,) = the
    median L1 of each family's mined exemplars to its ideal — high = unreachable).
    """
    tgt = torch.from_numpy(ideal_targets(Q_MAX)).to(DEVICE).float()    # (F, Q)
    amps = np.load(bank_path(n_ops), mmap_mode="r")["amps"]
    N = amps.shape[0]
    F = len(CANON)
    if verbose:
        print(f"Mining {N:,} configs ({n_ops}-op bank) for top-{top_k} nearest "
              f"to each of {F} ideal targets ...")

    best_d = torch.full((F, top_k), 1e9, device=DEVICE)
    best_fp = torch.zeros((F, top_k, Q_MAX), device=DEVICE)
    for c in range(0, N, chunk):
        blk = torch.from_numpy(np.ascontiguousarray(amps[c:c + chunk])).to(DEVICE).float()
        d = (blk.unsqueeze(0) - tgt.unsqueeze(1)).abs().sum(2)         # (F, b)
        # Merge this chunk's candidates into the running per-family top-k.
        cat_d = torch.cat([best_d, d], 1)                             # (F, k+b)
        cat_fp = torch.cat([best_fp, blk.unsqueeze(0).expand(F, -1, -1)], 1)
        keep = cat_d.argsort(1)[:, :top_k]                            # (F, k)
        best_d = torch.gather(cat_d, 1, keep)
        best_fp = torch.gather(cat_fp, 1, keep.unsqueeze(-1).expand(-1, -1, Q_MAX))
        del blk, d, cat_d, cat_fp

    centroids = best_fp.mean(1)                                       # (F, Q)
    centroids = centroids / centroids.sum(1, keepdim=True).clamp(min=1e-12)
    reach = best_d.median(1).values.cpu().numpy()
    if verbose:
        print(f"  {'family':<22s}{'reach L1':>9s}")
        for i, fam in enumerate(CANON):
            print(f"  {fam:<22s}{reach[i]:>9.3f}")
    return centroids.cpu().numpy().astype(np.float32), reach


EXEMPLARS_NPZ = Path("phase3/derived/family_exemplars.npz")


def mine_exemplars(top_k: int = 64, n_ops: int = 6, chunk: int = 500_000,
                   out: Path = EXEMPLARS_NPZ, verbose: bool = True):
    """Capture each family's top-K cleanest bank configs WITH their reconstructable
    params (ridx/depths/feedback/waves), so build_pull_index can emit clean
    per-family pull exemplars — the same canonical realisations the maps steer
    toward, populating every family uniformly (primes included, which the factory
    presets don't cover). Mirrors mine_centroids but tracks bank indices.

    Saves an (F, top_k)-shaped table of params + the global bank indices.
    """
    tgt = torch.from_numpy(ideal_targets(Q_MAX)).to(DEVICE).float()
    bank = np.load(bank_path(n_ops), mmap_mode="r")
    amps = bank["amps"]
    N = amps.shape[0]
    F = len(CANON)
    if verbose:
        print(f"Mining top-{top_k} exemplar params per family from "
              f"{N:,} configs ({n_ops}-op bank) ...")

    best_d = torch.full((F, top_k), 1e9, device=DEVICE)
    best_i = torch.full((F, top_k), -1, dtype=torch.long, device=DEVICE)
    for c in range(0, N, chunk):
        blk = torch.from_numpy(np.ascontiguousarray(amps[c:c + chunk])).to(DEVICE).float()
        d = (blk.unsqueeze(0) - tgt.unsqueeze(1)).abs().sum(2)         # (F, b)
        idx = torch.arange(c, c + blk.shape[0], device=DEVICE).expand(F, -1)
        cat_d = torch.cat([best_d, d], 1)
        cat_i = torch.cat([best_i, idx], 1)
        keep = cat_d.argsort(1)[:, :top_k]
        best_d = torch.gather(cat_d, 1, keep)
        best_i = torch.gather(cat_i, 1, keep)
        del blk, d
    gi = best_i.cpu().numpy()                                          # (F, top_k) global idx
    dist = best_d.cpu().numpy()

    # Pull params for the mined indices (sorted unique read keeps mmap happy).
    uniq = np.unique(gi.ravel())
    ridx_u = bank["ridx"][uniq]; dep_u = bank["depths"][uniq]
    fb_u = bank["feedback"][uniq] if "feedback" in bank.files else np.zeros(len(uniq), np.float32)
    wv_u = bank["waves"][uniq] if "waves" in bank.files else np.zeros_like(bank["ridx"][uniq])
    pos = {int(g): k for k, g in enumerate(uniq)}
    sel = np.vectorize(lambda g: pos[int(g)])(gi)                     # (F, top_k) -> uniq row
    out.parent.mkdir(parents=True, exist_ok=True)
    np.savez(out, family_names=np.array(CANON), dist=dist,
             ridx=ridx_u[sel], depths=dep_u[sel].astype(np.float32),
             feedback=fb_u[sel].astype(np.float32), waves=wv_u[sel])
    if verbose:
        print(f"Wrote {out}: {F} families x {top_k} exemplars "
              f"({ridx_u[sel].shape[2]} ops each).")


def save_centroids(centroids: np.ndarray, reach: np.ndarray,
                   out: Path = CENTROIDS_NPZ) -> None:
    out.parent.mkdir(parents=True, exist_ok=True)
    np.savez(out, centroids=centroids, reachability=reach,
             family_names=np.array(CANON), q_max=Q_MAX)


def load_centroids(path: Path = CENTROIDS_NPZ):
    """(centroids (F,Q) float32, family_names list). Mine first if absent."""
    d = np.load(path, allow_pickle=True)
    return d["centroids"].astype(np.float32), list(d["family_names"])


def classify(fingerprints: np.ndarray, centroids: np.ndarray,
             chunk: int = 500_000):
    """Nearest-centroid (fair L1) labels + distances for a (N,Q) fingerprint
    block. fingerprints and centroids must be in the same L1-normalised
    integer-grid space (gpu_fm fingerprint space)."""
    C = torch.from_numpy(centroids).to(DEVICE).float()               # (F, Q)
    N = fingerprints.shape[0]
    labels = np.empty(N, np.int16)
    dists = np.empty(N, np.float32)
    for c in range(0, N, chunk):
        blk = torch.from_numpy(np.ascontiguousarray(fingerprints[c:c + chunk])) \
                   .to(DEVICE).float()
        d = (blk.unsqueeze(1) - C.unsqueeze(0)).abs().sum(2)         # (b, F)
        best = d.min(1)
        labels[c:c + chunk] = best.indices.cpu().numpy().astype(np.int16)
        dists[c:c + chunk] = best.values.cpu().numpy()
        del blk, d, best
    return labels, dists


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("mine", help="(re)mine and cache the family centroids")
    m.add_argument("--top-k", type=int, default=DEFAULT_TOPK)
    m.add_argument("--n-ops", type=int, default=6, help="bank to mine from")
    e = sub.add_parser("exemplars", help="mine per-family pull exemplars (params)")
    e.add_argument("--top-k", type=int, default=64)
    e.add_argument("--n-ops", type=int, default=6, help="bank to mine from")
    a = ap.parse_args()
    if a.cmd == "mine":
        cents, reach = mine_centroids(top_k=a.top_k, n_ops=a.n_ops)
        save_centroids(cents, reach)
        print(f"\nWrote {CENTROIDS_NPZ} ({len(CANON)} family centroids, "
              f"q_max={Q_MAX}).")
    elif a.cmd == "exemplars":
        mine_exemplars(top_k=a.top_k, n_ops=a.n_ops)


if __name__ == "__main__":
    main()
