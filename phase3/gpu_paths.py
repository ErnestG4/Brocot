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
    return chosen, amps, labels[chosen], bank["ridx"][chosen], bank["depths"][chosen], fam_names


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
    nodeIdx, amps, node_lab, ridx, depths, fam_names = _subsample(n_nodes)
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
    nodeIdx, amps, node_lab, ridx, depths, fam_names = _subsample(n_nodes)
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
    a = ap.parse_args()
    if a.cmd == "demo":
        demo(a.nodes, a.k, a.targets)
    elif a.cmd == "export":
        export(a.nodes, a.k, a.k_paths, a.waypoints)


if __name__ == "__main__":
    main()
