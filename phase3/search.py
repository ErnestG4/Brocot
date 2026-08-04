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

def _sample_mod(
    rng: np.random.Generator,
    sample_cap: int,
    landmark_bias: float,
    landmark_jitter_std: float,
    depth_range: tuple[float, float],
) -> ModConfig:
    """Draw a single enabled modulator from the sampling prior.

    Extracted from `sample_random_config` so basin-hopping's op-swap
    kick reuses the identical prior. The rng draw *order* is preserved
    exactly (path length, path chars, landmark coin, depth) — Phase-1
    sampling for a given seed is unchanged.
    """
    # Path length from 0 so the *empty path* (ratio 1/1, the harmonic
    # fundamental) is reachable in Phase 1 — previously path_len ≥ 1 made
    # ratio-1 modulators impossible to seed, so any harmonic-stack target
    # with a 1/1 op could only be reached via Phase-2 parent moves (rare).
    path_len = int(rng.integers(0, sample_cap + 1))
    path = "".join("L" if rng.random() < 0.5 else "R" for _ in range(path_len))

    if rng.random() < landmark_bias:
        landmark = float(rng.choice(BESSEL_LANDMARKS))
        depth = landmark + float(rng.standard_normal()) * landmark_jitter_std
    else:
        depth = float(rng.uniform(*depth_range))
    depth = max(MIN_DEPTH, min(8.0, depth))

    return ModConfig(path=path, depth=depth, enabled=True)


def sample_random_config(
    rng: np.random.Generator,
    n_mods: int = 64,
    min_enabled: int = 4,
    max_enabled: int | None = 8,
    max_path_depth: int = 16,
    sample_max_path_depth: int = 6,
    landmark_bias: float = 0.55,
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
    - Path length uniform in [0, sample_max_path_depth] (default 6).
      Length 0 is the empty path (ratio 1/1, the fundamental); including
      it lets harmonic-stack targets be seeded directly. Sampler stays
      shallow because most random deep-path configs are noisy and never
      improve; coord-descent extends paths up to `max_path_depth`
      (default 16) where it pays off.
    - Depth: `landmark_bias` chance of being drawn near a Bessel
      landmark (Gaussian jitter); otherwise uniform over `depth_range`.
      Floored at MIN_DEPTH.
    """
    upper = n_mods if max_enabled is None else min(max_enabled, n_mods)
    n_enabled = int(rng.integers(min_enabled, upper + 1))
    sample_cap = min(sample_max_path_depth, max_path_depth)

    mods: list[ModConfig] = []
    for i in range(n_mods):
        if i >= n_enabled:
            mods.append(ModConfig(path="", depth=0.0, enabled=False))
            continue
        mods.append(_sample_mod(
            rng, sample_cap, landmark_bias, landmark_jitter_std, depth_range,
        ))

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
# Basin hopping (S10) — perturb-and-descend on top of coordinate descent
# ---------------------------------------------------------------------------
#
# Mode B residual (mode_b.md): coordinate descent finds perceptually-
# similar configs but gets trapped short of the exact generating one.
# Basin hopping wraps the existing descent in a Metropolis walk: kick
# the current optimum with a few structured random moves, re-descend
# with a short budget, accept better always / worse with probability
# exp(-Δ/T), and track the global best separately from the walker.
#
# This is a bespoke loop, NOT scipy.optimize.basinhopping — SB paths
# are discrete strings and scipy's continuous-step machinery fights
# discrete moves.

_KICK_KINDS = ("path", "depth", "landmark", "swap")


def perturb(
    cfg: Config,
    rng: np.random.Generator,
    *,
    kick_ops: tuple[int, int] = (1, 3),
    min_enabled: int = 4,
    max_path_depth: int = 16,
) -> Config:
    """Apply k (uniform in `kick_ops` inclusive) random kicks to a copy
    of `cfg` and return it. Kicks are drawn uniformly from:

      - path kick: one enabled op takes 1-3 random Stern-Brocot steps
        mixing parent-trims and L/R extensions (capped at
        `max_path_depth`);
      - depth kick: Gaussian nudge (σ=0.5), capped at 8.0; a landing
        below MIN_DEPTH auto-disables the op exactly like coordinate
        descent — but only if the enabled count stays ≥ `min_enabled`,
        otherwise the kick is a no-op;
      - landmark teleport: depth ← a random Bessel landmark;
      - op swap: disable one enabled op and enable a freshly-sampled
        one (same per-op prior as Phase 1), keeping the enabled count
        unchanged.

    All P3.7 invariants (min_enabled, MIN_DEPTH floor, max_path_depth)
    hold on the output. The input config is never mutated.
    """
    kicked = cfg.copy()
    n_kicks = int(rng.integers(kick_ops[0], kick_ops[1] + 1))

    for _ in range(n_kicks):
        enabled_idx = [i for i, m in enumerate(kicked.mods) if m.enabled]
        if not enabled_idx:
            break
        kind = _KICK_KINDS[int(rng.integers(0, len(_KICK_KINDS)))]

        if kind == "path":
            i = int(rng.choice(enabled_idx))
            path = kicked.mods[i].path
            for _ in range(int(rng.integers(1, 4))):
                moves = []
                if path:
                    moves.append("parent")
                if len(path) < max_path_depth:
                    moves.extend(("L", "R"))
                if not moves:
                    break
                mv = moves[int(rng.integers(0, len(moves)))]
                path = path[:-1] if mv == "parent" else path + mv
            kicked.mods[i].path = path

        elif kind == "depth":
            i = int(rng.choice(enabled_idx))
            m = kicked.mods[i]
            new_depth = min(8.0, m.depth + float(rng.standard_normal()) * 0.5)
            if new_depth < MIN_DEPTH:
                # Auto-disable, exactly like coordinate descent — only
                # if that keeps ≥ min_enabled ops on; otherwise reject.
                if len(enabled_idx) - 1 >= min_enabled:
                    m.enabled = False
                    m.depth = 0.0
                    m.path = ""
            else:
                m.depth = new_depth

        elif kind == "landmark":
            i = int(rng.choice(enabled_idx))
            kicked.mods[i].depth = float(rng.choice(BESSEL_LANDMARKS))

        else:  # "swap"
            disabled_idx = [
                i for i, m in enumerate(kicked.mods) if not m.enabled
            ]
            if not disabled_idx:
                continue  # every slot on — nothing to swap in
            i_off = int(rng.choice(enabled_idx))
            i_on = int(rng.choice(disabled_idx))
            kicked.mods[i_off] = ModConfig(path="", depth=0.0, enabled=False)
            kicked.mods[i_on] = _sample_mod(
                rng,
                sample_cap=min(6, max_path_depth),
                landmark_bias=0.55,
                landmark_jitter_std=0.15,
                depth_range=(MIN_DEPTH, 6.0),
            )

    return kicked


def _metropolis_accept(
    delta: float,
    temperature: float,
    rng: np.random.Generator,
) -> bool:
    """Metropolis criterion: always accept an improvement (Δ ≤ 0);
    accept a worsening with probability exp(-Δ/T). T ≤ 0 degenerates
    to strict descent (never accept worse)."""
    if delta <= 0.0:
        return True
    if temperature <= 0.0:
        return False
    return bool(rng.random() < np.exp(-delta / temperature))


def basin_hop(
    cfg: Config,
    family: Family,
    *,
    n_hops: int = 20,
    kick_ops: tuple[int, int] = (1, 3),
    temperature: float = 0.02,
    seed: int = 0,
    cd_budget_s: float = 1.5,
    **cd_kwargs,
) -> tuple[Config, float]:
    """Basin-hopping refinement of `cfg` against `family`.

    Descend `cfg` with the existing coordinate descent, then repeat
    `n_hops` times: perturb the walker, re-descend with a short budget
    (`cd_budget_s` → coordinate_descent's `time_budget_s`; pass
    `max_iterations` via `cd_kwargs` for iteration-bounded determinism),
    Metropolis-accept. The global best is tracked separately from the
    walker and returned canonicalised.

    Deterministic given `seed` (a single np.random.default_rng threads
    through perturb and acceptance), provided the descent itself is
    iteration-bounded rather than wall-clock-bounded.

    `cd_kwargs` are forwarded to `coordinate_descent` (q_max,
    threshold_db, eps, min_enabled, max_path_depth, lambda_crit,
    max_iterations, …); min_enabled / max_path_depth also govern
    `perturb`'s invariants.
    """
    rng = np.random.default_rng(seed)
    min_enabled = cd_kwargs.get("min_enabled", 4)
    max_path_depth = cd_kwargs.get("max_path_depth", 16)

    cur, cur_d = coordinate_descent(
        cfg, family, time_budget_s=cd_budget_s, **cd_kwargs,
    )
    best, best_d = cur, cur_d

    for _ in range(n_hops):
        kicked = perturb(
            cur, rng,
            kick_ops=kick_ops,
            min_enabled=min_enabled,
            max_path_depth=max_path_depth,
        )
        cand, cand_d = coordinate_descent(
            kicked, family, time_budget_s=cd_budget_s, **cd_kwargs,
        )
        if cand_d < best_d:
            best, best_d = cand, cand_d
        if _metropolis_accept(cand_d - cur_d, temperature, rng):
            cur, cur_d = cand, cand_d

    return best.canonicalised(), best_d


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
    n_basin_hops: int = 0,
    basin_temperature: float = 0.02,
    basin_cd_budget_s: float = 1.5,
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

    `n_basin_hops` (S11): when > 0, apply `basin_hop` to the top-3
    post-Phase-2 results (`n_basin_hops` hops each, walker temperature
    `basin_temperature`, per-descent budget `basin_cd_budget_s`), keep
    any improvement, then re-dedupe and re-sort. Defaults to 0 —
    Phase 1 + Phase 2 behaviour is byte-identical to before, so this
    doubles as the revert switch.
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

    # --- Basin hopping (S11) — optional escape pass on the top 3 ---
    if n_basin_hops > 0:
        for rank, r in enumerate(results[:3]):
            refined, d = basin_hop(
                r.config, family,
                n_hops=n_basin_hops,
                temperature=basin_temperature,
                seed=seed + 9001 + rank,
                cd_budget_s=basin_cd_budget_s,
                q_max=q_max,
                threshold_db=threshold_db_phase2,
                eps=eps_phase2,
                min_enabled=min_enabled,
                max_path_depth=max_path_depth,
                lambda_crit=lambda_crit,
            )
            if d < r.objective - 1e-9:
                r.config = refined  # basin_hop returns it canonicalised
                r.objective = d
                sp = refined.predict(
                    threshold_db=threshold_db_phase2, eps=eps_phase2,
                )
                r.family_distance = family_distance(
                    sp, family, refined.f_carrier, q_max=q_max,
                )
                if lambda_crit > 0.0:
                    from phase3.family_distance import criticality_score
                    r.criticality = criticality_score(
                        refined, family, q_max=q_max,
                        threshold_db=threshold_db_phase2, eps=eps_phase2,
                    )

        # Hops can land different candidates on the same optimum —
        # re-dedupe by fingerprint and re-sort.
        by_fp = {}
        for r in results:
            fp = r.config.fingerprint()
            if fp not in by_fp or r.objective < by_fp[fp].objective:
                by_fp[fp] = r
        results = list(by_fp.values())
        results.sort(key=lambda r: r.objective)

    return results
