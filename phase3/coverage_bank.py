"""T1 — the coverage bank: a large, deduped fingerprint index over the FM
parameter space, so Mode B is ready to match arbitrary input sounds.

Two uses:
  1. Fast retrieval — given a target spectrum, brute-force nearest configs.
  2. Warm-starting the search — seed two_phase_search Phase 2 from the
     nearest bank entries instead of cold random starts (the cold-start
     trapping was the residual Mode B failure mode).

Sampling strategy (cover the *spectrum* space, not every config — many
configs map to the same spectrum, so we dedup by quantised fingerprint):
  * 1-op: every curated Stern-Brocot ratio × a depth grid (Bessel landmarks
    + linear) — the dense, musically-meaningful core.
  * 2-op: a coarser curated ratio × depth grid, all unordered pairs.
  * 3–8-op: structured random sampling via the search sampler.

Fingerprints are the smoothed, L1-normalised integer-grid amplitude vectors
at q_max=128 (covers the audible range at f_carrier=220 — see landscape.md).

Artifacts (gitignored, derived): derived/coverage_bank.npz (float32 matrix)
+ derived/coverage_bank.jsonl (one config per row).

Run: python -m phase3.coverage_bank build [--n-random N] [--q-max Q]
     python -m phase3.coverage_bank coverage
"""

from __future__ import annotations

import argparse
import json
import os
import time
from concurrent.futures import ProcessPoolExecutor
from itertools import product
from pathlib import Path

import numpy as np

os.environ.setdefault("OMP_NUM_THREADS", "1")
os.environ.setdefault("MKL_NUM_THREADS", "1")
os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")

from phase3.config import BESSEL_LANDMARKS, Config, ModConfig, path_ratio  # noqa: E402
from phase3.family_distance import quantize_to_integer_grid  # noqa: E402
from phase3.partial_prediction import predict_partials  # noqa: E402
from phase3.search import sample_random_config  # noqa: E402

Q_MAX = 128
F_CARRIER = 220.0
NPZ = Path("phase3/derived/coverage_bank.npz")
JSONL = Path("phase3/derived/coverage_bank.jsonl")

# Predict params: coarse but fast — these fingerprints are for *retrieval*
# (find the right basin), not final scoring. Higher eps shrinks each op's
# sideband order (the real cost driver), so deep multi-op configs fingerprint
# in milliseconds instead of seconds.
_PRED = dict(threshold_db=-40.0, eps=5e-3, max_partials=400)


# ---------------------------------------------------------------------------
# Space enumeration
# ---------------------------------------------------------------------------

def curated_ratios(max_len: int = 5) -> list[str]:
    """All Stern-Brocot L/R paths up to `max_len` (unique ratios, incl. 1/1),
    shortest first — the simple, musically-meaningful ratios."""
    paths = [""]
    for L in range(1, max_len + 1):
        paths += ["".join(c) for c in product("LR", repeat=L)]
    seen: set[float] = set()
    out: list[str] = []
    for p in paths:
        key = round(path_ratio(p), 6)
        if key not in seen:
            seen.add(key)
            out.append(p)
    return out


def _depth_grid(step: float = 0.3, hi: float = 6.0) -> list[float]:
    g = set(round(float(d), 3) for d in np.arange(0.1, hi + 1e-9, step))
    g.update(round(float(d), 3) for d in BESSEL_LANDMARKS if d <= hi)
    return sorted(g)


def enumerate_configs(n_random: int, seed: int = 0):
    """Yield (paths_tuple, depths_tuple) config descriptors."""
    ratios = curated_ratios(5)                       # ~63 ratios
    depths = _depth_grid(0.3)                         # ~28 depths

    # 1-op: full grid.
    for p in ratios:
        for d in depths:
            yield ((p,), (d,))

    # 2-op: coarser curated grid, all unordered pairs of (ratio, depth) units.
    short = ratios[:24]
    depths2 = _depth_grid(0.6)                        # ~12 depths
    units = [(p, d) for p in short for d in depths2]
    for i in range(len(units)):
        for j in range(i + 1, len(units)):
            yield ((units[i][0], units[j][0]), (units[i][1], units[j][1]))

    # 3–5-op: structured random sampling (capped op-count + shallow paths so
    # the fingerprint predict stays fast; complex timbres still appear via the
    # multi-op combinations).
    rng = np.random.default_rng(seed)
    for _ in range(n_random):
        cfg = sample_random_config(rng, min_enabled=3, max_enabled=5,
                                   sample_max_path_depth=5, f_carrier=F_CARRIER)
        ps, ds = [], []
        for m in cfg.mods:
            if m.enabled:
                ps.append(m.path)
                ds.append(round(m.depth, 3))
        if ps:
            yield (tuple(ps), tuple(ds))


def _dedup_key(desc) -> tuple:
    paths, depths = desc
    # Order-independent (parallel ops) + depth-quantised.
    return tuple(sorted(zip(paths, (round(d, 2) for d in depths))))


# ---------------------------------------------------------------------------
# Fingerprinting (worker)
# ---------------------------------------------------------------------------

def _fingerprint(desc) -> np.ndarray:
    paths, depths = desc
    ratios = [path_ratio(p) for p in paths]
    sp = predict_partials(ratios, list(depths), f_carrier=F_CARRIER, **_PRED)
    a = quantize_to_integer_grid(sp, F_CARRIER, Q_MAX)
    s = a.sum()
    if s > 0:
        a = a / s
    return a.astype(np.float32)


# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------

def build(n_random: int = 80_000, workers: int = 10):
    t0 = time.time()
    print(f"enumerating configs (n_random={n_random}) …")
    seen: set[tuple] = set()
    descs: list[tuple] = []
    for desc in enumerate_configs(n_random):
        k = _dedup_key(desc)
        if k not in seen:
            seen.add(k)
            descs.append(desc)
    print(f"  {len(descs)} unique configs ({time.time()-t0:.0f}s). Fingerprinting …")

    amps = np.empty((len(descs), Q_MAX), dtype=np.float32)
    with ProcessPoolExecutor(max_workers=workers) as ex:
        for i, a in enumerate(ex.map(_fingerprint, descs, chunksize=64)):
            amps[i] = a
            if (i + 1) % 20000 == 0:
                print(f"  {i+1}/{len(descs)} ({time.time()-t0:.0f}s)")

    # Drop all-zero (silent) fingerprints.
    nz = amps.sum(axis=1) > 0
    amps, descs = amps[nz], [d for d, keep in zip(descs, nz) if keep]

    NPZ.parent.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(NPZ, amps=amps)
    with JSONL.open("w") as f:
        for paths, depths in descs:
            f.write(json.dumps({"paths": list(paths),
                                "depths": [float(x) for x in depths]}) + "\n")
    mb = NPZ.stat().st_size / 1e6
    print(f"\nbank: {len(descs)} fingerprints @ q_max={Q_MAX}  "
          f"({mb:.1f} MB) in {time.time()-t0:.0f}s -> {NPZ}")


# ---------------------------------------------------------------------------
# Load / retrieve
# ---------------------------------------------------------------------------

def load_bank():
    amps = np.load(NPZ)["amps"]
    configs = [json.loads(line) for line in JSONL.read_text().splitlines()]
    return amps, configs


def query(amps: np.ndarray, target: np.ndarray, k: int = 10,
          metric: str = "l1"):
    """Top-k nearest bank rows to `target` (an L1-normalised vector). Returns
    (indices, distances)."""
    t = np.asarray(target, dtype=np.float32)
    s = t.sum()
    if s > 0:
        t = t / s
    if metric == "l1":
        d = np.abs(amps - t[None, :]).sum(axis=1)
    elif metric == "kl":                       # D_KL(target || bank)
        eps = 1e-9
        tn = t + eps
        d = (tn * np.log(tn[None, :] / (amps + eps))).sum(axis=1)
    else:
        raise ValueError(metric)
    idx = np.argpartition(d, min(k, len(d) - 1))[:k]
    idx = idx[np.argsort(d[idx])]
    return idx, d[idx]


# ---------------------------------------------------------------------------
# Warm-started search — retrieve nearest bank configs, then refine
# ---------------------------------------------------------------------------

def config_from_entry(entry: dict) -> Config:
    """Reconstruct a Config from a bank jsonl row."""
    mods = [ModConfig(path=p, depth=float(d), enabled=True)
            for p, d in zip(entry["paths"], entry["depths"])]
    return Config(mods=mods, f_carrier=F_CARRIER)


def retrieve_seeds(target_amps: np.ndarray, amps: np.ndarray,
                   configs: list[dict], k: int = 20,
                   metric: str = "l1") -> list[Config]:
    """Nearest-k bank configs to a target amplitude vector."""
    idx, _ = query(amps, target_amps, k=k, metric=metric)
    return [config_from_entry(configs[i]) for i in idx]


def bank_seeded_search(family, *, amps=None, configs=None, k_seeds: int = 24,
                       f_carrier: float = F_CARRIER, q_max: int = 16,
                       time_budget_per_candidate_s: float = 3.0,
                       lambda_crit: float = 0.0, n_random_aug: int = 0,
                       seed: int = 7):
    """Mode B search warm-started from the coverage bank: retrieve the
    nearest bank configs to the family's target spectrum and refine each by
    coordinate descent — no cold random Phase 1, so the search starts already
    in the right basin (the fix for cold-start trapping).

    `n_random_aug` (default 0) optionally mixes in the best of a random draw
    alongside the bank seeds. In practice the bank seeds dominate and the aug
    mostly adds cost (≈4× slower for no gain on the tested targets), so it's
    off by default; raise it only when deliberately probing the coverage tail.
    Returns top results sorted by family_distance (lower = closer).
    """
    from phase3.family_distance import family_distance
    from phase3.search import SearchResult, coordinate_descent, sample_random_config

    if amps is None or configs is None:
        amps, configs = load_bank()

    # Retrieve against the bank's native q_max, then refine at the requested.
    target = family.target_amplitudes(amps.shape[1])
    seeds = retrieve_seeds(target, amps, configs, k=k_seeds)

    # Augment with the best of a quick random draw (covers the bank's tail).
    if n_random_aug > 0:
        rng = np.random.default_rng(seed)
        scored = []
        for _ in range(n_random_aug):
            cfg = sample_random_config(rng, min_enabled=3, max_enabled=6,
                                       f_carrier=f_carrier)
            d = family_distance(cfg.predict(), family, f_carrier, q_max=q_max)
            scored.append((d, cfg))
        scored.sort(key=lambda x: x[0])
        seeds += [c for _, c in scored[:6]]

    results: list[SearchResult] = []
    seen: set = set()
    for seed in seeds:
        refined, obj = coordinate_descent(
            seed, family, time_budget_s=time_budget_per_candidate_s,
            q_max=q_max, lambda_crit=lambda_crit)
        fp = refined.fingerprint()
        if fp in seen:
            continue
        seen.add(fp)
        sp = refined.predict()
        results.append(SearchResult(
            config=refined.canonicalised(), objective=obj, family=family.name,
            seed=-1, phase1_objective=0.0,
            family_distance=family_distance(sp, family, f_carrier, q_max=q_max)))
    results.sort(key=lambda r: r.objective)
    return results


# ---------------------------------------------------------------------------
# Coverage report
# ---------------------------------------------------------------------------

def coverage(n_probe: int = 500, seed: int = 12345):
    """Query the bank with held-out random configs and report the nearest-
    neighbour distance distribution — how well any target is covered."""
    amps, _ = load_bank()
    print(f"bank: {amps.shape[0]} fingerprints @ q_max={amps.shape[1]}")
    rng = np.random.default_rng(seed)
    nn = []
    for _ in range(n_probe):
        cfg = sample_random_config(rng, min_enabled=3, max_enabled=5,
                                   sample_max_path_depth=5, f_carrier=F_CARRIER)
        ps, ds = cfg.ratios_and_depths()
        if not ps:
            continue
        sp = predict_partials(ps, ds, f_carrier=F_CARRIER, **_PRED)
        a = quantize_to_integer_grid(sp, F_CARRIER, Q_MAX)
        if a.sum() <= 0:
            continue
        a = a / a.sum()
        _, dist = query(amps, a, k=1, metric="l1")
        nn.append(float(dist[0]))
    nn = np.array(nn)
    print(f"held-out nearest-neighbour L1 distance over {len(nn)} probes:")
    for p in (50, 75, 90, 95, 99):
        print(f"  p{p}: {np.percentile(nn, p):.4f}")
    print(f"  max: {nn.max():.4f}   mean: {nn.mean():.4f}")
    print(f"  within 0.10: {(nn<0.10).mean()*100:.1f}%   "
          f"within 0.25: {(nn<0.25).mean()*100:.1f}%")


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("--n-random", type=int, default=80_000)
    b.add_argument("--workers", type=int, default=10)
    c = sub.add_parser("coverage")
    c.add_argument("--n-probe", type=int, default=500)
    args = ap.parse_args()
    if args.cmd == "build":
        build(n_random=args.n_random, workers=args.workers)
    else:
        coverage(n_probe=args.n_probe)


if __name__ == "__main__":
    main()
