"""Multi-path navigation across the FM soundspace — the "many routes, explore
the journey" model (Will).

The FM landscape is fixed; our job is to track smoothly across it. There isn't
one path to a given sound but many, and traversing different routes — and the
shortcuts between distant regions — is the point.

So we model the dense bank as a *graph*:
  - nodes  = bank configs (each reconstructable to real synth params:
             ridx → Stern-Brocot paths, plus depths), with a fingerprint and a
             family label from gpu_landscape.
  - edges  = k nearest neighbours in fingerprint space; edge weight = the L1
             timbre step, so walking an edge is a small, smooth glide.
Then every graph path to a family is a distinct sonic journey, and short hops
between timbrally-distant nodes are the "shortcuts."

This module subsamples the 10M bank to a navigable node set, builds the k-NN
graph on GPU, and finds K *distinct* smooth paths from a source config to a
target family (penalised-reroute Dijkstra). It prints example journeys to prove
the idea out before we wire it into pull_index / the runtime.

    demo:  phase3/.venv-gpu/bin/python -m phase3.gpu_paths demo --nodes 60000

Run under phase3/.venv-gpu (torch). Reads derived/gpu_bank.npz +
derived/landscape_labels.npz.
"""

from __future__ import annotations

import argparse
import time
from pathlib import Path

import numpy as np
import torch
from scipy.sparse import csr_matrix
from scipy.sparse.csgraph import dijkstra

from phase3.gpu_bank import NPZ as BANK_NPZ
from phase3.gpu_fm import DEVICE, F_CARRIER
from phase3.gpu_landscape import OUT as LABELS_NPZ

N_OPS = 6   # gpu_bank's op count


def _curated_paths():
    from phase3.coverage_bank import curated_ratios
    return curated_ratios(5)


def _structure_score(xy: np.ndarray, k: int = 16) -> np.ndarray:
    """Per-node 'criticality' from the 2D layout: the largest angular GAP among
    a node's k nearest neighbours. On a filament/line, neighbours lie along the
    line → one big empty sector (~180°); at a junction/plus, a few arms → medium
    gaps; on an edge/void rim, neighbours only on the inward side → big gap; in a
    dense blob, neighbours all around → tiny gap. So lines + crossings + void
    rims score high, the bulk scores low. Returned normalised to [0,1]."""
    from scipy.spatial import cKDTree
    tree = cKDTree(xy)
    _, idx = tree.query(xy, k=k + 1)                 # +1 = self
    two_pi = 2.0 * np.pi
    out = np.empty(xy.shape[0], np.float32)
    for i in range(xy.shape[0]):
        nb = idx[i, 1:]
        ang = np.sort(np.arctan2(xy[nb, 1] - xy[i, 1], xy[nb, 0] - xy[i, 0]))
        gaps = np.diff(ang)
        wrap = (ang[0] + two_pi) - ang[-1]           # across the -pi/pi seam
        out[i] = max(float(gaps.max()) if gaps.size else 0.0, wrap)
    out /= two_pi                                    # 0..1 (0.5 ≈ a clean line)
    return out


def _intrinsic_criticality(amps: torch.Tensor, k: int = 16, chunk: int = 3000) -> np.ndarray:
    """Per-node 'hidden' criticality from the FULL fingerprint space (not the 2D
    layout, which buries structure inside dense projections). Local PCA of each
    node's k nearest neighbours: a filament is intrinsically 1-D, a junction/plus
    2-D, chaos is high-D. Score = 1 / participation-ratio of the singular values²
    (1.0 ≈ line, ~0.5 ≈ plus/plane, low ≈ chaotic bulk) — so hidden lines AND
    crossings light up wherever they are. Normalised to [0,1]."""
    n = amps.shape[0]
    out = np.empty(n, np.float32)
    for c in range(0, n, chunk):
        block = amps[c:c + chunk]                                  # (b, Q)
        d = torch.cdist(block, amps, p=1)
        nb = torch.topk(d, k + 1, largest=False).indices[:, 1:]    # (b, k)
        pts = amps[nb]                                             # (b, k, Q)
        pts = pts - pts.mean(1, keepdim=True)
        s = torch.linalg.svdvals(pts)                             # (b, min(k,Q))
        s2 = s * s
        pr = (s2.sum(1) ** 2) / (s2.pow(2).sum(1) + 1e-12)         # participation ratio
        out[c:c + chunk] = (1.0 / pr.clamp(min=1.0)).cpu().numpy()
    return out


def _embed2d(amps_np: np.ndarray) -> np.ndarray:
    """2D layout of node fingerprints for the cloud (screen dist ~ timbral
    dist). t-SNE with PCA init when sklearn is present; PCA fallback otherwise.
    Returns (n, 2) normalised to [-1, 1]. Swap the projector here when redoing
    the cloud's weighting/alignment."""
    n = amps_np.shape[0]
    try:
        from sklearn.manifold import TSNE
        perp = float(min(30, max(5, n // 100)))
        xy = TSNE(n_components=2, init="pca", perplexity=perp,
                  random_state=0).fit_transform(amps_np.astype(np.float32))
    except Exception as e:
        print(f"  (t-SNE unavailable: {e}; PCA fallback)")
        a = amps_np.astype(np.float32) - amps_np.mean(0, keepdims=True)
        _, _, vt = np.linalg.svd(a, full_matrices=False)
        xy = a @ vt[:2].T
    xy = xy - xy.min(0)
    span = xy.max(0)
    span[span == 0] = 1.0
    return (xy / span) * 2.0 - 1.0      # → [-1, 1] per axis


def _subsample(n_nodes: int, seed: int = 0):
    """Stratified subsample of the bank: keep every family represented so rare
    basins (Square, Glass, ...) are still reachable. Returns node indices into
    the bank + their fingerprints/labels/params."""
    bank = np.load(BANK_NPZ)
    lab = np.load(LABELS_NPZ, allow_pickle=True)
    labels = lab["labels"]
    fam_names = list(lab["family_names"])
    N = labels.shape[0]
    rng = np.random.default_rng(seed)

    # Proportional-with-floor: each family gets min(its count, floor) guaranteed,
    # remaining budget filled proportionally. Keeps rare families on the map.
    per_fam = {}
    fams = np.unique(labels)
    floor = max(50, n_nodes // (len(fams) * 8))
    chosen = []
    for fam in fams:
        idx = np.where(labels == fam)[0]
        take = min(len(idx), floor)
        chosen.append(rng.choice(idx, take, replace=False))
    chosen = np.concatenate(chosen)
    if chosen.size < n_nodes:                       # top up proportionally
        rest = np.setdiff1d(np.arange(N), chosen, assume_unique=False)
        extra = rng.choice(rest, min(n_nodes - chosen.size, rest.size), replace=False)
        chosen = np.concatenate([chosen, extra])
    chosen = np.sort(chosen[:n_nodes])

    amps = torch.from_numpy(bank["amps"][chosen]).to(DEVICE).float()
    # op-0 feedback per node (radians 0..π); absent in older banks → zeros.
    fb = (bank["feedback"][chosen].astype(np.float32)
          if "feedback" in bank.files else np.zeros(len(chosen), np.float32))
    return (chosen, amps, labels[chosen], bank["ridx"][chosen],
            bank["depths"][chosen], fb, fam_names)


def _knn_graph(amps: torch.Tensor, k: int = 12, chunk: int = 2000):
    """k nearest neighbours (L1) per node → symmetric sparse graph. Returns a
    scipy CSR adjacency with edge weights = fingerprint L1 distance."""
    n = amps.shape[0]
    rows, cols, wts = [], [], []
    for c in range(0, n, chunk):
        block = amps[c:c + chunk]                                  # (b, Q)
        # cdist(p=1) gives the (b, n) L1 matrix without the (b, n, Q) transient.
        d = torch.cdist(block, amps, p=1)                          # (b, n)
        vals, idx = torch.topk(d, k + 1, largest=False)            # +1 = self
        vals, idx = vals[:, 1:].cpu().numpy(), idx[:, 1:].cpu().numpy()
        b = block.shape[0]
        src = np.repeat(np.arange(c, c + b), k)
        rows.append(src); cols.append(idx.reshape(-1)); wts.append(vals.reshape(-1))
    r = np.concatenate(rows); cc = np.concatenate(cols); w = np.concatenate(wts)
    # Symmetrise (mutual reachability) so paths can traverse either direction.
    R = np.concatenate([r, cc]); C = np.concatenate([cc, r]); W = np.concatenate([w, w])
    g = csr_matrix((W, (R, C)), shape=(n, n))
    return g


def _k_distinct_paths(graph: csr_matrix, src: int, targets: np.ndarray,
                      k_paths: int = 3, penalty: float = 4.0,
                      max_overlap: float = 0.6, attempts: int = 8):
    """K *distinct* smooth paths from src to the nearest of `targets`. After
    each accepted path we inflate the weight of its edges so the next Dijkstra
    reroutes; a candidate is rejected (and the penalty raised) if it shares more
    than `max_overlap` of its nodes with an already-accepted path. Not strict
    Yen's, but it reliably yields genuinely different journeys, which is what the
    exploration model wants."""
    g = graph.tolil(copy=True)
    paths, node_sets = [], []
    while len(paths) < k_paths:
        accepted = False
        for _ in range(attempts):
            dist, pred = dijkstra(g.tocsr(), indices=src, return_predecessors=True)
            tgt = targets[int(np.argmin(dist[targets]))]
            if not np.isfinite(dist[tgt]):
                return paths
            seq, node = [], tgt
            while node != src and node >= 0:
                seq.append(node); node = pred[node]
            if node != src:
                return paths
            seq.append(src); seq.reverse()
            s = set(seq)
            overlap = max((len(s & ps) / len(s) for ps in node_sets), default=0.0)
            # Always penalise this route's edges so the next Dijkstra differs.
            for a, b in zip(seq[:-1], seq[1:]):
                g[a, b] *= penalty; g[b, a] *= penalty
            if overlap <= max_overlap:
                paths.append((seq, float(dist[tgt]))); node_sets.append(s)
                accepted = True
                break
        if not accepted:           # couldn't find a distinct-enough route
            break
    return paths


def _thin(seq, amps, m: int):
    """Down-sample a node path to ~m waypoints, evenly spaced by *cumulative
    fingerprint distance* (so each retained hop is a similar timbre step).
    Endpoints always kept."""
    if len(seq) <= m:
        return list(seq)
    fps = amps[torch.tensor(seq, device=amps.device)]
    steps = (fps[1:] - fps[:-1]).abs().sum(1).cpu().numpy()
    cum = np.concatenate([[0.0], np.cumsum(steps)])
    marks = np.linspace(0.0, cum[-1], m)
    keep = sorted(set(int(np.searchsorted(cum, mk)) for mk in marks) | {0, len(seq) - 1})
    return [seq[i] for i in keep]


def _reconstruct(node: int, ridx, depths, paths):
    """One bank node → OpState list (path / depth / enabled), the shape
    PullIndex::morph consumes. Disabled slots (depth 0) are emitted too so the
    op count is explicit."""
    ops = []
    for slot in range(N_OPS):
        d = float(depths[node, slot])
        ri = int(ridx[node, slot])
        ops.append({"path": paths[ri], "depth": round(d, 4), "enabled": d > 1e-4})
    return ops


def demo(n_nodes: int, k: int, targets_fam: list[str]):
    t0 = time.time()
    print(f"Subsampling {n_nodes:,} nodes (stratified by family) ...")
    nodeIdx, amps, node_lab, ridx, depths, node_fb, fam_names = _subsample(n_nodes)
    print(f"  {amps.shape[0]:,} nodes ({time.time()-t0:.1f}s)")

    print(f"Building {k}-NN graph on GPU ...")
    g = _knn_graph(amps, k=k)
    print(f"  {g.nnz:,} directed edges ({time.time()-t0:.1f}s)")

    paths_curated = _curated_paths()

    # Source = the single most "neutral" node: nearest to a pure carrier
    # (energy concentrated in partial 1). Good shared origin for journeys.
    pure = torch.zeros(amps.shape[1], device=DEVICE); pure[0] = 1.0
    src = int((amps - pure).abs().sum(1).argmin().item())
    print(f"  source node #{src} (family '{fam_names[node_lab[src]]}')\n")

    for fam in targets_fam:
        if fam not in fam_names:
            print(f"  (no family '{fam}')"); continue
        fi = fam_names.index(fam)
        tgt_nodes = np.where(node_lab == fi)[0]
        if tgt_nodes.size == 0:
            print(f"  '{fam}': no nodes in subsample"); continue
        routes = _k_distinct_paths(g, src, tgt_nodes, k_paths=3)
        print(f"=== {len(routes)} distinct paths  →  {fam} ===")
        for n, (seq, cost) in enumerate(routes, 1):
            fams_on_route = [fam_names[node_lab[s]] for s in seq]
            # Compress consecutive duplicates → the timbral "stops" en route.
            stops = [fams_on_route[0]]
            for f in fams_on_route[1:]:
                if f != stops[-1]:
                    stops.append(f)
            print(f"  path {n}: {len(seq)} waypoints, total L1 {cost:.2f}")
            print(f"          via: {' → '.join(stops)}")
        print()

    print(f"done ({time.time()-t0:.0f}s). Each waypoint is reconstructable to "
          f"synth params (ridx→paths, depths) for a runtime morph.")


def export(n_nodes: int, k: int, k_paths: int, waypoints: int,
           out: Path = Path("resources/landscape_paths.json")):
    """Build the graph and write a compact per-family path-set the synth reads:
    K distinct scenic routes from a neutral origin to each family, thinned to
    ~`waypoints` reconstructable OpState waypoints. Runtime walks them with the
    existing PullIndex::morph."""
    import json as _json
    t0 = time.time()
    print(f"Subsampling {n_nodes:,} nodes ...")
    nodeIdx, amps, node_lab, ridx, depths, node_fb, fam_names = _subsample(n_nodes)
    print(f"Building {k}-NN graph ...")
    g = _knn_graph(amps, k=k)
    cur_paths = _curated_paths()

    pure = torch.zeros(amps.shape[1], device=DEVICE); pure[0] = 1.0
    src = int((amps - pure).abs().sum(1).argmin().item())

    families = {}
    n_routes = 0
    for fi, fam in enumerate(fam_names):
        tgt_nodes = np.where(node_lab == fi)[0]
        if tgt_nodes.size == 0:
            continue
        routes = _k_distinct_paths(g, src, tgt_nodes, k_paths=k_paths)
        out_routes = []
        for seq, cost in routes:
            thin = _thin(seq, amps, waypoints)
            out_routes.append({
                "cost": round(cost, 4),
                "waypoints": [{"ops": _reconstruct(n, ridx, depths, cur_paths)}
                              for n in thin],
            })
        if out_routes:
            families[fam] = out_routes
            n_routes += len(out_routes)

    doc = {
        "version": "1.0",
        "f_carrier_ref": F_CARRIER,
        "q_max": 64,
        "origin_family": fam_names[node_lab[src]],
        "n_families": len(families),
        "n_routes": n_routes,
        "families": families,
    }
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(_json.dumps(doc, ensure_ascii=False) + "\n")
    print(f"\nWrote {out} ({out.stat().st_size/1024:.0f} KB): "
          f"{len(families)} families, {n_routes} routes "
          f"(<= {waypoints} waypoints each) in {time.time()-t0:.0f}s")


def gradients(n_nodes: int, k: int, min_family: int = 30,
              out: Path = Path("resources/landscape_graph.json")):
    """Export the runtime graph + per-family gradient fields ("vector LUTs").

    For each family we run a multi-source Dijkstra INTO the family (a virtual
    super-source wired to every family node), giving, for every node, the
    next-hop node one smooth step closer to that family. At runtime the synth
    locates its current patch by parameter distance to the nearest node, then
    follows family F's next-hop pointers — the shortest smooth path to F, with
    no runtime Dijkstra and no spectral analysis. Different start nodes yield
    different routes (the "where are we / where are we going" multiplicity)."""
    import json as _json
    from scipy.sparse import vstack, hstack
    t0 = time.time()
    print(f"Subsampling {n_nodes:,} nodes ...")
    nodeIdx, amps, node_lab, ridx, depths, node_fb, fam_names = _subsample(n_nodes)
    n = amps.shape[0]
    print(f"Building {k}-NN graph ...")
    g = _knn_graph(amps, k=k).tocsr()
    cur_paths = _curated_paths()

    # Family centroids (full-bank, from the classifier) → the gradient anchors
    # at each family's PROTOTYPE, not its whole cloud: we steer TOWARD the clean
    # canonical waveform (nearest the centroid), not whichever chaotic member is
    # graph-closest. Anchoring at the top-K-closest-to-centroid keeps route
    # diversity (many starts converge) while fixing the destination to be clean.
    lab = np.load(LABELS_NPZ, allow_pickle=True)
    centroids = torch.from_numpy(lab["centroids"]).to(DEVICE).float()   # (F, Q)
    # Small anchor set: routes from many starts converge on the same few clean
    # canonical members (the destination is one SOUND; the journey is where the
    # many-paths diversity lives — Will's model).
    anchor_k = 6

    # 2D embedding for the neighbor-cloud visualizer: screen distance ≈ timbral
    # (fingerprint) distance, so families cluster and mis-alignment is visible.
    # t-SNE (PCA-init) over the node fingerprints; normalised to [-1, 1]. This
    # is the cloud's "weighting/alignment" — swap the projector here to redo it.
    amps_np = amps.cpu().numpy()
    xy = _embed2d(amps_np)
    # Criticality = visible structure (2D angular gap: rims, lines at the voids)
    # combined with HIDDEN structure (intrinsic low-dimensionality in fingerprint
    # space: filaments/junctions buried inside dense projections). Each [0,1]
    # normalised, then max — so lines, plus-signs, and void rims all light up
    # whether or not the 2D layout exposes them.
    s2d = _structure_score(xy)
    sHD = _intrinsic_criticality(amps)
    def _norm(a):
        lo, hi = float(np.percentile(a, 2)), float(np.percentile(a, 98))
        return np.clip((a - lo) / max(hi - lo, 1e-6), 0.0, 1.0)
    struct = np.maximum(_norm(s2d), _norm(sHD)).astype(np.float32)

    # Interpretable per-node spectral descriptors so the cloud can be coloured
    # by character (brightness / density / hollowness) and interrogated — for
    # exploring the fine structure (small clusters, tails, outliers).
    from phase3.gpu_characterize import features as _spectral_features
    feat = _spectral_features(amps_np, node_fb)
    bmax = float(max(feat["brightness"].max(), 1e-6))
    dmax = float(max(feat["n_partials"].max(), 1e-6))
    fmax = float(max(feat["flatness"].max(), 1e-6))   # for tonality normalisation

    # Node params (reconstructable synth state) + family + 2D pos + descriptors.
    # feedback stored in the engine's 0..1 param convention (β = param·π rad);
    # brightness/density normalised to [0,1] for colour mapping; odd in [0,1].
    nodes = [{"family": int(node_lab[i]),
              "feedback": round(float(node_fb[i]) / float(np.pi), 4),
              "x": round(float(xy[i, 0]), 4), "y": round(float(xy[i, 1]), 4),
              "bright": round(float(feat["brightness"][i]) / bmax, 4),
              "dense":  round(float(feat["n_partials"][i]) / dmax, 4),
              "odd":    round(float(feat["odd_frac"][i]), 4),
              "tonal":  round(1.0 - float(feat["flatness"][i]) / fmax, 4),  # 1=musical
              "crit":   round(float(struct[i]), 4),   # lines/junctions/void rims
              "ops": _reconstruct(i, ridx, depths, cur_paths)} for i in range(n)]

    # Per-family next-hop field via super-source-into-family Dijkstra.
    print("Computing per-family gradient fields ...")
    fields = {}
    for fi, fam in enumerate(fam_names):
        members = np.where(node_lab == fi)[0]
        if members.size < min_family:             # skip thinly-covered families
            continue
        # Anchor = the family's CLEANEST members, so the gradient leads to the
        # canonical waveform, not a chaotic outlier. The classifier centroid is
        # itself complex (the random bank is dominated by dense high-op configs,
        # so the family mean is high-entropy), so centroid-nearest ≠ clean.
        # Instead: among members reasonably close to the centroid (still
        # representative of the family shape), pick the lowest spectral-entropy
        # ones (energy in a few ordered harmonics = tonal/clean, not mush).
        mfp = amps[torch.from_numpy(members).to(DEVICE)]            # (m, Q)
        cdist = (mfp - centroids[fi].unsqueeze(0)).abs().sum(1)     # L1 to centroid
        # representative pool: closest 60% to the centroid (drops far outliers).
        pool_k = max(anchor_k, int(0.6 * members.size))
        pool_k = int(min(pool_k, members.size))
        pool_local = torch.topk(cdist, pool_k, largest=False).indices
        p = mfp[pool_local] / mfp[pool_local].sum(1, keepdim=True).clamp(min=1e-9)
        entropy = -(p * (p + 1e-9).log()).sum(1)                   # tonal = low H
        kk = int(min(anchor_k, pool_k))
        clean_local = pool_local[torch.topk(entropy, kk, largest=False).indices]
        tgt = members[clean_local.cpu().numpy()]
        # Augment: node n = super-source, ~0-weight edges to the prototype set.
        top = hstack([g, csr_matrix((n, 1))])
        bottomrow = np.zeros(n + 1, np.float32); bottomrow[tgt] = 1e-6
        aug = vstack([top, csr_matrix(bottomrow)]).tocsr()
        dist, pred = dijkstra(aug, indices=n, return_predecessors=True)
        # pred[i] = next node toward the family (or n = arrived at family edge).
        nh = pred[:n].astype(np.int32)
        nh[nh == n] = -1                           # -1 = "at the family"
        nh[~np.isfinite(dist[:n])] = -2            # -2 = unreachable
        fields[fam] = nh.tolist()

    doc = {
        "version": "1.0",
        "f_carrier_ref": F_CARRIER,
        "n_nodes": n,
        "n_families": len(fields),
        "family_names": list(fields.keys()),
        "nodes": nodes,
        "next_hop": fields,            # family -> [next-hop node per node]
    }
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(_json.dumps(doc, ensure_ascii=False) + "\n")
    print(f"\nWrote {out} ({out.stat().st_size/1e6:.1f} MB): {n:,} nodes, "
          f"{len(fields)} family gradient fields in {time.time()-t0:.0f}s")

    # Quick sanity: average route length from a neutral origin to a few families.
    pure = torch.zeros(amps.shape[1], device=DEVICE); pure[0] = 1.0
    src = int((amps - pure).abs().sum(1).argmin().item())
    for fam in list(fields.keys())[:4]:
        nh = fields[fam]; node = src; hops = 0
        while nh[node] >= 0 and hops < 1000:
            node = nh[node]; hops += 1
        print(f"  origin → {fam}: {hops} hops")


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    d = sub.add_parser("demo")
    d.add_argument("--nodes", type=int, default=60_000)
    d.add_argument("--k", type=int, default=12)
    d.add_argument("--targets", nargs="*",
                   default=["Sawtooth", "Bell (stretched)", "Glass", "Square"])
    e = sub.add_parser("export")
    e.add_argument("--nodes", type=int, default=60_000)
    e.add_argument("--k", type=int, default=12)
    e.add_argument("--k-paths", type=int, default=3)
    e.add_argument("--waypoints", type=int, default=10)
    gr = sub.add_parser("gradients")
    gr.add_argument("--nodes", type=int, default=16_000)
    gr.add_argument("--k", type=int, default=12)
    a = ap.parse_args()
    if a.cmd == "demo":
        demo(a.nodes, a.k, a.targets)
    elif a.cmd == "export":
        export(a.nodes, a.k, a.k_paths, a.waypoints)
    elif a.cmd == "gradients":
        gradients(a.nodes, a.k)


if __name__ == "__main__":
    main()
