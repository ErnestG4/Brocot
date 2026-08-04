"""Smooth cross-basin jumps ("wormholes") — ground truth + calibration.

The runtime morph between two map nodes is the stepped Stern-Brocot walk
mirrored by `phase3/morph_walk.py`. This module renders those trajectories
on the GPU in the bank's exact fingerprint space and measures how ROUGH
each pair's morph actually is, producing the dataset the jump scorer
trains/validates on and the calibration numbers the whole feature is
gated on (see phase3/jumps.md — the gates are pre-registered).

Per-pair roughness metrics (all rendered, never predicted):
  R_max      max fingerprint-L1 jump across an event bracket — the worst
             instantaneous "bump" (events = the walk's discrete ratio
             steps + the waveform snap at α=0.5)
  TV         total variation of the fingerprint along the merged
             (uniform ∪ event-bracket) alpha sequence
  TV_excess  TV − endpoint L1 (detour cost of the walk)
  alpha_worst  where the worst bump sits

Speed: instead of calling morph() per (pair, α), each pair precomputes a
per-slot RATIO LADDER (the ratio at every discrete step of its walk) and
the step index is vectorised over α with the same float32-product +
ties-to-even rounding the C++ uses. `_self_check()` asserts agreement
with the morph_walk mirror on random cases at every run — conformance
is checked, not assumed.

Graph distances use a 32-landmark approximation (min over landmarks of
d(l,a)+d(l,b), an upper bound) — exact all-pairs Dijkstra at 50k nodes
is not worth its cost for a stratification/feature signal.

Run (GPU venv):
  phase3/.venv-gpu/bin/python -m phase3.gpu_jumps dataset \
      --map resources/landscape_graph.json.gz --pairs 300000
  phase3/.venv-gpu/bin/python -m phase3.gpu_jumps calibrate
"""

from __future__ import annotations

import argparse
import gzip
import json
import time
from pathlib import Path as FsPath
from typing import List, Tuple

import numpy as np
import torch
from scipy.sparse.csgraph import dijkstra
from scipy.stats import rankdata

from phase3.gpu_fm import (DEVICE, F_CARRIER, SAMPLE_RATE, fingerprint_batch,
                           interp_matrix, render_batch)
from phase3.gpu_paths import _knn_graph
from phase3.morph_walk import (FEATURE_NAMES, Op, events, feature_vector,
                               morph, path_walk)

REPO = FsPath(__file__).resolve().parent.parent
DERIVED = REPO / "phase3" / "derived"
DEFAULT_MAP = REPO / "resources" / "landscape_graph.json.gz"
DATASET_NPZ = DERIVED / "jumps_dataset.npz"
CALIB_JSON = DERIVED / "jumps_calibration.json"

Q_MAX, N_FFT = 64, 4096
N_UNIFORM = 17          # uniform trajectory alphas (includes 0 and 1)
EVENT_EPS = 1e-3        # bracket half-width around each event
MAX_EVENTS = 48         # cap per pair (rare monster walks; count reported)
LANDMARKS = 32          # graph-distance approximation sources

STRATA = {              # fractions of --pairs (see jumps.md dataset design)
    "knn_edge": 0.10, "within": 0.20, "cross": 0.40,
    "suspect": 0.15, "prefix": 0.15,
}
STRATUM_CODE = {name: i for i, name in enumerate(STRATA)}


# ---------------------------------------------------------------------------
# Map loading
# ---------------------------------------------------------------------------

def load_map(path: FsPath) -> dict:
    raw = gzip.open(path, "rb").read() if path.suffix == ".gz" else path.read_bytes()
    return json.loads(raw)


def node_states(doc: dict) -> Tuple[List[List[Op]], np.ndarray, np.ndarray]:
    """Per node: padded Op list (map n_ops slots), feedback, family."""
    n_ops = int(doc.get("n_ops", 6))
    ops_all, fb, fam = [], [], []
    for nd in doc["nodes"]:
        ops = [Op(o.get("path", ""), float(o.get("depth", 0.0)),
                  bool(o.get("enabled", False)), int(o.get("wave", 0)))
               for o in nd["ops"][:n_ops]]
        while len(ops) < n_ops:
            ops.append(Op("", 0.0, False, 0))
        ops_all.append(ops)
        fb.append(float(nd.get("feedback", 0.0)))
        fam.append(int(nd.get("family", -1)))
    return ops_all, np.asarray(fb, np.float32), np.asarray(fam, np.int32)


# ---------------------------------------------------------------------------
# Trajectory ladders — vectorised morph sampling
# ---------------------------------------------------------------------------

def _walk_ratio(path: str) -> float:
    lp, lq, rp, rq, p, q = 0, 1, 1, 0, 1, 1
    for s in path:
        if s == "L":
            rp, rq = p, q
        else:
            lp, lq = p, q
        p, q = lp + rp, lq + rq
    return p / q


def _ladder_paths(frm: str, to: str) -> List[str]:
    """Path string at every integer step of the walk (index 0 = frm)."""
    lca = 0
    for a, b in zip(frm, to):
        if a != b:
            break
        lca += 1
    up = len(frm) - lca
    out = [frm[: len(frm) - s] for s in range(up + 1)]
    out += [to[: lca + s] for s in range(1, len(to) - lca + 1)]
    return out


def _steps_for(alphas: np.ndarray, total: int) -> np.ndarray:
    """Vectorised mirror of the walk's step function: float32 product,
    round-half-to-even (juce::roundToInt semantics), clipped."""
    if total == 0:
        return np.zeros(len(alphas), np.int64)
    x = (alphas.astype(np.float32) * np.float32(total)).astype(np.float64)
    return np.clip(np.rint(x).astype(np.int64), 0, total)


class PairLadder:
    """Per-pair precomputation: sample ratio/depth/wave/feedback at any
    alpha vector without re-walking strings."""

    def __init__(self, cur: List[Op], tgt: List[Op], fb_a: float, fb_b: float):
        k = max(len(cur), len(tgt))
        pad = Op("", 0.0, False, 0)
        cur = list(cur) + [pad] * (k - len(cur))
        tgt = list(tgt) + [pad] * (k - len(tgt))
        self.k = k
        self.fb_a, self.fb_b = fb_a, fb_b
        self.ladders: List[np.ndarray] = []      # per slot, ratio at each step
        self.totals: List[int] = []
        self.d0 = np.array([o.depth if o.enabled else 0.0 for o in cur], np.float64)
        self.d1 = np.array([o.depth if o.enabled else 0.0 for o in tgt], np.float64)
        self.w0 = np.array([o.wave for o in cur], np.int64)
        self.w1 = np.array([o.wave for o in tgt], np.int64)
        for oc, ot in zip(cur, tgt):
            paths = _ladder_paths(oc.path, ot.path)
            self.ladders.append(np.array([_walk_ratio(p) for p in paths]))
            self.totals.append(len(paths) - 1)

    def sample(self, alphas: np.ndarray):
        """(ratio, depth, wave, feedback) arrays for an alpha vector:
        shapes (A, k), (A, k), (A, k), (A,)."""
        a = np.clip(alphas, 0.0, 1.0)
        ratio = np.empty((len(a), self.k))
        for s, (lad, tot) in enumerate(zip(self.ladders, self.totals)):
            ratio[:, s] = lad[_steps_for(a, tot)]
        depth = (1.0 - a)[:, None] * self.d0[None, :] + a[:, None] * self.d1[None, :]
        wave = np.where(a[:, None] < 0.5, self.w0[None, :], self.w1[None, :])
        fb = (1.0 - a) * self.fb_a + a * self.fb_b
        return ratio, depth, wave, fb


def _self_check(rng: np.random.Generator, n: int = 4000) -> None:
    """Ladders must agree exactly with the morph_walk mirror."""
    alphabet = ["", "L", "R", "LL", "LR", "RL", "RRL", "LRLR", "RRRRL"]
    for _ in range(n):
        frm = str(rng.choice(alphabet))
        to = str(rng.choice(alphabet))
        alpha = float(rng.random())
        lad = _ladder_paths(frm, to)
        tot = len(lad) - 1
        step = int(_steps_for(np.array([alpha]), tot)[0])
        if lad[step] != path_walk(frm, to, alpha):
            raise AssertionError(
                f"ladder/path_walk divergence: {frm!r}->{to!r} @ {alpha}")
    # And a few full morphs (depth/wave/enable semantics).
    for _ in range(200):
        cur = [Op(str(rng.choice(alphabet)), float(rng.random() * 3),
                  bool(rng.random() < 0.8), int(rng.integers(0, 4)))
               for _ in range(3)]
        tgt = [Op(str(rng.choice(alphabet)), float(rng.random() * 3),
                  bool(rng.random() < 0.8), int(rng.integers(0, 4)))
               for _ in range(3)]
        alpha = float(rng.random())
        pl = PairLadder(cur, tgt, 0.2, 0.7)
        ratio, depth, wave, _ = pl.sample(np.array([alpha]))
        for s, o in enumerate(morph(cur, tgt, alpha)):
            if abs(_walk_ratio(o.path) - ratio[0, s]) > 1e-12:
                raise AssertionError("ratio divergence vs morph()")
            # morph lerps depth in float32; the ladder uses float64 — the
            # ~1e-7 gap is inaudible and irrelevant to rendering, but the
            # values must agree to that scale.
            if abs(o.depth - depth[0, s]) > 1e-5:
                raise AssertionError("depth divergence vs morph()")
            if o.wave != wave[0, s] and o.depth > 1e-6:
                raise AssertionError("wave divergence vs morph()")


# ---------------------------------------------------------------------------
# Rendering
# ---------------------------------------------------------------------------

def _render_fps(ratio: np.ndarray, depth: np.ndarray, wave: np.ndarray,
                fb: np.ndarray, t, win, W, sub: int = 24000) -> torch.Tensor:
    """(B,K) config arrays → (B, Q_MAX) fp32 fingerprints, sub-batched."""
    out = []
    for c in range(0, len(ratio), sub):
        lr = torch.log(torch.as_tensor(ratio[c:c + sub], dtype=torch.float32,
                                       device=DEVICE))
        d = torch.as_tensor(depth[c:c + sub], dtype=torch.float32, device=DEVICE)
        wv = torch.as_tensor(wave[c:c + sub], dtype=torch.float32, device=DEVICE)
        beta = torch.as_tensor(fb[c:c + sub], dtype=torch.float32,
                               device=DEVICE) * float(np.pi)
        det = torch.zeros_like(lr)
        x = render_batch(lr, d, det, t, F_CARRIER, feedback=beta, waves=wv)
        out.append(fingerprint_batch(x, W, win).float())
    return torch.cat(out) if out else torch.empty(0, Q_MAX, device=DEVICE)


def render_node_fps(ops_all, fb, batch: int = 4096) -> torch.Tensor:
    t = torch.arange(N_FFT, device=DEVICE, dtype=torch.float32) / SAMPLE_RATE
    win = torch.hann_window(N_FFT, device=DEVICE)
    W = interp_matrix(N_FFT, SAMPLE_RATE, Q_MAX, F_CARRIER)
    k = len(ops_all[0])
    ratio = np.array([[_walk_ratio(o.path) for o in ops] for ops in ops_all])
    depth = np.array([[o.depth if o.enabled else 0.0 for o in ops]
                      for ops in ops_all])
    wave = np.array([[o.wave for o in ops] for ops in ops_all])
    return _render_fps(ratio.reshape(-1, k), depth.reshape(-1, k),
                       wave.reshape(-1, k), fb, t, win, W, sub=batch)


# ---------------------------------------------------------------------------
# Pair sampling strata
# ---------------------------------------------------------------------------

def _landmark_dists(graph, n: int, rng) -> np.ndarray:
    """(LANDMARKS, n) Dijkstra distances from random landmark nodes."""
    src = rng.choice(n, size=min(LANDMARKS, n), replace=False)
    return dijkstra(graph, indices=src, directed=False), src


def _approx_graph_dist(dl: np.ndarray, a: np.ndarray, b: np.ndarray) -> np.ndarray:
    """Upper-bound graph distance via landmarks: min_l d(l,a)+d(l,b)."""
    return np.min(dl[:, a] + dl[:, b], axis=0)


def sample_pairs(n_pairs: int, fam: np.ndarray, node_fps: torch.Tensor,
                 graph, ops_all, rng) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Returns (pairs (P,2) int64, stratum (P,) int8, graph_dist (P,) f32)."""
    n = len(fam)
    coo = graph.tocoo()
    edge_mask = coo.row < coo.col
    edges = np.stack([coo.row[edge_mask], coo.col[edge_mask]], 1)

    dl, _ = _landmark_dists(graph, n, rng)

    def rand_pairs(count, same_family):
        out = np.empty((0, 2), np.int64)
        while len(out) < count:
            a = rng.integers(0, n, count * 2)
            b = rng.integers(0, n, count * 2)
            ok = a != b
            ok &= (fam[a] == fam[b]) if same_family else (fam[a] != fam[b])
            cand = np.stack([a[ok], b[ok]], 1)
            out = np.concatenate([out, cand])[:count]
        return out

    def endpoint_l1(p):
        out = np.empty(len(p), np.float32)
        for c in range(0, len(p), 8192):
            s = slice(c, c + 8192)
            out[s] = (node_fps[p[s, 0]] - node_fps[p[s, 1]]).abs().sum(1).cpu().numpy()
        return out

    pairs, strat = [], []

    n_edge = int(n_pairs * STRATA["knn_edge"])
    idx = rng.choice(len(edges), size=min(n_edge, len(edges)), replace=False)
    pairs.append(edges[idx]); strat.append(np.full(len(idx), STRATUM_CODE["knn_edge"]))

    n_within = int(n_pairs * STRATA["within"])
    pairs.append(rand_pairs(n_within, True))
    strat.append(np.full(n_within, STRATUM_CODE["within"]))

    # Cross-family, stratified by endpoint-L1 decile: oversample then thin so
    # every decile of the candidate pool is represented equally.
    n_cross = int(n_pairs * STRATA["cross"])
    pool = rand_pairs(n_cross * 3, False)
    l1 = endpoint_l1(pool)
    deciles = np.clip((rankdata(l1) - 1) / len(l1) * 10, 0, 9).astype(int)
    take = []
    per = n_cross // 10
    for d in range(10):
        cand = np.where(deciles == d)[0]
        take.append(rng.choice(cand, size=min(per, len(cand)), replace=False))
    take = np.concatenate(take)
    pairs.append(pool[take]); strat.append(np.full(len(take), STRATUM_CODE["cross"]))

    # Wormhole suspects: endpoint-L1 low, landmark-graph-distance high.
    n_sus = int(n_pairs * STRATA["suspect"])
    pool = rand_pairs(n_sus * 5, False)
    l1 = endpoint_l1(pool)
    gd = _approx_graph_dist(dl, pool[:, 0], pool[:, 1])
    score = gd / np.maximum(l1, 1e-3)
    take = np.argsort(-score)[:n_sus]
    pairs.append(pool[take]); strat.append(np.full(len(take), STRATUM_CODE["suspect"]))

    # Walk-cost extremes: prefix-aligned (cheap walk) vs prefix-disjoint.
    n_pre = int(n_pairs * STRATA["prefix"])
    pool = rand_pairs(n_pre * 4, False)
    cost = np.empty(len(pool))
    for i, (a, b) in enumerate(pool):
        cost[i] = sum(len(_ladder_paths(oa.path, ob.path)) - 1
                      for oa, ob in zip(ops_all[a], ops_all[b]))
    order = np.argsort(cost)
    half = n_pre // 2
    take = np.concatenate([order[:half], order[-(n_pre - half):]])
    pairs.append(pool[take]); strat.append(np.full(len(take), STRATUM_CODE["prefix"]))

    P = np.concatenate(pairs).astype(np.int64)
    S = np.concatenate(strat).astype(np.int8)
    G = _approx_graph_dist(dl, P[:, 0], P[:, 1]).astype(np.float32)
    return P, S, G


# ---------------------------------------------------------------------------
# Dataset build
# ---------------------------------------------------------------------------

def dataset(map_path: FsPath, n_pairs: int, seed: int, out: FsPath,
            ratio_mode: str = "walk", chunk_pairs: int = 1200) -> None:
    rng = np.random.default_rng(seed)
    _self_check(rng)
    t0 = time.time()

    doc = load_map(map_path)
    ops_all, fb, fam = node_states(doc)
    n = len(ops_all)
    print(f"map {map_path.name}: {n:,} nodes, n_ops={len(ops_all[0])}")

    node_fps = render_node_fps(ops_all, fb)
    graph = _knn_graph(node_fps, k=12)
    print(f"node fps + knn graph  [{time.time()-t0:.1f}s]")

    P, S, G = sample_pairs(n_pairs, fam, node_fps, graph, ops_all, rng)
    print(f"{len(P):,} pairs sampled  [{time.time()-t0:.1f}s]")

    t = torch.arange(N_FFT, device=DEVICE, dtype=torch.float32) / SAMPLE_RATE
    win = torch.hann_window(N_FFT, device=DEVICE)
    W = interp_matrix(N_FFT, SAMPLE_RATE, Q_MAX, F_CARRIER)
    uni = np.linspace(0.0, 1.0, N_UNIFORM)

    m_rmax = np.zeros(len(P), np.float32)
    m_tv = np.zeros(len(P), np.float32)
    m_tvx = np.zeros(len(P), np.float32)
    m_aw = np.zeros(len(P), np.float32)
    m_el1 = np.zeros(len(P), np.float32)
    feats = np.zeros((len(P), len(FEATURE_NAMES)), np.float32)
    n_event_capped = 0

    for c0 in range(0, len(P), chunk_pairs):
        c1 = min(c0 + chunk_pairs, len(P))
        alphas_per, ladders = [], []
        for i in range(c0, c1):
            a, b = P[i]
            cur, tgt = ops_all[a], ops_all[b]
            if ratio_mode == "walk":
                ev = events(cur, tgt)
                if len(ev) > MAX_EVENTS:
                    ev = ev[:MAX_EVENTS]
                    n_event_capped += 1
            else:                       # glide: only the wave snap is discrete
                ev = [0.5] if any(
                    oc.wave != ot.wave and (oc.enabled or ot.enabled)
                    for oc, ot in zip(cur, tgt)) else []
            br = np.array(ev, np.float64)
            alphas = np.unique(np.clip(np.concatenate(
                [uni, br - EVENT_EPS, br + EVENT_EPS]), 0.0, 1.0))
            alphas_per.append((alphas, np.array(ev)))
            ladders.append(PairLadder(cur, tgt, fb[a], fb[b]))
            feats[i] = feature_vector(cur, tgt)

        # Flatten chunk configs → one render.
        counts = np.array([len(a) for a, _ in alphas_per])
        offs = np.concatenate([[0], np.cumsum(counts)])
        k = ladders[0].k
        R = np.empty((offs[-1], k)); D = np.empty((offs[-1], k))
        Wv = np.empty((offs[-1], k)); F = np.empty(offs[-1])
        for j, ((alphas, _), pl) in enumerate(zip(alphas_per, ladders)):
            if ratio_mode == "walk":
                r, d, w, f = pl.sample(alphas)
            else:
                r0, _, w, f = pl.sample(np.zeros(1))
                r1 = pl.sample(np.ones(1))[0]
                lr = ((1 - alphas)[:, None] * np.log2(np.maximum(r0, 1e-9))
                      + alphas[:, None] * np.log2(np.maximum(r1, 1e-9)))
                r = np.power(2.0, lr)
                _, d, w, f = pl.sample(alphas)
            R[offs[j]:offs[j + 1]] = r; D[offs[j]:offs[j + 1]] = d
            Wv[offs[j]:offs[j + 1]] = w; F[offs[j]:offs[j + 1]] = f

        fps = _render_fps(R, D, Wv, F, t, win, W)

        # Per-pair metrics from the flat fingerprints.
        d_seq = (fps[1:] - fps[:-1]).abs().sum(1).cpu().numpy()
        fps_cpu0 = fps[offs[:-1]].cpu().numpy()   # alpha=0 rows
        fps_cpu1 = fps[offs[1:] - 1].cpu().numpy()  # alpha=1 rows
        m_el1[c0:c1] = np.abs(fps_cpu0 - fps_cpu1).sum(1)
        for j in range(c1 - c0):
            alphas, ev = alphas_per[j]
            seg = d_seq[offs[j]:offs[j + 1] - 1]
            m_tv[c0 + j] = seg.sum()
            m_tvx[c0 + j] = max(0.0, m_tv[c0 + j] - m_el1[c0 + j])
            if len(ev):
                rmax, aw = 0.0, 0.0
                for e in ev:
                    lo = np.searchsorted(alphas, np.clip(e - EVENT_EPS, 0, 1))
                    hi = np.searchsorted(alphas, np.clip(e + EVENT_EPS, 0, 1))
                    lo, hi = min(lo, len(alphas) - 1), min(hi, len(alphas) - 1)
                    if hi > lo:
                        jump = float(np.abs(
                            fps[offs[j] + hi].cpu().numpy()
                            - fps[offs[j] + lo].cpu().numpy()).sum())
                        if jump > rmax:
                            rmax, aw = jump, float(e)
                m_rmax[c0 + j], m_aw[c0 + j] = rmax, aw
        if (c0 // chunk_pairs) % 20 == 0:
            print(f"  {c1:,}/{len(P):,} pairs  [{time.time()-t0:.1f}s]")

    out.parent.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(
        out, a=P[:, 0].astype(np.int32), b=P[:, 1].astype(np.int32),
        stratum=S, graph_dist=G,
        same_family=(fam[P[:, 0]] == fam[P[:, 1]]),
        endpoint_l1=m_el1, r_max=m_rmax, tv=m_tv, tv_excess=m_tvx,
        alpha_worst=m_aw, features=feats.astype(np.float16),
        feature_names=np.array(FEATURE_NAMES),
        map=str(map_path.name), ratio_mode=ratio_mode, seed=seed)
    print(f"wrote {out}  ({len(P):,} pairs, {n_event_capped} event-capped)"
          f"  [{time.time()-t0:.1f}s]")


# ---------------------------------------------------------------------------
# Calibration — the pre-registered gate
# ---------------------------------------------------------------------------

def _spear(a, b) -> float:
    ra, rb = rankdata(a), rankdata(b)
    ra = (ra - ra.mean()) / (ra.std() + 1e-12)
    rb = (rb - rb.mean()) / (rb.std() + 1e-12)
    return float((ra * rb).mean())


def calibrate(ds_path: FsPath) -> dict:
    d = np.load(ds_path, allow_pickle=False)
    S, rmax = d["stratum"], d["r_max"]
    knn = rmax[S == STRATUM_CODE["knn_edge"]]
    within = rmax[(S == STRATUM_CODE["within"])]
    cross = rmax[(S == STRATUM_CODE["cross"])]

    med = {k: float(np.median(v)) for k, v in
           [("knn_edge", knn), ("within_family", within), ("cross_family", cross)]}
    rho_el1 = _spear(rmax, d["endpoint_l1"])
    rho_tv = _spear(rmax, d["tv"])
    ordering_ok = med["knn_edge"] < med["within_family"] < med["cross_family"]
    # "well below 1": the walk metric must not be a re-labelling of endpoint
    # L1 — below 0.9 leaves the net genuine signal to add.
    gate_pass = bool(ordering_ok and rho_el1 < 0.9)

    out = {
        "dataset": str(ds_path.name), "map": str(d["map"]),
        "ratio_mode": str(d["ratio_mode"]), "n_pairs": int(len(rmax)),
        "median_r_max": med,
        "spearman_r_max_vs_endpoint_l1": round(rho_el1, 4),
        "spearman_r_max_vs_tv": round(rho_tv, 4),
        "ordering_ok": ordering_ok, "gate_pass": gate_pass,
    }
    CALIB_JSON.write_text(json.dumps(out, indent=1) + "\n")
    print(json.dumps(out, indent=1))
    print("GATE:", "PASS — proceed to training/mining" if gate_pass else
          "FAIL — walk roughness is not a distinct signal; document and stop")
    return out


# ---------------------------------------------------------------------------
# JumpNet — pair roughness surrogate (§A3). Success/negative-result numbers
# are pre-registered in jumps.md; the baselines below are the bar.
# ---------------------------------------------------------------------------

JUMPNET_PT = DERIVED / "jumpnet.pt"
EVAL_JSON = DERIVED / "jumpnet_eval.json"
N_NODE_SCALARS = 8   # n_active, depth_sum, depth_max, feedback, wave hist (4)


def _node_scalars(ops_all, fb) -> np.ndarray:
    out = np.zeros((len(ops_all), N_NODE_SCALARS), np.float32)
    for i, ops in enumerate(ops_all):
        act = [o for o in ops if o.enabled and o.depth > 1e-4]
        out[i, 0] = len(act)
        out[i, 1] = sum(o.depth for o in act)
        out[i, 2] = max((o.depth for o in act), default=0.0)
        out[i, 3] = fb[i]
        for o in act:
            out[i, 4 + min(o.wave, 3)] += 1.0
    return out


class JumpNet(torch.nn.Module):
    """Siamese towers over (fingerprint ⊕ node scalars), symmetric pooling
    [h_a + h_b, |h_a − h_b|], engineered pair features appended, two heads:
    log(R_max + 1e-3), log(TV + 1e-3). Deliberately invnet-scale."""

    def __init__(self, n_feats: int = len(FEATURE_NAMES)):
        super().__init__()
        nn = torch.nn
        self.tower = nn.Sequential(
            nn.Linear(Q_MAX + N_NODE_SCALARS, 256), nn.GELU(),
            nn.Linear(256, 256), nn.GELU())
        self.head = nn.Sequential(
            nn.Linear(512 + n_feats, 256), nn.GELU(),
            nn.Linear(256, 2))

    def forward(self, xa, xb, feats):
        ha, hb = self.tower(xa), self.tower(xb)
        return self.head(torch.cat([ha + hb, (ha - hb).abs(), feats], dim=1))

    def embed(self, x):
        return self.tower(x)


def _node_split(a: np.ndarray, b: np.ndarray, n_nodes: int, seed: int,
                held_frac: float = 0.05):
    """Node-disjoint split: pairs touching a held-out node are validation,
    the rest train. Pair-level splits leak node identity — never use them."""
    rng = np.random.default_rng(seed + 1)
    held = np.zeros(n_nodes, bool)
    held[rng.choice(n_nodes, int(n_nodes * held_frac), replace=False)] = True
    val = held[a] | held[b]
    return ~val, val


def _spearman_np(a: np.ndarray, b: np.ndarray) -> float:
    return _spear(a, b)


def train(ds_path: FsPath, map_path: FsPath, epochs: int = 15,
          batch: int = 8192, lr: float = 2e-3, seed: int = 0) -> dict:
    d = np.load(ds_path, allow_pickle=False)
    doc = load_map(map_path)
    ops_all, fb, _fam = node_states(doc)
    node_fps = render_node_fps(ops_all, fb)
    scal = torch.as_tensor(_node_scalars(ops_all, fb), device=DEVICE)
    X = torch.cat([node_fps, scal], dim=1)                       # (n, 72)

    a, b = d["a"].astype(np.int64), d["b"].astype(np.int64)
    feats = torch.as_tensor(d["features"].astype(np.float32), device=DEVICE)
    y = torch.as_tensor(np.stack([
        np.log(d["r_max"].astype(np.float64) + 1e-3),
        np.log(d["tv"].astype(np.float64) + 1e-3)], 1).astype(np.float32),
        device=DEVICE)

    tr, va = _node_split(a, b, len(ops_all), seed)
    ta = torch.as_tensor(a, device=DEVICE)
    tb = torch.as_tensor(b, device=DEVICE)
    tr_idx = torch.as_tensor(np.where(tr)[0], device=DEVICE)
    va_idx = torch.as_tensor(np.where(va)[0], device=DEVICE)
    print(f"train {len(tr_idx):,} / val {len(va_idx):,} pairs "
          f"(node-disjoint 5%)")

    torch.manual_seed(seed)
    net = JumpNet().to(DEVICE)
    opt = torch.optim.AdamW(net.parameters(), lr=lr)
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(
        opt, T_max=epochs * (len(tr_idx) // batch + 1))
    huber = torch.nn.HuberLoss()

    for ep in range(epochs):
        perm = tr_idx[torch.randperm(len(tr_idx), device=DEVICE)]
        tot = 0.0
        for c in range(0, len(perm), batch):
            idx = perm[c:c + batch]
            pred = net(X[ta[idx]], X[tb[idx]], feats[idx])
            loss = huber(pred, y[idx])
            opt.zero_grad(); loss.backward(); opt.step(); sched.step()
            tot += float(loss) * len(idx)
        print(f"epoch {ep + 1:2d}/{epochs}  huber {tot / len(perm):.4f}")

    net.eval()
    with torch.no_grad():
        preds = []
        for c in range(0, len(va_idx), 65536):
            idx = va_idx[c:c + 65536]
            preds.append(net(X[ta[idx]], X[tb[idx]], feats[idx]))
        pred = torch.cat(preds).cpu().numpy()
    torch.save({"state": net.state_dict(), "seed": seed,
                "map": str(map_path.name)}, JUMPNET_PT)

    # --- Baselines on the SAME val pairs -----------------------------------
    vy = y[va_idx, 0].cpu().numpy()                     # log R_max target
    va_np = np.where(va)[0]
    rho_net = _spearman_np(pred[:, 0], vy)
    rho_l1 = _spearman_np(d["endpoint_l1"][va_np], vy)
    rho_gd = _spearman_np(d["graph_dist"][va_np], vy)

    # Engineered-only MLP, same protocol (the ablation that decides whether
    # the towers add anything beyond the hand physics).
    torch.manual_seed(seed)
    eng = torch.nn.Sequential(
        torch.nn.Linear(len(FEATURE_NAMES), 128), torch.nn.GELU(),
        torch.nn.Linear(128, 128), torch.nn.GELU(),
        torch.nn.Linear(128, 1)).to(DEVICE)
    eopt = torch.optim.AdamW(eng.parameters(), lr=lr)
    for ep in range(epochs):
        perm = tr_idx[torch.randperm(len(tr_idx), device=DEVICE)]
        for c in range(0, len(perm), batch):
            idx = perm[c:c + batch]
            loss = huber(eng(feats[idx])[:, 0], y[idx, 0])
            eopt.zero_grad(); loss.backward(); eopt.step()
    eng.eval()
    with torch.no_grad():
        epred = torch.cat([eng(feats[va_idx[c:c + 65536]])[:, 0]
                           for c in range(0, len(va_idx), 65536)]).cpu().numpy()
    rho_eng = _spearman_np(epred, vy)

    best_base = max(abs(rho_l1), abs(rho_gd), abs(rho_eng))
    verdict = ("SUCCESS" if rho_net >= 0.90 and rho_net - best_base >= 0.05
               else "ENGINEERED-TIES" if rho_net - rho_eng < 0.02
               else "PARTIAL")
    out = {
        "n_train": int(len(tr_idx)), "n_val": int(len(va_idx)),
        "spearman_val_logRmax": {
            "jumpnet": round(rho_net, 4), "endpoint_l1": round(rho_l1, 4),
            "graph_dist_landmark": round(rho_gd, 4),
            "engineered_mlp": round(rho_eng, 4)},
        "pre_registered": "net >= 0.90 and net - best_baseline >= 0.05",
        "verdict": verdict,
    }
    EVAL_JSON.write_text(json.dumps(out, indent=1) + "\n")
    print(json.dumps(out, indent=1))
    return out


# ---------------------------------------------------------------------------
# Mining + render-verification + emit (§A4). The net proposes, the renderer
# disposes: shipped numbers are always rendered, never predictions.
# ---------------------------------------------------------------------------

MINED_NPZ = DERIVED / "jumps_mined.npz"


_FEAT_CTX: dict = {}   # fork-inherited state for _feat_worker (ops, pairs)


def _feat_worker(args):
    lo, hi = args
    ops_all, pairs = _FEAT_CTX["ops"], _FEAT_CTX["pairs"]
    out = np.empty((hi - lo, len(FEATURE_NAMES)), np.float32)
    for i in range(lo, hi):
        out[i - lo] = feature_vector(ops_all[pairs[i, 0]], ops_all[pairs[i, 1]])
    return lo, out


def _pool_features(ops_all, pairs: np.ndarray, workers: int = 10) -> np.ndarray:
    """Engineered features for a candidate pool. Fork start method inherits
    _FEAT_CTX copy-on-write, so only (lo, hi) ranges cross the pipe."""
    import multiprocessing as mp

    _FEAT_CTX["ops"], _FEAT_CTX["pairs"] = ops_all, pairs
    n = len(pairs)
    step = max(1, n // (workers * 8))
    ranges = [(lo, min(lo + step, n)) for lo in range(0, n, step)]
    feats = np.empty((n, len(FEATURE_NAMES)), np.float32)
    with mp.get_context("fork").Pool(workers) as pool:
        for lo, block in pool.imap_unordered(_feat_worker, ranges):
            feats[lo:lo + len(block)] = block
    _FEAT_CTX.clear()
    return feats


def mine(map_path: FsPath, pool_size: int = 20_000_000,
         keep_top: int = 30_000, seed: int = 0,
         hop_floor: float = 8.0) -> None:
    """Propose smooth cross-family candidates with the trained scorer.
    Sampled mining (not exhaustive): `pool_size` random cross-family pairs
    are screened — log what that leaves behind, no silent caps."""
    rng = np.random.default_rng(seed + 7)
    t0 = time.time()
    doc = load_map(map_path)
    ops_all, fb, fam = node_states(doc)
    n = len(ops_all)
    node_fps = render_node_fps(ops_all, fb)
    graph = _knn_graph(node_fps, k=12)
    dl, _ = _landmark_dists(graph, n, rng)
    med_edge = float(np.median(graph.tocoo().data))
    gd_floor = hop_floor * med_edge     # "≥ 8 hops" in edge-weight units
    print(f"map {map_path.name}: {n:,} nodes; median edge {med_edge:.3f} "
          f"→ graph-dist floor {gd_floor:.2f}  [{time.time()-t0:.1f}s]")

    # Screen the random pool on GPU-cheap signals first.
    kept_pairs, kept_l1, kept_gd = [], [], []
    chunk = 2_000_000
    l1_cut = None
    for c in range(0, pool_size, chunk):
        m = min(chunk, pool_size - c)
        a = rng.integers(0, n, m * 2)
        b = rng.integers(0, n, m * 2)
        ok = (a != b) & (fam[a] != fam[b])
        a, b = a[ok][:m], b[ok][:m]
        l1 = np.empty(len(a), np.float32)
        for s in range(0, len(a), 16384):
            sl = slice(s, s + 16384)
            l1[sl] = (node_fps[a[sl]] - node_fps[b[sl]]).abs().sum(1).cpu().numpy()
        if l1_cut is None:
            l1_cut = float(np.percentile(l1, 40))   # from the first chunk
        gd = _approx_graph_dist(dl, a, b)
        keep = (l1 <= l1_cut) & (gd >= gd_floor) & np.isfinite(gd)
        kept_pairs.append(np.stack([a[keep], b[keep]], 1))
        kept_l1.append(l1[keep]); kept_gd.append(gd[keep])
    pairs = np.concatenate(kept_pairs)
    l1 = np.concatenate(kept_l1); gd = np.concatenate(kept_gd)
    print(f"screen: {pool_size:,} sampled → {len(pairs):,} survivors "
          f"(l1 ≤ {l1_cut:.2f}, gd ≥ {gd_floor:.2f})  [{time.time()-t0:.1f}s]")

    # De-dup canonical a<b.
    lo = np.minimum(pairs[:, 0], pairs[:, 1])
    hi = np.maximum(pairs[:, 0], pairs[:, 1])
    _, uniq = np.unique(lo.astype(np.int64) * n + hi, return_index=True)
    pairs = np.stack([lo[uniq], hi[uniq]], 1)
    l1, gd = l1[uniq], gd[uniq]

    # Score with the trained net (or engineered head if that's the verdict).
    feats = _pool_features(ops_all, pairs)
    print(f"features for {len(pairs):,} pairs  [{time.time()-t0:.1f}s]")
    ckpt = torch.load(JUMPNET_PT, map_location=DEVICE, weights_only=False)
    net = JumpNet().to(DEVICE); net.load_state_dict(ckpt["state"]); net.eval()
    scal = torch.as_tensor(_node_scalars(ops_all, fb), device=DEVICE)
    X = torch.cat([node_fps, scal], dim=1)
    tf = torch.as_tensor(feats, device=DEVICE)
    ta = torch.as_tensor(pairs[:, 0], device=DEVICE)
    tb = torch.as_tensor(pairs[:, 1], device=DEVICE)
    with torch.no_grad():
        pred = torch.cat([
            net(X[ta[c:c + 65536]], X[tb[c:c + 65536]], tf[c:c + 65536])[:, 0]
            for c in range(0, len(pairs), 65536)]).cpu().numpy()

    # Family-pair caps on the PROPOSAL side (500/pair) neutralise basin skew,
    # then a global keep_top by predicted smoothness.
    fp_key = fam[pairs[:, 0]].astype(np.int64) * 64 + fam[pairs[:, 1]]
    order = np.argsort(pred)                       # smoothest-predicted first
    counts: dict = {}
    take = []
    for i in order:
        k = int(fp_key[i])
        if counts.get(k, 0) >= 500:
            continue
        counts[k] = counts.get(k, 0) + 1
        take.append(i)
        if len(take) >= keep_top:
            break
    take = np.array(take)
    np.savez_compressed(MINED_NPZ, a=pairs[take, 0].astype(np.int32),
                        b=pairs[take, 1].astype(np.int32),
                        pred_log_rmax=pred[take].astype(np.float32),
                        endpoint_l1=l1[take], graph_dist=gd[take],
                        map=str(map_path.name), pool_size=pool_size,
                        l1_cut=l1_cut, gd_floor=gd_floor)
    print(f"mined {len(take):,} candidates → {MINED_NPZ}  "
          f"[{time.time()-t0:.1f}s]")


def verify_and_emit(map_path: FsPath, out_path: FsPath | None = None,
                    per_family_pair: int = 16, per_node: int = 4,
                    total_cap: int = 2048, seed: int = 0) -> dict:
    """Render-verify every mined candidate, apply the pre-registered wormhole
    definition, and write the surviving `jumps` block into the map JSON."""
    t0 = time.time()
    d = np.load(MINED_NPZ, allow_pickle=False)
    ds = np.load(DATASET_NPZ, allow_pickle=False)
    doc = load_map(map_path)
    ops_all, fb, fam = node_states(doc)

    # τ = 25th percentile of RENDERED k-NN-edge roughness from the dataset:
    # "smoother than a typical intra-basin hop".
    knn_rmax = ds["r_max"][ds["stratum"] == STRATUM_CODE["knn_edge"]]
    tau = float(np.percentile(knn_rmax, 25))

    a, b = d["a"].astype(np.int64), d["b"].astype(np.int64)
    t = torch.arange(N_FFT, device=DEVICE, dtype=torch.float32) / SAMPLE_RATE
    win = torch.hann_window(N_FFT, device=DEVICE)
    W = interp_matrix(N_FFT, SAMPLE_RATE, Q_MAX, F_CARRIER)
    uni = np.linspace(0.0, 1.0, N_UNIFORM)

    r_max = np.zeros(len(a), np.float32)
    tv = np.zeros(len(a), np.float32)
    aw = np.zeros(len(a), np.float32)
    for c0 in range(0, len(a), 1200):
        c1 = min(c0 + 1200, len(a))
        alphas_per, ladders = [], []
        for i in range(c0, c1):
            cur, tgt = ops_all[a[i]], ops_all[b[i]]
            ev = events(cur, tgt)[:MAX_EVENTS]
            br = np.array(ev, np.float64)
            alphas = np.unique(np.clip(np.concatenate(
                [uni, br - EVENT_EPS, br + EVENT_EPS]), 0.0, 1.0))
            alphas_per.append((alphas, np.array(ev)))
            ladders.append(PairLadder(cur, tgt, fb[a[i]], fb[b[i]]))
        counts = np.array([len(x) for x, _ in alphas_per])
        offs = np.concatenate([[0], np.cumsum(counts)])
        k = ladders[0].k
        R = np.empty((offs[-1], k)); D = np.empty((offs[-1], k))
        Wv = np.empty((offs[-1], k)); F = np.empty(offs[-1])
        for j, ((alphas, _), pl) in enumerate(zip(alphas_per, ladders)):
            r, dep, w, f = pl.sample(alphas)
            R[offs[j]:offs[j + 1]] = r; D[offs[j]:offs[j + 1]] = dep
            Wv[offs[j]:offs[j + 1]] = w; F[offs[j]:offs[j + 1]] = f
        fps = _render_fps(R, D, Wv, F, t, win, W)
        d_seq = (fps[1:] - fps[:-1]).abs().sum(1).cpu().numpy()
        for j in range(c1 - c0):
            alphas, ev = alphas_per[j]
            tv[c0 + j] = d_seq[offs[j]:offs[j + 1] - 1].sum()
            rm, wa = 0.0, 0.0
            for e in ev:
                loi = np.searchsorted(alphas, np.clip(e - EVENT_EPS, 0, 1))
                hii = np.searchsorted(alphas, np.clip(e + EVENT_EPS, 0, 1))
                loi = min(loi, len(alphas) - 1); hii = min(hii, len(alphas) - 1)
                if hii > loi:
                    jmp = float(np.abs(fps[offs[j] + hii].cpu().numpy()
                                       - fps[offs[j] + loi].cpu().numpy()).sum())
                    if jmp > rm:
                        rm, wa = jmp, float(e)
            r_max[c0 + j], aw[c0 + j] = rm, wa
    print(f"verified {len(a):,} candidates  [{time.time()-t0:.1f}s]")

    keep = r_max <= tau
    print(f"wormhole rule: rendered R_max ≤ τ={tau:.3f} → {keep.sum():,} pass "
          f"({100.0 * keep.mean():.1f}% of proposals)")

    # Rank by wormhole score, then apply the shipping caps.
    score = d["graph_dist"] / np.maximum(r_max, 1e-3)
    order = np.argsort(-score)
    fp_count: dict = {}; node_count: dict = {}
    edges = []
    for i in order:
        if not keep[i]:
            continue
        ai, bi = int(a[i]), int(b[i])
        fpk = (int(fam[ai]) << 8) | int(fam[bi])
        if fp_count.get(fpk, 0) >= per_family_pair:
            continue
        if node_count.get(ai, 0) >= per_node or node_count.get(bi, 0) >= per_node:
            continue
        fp_count[fpk] = fp_count.get(fpk, 0) + 1
        node_count[ai] = node_count.get(ai, 0) + 1
        node_count[bi] = node_count.get(bi, 0) + 1
        edges.append({"a": min(ai, bi), "b": max(ai, bi),
                      "r": round(float(r_max[i]), 3),
                      "t": round(float(tv[i]), 2),
                      "g": round(float(d["graph_dist"][i]), 1),
                      "w": round(float(aw[i]), 2)})
        if len(edges) >= total_cap:
            break
    edges.sort(key=lambda e: e["r"])              # shipped smoothest-first
    print(f"caps (≤{per_family_pair}/family-pair, ≤{per_node}/node, "
          f"≤{total_cap} total) → {len(edges):,} shipped")

    doc["jumps"] = {"version": 1, "metric": "maxstep_fp_l1",
                    "tau": round(tau, 4), "edges": edges}
    out_path = out_path or map_path
    # dict insertion order puts "jumps" last → after "nodes", so the C++
    # peekHeader preamble scan is unaffected. Raw UTF-8 (the json gotcha).
    payload = json.dumps(doc, ensure_ascii=False,
                         separators=(",", ":")).encode()
    with gzip.open(out_path, "wb") as f:
        f.write(payload)
    print(f"wrote {out_path} (+jumps block, {len(edges)} edges)")

    summary = {"tau": tau, "n_proposed": int(len(a)),
               "n_pass": int(keep.sum()), "n_shipped": len(edges),
               "median_shipped_r": float(np.median([e["r"] for e in edges]))
               if edges else None,
               "median_shipped_g": float(np.median([e["g"] for e in edges]))
               if edges else None}
    (DERIVED / "jumps_emit.json").write_text(json.dumps(summary, indent=1) + "\n")
    print(json.dumps(summary, indent=1))
    return summary


# ---------------------------------------------------------------------------
# Perceptual spot-check — the metric's claim is itself falsifiable by ear.
# ---------------------------------------------------------------------------

def render_audio(map_path: FsPath, n_each: int = 10, seconds: float = 2.5,
                 sr: int = 44100, seed: int = 0) -> None:
    """WAV renders of the top-N shipped wormholes vs N random cross-family
    morphs (same duration/pacing) into derived/jumps_audio/ for A/B listening.
    Chunked render (30 alpha steps/s) with 5 ms crossfades; the modulator
    phases are t-continuous so seams carry only the param step the runtime
    smoother would also see."""
    from scipy.io import wavfile

    rng = np.random.default_rng(seed + 13)
    doc = load_map(map_path)
    ops_all, fb, fam = node_states(doc)
    jumps = (doc.get("jumps") or {}).get("edges", [])[:n_each]
    if not jumps:
        print("no jumps block in map — run emit first"); return

    rand_pairs = []
    n = len(ops_all)
    while len(rand_pairs) < n_each:
        i, j = rng.integers(0, n, 2)
        if i != j and fam[i] != fam[j]:
            rand_pairs.append((int(i), int(j)))

    out_dir = DERIVED / "jumps_audio"
    out_dir.mkdir(parents=True, exist_ok=True)
    steps = int(seconds * 30)
    seg = int(sr / 30)
    fade = int(sr * 0.005)
    ramp = np.linspace(0.0, 1.0, fade)

    def render_pair(ai, bi, name):
        pl = PairLadder(ops_all[ai], ops_all[bi], fb[ai], fb[bi])
        alphas = np.linspace(0.0, 1.0, steps)
        ratio, depth, wave, fbv = pl.sample(alphas)
        audio = np.zeros(steps * seg + fade, np.float32)
        for s in range(steps):
            t0 = s * seg / sr
            t = torch.arange(seg + fade, device=DEVICE,
                             dtype=torch.float32) / sr + t0
            lr = torch.log(torch.as_tensor(ratio[s:s + 1], dtype=torch.float32,
                                           device=DEVICE))
            dp = torch.as_tensor(depth[s:s + 1], dtype=torch.float32, device=DEVICE)
            wv = torch.as_tensor(wave[s:s + 1], dtype=torch.float32, device=DEVICE)
            bt = torch.as_tensor(fbv[s:s + 1], dtype=torch.float32,
                                 device=DEVICE) * float(np.pi)
            x = render_batch(lr, dp, torch.zeros_like(lr), t, F_CARRIER,
                             feedback=bt, waves=wv)[0].cpu().numpy()
            x[:fade] *= ramp
            x[-fade:] *= ramp[::-1]
            audio[s * seg: s * seg + seg + fade] += x
        peak = np.abs(audio).max()
        if peak > 0:
            audio *= 0.7 / peak
        wavfile.write(out_dir / f"{name}.wav", sr,
                      (audio * 32767).astype(np.int16))

    for k, e in enumerate(jumps):
        render_pair(e["a"], e["b"], f"wormhole_{k:02d}_r{e['r']:.2f}")
    for k, (ai, bi) in enumerate(rand_pairs):
        render_pair(ai, bi, f"random_{k:02d}")
    print(f"wrote {len(jumps)} wormhole + {len(rand_pairs)} random morph "
          f"renders → {out_dir}")


# ---------------------------------------------------------------------------

def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)

    d = sub.add_parser("dataset", help="render morph-roughness ground truth")
    d.add_argument("--map", type=FsPath, default=DEFAULT_MAP)
    d.add_argument("--pairs", type=int, default=300_000)
    d.add_argument("--seed", type=int, default=0)
    d.add_argument("--ratio-mode", choices=["walk", "glide"], default="walk")
    d.add_argument("--out", type=FsPath, default=DATASET_NPZ)

    c = sub.add_parser("calibrate", help="strata medians + gate verdict")
    c.add_argument("--dataset", type=FsPath, default=DATASET_NPZ)

    t = sub.add_parser("train", help="JumpNet + baselines on the dataset")
    t.add_argument("--dataset", type=FsPath, default=DATASET_NPZ)
    t.add_argument("--map", type=FsPath, default=DEFAULT_MAP)
    t.add_argument("--epochs", type=int, default=15)
    t.add_argument("--seed", type=int, default=0)

    m = sub.add_parser("mine", help="propose smooth cross-family candidates")
    m.add_argument("--map", type=FsPath, default=DEFAULT_MAP)
    m.add_argument("--pool", type=int, default=20_000_000)
    m.add_argument("--top", type=int, default=30_000)
    m.add_argument("--seed", type=int, default=0)

    e = sub.add_parser("emit", help="render-verify mined candidates and "
                                    "write the jumps block into the map")
    e.add_argument("--map", type=FsPath, default=DEFAULT_MAP)
    e.add_argument("--out", type=FsPath, default=None)

    au = sub.add_parser("audio", help="A/B WAVs: top wormholes vs random")
    au.add_argument("--map", type=FsPath, default=DEFAULT_MAP)

    args = ap.parse_args()
    if args.cmd == "audio":
        render_audio(args.map)
    elif args.cmd == "dataset":
        dataset(args.map, args.pairs, args.seed, args.out, args.ratio_mode)
    elif args.cmd == "calibrate":
        calibrate(args.dataset)
    elif args.cmd == "train":
        train(args.dataset, args.map, epochs=args.epochs, seed=args.seed)
    elif args.cmd == "mine":
        mine(args.map, pool_size=args.pool, keep_top=args.top, seed=args.seed)
    elif args.cmd == "emit":
        verify_and_emit(args.map, args.out)


if __name__ == "__main__":
    main()
