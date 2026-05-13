"""Two-phase search per unified-synth 3.0 §2.2.

Phase 1: constrained random sampling.
    Draw N candidate configurations from a prior biased toward shallow
    Stern-Brocot paths and Bessel-landmark depths. Evaluate the
    objective for each; retain the top K *unique* canonical forms.

Phase 2: local refinement via coordinate descent.
    For each Phase-1 candidate, perturb one parameter at a time and
    accept moves that reduce the objective. Continuous parameters
    (depths) take small ± steps; discrete parameters (Stern-Brocot
    paths) take one-step L/R/parent moves. Stop on convergence or when
    the per-candidate time budget is exhausted.

P3.7 changes:
    - Permutation dedup at both phases — operator order is musically
      meaningless, so two configs that only differ by slot assignment
      are the same patch and we keep only one.
    - Min enabled-op depth `MIN_DEPTH` (=0.04 by default) — operators
      below the floor are auto-disabled. A coordinate-descent move
      that would drop an op's depth below `MIN_DEPTH` turns it off
      instead of forcing the depth, which lets the search naturally
      decide enabled-count.
    - Minimum enabled-op count (default 4). Sampler draws into [4..N],
      coord-descent won't accept a move that drops the count below.
    - Deeper paths (default max 16). Long SB paths let single FM ops
      approximate arbitrary inharmonic ratios, so the search can find
      the same patches the V2 free-Hz modulator bank could.
    - Composite objective `family_distance − λ_crit · criticality`.
      With λ_crit = 0 the criticality term is skipped entirely and the
      objective matches P3.6 behaviour. With λ_crit > 0 the search
      biases toward configurations whose spectra react sensitively to
      small parameter changes.

Mode B (reference-spectrum match) and Mode C (criticality-as-primary)
from the 3.0 spec are still deferred; here criticality only weights
the family-search objective.
"""

from __future__ import annotations

import time
from dataclasses import dataclass

import numpy as np

from phase3.config import BESSEL_LANDMARKS, Config, MIN_DEPTH, ModConfig
from phase3.families import Family
from phase3.family_distance import composite_objective, family_distance


# ---------------------------------------------------------------------------
# Phase 1 — random sampling
# ---------------------------------------------------------------------------

def sample_random_config(
    rng: np.random.Generator,
    n_mods: int = 8,
    min_enabled: int = 4,
    max_path_depth: int = 16,
    sample_max_path_depth: int = 6,
    landmark_bias: float = 0.7,
    landmark_jitter_std: float = 0.15,
    depth_range: tuple[float, float] = (MIN_DEPTH, 6.0),
    f_carrier: float = 220.0,
) -> Config:
    """Sample a configuration biased toward the structurally meaningful
    sub-region of the search space.

    - Number of enabled modulators uniform in [min_enabled, n_mods].
    - Each enabled mod's slot is initially position-0..n_enabled-1 —
      the random subset choice and the slot assignment are decoupled
      because the slot doesn't matter musically (canonicalisation
      packs the leading slots anyway).
    - Path length uniform in [1, sample_max_path_depth] (default 6).
      Sampler stays shallow because most random deep-path configs are
      noisy and never improve; coord-descent extends paths up to
      `max_path_depth` (default 16) where it pays off.
    - Depth: `landmark_bias` chance of being drawn near a Bessel
      landmark (Gaussian jitter); otherwise uniform over `depth_range`.
      Floored at MIN_DEPTH.
    """
    n_enabled = int(rng.integers(min_enabled, n_mods + 1))
    sample_cap = min(sample_max_path_depth, max_path_depth)

    mods: list[ModConfig] = []
    for i in range(n_mods):
        if i >= n_enabled:
            mods.append(ModConfig(path="", depth=0.0, enabled=False))
            continue
        path_len = int(rng.integers(1, sample_cap + 1))
        path = "".join("L" if rng.random() < 0.5 else "R" for _ in range(path_len))

        if rng.random() < landmark_bias:
            landmark = float(rng.choice(BESSEL_LANDMARKS))
            depth = landmark + float(rng.standard_normal()) * landmark_jitter_std
        else:
            depth = float(rng.uniform(*depth_range))
        depth = max(MIN_DEPTH, min(8.0, depth))

        mods.append(ModConfig(path=path, depth=depth, enabled=True))

    return Config(mods=mods, f_carrier=f_carrier)


# ---------------------------------------------------------------------------
# Phase 2 — coordinate descent
# ---------------------------------------------------------------------------

def _eval_config(
    cfg: Config,
    family: Family,
    *,
    q_max: int,
    lambda_crit: float,
    threshold_db: float,
    eps: float,
) -> float:
    """Evaluate the composite objective. When lambda_crit == 0 we skip
    the criticality step (much cheaper) and fall through to plain
    family_distance — exactly what the P3.6 search did."""
    if lambda_crit == 0.0:
        sp = cfg.predict(threshold_db=threshold_db, eps=eps)
        return family_distance(sp, family, cfg.f_carrier, q_max=q_max)
    obj, _, _ = composite_objective(
        cfg, family,
        f_carrier=cfg.f_carrier,
        q_max=q_max,
        lambda_crit=lambda_crit,
        threshold_db=threshold_db,
        eps=eps,
    )
    return obj


def coordinate_descent(
    cfg: Config,
    family: Family,
    *,
    max_iterations: int = 60,
    time_budget_s: float = 4.0,
    depth_steps: tuple[float, ...] = (0.5, 0.15, 0.05),
    q_max: int = 16,
    threshold_db: float = -50.0,
    eps: float = 1e-3,
    min_enabled: int = 4,
    max_path_depth: int = 16,
    lambda_crit: float = 0.0,
) -> tuple[Config, float]:
    """Refine `cfg` against `family` until no single-parameter move
    decreases the objective, or the time budget runs out.

    Each iteration walks every enabled modulator and tries:
      - Stern-Brocot L / R / parent path moves (capped at max_path_depth)
      - depth ± each step in `depth_steps` (coarse → fine); if a step
        would drop below MIN_DEPTH the move is converted to auto-disable
        (the operator turns off, freeing search budget) — but only if
        doing so keeps the enabled count ≥ min_enabled. Otherwise the
        move is rejected.
      - depth set to a nearby Bessel landmark (snapping moves; helps
        escape low-information local optima).

    Accepts the first improvement found (greedy descent). Re-runs from
    the start of the parameter list after accepting, so the search can
    chain dependent improvements.
    """
    start = time.time()
    current = cfg.copy()
    current_d = _eval_config(
        current, family,
        q_max=q_max, lambda_crit=lambda_crit,
        threshold_db=threshold_db, eps=eps,
    )

    def evaluate(trial: Config) -> float:
        return _eval_config(
            trial, family,
            q_max=q_max, lambda_crit=lambda_crit,
            threshold_db=threshold_db, eps=eps,
        )

    for _ in range(max_iterations):
        if time.time() - start > time_budget_s:
            break
        improved = False

        for i, m in enumerate(current.mods):
            if not m.enabled:
                continue

            # 1) Path perturbations.
            path_moves: list[str] = [m.path + "L", m.path + "R"]
            if m.path:
                path_moves.append(m.path[:-1])  # parent
            for new_path in path_moves:
                if new_path == m.path:
                    continue
                if len(new_path) > max_path_depth:
                    continue
                trial = current.copy()
                trial.mods[i].path = new_path
                d_t = evaluate(trial)
                if d_t < current_d - 1e-7:
                    current, current_d = trial, d_t
                    improved = True
                    break
            if improved:
                break

            # 2) Depth perturbations, coarse first. A step that would
            #    take the depth below MIN_DEPTH is reinterpreted as
            #    "disable this op" — but only if that leaves at least
            #    min_enabled mods on.
            for step in depth_steps:
                for delta in (step, -step):
                    new_depth = m.depth + delta
                    if new_depth > 8.0:
                        continue

                    trial = current.copy()
                    if new_depth < MIN_DEPTH:
                        # Auto-disable. Requires we keep ≥ min_enabled.
                        if trial.num_enabled() - 1 < min_enabled:
                            continue
                        trial.mods[i].enabled = False
                        trial.mods[i].depth = 0.0
                        trial.mods[i].path = ""
                    else:
                        trial.mods[i].depth = new_depth

                    d_t = evaluate(trial)
                    if d_t < current_d - 1e-7:
                        current, current_d = trial, d_t
                        improved = True
                        break
                if improved:
                    break
            if improved:
                break

            # 3) Landmark-snap moves: try each Bessel landmark in place
            #    of the current depth (only if current isn't already
            #    near one). These help the search jump out of low-info
            #    plateaus when local steps aren't biting.
            if not any(abs(m.depth - lm) < 0.08 for lm in BESSEL_LANDMARKS):
                for lm in BESSEL_LANDMARKS:
                    trial = current.copy()
                    trial.mods[i].depth = lm
                    d_t = evaluate(trial)
                    if d_t < current_d - 1e-7:
                        current, current_d = trial, d_t
                        improved = True
                        break
            if improved:
                break

        if not improved:
            break

    return current, current_d


# ---------------------------------------------------------------------------
# Two-phase search
# ---------------------------------------------------------------------------

@dataclass
class SearchResult:
    config: Config
    objective: float       # final composite-objective value
    family: str            # family name
    seed: int
    phase1_objective: float
    iterations_phase2: int = 0
    family_distance: float = 0.0
    criticality: float = 0.0


def _dedupe_by_fingerprint(
    items: list[tuple[Config, float]],
) -> list[tuple[Config, float]]:
    """Keep the best-scoring entry for each canonical fingerprint."""
    best: dict[tuple, tuple[Config, float]] = {}
    for cfg, d in items:
        fp = cfg.fingerprint()
        if fp not in best or d < best[fp][1]:
            best[fp] = (cfg, d)
    return list(best.values())


def two_phase_search(
    family: Family,
    *,
    f_carrier: float = 220.0,
    n_phase1: int = 1000,
    k_top: int = 20,
    seed: int = 42,
    time_budget_per_candidate_s: float = 4.0,
    q_max: int = 16,
    threshold_db_phase1: float = -30.0,
    threshold_db_phase2: float = -50.0,
    eps_phase1: float = 5e-3,
    eps_phase2: float = 1e-3,
    max_partials_phase1: int = 800,
    min_enabled: int = 4,
    max_path_depth: int = 16,
    lambda_crit: float = 0.0,
) -> list[SearchResult]:
    """Run the full two-phase search and return the top-K refined,
    *uniquely-fingerprinted* configurations sorted by objective.

    `threshold_db_phase1` is intentionally looser than phase 2 — the
    coarse ranking in Phase 1 doesn't need the same numerical precision
    that the refinement step does, and the looser threshold makes the
    1000-evaluation pass run faster.

    `lambda_crit` controls how much the criticality bonus skews the
    objective in Phase 2 refinement. Phase 1 always uses plain
    `family_distance` regardless — the 1000+ random samples need to
    cull fast, and criticality is a refinement bias rather than a
    candidate-selection signal (most random samples are bland whether
    you measure their criticality or not). Defaults to 0 (no
    criticality anywhere, exactly P3.6 behaviour).
    """
    rng = np.random.default_rng(seed)

    # --- Phase 1 ---
    # Always plain family_distance here; criticality is too expensive
    # for the 1000-evaluation pass and culls poorly on random samples.
    phase1: list[tuple[Config, float]] = []
    for _ in range(n_phase1):
        cfg = sample_random_config(
            rng,
            min_enabled=min_enabled,
            max_path_depth=max_path_depth,
            f_carrier=f_carrier,
        )
        d = family_distance(
            cfg.predict(
                threshold_db=threshold_db_phase1,
                eps=eps_phase1,
                max_partials=max_partials_phase1,
            ),
            family, f_carrier, q_max=q_max,
        )
        phase1.append((cfg.canonicalised(), d))

    # Dedupe by canonical fingerprint, then take top-K candidates.
    phase1 = _dedupe_by_fingerprint(phase1)
    phase1.sort(key=lambda x: x[1])
    top = phase1[:k_top]

    # --- Phase 2 ---
    raw_results: list[SearchResult] = []
    for cfg, p1_d in top:
        refined, final_d = coordinate_descent(
            cfg, family,
            time_budget_s=time_budget_per_candidate_s,
            q_max=q_max,
            threshold_db=threshold_db_phase2,
            eps=eps_phase2,
            min_enabled=min_enabled,
            max_path_depth=max_path_depth,
            lambda_crit=lambda_crit,
        )

        # Record family_distance + criticality separately for the
        # provenance block (so we can show 'obj=X (fd=Y, crit=Z)' in
        # the audit).
        sp = refined.predict(threshold_db=threshold_db_phase2, eps=eps_phase2)
        fd = family_distance(sp, family, refined.f_carrier, q_max=q_max)
        if lambda_crit > 0.0:
            from phase3.family_distance import criticality_score  # local
            cs = criticality_score(
                refined, family, q_max=q_max,
                threshold_db=threshold_db_phase2, eps=eps_phase2,
            )
        else:
            cs = 0.0

        raw_results.append(SearchResult(
            config=refined.canonicalised(),
            objective=final_d,
            family=family.name,
            seed=seed,
            phase1_objective=p1_d,
            family_distance=fd,
            criticality=cs,
        ))

    # Post-refinement dedup — coord-descent can land different Phase-1
    # candidates onto the same local optimum.
    by_fp: dict[tuple, SearchResult] = {}
    for r in raw_results:
        fp = r.config.fingerprint()
        if fp not in by_fp or r.objective < by_fp[fp].objective:
            by_fp[fp] = r
    results = list(by_fp.values())
    results.sort(key=lambda r: r.objective)
    return results
