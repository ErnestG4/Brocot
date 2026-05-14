"""Experimental Phase 3 — non-family spectral search.

Where the family search of P3.6/P3.7 optimises for "close to a known
musical-family target", this one optimises for *spectral shape*
directly:

    obj = − H(amps)                           ← maximise entropy
          + λ_peak     · peak_to_mean(amps)   ← punish dominance
          + λ_sparsity · 1/n_audible          ← reward density
          − λ_crit     · criticality(config)  ← keep some liveness

Lower objective is better. The textured-smooth region of FM space sits
at depths well below the Bessel landmarks the family search lived in —
max depth ≈ 0.7 keeps every operator in its J₀-dominant regime, so
each contributes a tiny J₀-bright + faint ±1 sideband cluster. With
4–8 ops in parallel on sub-harmonic L-side-heavy paths, those clusters
overlap into a dense flat carpet without any single bin dominating.

This module is independent of the family-search code in `search.py`:
the objective is structural (only the predicted amplitude vector is
needed; no family target). Strict-op-count is also new — sampler
draws *exactly* `n_enabled` operators rather than the [min, max]
range the family sampler uses.

CLI:
    python -m phase3.experimental \\
        --output resources/presets/factory/experimental_textured_smooth \\
        --op-counts 4,5,6,7 --presets-per-count 16 \\
        --max-depth 0.7 --time-per-candidate 3 --jobs 4
"""

from __future__ import annotations

import argparse
import datetime as _dt
import json
import multiprocessing as mp
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

import numpy as np

from phase3.config import BESSEL_LANDMARKS, Config, MIN_DEPTH, ModConfig
from phase3.family_distance import (
    criticality_score,
    quantize_to_integer_grid,
)
from phase3.partial_prediction import predict_partials


# ---------------------------------------------------------------------------
# Spectral-shape helpers
# ---------------------------------------------------------------------------

def spectral_entropy(amps: np.ndarray) -> float:
    """Shannon entropy of the normalised amplitude vector, in nats.

    Higher = energy spread evenly across many partials (smooth/dense).
    Lower  = energy concentrated in a few bins (peaky/sparse).
    Returns 0 for an empty or single-bin spectrum.
    """
    if amps.size == 0:
        return 0.0
    a = np.maximum(amps, 0.0)
    s = a.sum()
    if s <= 0:
        return 0.0
    p = a / s
    nz = p[p > 0]
    return float(-(nz * np.log(nz)).sum())


def peak_to_mean(amps: np.ndarray) -> float:
    """Loudest partial / mean partial — 1.0 means perfectly flat, >>1
    means one bin dominates. Robust to zero-length input."""
    if amps.size == 0:
        return 0.0
    a = np.maximum(amps, 0.0)
    m = float(a.mean())
    if m <= 0:
        return 0.0
    return float(a.max() / m)


# ---------------------------------------------------------------------------
# Objective
# ---------------------------------------------------------------------------

@dataclass
class TexturedSmoothBreakdown:
    obj: float
    entropy: float
    peak_mean: float
    sparsity: float
    n_partials: int
    criticality: float


def textured_smooth_objective(
    cfg: Config,
    *,
    lambda_peak: float = 1.0,
    lambda_sparsity: float = 2.0,
    lambda_crit: float = 0.2,
    threshold_db: float = -50.0,
    eps: float = 1e-3,
    max_partials: int | None = None,
) -> TexturedSmoothBreakdown:
    """Lower is better. The criticality term is on by default at a
    modest weight — we want texture *and* a little life, not a dead
    smooth carpet.

    `max_partials` caps the recursive expansion in `predict_partials`
    — important at high op counts (P3.12: 64-op + low-depth searches
    benefit from cap=200 + threshold_db=-30 + eps=5e-3 to keep
    per-eval time bounded at ~20 ms instead of seconds)."""
    sp = cfg.predict(threshold_db=threshold_db, eps=eps,
                     max_partials=max_partials)
    amps = sp.amps if hasattr(sp, "amps") else np.array([])

    H = spectral_entropy(amps)
    pm = peak_to_mean(amps)
    n = int(amps.size)
    sparsity = 1.0 / max(1, n)

    if lambda_crit > 0.0:
        crit = _criticality_no_family(cfg, threshold_db=threshold_db, eps=eps)
    else:
        crit = 0.0

    obj = (-H
           + lambda_peak * pm
           + lambda_sparsity * sparsity
           - lambda_crit * crit)
    return TexturedSmoothBreakdown(
        obj=obj, entropy=H, peak_mean=pm, sparsity=sparsity,
        n_partials=n, criticality=crit,
    )


def _criticality_no_family(
    cfg: Config,
    *,
    eps_depth: float = 0.02,
    threshold_db: float = -50.0,
    eps: float = 1e-3,
) -> float:
    """Family-free criticality — same L1 perturbation sum as in
    `family_distance.criticality_score`, but operates on raw partial
    amplitudes rather than the family-quantised grid. Lets the
    experimental search reward sensitivity without needing a family
    target."""
    rs, ds = cfg.ratios_and_depths()
    if not rs:
        return 0.0
    base_sp = predict_partials(
        ratios=rs, depths=ds, f_carrier=cfg.f_carrier,
        threshold_db=threshold_db, eps=eps,
    )
    base_amps = base_sp.amps
    base_freqs = base_sp.freqs
    if base_amps.size == 0:
        return 0.0
    # Bin each perturbed spectrum onto the base's frequency support
    # (coarse grid; we just want L1 sensitivity).
    base_total = float(np.sum(base_amps)) + 1e-12
    total_l1 = 0.0
    for j in range(len(rs)):
        for sign in (+1.0, -1.0):
            ds_pert = list(ds)
            ds_pert[j] = max(0.0, ds_pert[j] + sign * eps_depth)
            sp_p = predict_partials(
                ratios=rs, depths=ds_pert, f_carrier=cfg.f_carrier,
                threshold_db=threshold_db, eps=eps,
            )
            # Difference of total energy + max-bin shift — cheap proxy.
            total_l1 += abs(float(np.sum(sp_p.amps)) - base_total)
            if sp_p.amps.size and base_amps.size:
                total_l1 += abs(float(sp_p.amps.max()) - float(base_amps.max()))
    return total_l1 / (base_total * 2.0 * max(1, len(rs)))


# ---------------------------------------------------------------------------
# Strict-op-count sampling + descent
# ---------------------------------------------------------------------------

def sample_exact_op_count(
    rng: np.random.Generator,
    n_enabled: int,
    *,
    n_mods: int = 64,
    max_path_depth: int = 6,
    landmark_bias: float = 0.0,        # landmarks at depth ~2-7; not in [0.04, 0.7]
    depth_range: tuple[float, float] = (MIN_DEPTH, 0.7),
    f_carrier: float = 220.0,
) -> Config:
    """Sample a config with EXACTLY `n_enabled` operators on.

    Defaults turn off the landmark-depth bias since the landmarks all
    sit outside the textured-smooth depth range. Path-depth cap stays
    at 6 for the sampler; coord-descent extends from there.
    """
    mods: list[ModConfig] = []
    for i in range(n_mods):
        if i >= n_enabled:
            mods.append(ModConfig(path="", depth=0.0, enabled=False))
            continue
        path_len = int(rng.integers(1, max_path_depth + 1))
        path = "".join("L" if rng.random() < 0.5 else "R" for _ in range(path_len))

        if landmark_bias > 0.0 and rng.random() < landmark_bias:
            lm = float(rng.choice(BESSEL_LANDMARKS))
            d = lm + float(rng.standard_normal()) * 0.1
        else:
            d = float(rng.uniform(*depth_range))
        d = max(depth_range[0], min(depth_range[1], d))
        mods.append(ModConfig(path=path, depth=d, enabled=True))
    return Config(mods=mods, f_carrier=f_carrier)


def coord_descent_exact(
    cfg: Config,
    objective_fn: Callable[[Config], float],
    *,
    max_iterations: int = 50,
    time_budget_s: float = 3.0,
    depth_steps: tuple[float, ...] = (0.15, 0.05, 0.02),
    max_path_depth: int = 12,
    max_depth: float = 0.7,
) -> tuple[Config, float]:
    """Strict-op-count coordinate descent. The enabled set is fixed —
    moves only adjust path strings and depths within `[MIN_DEPTH,
    max_depth]`. Auto-disable is OFF here because the count is locked.
    """
    start = time.time()
    current = cfg.copy()
    current_d = objective_fn(current)

    for _ in range(max_iterations):
        if time.time() - start > time_budget_s:
            break
        improved = False

        for i, m in enumerate(current.mods):
            if not m.enabled:
                continue

            # 1) Path perturbations (L/R add, parent chop).
            path_moves: list[str] = [m.path + "L", m.path + "R"]
            if m.path:
                path_moves.append(m.path[:-1])
            for new_path in path_moves:
                if new_path == m.path or len(new_path) > max_path_depth:
                    continue
                trial = current.copy()
                trial.mods[i].path = new_path
                d_t = objective_fn(trial)
                if d_t < current_d - 1e-7:
                    current, current_d = trial, d_t
                    improved = True
                    break
            if improved:
                break

            # 2) Depth perturbations, coarse first.
            for step in depth_steps:
                for delta in (step, -step):
                    new_depth = m.depth + delta
                    if new_depth < MIN_DEPTH or new_depth > max_depth:
                        continue
                    trial = current.copy()
                    trial.mods[i].depth = new_depth
                    d_t = objective_fn(trial)
                    if d_t < current_d - 1e-7:
                        current, current_d = trial, d_t
                        improved = True
                        break
                if improved:
                    break
            if improved:
                break

        if not improved:
            break

    return current, current_d


# ---------------------------------------------------------------------------
# Two-phase experimental search
# ---------------------------------------------------------------------------

@dataclass
class ExperimentalResult:
    config: Config
    objective: float
    phase1_objective: float
    op_count: int
    seed: int
    breakdown: TexturedSmoothBreakdown


def search_exact_op_count(
    n_enabled: int,
    *,
    n_phase1: int = 800,
    k_top: int = 32,
    seed: int = 1337,
    time_budget_per_candidate_s: float = 3.0,
    max_depth: float = 0.7,
    max_path_depth: int = 12,
    lambda_peak: float = 1.0,
    lambda_sparsity: float = 2.0,
    lambda_crit: float = 0.2,
    f_carrier: float = 220.0,
    predict_threshold_db: float = -50.0,
    predict_eps: float = 1e-3,
    predict_max_partials: int | None = None,
) -> list[ExperimentalResult]:
    """Two-phase search at a strict op-count. Returns deduped top-K
    refined results sorted by objective (lowest first)."""
    rng = np.random.default_rng(seed)

    def obj_breakdown(cfg: Config) -> TexturedSmoothBreakdown:
        return textured_smooth_objective(
            cfg,
            lambda_peak=lambda_peak,
            lambda_sparsity=lambda_sparsity,
            lambda_crit=lambda_crit,
            threshold_db=predict_threshold_db,
            eps=predict_eps,
            max_partials=predict_max_partials,
        )

    def obj_scalar(cfg: Config) -> float:
        return obj_breakdown(cfg).obj

    # Phase 1: random samples scored on a *cheaper* objective (no
    # criticality), then we re-score the top-K with the full breakdown.
    def obj_phase1(cfg: Config) -> float:
        return textured_smooth_objective(
            cfg,
            lambda_peak=lambda_peak,
            lambda_sparsity=lambda_sparsity,
            lambda_crit=0.0,  # skip criticality in Phase 1
            threshold_db=predict_threshold_db,
            eps=predict_eps,
            max_partials=predict_max_partials,
        ).obj

    phase1: list[tuple[Config, float]] = []
    for _ in range(n_phase1):
        cfg = sample_exact_op_count(
            rng, n_enabled, max_path_depth=max_path_depth,
            depth_range=(MIN_DEPTH, max_depth),
            f_carrier=f_carrier,
        )
        phase1.append((cfg.canonicalised(), obj_phase1(cfg)))

    # Dedupe canonical-fingerprint and take top-K candidates.
    best: dict[tuple, tuple[Config, float]] = {}
    for cfg, d in phase1:
        fp = cfg.fingerprint()
        if fp not in best or d < best[fp][1]:
            best[fp] = (cfg, d)
    candidates = sorted(best.values(), key=lambda x: x[1])[:k_top]

    # Phase 2: refine with the full objective.
    refined: list[ExperimentalResult] = []
    for cfg, p1 in candidates:
        out, d = coord_descent_exact(
            cfg, obj_scalar,
            time_budget_s=time_budget_per_candidate_s,
            max_path_depth=max_path_depth,
            max_depth=max_depth,
        )
        br = obj_breakdown(out)
        refined.append(ExperimentalResult(
            config=out.canonicalised(),
            objective=d,
            phase1_objective=p1,
            op_count=n_enabled,
            seed=seed,
            breakdown=br,
        ))

    # Post-refinement dedup.
    by_fp: dict[tuple, ExperimentalResult] = {}
    for r in refined:
        fp = r.config.fingerprint()
        if fp not in by_fp or r.objective < by_fp[fp].objective:
            by_fp[fp] = r
    results = list(by_fp.values())
    results.sort(key=lambda r: r.objective)
    return results


# ---------------------------------------------------------------------------
# Writer
# ---------------------------------------------------------------------------

def _result_to_brocot(result: ExperimentalResult, name: str) -> dict:
    """Write an experimental result as a v2 .brocot.

    Uses kind='spectral-search' so the file is distinguishable from
    family-search outputs; the loader doesn't validate kind, so this
    is fine."""
    cfg = result.config
    operators = [
        {"enabled": bool(m.enabled),
         "path":    m.path,
         "depth":   float(m.depth)}
        for m in cfg.mods
    ]
    br = result.breakdown
    return {
        "name":    name,
        "version": "2.0",
        "provenance": {
            "kind":        "spectral-search",
            "author":      "ARS Phase 3 (experimental)",
            "description": (
                f"Textured-smooth experimental discovery: {result.op_count} ops, "
                f"objective {result.objective:.4f} "
                f"(H={br.entropy:.3f}, peak/mean={br.peak_mean:.2f}, "
                f"n_partials={br.n_partials}, crit={br.criticality:.4f})."
            ),
            "category":    f"Textured-Smooth · {result.op_count} ops",
            "search": {
                "mode":        "spectral-search/textured-smooth",
                "op_count":    int(result.op_count),
                "seed":        int(result.seed),
                "objective":   float(result.objective),
                "entropy":     float(br.entropy),
                "peak_mean":   float(br.peak_mean),
                "n_partials":  int(br.n_partials),
                "criticality": float(br.criticality),
                "timestamp":   _dt.datetime.now(_dt.timezone.utc).isoformat(),
            },
        },
        "engine": {
            "polyphony":   "mono",
            "octaveShift": 0,
            "pathMode":    "free",
            "operators":   operators,
        },
        "filter":   {"type": "lp", "cutoff": 0.85, "resonance": 0.10},
        "envelope": {"attack": 0.01, "decay": 0.15, "sustain": 0.80, "release": 0.40},
        "lfo":      {"shape": "sine", "rate": 0.5, "depth": 0.0, "keySync": False},
        "master":   {"level": 0.5},
    }


def write_results(
    results: list[ExperimentalResult],
    output_dir: Path,
    op_count: int,
    max_depth: float | None = None,
) -> list[Path]:
    """Write results into `<output_dir>/<N>_op/`. The optional `max_depth`
    suffix on the file stem disambiguates parallel runs at the same op
    count but different depth caps (P3.12: 64-op × {0.3, 0.5} both land
    here without filename collisions)."""
    sub = output_dir / f"{op_count}_op"
    sub.mkdir(parents=True, exist_ok=True)
    depth_tag = "" if max_depth is None else f"_d{int(round(max_depth * 100)):03d}"
    paths: list[Path] = []
    for i, r in enumerate(results):
        name = (f"Textured·{op_count}op·{i+1:02d}  "
                f"(H={r.breakdown.entropy:.2f}, n={r.breakdown.n_partials})")
        stem = f"textured_smooth_{op_count}op{depth_tag}_{i+1:02d}"
        path = sub / f"{stem}.brocot"
        doc = _result_to_brocot(r, name)
        path.write_text(json.dumps(doc, indent=2) + "\n")
        paths.append(path)
    return paths


# ---------------------------------------------------------------------------
# CLI entry
# ---------------------------------------------------------------------------

def _run_one_op_count(
    op_count: int,
    *,
    output_dir: Path,
    presets_per_count: int,
    n_phase1: int,
    time_budget_per_candidate_s: float,
    seed_base: int,
    max_depth: float,
    max_path_depth: int,
    lambda_peak: float,
    lambda_sparsity: float,
    lambda_crit: float,
    predict_threshold_db: float = -50.0,
    predict_eps: float = 1e-3,
    predict_max_partials: int | None = None,
) -> tuple[int, int, float]:
    t0 = time.time()
    results = search_exact_op_count(
        op_count,
        n_phase1=n_phase1,
        k_top=presets_per_count * 2,  # over-sample, dedupe down
        seed=seed_base + op_count,
        time_budget_per_candidate_s=time_budget_per_candidate_s,
        max_depth=max_depth,
        max_path_depth=max_path_depth,
        lambda_peak=lambda_peak,
        lambda_sparsity=lambda_sparsity,
        lambda_crit=lambda_crit,
        predict_threshold_db=predict_threshold_db,
        predict_eps=predict_eps,
        predict_max_partials=predict_max_partials,
    )
    results = results[:presets_per_count]
    paths = write_results(results, output_dir, op_count, max_depth=max_depth)
    elapsed = time.time() - t0
    objs = [r.objective for r in results]
    print(f"  [{op_count}-op] {len(paths)} presets in {elapsed:.1f}s "
          f"| obj {objs[0]:.3f}..{objs[-1]:.3f}  "
          f"H̄={np.mean([r.breakdown.entropy for r in results]):.2f}  "
          f"n̄={np.mean([r.breakdown.n_partials for r in results]):.0f}")
    return op_count, len(paths), elapsed


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Experimental Phase-3: textured-smooth spectral search.")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--op-counts", default="4,5,6,7",
                        help="Comma-separated op-counts to run (default 4,5,6,7).")
    parser.add_argument("--presets-per-count", type=int, default=16)
    parser.add_argument("--n-phase1", type=int, default=800)
    parser.add_argument("--time-per-candidate", type=float, default=3.0)
    parser.add_argument("--max-depth", type=float, default=0.7)
    parser.add_argument("--max-path-depth", type=int, default=12)
    parser.add_argument("--seed", type=int, default=2701)
    parser.add_argument("--lambda-peak", type=float, default=1.0)
    parser.add_argument("--lambda-sparsity", type=float, default=2.0)
    parser.add_argument("--lambda-crit", type=float, default=0.2)
    parser.add_argument("--predict-threshold-db", type=float, default=-50.0,
                        help="dB floor for partial-prediction (looser at high op counts; "
                             "P3.12 uses -30 for 64-op).")
    parser.add_argument("--predict-eps", type=float, default=1e-3,
                        help="Per-modulator order-bound tolerance (looser at high op "
                             "counts; P3.12 uses 5e-3 for 64-op).")
    parser.add_argument("--predict-max-partials", type=int, default=0,
                        help="Cap on the recursive expansion's emitted partials. "
                             "0 = no cap (default for ≤16 ops); P3.12 uses 200 for 64-op.")
    parser.add_argument("--jobs", type=int, default=1,
                        help="Run this many op-counts in parallel.")
    args = parser.parse_args(argv)

    op_counts = [int(x) for x in args.op_counts.split(",") if x.strip()]
    args.output.mkdir(parents=True, exist_ok=True)

    predict_cap = args.predict_max_partials if args.predict_max_partials > 0 else None

    print(f"Experimental textured-smooth search.")
    print(f"  op-counts={op_counts}  presets-per-count={args.presets_per_count}")
    print(f"  max_depth={args.max_depth}  λ_peak={args.lambda_peak}  "
          f"λ_sparsity={args.lambda_sparsity}  λ_crit={args.lambda_crit}")
    print(f"  predict: threshold_db={args.predict_threshold_db}  "
          f"eps={args.predict_eps}  max_partials={predict_cap}")
    print(f"  output: {args.output.resolve()}\n")

    t0 = time.time()
    if args.jobs <= 1:
        for n in op_counts:
            _run_one_op_count(
                n,
                output_dir=args.output,
                presets_per_count=args.presets_per_count,
                n_phase1=args.n_phase1,
                time_budget_per_candidate_s=args.time_per_candidate,
                seed_base=args.seed,
                max_depth=args.max_depth,
                max_path_depth=args.max_path_depth,
                lambda_peak=args.lambda_peak,
                lambda_sparsity=args.lambda_sparsity,
                lambda_crit=args.lambda_crit,
                predict_threshold_db=args.predict_threshold_db,
                predict_eps=args.predict_eps,
                predict_max_partials=predict_cap,
            )
    else:
        ctx = mp.get_context("fork")
        with ctx.Pool(processes=args.jobs) as pool:
            jobs = [pool.apply_async(
                _run_one_op_count,
                args=(n,),
                kwds=dict(
                    output_dir=args.output,
                    presets_per_count=args.presets_per_count,
                    n_phase1=args.n_phase1,
                    time_budget_per_candidate_s=args.time_per_candidate,
                    seed_base=args.seed,
                    max_depth=args.max_depth,
                    max_path_depth=args.max_path_depth,
                    lambda_peak=args.lambda_peak,
                    lambda_sparsity=args.lambda_sparsity,
                    lambda_crit=args.lambda_crit,
                ),
            ) for n in op_counts]
            for j in jobs:
                j.get()
    elapsed = time.time() - t0
    print(f"\nDone in {elapsed:.1f}s.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
