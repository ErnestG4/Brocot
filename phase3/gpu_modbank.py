"""Mod-map offline pipeline — trajectory-fingerprint bank + the "6op mod" map.

Implements the pipeline designed in phase3/modmap.md (feasibility probed in
phase3/modmap_probe.py): each config is an ordinary 6-op parallel-FM patch in
gpu_bank's sampling style, plus ONE modulator (an LFO on either a random active
op's depth or on op-0 feedback). The fingerprint is the **trajectory
fingerprint** — K=4 frames of the ordinary 64-bin fingerprint rendered at
real-time offsets dt=120 ms of the LFO cycle, so modulation RATE enters the
geometry (probe P2), while frame 0 (LFO phase 0, scale=1) is exactly the
unmodulated timbre so families and retrieval stay timbre-defined (probe P3).

    build:    phase3/.venv-gpu/bin/python -m phase3.gpu_modbank build --n 2000000
    classify: phase3/.venv-gpu/bin/python -m phase3.gpu_modbank classify
    map:      phase3/.venv-gpu/bin/python -m phase3.gpu_modbank map --nodes 30000

Artifacts: derived/gpu_modbank.npz (bank; `amps` is (N, 4, 64) fp16 — frame 0
first — plus gpu_bank's ridx/depths/feedback/waves keys so gpu_paths._subsample
reads it unchanged, plus mod_* metadata), derived/landscape_labels_6mod.npz
(same keys gpu_landscape.classify writes), resources/landscape_graph_6mod.json.gz
(the map; standard node schema + per-node `mods`/`motion`/`rate_n`, doc-level
`"mod_map": 1`, NO jumps block — see modmap.md).

Torch imports are lazy: the module-level helpers are pure numpy so the CPU test
suite (tests/test_modbank.py) imports them without a GPU environment.
"""

from __future__ import annotations

import argparse
import time
from pathlib import Path

import numpy as np

MODBANK_NPZ = Path("phase3/derived/gpu_modbank.npz")
MODLABELS_NPZ = Path("phase3/derived/landscape_labels_6mod.npz")
MAP_OUT = Path("resources/landscape_graph_6mod.json.gz")

N_OPS = 6
K_FRAMES = 4            # trajectory frames (frame 0 = LFO phase 0 = base timbre)
FRAME_DT = 0.120        # seconds between frames (probe P2's rate-separable spacing)
RATE_LO, RATE_HI = 0.3, 7.0     # LFO rate range, ln-uniform (Hz)
MDEPTH_LO, MDEPTH_HI = 0.3, 1.0
MOD_BUDGET = 12.0       # gpu_bank's total-modulation budget

KIND_NAMES = ("depth", "feedback")      # mod_kind 0 / 1
SHAPE_NAMES = ("sine", "tri")           # mod_shape 0 / 1


# ---------------------------------------------------------------------------
# Pure helpers (numpy only — unit-tested CPU-side in tests/test_modbank.py)
# ---------------------------------------------------------------------------

def lfo_value(phase, shape):
    """LFO waveform value at `phase` (radians). shape 0=sine, 1=tri (both in
    [-1, 1], both zero at phase 0 so frame 0 is the unmodulated timbre).
    tri = (2/pi)·asin(sin(ph)) — the standard bipolar triangle."""
    phase = np.asarray(phase, np.float64)
    s = np.sin(phase)
    tri = (2.0 / np.pi) * np.arcsin(np.clip(s, -1.0, 1.0))
    return np.where(np.asarray(shape) == 1, tri, s)


def mod_scale(phase, mdepth, shape):
    """Multiplicative target scale: clip(1 + mdepth·lfo(phase), 0, None)."""
    return np.clip(1.0 + np.asarray(mdepth, np.float64) * lfo_value(phase, shape),
                   0.0, None)


def apply_modulator(depths, fb_rad, kind, op, scale):
    """Apply one modulator frame to a batch of configs.

    depths (b, K) op depths; fb_rad (b,) op-0 feedback in radians (0..pi);
    kind (b,) 0=depth 1=feedback; op (b,) target op for depth kind; scale (b,)
    from mod_scale. Returns (depths', fb_rad') copies.

    depth kind:    d[target] *= scale.
    feedback kind: the engine's 0..1 feedback PARAM is scaled then clamped to
                   [0, 1] before conversion back to radians (·pi) — feedback
                   cannot exceed the engine's param range mid-cycle."""
    depths = np.asarray(depths, np.float64)
    fb_rad = np.asarray(fb_rad, np.float64)
    kind = np.asarray(kind)
    scale = np.asarray(scale, np.float64)
    d = depths.copy()
    rows = np.arange(d.shape[0])
    d[rows, np.asarray(op)] *= np.where(kind == 0, scale, 1.0)
    fb01 = fb_rad / np.pi
    fb01 = np.where(kind == 1, np.clip(fb01 * scale, 0.0, 1.0), fb01)
    return d, fb01 * np.pi


def mods_entry(kind, op, shape, rate, mdepth):
    """One node's `mods` JSON list (a single modulator this pass)."""
    return [{"kind": KIND_NAMES[int(kind)],
             "op": int(op),
             "shape": SHAPE_NAMES[int(shape)],
             "rate": round(float(rate), 3),
             "depth": round(float(mdepth), 3)}]


def motion_raw(frames):
    """(n, K, Q) trajectory frames → (n,) mean frame-to-frame L1 distance."""
    frames = np.asarray(frames, np.float32)
    return np.abs(np.diff(frames, axis=1)).sum(-1).mean(-1)


def norm_unit(raw, p=98.0):
    """Normalise a nonnegative score to [0, 1] by its p-th percentile."""
    raw = np.asarray(raw, np.float32)
    hi = float(np.percentile(raw, p))
    return np.clip(raw / max(hi, 1e-9), 0.0, 1.0)


def rate_norm(rate, lo=RATE_LO, hi=RATE_HI):
    """LFO rate (Hz) → [0, 1], log-mapped over [lo, hi]."""
    r = np.log(np.clip(np.asarray(rate, np.float64), lo, hi))
    return (r - np.log(lo)) / (np.log(hi) - np.log(lo))


def node_extras(kind, op, shape, rate, mdepth, motion_n):
    """The mod-map node-dict EXTRAS (pure, JSON-ready): mods + motion + rate_n."""
    return {"motion": round(float(motion_n), 4),
            "rate_n": round(float(rate_norm(rate)), 4),
            "mods": mods_entry(kind, op, shape, rate, mdepth)}


# ---------------------------------------------------------------------------
# M1 — the trajectory-fingerprint bank
# ---------------------------------------------------------------------------

def _sample_chunk(rng, b, n_ratios):
    """Sample b configs + one modulator each, gpu_bank's config style.

    One deliberate deviation from gpu_bank's 50% feedback gate: configs whose
    modulator TARGETS feedback always get nonzero feedback (the gate applies
    only to depth-target configs) — an LFO on a zero feedback is silent, and
    silently-static nodes labelled "feedback modulated" would pollute the
    motion geometry."""
    ri = rng.integers(0, n_ratios, (b, N_OPS))
    keepn = rng.integers(1, N_OPS + 1, b)
    active = (np.arange(N_OPS)[None, :] < keepn[:, None]).astype(np.float64)
    dmax = np.clip(2.0 * MOD_BUDGET / keepn, None, 5.0)
    dp = rng.uniform(0.0, 1.0, (b, N_OPS)) * dmax[:, None] * active
    kind = rng.integers(0, 2, b)                      # 0=depth, 1=feedback
    fb = rng.uniform(0.0, np.pi, b)
    fb *= ((rng.random(b) < 0.5) | (kind == 1)).astype(np.float64)
    tgt = np.floor(rng.random(b) * keepn).astype(np.int64)   # random ACTIVE op
    tgt[kind == 1] = 0                                # feedback lives on op 0
    shape = rng.integers(0, 2, b)
    rate = np.exp(rng.uniform(np.log(RATE_LO), np.log(RATE_HI), b))
    mdepth = rng.uniform(MDEPTH_LO, MDEPTH_HI, b)
    return ri, dp, fb, kind, tgt, shape, rate, mdepth


def build(n_total: int = 2_000_000, chunk: int = 50_000, seed: int = 0,
          out: Path = MODBANK_NPZ):
    import torch
    from phase3.gpu_bank import N_FFT, Q_MAX, _ratio_values
    from phase3.gpu_fm import (DEVICE, F_CARRIER, SAMPLE_RATE,
                               fingerprint_batch, interp_matrix, render_batch)

    paths, rv = _ratio_values()
    rng = np.random.default_rng(seed)
    t = torch.arange(N_FFT, device=DEVICE, dtype=torch.float32) / SAMPLE_RATE
    win = torch.hann_window(N_FFT, device=DEVICE)
    W = interp_matrix(N_FFT, SAMPLE_RATE, Q_MAX, F_CARRIER)
    lograt = np.log(rv.astype(np.float64))

    amps = np.empty((n_total, K_FRAMES, Q_MAX), dtype=np.float16)
    ridx = np.empty((n_total, N_OPS), dtype=np.uint8)
    depths = np.empty((n_total, N_OPS), dtype=np.float16)
    feedback = np.empty((n_total,), dtype=np.float16)
    waves = np.zeros((n_total, N_OPS), dtype=np.uint8)       # sine-only this pass
    mod_kind = np.empty((n_total,), dtype=np.int8)
    mod_op = np.empty((n_total,), dtype=np.int8)
    mod_shape = np.empty((n_total,), dtype=np.int8)
    mod_rate = np.empty((n_total,), dtype=np.float16)
    mod_mdepth = np.empty((n_total,), dtype=np.float16)

    t0 = time.time()
    done = 0
    while done < n_total:
        b = min(chunk, n_total - done)
        ri, dp, fb, kind, tgt, shape, rate, mdepth = _sample_chunk(rng, b, len(rv))
        lr = torch.as_tensor(lograt[ri], dtype=torch.float32, device=DEVICE)
        det = torch.zeros(b, N_OPS, device=DEVICE)
        for k in range(K_FRAMES):
            ph = 2.0 * np.pi * rate * (k * FRAME_DT)         # real-time offsets
            d_k, fb_k = apply_modulator(dp, fb, kind, tgt,
                                        mod_scale(ph, mdepth, shape))
            fp = fingerprint_batch(
                render_batch(lr, torch.as_tensor(d_k, dtype=torch.float32,
                                                 device=DEVICE),
                             det, t, F_CARRIER,
                             feedback=torch.as_tensor(fb_k, dtype=torch.float32,
                                                      device=DEVICE)),
                W, win)
            amps[done:done + b, k] = fp.half().cpu().numpy()
        ridx[done:done + b] = ri.astype(np.uint8)
        depths[done:done + b] = dp.astype(np.float16)
        feedback[done:done + b] = fb.astype(np.float16)
        mod_kind[done:done + b] = kind.astype(np.int8)
        mod_op[done:done + b] = tgt.astype(np.int8)
        mod_shape[done:done + b] = shape.astype(np.int8)
        mod_rate[done:done + b] = rate.astype(np.float16)
        mod_mdepth[done:done + b] = mdepth.astype(np.float16)
        done += b
        if done % 200_000 == 0:
            print(f"  {done:,}/{n_total:,} ({time.time()-t0:.0f}s, "
                  f"{done/(time.time()-t0):,.0f} cfg/s x {K_FRAMES} frames)")

    out.parent.mkdir(parents=True, exist_ok=True)
    np.savez(out, amps=amps, ridx=ridx, depths=depths, feedback=feedback,
             waves=waves, mod_kind=mod_kind, mod_op=mod_op, mod_shape=mod_shape,
             mod_rate=mod_rate, mod_mdepth=mod_mdepth)
    print(f"\nmod bank: {n_total:,} configs x {K_FRAMES} frames @ {N_OPS} ops "
          f"({out.stat().st_size/1e9:.2f} GB) in {time.time()-t0:.0f}s -> {out}")


# ---------------------------------------------------------------------------
# Classify — families from the PHASE-0 frame only (families stay timbre-defined)
# ---------------------------------------------------------------------------

def classify(bank_npz: Path = MODBANK_NPZ, out: Path = MODLABELS_NPZ):
    """Label each mod-bank config by nearest family centroid of its frame-0
    (unmodulated) fingerprint. Writes the same keys gpu_landscape.classify
    writes (labels/dists/family_names/centroids) so gpu_paths' downstream
    machinery reads it unchanged."""
    from collections import Counter
    from phase3.family_axis import classify as fa_classify
    from phase3.family_axis import load_centroids

    t0 = time.time()
    centroids, fam_names = load_centroids()
    amps = np.load(bank_npz, mmap_mode="r")["amps"]           # (N, K, Q)
    frame0 = amps[:, 0, :]                                    # timbre frame only
    N = frame0.shape[0]
    print(f"Classifying {N:,} mod-bank rows (frame-0 fingerprints) against "
          f"{len(fam_names)} mined family centroids ...")
    labels, dists = fa_classify(frame0, centroids)

    counts = Counter(labels.tolist())
    for i in sorted(range(len(fam_names)), key=lambda i: -counts.get(i, 0)):
        n = counts.get(i, 0)
        print(f"  {fam_names[i]:<24s}{n:>10,d}{100*n/N:>6.1f}%")
    out.parent.mkdir(parents=True, exist_ok=True)
    np.savez(out, labels=labels, dists=dists,
             family_names=np.array(fam_names), centroids=centroids)
    print(f"Wrote {out} ({time.time()-t0:.0f}s)")


# ---------------------------------------------------------------------------
# M2 — the map (k-NN graph + embed on the FULL flattened trajectory vector)
# ---------------------------------------------------------------------------

def build_map(n_nodes: int = 30_000, k: int = 12, min_family: int = 30,
              name: str = "6op mod", bank_npz: Path = MODBANK_NPZ,
              labels_npz: Path = MODLABELS_NPZ, out: Path = MAP_OUT):
    """Mirror of gpu_paths.gradients with two mod-map differences: the k-NN
    graph, 2D embedding and criticality run on the FULL flattened (n, 256)
    trajectory vector (motion-aware neighbourhoods), while families, gradient
    anchors and spectral descriptors use the phase-0 frame (timbre-defined).
    Node dicts add `mods`/`motion`/`rate_n`; the doc adds `"mod_map": 1` and
    carries NO jumps block (time-varying morph semantics is a different space)."""
    import gzip as _gz
    import json as _json

    import torch
    from scipy.sparse import csr_matrix, hstack, vstack
    from scipy.sparse.csgraph import dijkstra

    from phase3.gpu_characterize import features as _spectral_features
    from phase3.gpu_fm import DEVICE, F_CARRIER
    from phase3.gpu_paths import (_curated_paths, _embed2d,
                                  _intrinsic_criticality, _knn_graph,
                                  _reconstruct, _structure_score, _subsample)

    t0 = time.time()
    print(f"Subsampling {n_nodes:,} nodes (stratified by family) ...")
    chosen, amps, node_lab, ridx, depths, node_fb, node_wv, fam_names = \
        _subsample(n_nodes, bank_npz=bank_npz, labels_npz=labels_npz)
    n = amps.shape[0]                       # amps: (n, K_FRAMES, Q) on GPU
    n_ops = int(ridx.shape[1])
    traj = amps.reshape(n, -1)              # (n, K*Q) — the motion-aware vector
    frame0 = amps[:, 0, :]                  # (n, Q) — the timbre frame

    # This map's mod metadata for the chosen nodes.
    mb = np.load(bank_npz, mmap_mode="r")
    m_kind = mb["mod_kind"][chosen]
    m_op = mb["mod_op"][chosen]
    m_shape = mb["mod_shape"][chosen]
    m_rate = mb["mod_rate"][chosen].astype(np.float32)
    m_mdepth = mb["mod_mdepth"][chosen].astype(np.float32)

    print(f"Building {k}-NN graph on the full {traj.shape[1]}-dim trajectory ...")
    g = _knn_graph(traj, k=k).tocsr()
    cur_paths = _curated_paths()

    lab = np.load(labels_npz, allow_pickle=True)
    centroids = torch.from_numpy(lab["centroids"]).to(DEVICE).float()   # (F, Q)
    anchor_k = 256

    traj_np = traj.cpu().numpy()
    print("Embedding (t-SNE on the trajectory vectors) ...")
    xy = _embed2d(traj_np)
    s2d = _structure_score(xy)
    sHD = _intrinsic_criticality(traj)

    def _norm(a):
        lo, hi = float(np.percentile(a, 2)), float(np.percentile(a, 98))
        return np.clip((a - lo) / max(hi - lo, 1e-6), 0.0, 1.0)
    struct = np.maximum(_norm(s2d), _norm(sHD)).astype(np.float32)

    frame0_np = frame0.cpu().numpy()
    feat = _spectral_features(frame0_np, node_fb)
    bmax = float(max(feat["brightness"].max(), 1e-6))
    dmax = float(max(feat["n_partials"].max(), 1e-6))
    fmax = float(max(feat["flatness"].max(), 1e-6))

    # Motion descriptor: mean frame-to-frame L1, p98-normalised to [0, 1].
    frames_np = amps.cpu().numpy()
    motion_n = norm_unit(motion_raw(frames_np))

    nodes = []
    for i in range(n):
        nd = {"family": int(node_lab[i]),
              "feedback": round(float(node_fb[i]) / float(np.pi), 4),
              "x": round(float(xy[i, 0]), 4), "y": round(float(xy[i, 1]), 4),
              "bright": round(float(feat["brightness"][i]) / bmax, 4),
              "dense":  round(float(feat["n_partials"][i]) / dmax, 4),
              "odd":    round(float(feat["odd_frac"][i]), 4),
              "tonal":  round(1.0 - float(feat["flatness"][i]) / fmax, 4),
              "crit":   round(float(struct[i]), 4)}
        nd.update(node_extras(m_kind[i], m_op[i], m_shape[i], m_rate[i],
                              m_mdepth[i], motion_n[i]))
        nd["ops"] = _reconstruct(i, ridx, depths, cur_paths, node_wv)
        nodes.append(nd)

    # Per-family next-hop gradient fields — anchors by frame-0 (timbre) distance
    # to the mined centroid; routes walk the motion-aware graph.
    print("Computing per-family gradient fields ...")
    fields = {}
    for fi, fam in enumerate(fam_names):
        members = np.where(node_lab == fi)[0]
        if members.size < min_family:
            continue
        mfp = frame0[torch.from_numpy(members).to(DEVICE)]           # (m, Q)
        cdist = (mfp - centroids[fi].unsqueeze(0)).abs().sum(1)
        kk = int(min(anchor_k, members.size))
        near_local = torch.topk(cdist, kk, largest=False).indices
        tgt = members[near_local.cpu().numpy()]
        top = hstack([g, csr_matrix((n, 1))])
        bottomrow = np.zeros(n + 1, np.float32); bottomrow[tgt] = 1e-6
        aug = vstack([top, csr_matrix(bottomrow)]).tocsr()
        dist, pred = dijkstra(aug, indices=n, return_predecessors=True)
        nh = pred[:n].astype(np.int32)
        nh[nh == n] = -1
        nh[~np.isfinite(dist[:n])] = -2
        fields[fam] = nh.tolist()

    doc = {
        "version": "1.0",
        "f_carrier_ref": F_CARRIER,
        "name": name,
        "n_ops": n_ops,
        "mod_map": 1,                  # gates mod-specific runtime UI
        "n_nodes": n,
        "n_families": len(fields),
        "family_names": list(fields.keys()),
        "nodes": nodes,
        "next_hop": fields,
    }
    out.parent.mkdir(parents=True, exist_ok=True)
    payload = (_json.dumps(doc, ensure_ascii=False) + "\n").encode("utf-8")
    with _gz.open(out, "wb", compresslevel=6) as f:
        f.write(payload)
    print(f"\nWrote {out} ({out.stat().st_size/1e6:.1f} MB): {n:,} nodes, "
          f"{len(fields)} family gradient fields in {time.time()-t0:.0f}s")

    # Sanity: routes from a neutral origin (pure carrier repeated in each frame).
    pure = torch.zeros(traj.shape[1], device=DEVICE)
    Q = frame0.shape[1]
    for kf in range(K_FRAMES):
        pure[kf * Q] = 1.0
    src = int((traj - pure).abs().sum(1).argmin().item())
    for fam in list(fields.keys())[:4]:
        nh = fields[fam]; node = src; hops = 0
        while nh[node] >= 0 and hops < 1000:
            node = nh[node]; hops += 1
        print(f"  origin -> {fam}: {hops} hops")

    # Honest numbers for modmap.md.
    from collections import Counter
    counts = Counter(int(x) for x in node_lab)
    print("\nfamily distribution (nodes):")
    for fi in sorted(counts, key=lambda fi: -counts[fi]):
        print(f"  {fam_names[fi]:<24s}{counts[fi]:>7,d}{100*counts[fi]/n:>6.1f}%")
    raw = motion_raw(frames_np)
    print(f"motion (raw mean frame-to-frame L1): p10 {np.percentile(raw,10):.3f}  "
          f"p50 {np.percentile(raw,50):.3f}  p90 {np.percentile(raw,90):.3f}  "
          f"p98 {np.percentile(raw,98):.3f}")
    hist, edges = np.histogram(motion_n, bins=5, range=(0.0, 1.0))
    print("motion_n histogram (5 bins 0..1): "
          + "  ".join(f"{100*h/n:.0f}%" for h in hist))
    kinds = Counter(int(x) for x in m_kind)
    shapes = Counter(int(x) for x in m_shape)
    print(f"mod kinds: depth {kinds.get(0,0):,} / feedback {kinds.get(1,0):,}   "
          f"shapes: sine {shapes.get(0,0):,} / tri {shapes.get(1,0):,}")


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("--n", type=int, default=2_000_000)
    b.add_argument("--chunk", type=int, default=50_000)
    b.add_argument("--seed", type=int, default=0)
    b.add_argument("--out", type=str, default=None)
    c = sub.add_parser("classify")
    c.add_argument("--bank", type=str, default=None)
    c.add_argument("--out", type=str, default=None)
    m = sub.add_parser("map")
    m.add_argument("--nodes", type=int, default=30_000)
    m.add_argument("--k", type=int, default=12)
    m.add_argument("--name", type=str, default="6op mod")
    m.add_argument("--bank", type=str, default=None)
    m.add_argument("--labels", type=str, default=None)
    m.add_argument("--out", type=str, default=None)
    a = ap.parse_args()
    if a.cmd == "build":
        build(n_total=a.n, chunk=a.chunk, seed=a.seed,
              out=(Path(a.out) if a.out else MODBANK_NPZ))
    elif a.cmd == "classify":
        classify(bank_npz=(Path(a.bank) if a.bank else MODBANK_NPZ),
                 out=(Path(a.out) if a.out else MODLABELS_NPZ))
    elif a.cmd == "map":
        build_map(n_nodes=a.nodes, k=a.k, name=a.name,
                  bank_npz=(Path(a.bank) if a.bank else MODBANK_NPZ),
                  labels_npz=(Path(a.labels) if a.labels else MODLABELS_NPZ),
                  out=(Path(a.out) if a.out else MAP_OUT))


if __name__ == "__main__":
    main()
