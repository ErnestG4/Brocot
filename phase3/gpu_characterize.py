"""Characterize the FM-soundspace landscape: what the regions ACTUALLY are vs
what we labelled them.

The family labels are imposed (nearest-centroid to factory exemplars). The cloud
shows real emergent structure. This tool measures interpretable spectral
descriptors per config, then:
  A. profiles each imposed family — and suggests a characteristic-based name,
     flagging where it disagrees with the current label;
  B. unsupervised-clusters the bank and profiles each emergent cluster (its
     dominant family + descriptor) — "what we have" independent of the labels.

Run:  phase3/.venv-gpu/bin/python -m phase3.gpu_characterize [--n 500000] [--k 32]
Reads derived/gpu_bank.npz + derived/landscape_labels.npz.
"""

from __future__ import annotations

import argparse
from collections import Counter

import numpy as np

from phase3.gpu_bank import NPZ as BANK_NPZ
from phase3.gpu_landscape import OUT as LABELS_NPZ


# --- interpretable spectral descriptors over an integer-grid fingerprint -----
# amps[k] = energy at partial p=k+1 of the carrier (L1-normalised).

def features(amps: np.ndarray, feedback: np.ndarray) -> dict:
    a = amps.astype(np.float32)
    a = a / np.clip(a.sum(1, keepdims=True), 1e-9, None)
    p = np.arange(1, a.shape[1] + 1, dtype=np.float32)        # partial numbers
    centroid = a @ p                                          # mean partial (brightness)
    fundamental = a[:, 0]                                     # partial-1 energy
    n_eff = 1.0 / np.clip((a * a).sum(1), 1e-9, None)         # ~# active partials
    odd = a[:, 0::2].sum(1)                                   # partials 1,3,5,...
    entropy = -(a * np.log(a + 1e-9)).sum(1)
    cum = np.cumsum(a, 1)
    rolloff = (cum < 0.85).sum(1) + 1                         # partial holding 85% energy
    return dict(brightness=centroid, fundamental=fundamental, n_partials=n_eff,
                odd_frac=odd, entropy=entropy, rolloff=rolloff.astype(np.float32),
                feedback=feedback.astype(np.float32))


def descriptor(f: dict) -> str:
    """A short characteristic name from a profile of mean feature values."""
    parts = []
    b = f["brightness"]
    parts.append("dark" if b < 3 else "warm" if b < 7 else
                 "bright" if b < 16 else "brilliant")
    if f["fundamental"] > 0.55:           parts.append("pure")
    elif f["n_partials"] < 3.0:           parts.append("simple")
    elif f["n_partials"] > 14.0:          parts.append("dense")
    if f["odd_frac"] > 0.70:              parts.append("hollow")
    if f["feedback"] > 0.35:              parts.append("feedback")
    return " ".join(parts)


def _profile(feat: dict, mask) -> dict:
    return {k: float(np.mean(v[mask])) for k, v in feat.items()}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=500_000, help="subsample size")
    ap.add_argument("--k", type=int, default=32, help="unsupervised clusters")
    a = ap.parse_args()

    bank = np.load(BANK_NPZ)
    lab = np.load(LABELS_NPZ, allow_pickle=True)
    labels_all = lab["labels"]
    fam_names = list(lab["family_names"])
    N = bank["amps"].shape[0]

    rng = np.random.default_rng(0)
    idx = np.sort(rng.choice(N, min(a.n, N), replace=False))
    amps = bank["amps"][idx]
    fb = bank["feedback"][idx] if "feedback" in bank.files else np.zeros(len(idx), np.float32)
    labels = labels_all[idx]
    feat = features(amps, fb)

    # --- A. imposed families: profile + suggested name -----------------------
    print(f"\n=== A. imposed families — profile + suggested name "
          f"({len(idx):,} sampled) ===")
    print(f"  {'family':<24s}{'n':>7s} {'bright':>7s}{'nptl':>6s}{'odd':>5s}"
          f"{'fb':>5s}  suggested")
    print("  " + "-" * 78)
    fams = sorted(set(labels.tolist()))
    rows = []
    for fi in fams:
        m = labels == fi
        if m.sum() < 50:
            continue
        prof = _profile(feat, m)
        rows.append((fam_names[fi], int(m.sum()), prof, descriptor(prof)))
    for name, n, prof, sug in sorted(rows, key=lambda r: -r[1]):
        flag = "" if sug.split()[0] in name.lower() else "  <-- rename?"
        print(f"  {name:<24s}{n:>7d} {prof['brightness']:>7.1f}"
              f"{prof['n_partials']:>6.1f}{prof['odd_frac']:>5.2f}"
              f"{prof['feedback']:>5.2f}  {sug}{flag}")

    # --- B. emergent clusters (unsupervised) ---------------------------------
    print(f"\n=== B. emergent clusters (k={a.k}, what's ACTUALLY there) ===")
    try:
        from sklearn.cluster import MiniBatchKMeans
        X = np.stack([feat[k] for k in
                      ("brightness", "fundamental", "n_partials", "odd_frac",
                       "entropy", "rolloff", "feedback")], 1)
        X = (X - X.mean(0)) / (X.std(0) + 1e-9)
        km = MiniBatchKMeans(n_clusters=a.k, random_state=0, n_init=3).fit(X)
        cl = km.labels_
    except Exception as e:
        print(f"  (clustering unavailable: {e})")
        return

    print(f"  {'#':>3s}{'n':>8s} {'bright':>7s}{'nptl':>6s}{'odd':>5s}{'fb':>5s}"
          f"  descriptor  (dominant family)")
    print("  " + "-" * 76)
    for c in sorted(set(cl.tolist()), key=lambda c: -(cl == c).sum()):
        m = cl == c
        prof = _profile(feat, m)
        domfam = Counter(labels[m].tolist()).most_common(1)[0]
        frac = domfam[1] / m.sum()
        print(f"  {c:>3d}{int(m.sum()):>8d} {prof['brightness']:>7.1f}"
              f"{prof['n_partials']:>6.1f}{prof['odd_frac']:>5.2f}"
              f"{prof['feedback']:>5.2f}  {descriptor(prof):<22s}"
              f"{fam_names[domfam[0]]} {frac*100:.0f}%")


if __name__ == "__main__":
    main()
