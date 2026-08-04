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

    args = ap.parse_args()
    if args.cmd == "dataset":
        dataset(args.map, args.pairs, args.seed, args.out, args.ratio_mode)
    elif args.cmd == "calibrate":
        calibrate(args.dataset)


if __name__ == "__main__":
    main()
