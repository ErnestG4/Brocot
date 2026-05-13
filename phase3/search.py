"""Two-phase search per unified-synth 3.0 §2.2.

Phase 1: constrained random sampling.
    Draw N candidate configurations from a prior biased toward shallow
    Stern-Brocot paths (depth ≤ 6) and Bessel-landmark depths. Evaluate
    family_distance for each; retain the top K.

Phase 2: local refinement via coordinate descent.
    For each Phase-1 candidate, perturb one parameter at a time and
    accept moves that reduce the objective. Continuous parameters
    (depths) take small ± steps; discrete parameters (Stern-Brocot
    paths) take one-step L/R/parent moves. Stop on convergence or
    when the per-candidate time budget is exhausted.

The output is K refined SearchResult objects sorted by final
objective. The CLI in `generate.py` (P3.6) wraps this and dumps the
top result(s) as .brocot v2 JSON files.

Mode B (reference-spectrum match) and Mode C (criticality) from the
3.0 spec are deferred — only Mode A (family target) is implemented
in v1 of Phase 3.
"""

from __future__ import annotations

import time
from dataclasses import dataclass

import numpy as np

from phase3.config import BESSEL_LANDMARKS, Config, ModConfig
from phase3.families import Family
from phase3.family_distance import family_distance


# ---------------------------------------------------------------------------
# Phase 1 — random sampling
# ---------------------------------------------------------------------------

def sample_random_config(
    rng: np.random.Generator,
    n_mods: int = 8,
    max_path_depth: int = 6,
    landmark_bias: float = 0.7,
    landmark_jitter_std: float = 0.15,
    depth_range: tuple[float, float] = (0.1, 6.0),
    f_carrier: float = 220.0,
) -> Config:
    """Sample a configuration biased toward the structurally meaningful
    sub-region of the search space.

    - Number of enabled modulators uniform in [2, n_mods].
    - For each enabled modulator:
        * Path length uniform in [1, max_path_depth].
        * Path characters uniform L / R.
        * Depth: `landmark_bias` chance of being drawn near a Bessel
          landmark (Gaussian jitter, std `landmark_jitter_std`);
          otherwise uniform over `depth_range`.
    """
    n_enabled = int(rng.integers(2, n_mods + 1))
    enabled_idx = set(rng.choice(n_mods, size=n_enabled, replace=False).tolist())

    mods: list[ModConfig] = []
    for i in range(n_mods):
        if i not in enabled_idx:
            mods.append(ModConfig(path="", depth=0.0, enabled=False))
            continue
        path_len = int(rng.integers(1, max_path_depth + 1))
        path = "".join("L" if rng.random() < 0.5 else "R" for _ in range(path_len))

        if rng.random() < landmark_bias:
            landmark = float(rng.choice(BESSEL_LANDMARKS))
            depth = max(0.0, landmark + float(rng.standard_normal()) * landmark_jitter_std)
        else:
            depth = float(rng.uniform(*depth_range))
        depth = max(0.0, min(8.0, depth))

        mods.append(ModConfig(path=path, depth=depth, enabled=True))

    return Config(mods=mods, f_carrier=f_carrier)


# ---------------------------------------------------------------------------
# Phase 2 — coordinate descent
# ---------------------------------------------------------------------------

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
) -> tuple[Config, float]:
    """Refine `cfg` against `family` until no single-parameter move
    decreases the objective, or the time budget runs out.

    Each iteration walks every enabled modulator and tries:
      - Stern-Brocot L / R / parent path moves
      - depth ± each step in `depth_steps` (coarse → fine)

    Accepts the first improvement found (greedy descent). Re-runs from
    the start of the parameter list after accepting, so the search can
    chain dependent improvements.
    """
    start = time.time()
    current = cfg.copy()
    sp = current.predict(threshold_db=threshold_db, eps=eps)
    current_d = family_distance(sp, family, current.f_carrier, q_max=q_max)

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
                if len(new_path) > 16:  # cap path depth at 16 (Fibonacci-safe int32)
                    continue
                trial = current.copy()
                trial.mods[i].path = new_path
                d_t = family_distance(
                    trial.predict(threshold_db=threshold_db, eps=eps),
                    family, trial.f_carrier, q_max=q_max,
                )
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
                    if new_depth < 0.0 or new_depth > 8.0:
                        continue
                    trial = current.copy()
                    trial.mods[i].depth = new_depth
                    d_t = family_distance(
                        trial.predict(threshold_db=threshold_db, eps=eps),
                        family, trial.f_carrier, q_max=q_max,
                    )
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
# Two-phase search
# ---------------------------------------------------------------------------

@dataclass
class SearchResult:
    config: Config
    objective: float       # final family_distance value
    family: str            # family name
    seed: int
    phase1_objective: float
    iterations_phase2: int = 0


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
) -> list[SearchResult]:
    """Run the full two-phase search and return the top-K refined
    configurations sorted by objective (lowest first).

    `threshold_db_phase1` is intentionally looser than phase 2 — the
    coarse ranking in Phase 1 doesn't need the same numerical precision
    that the refinement step does, and the looser threshold makes the
    1000-evaluation pass run faster.
    """
    rng = np.random.default_rng(seed)

    # --- Phase 1 ---
    phase1: list[tuple[Config, float]] = []
    for _ in range(n_phase1):
        cfg = sample_random_config(rng, f_carrier=f_carrier)
        d = family_distance(
            cfg.predict(
                threshold_db=threshold_db_phase1,
                eps=eps_phase1,
                max_partials=max_partials_phase1,
            ),
            family, f_carrier, q_max=q_max,
        )
        phase1.append((cfg, d))

    phase1.sort(key=lambda x: x[1])
    top = phase1[:k_top]

    # --- Phase 2 ---
    results: list[SearchResult] = []
    for cfg, p1_d in top:
        refined, final_d = coordinate_descent(
            cfg, family,
            time_budget_s=time_budget_per_candidate_s,
            q_max=q_max,
            threshold_db=threshold_db_phase2,
            eps=eps_phase2,
        )
        results.append(SearchResult(
            config=refined,
            objective=final_d,
            family=family.name,
            seed=seed,
            phase1_objective=p1_d,
        ))

    results.sort(key=lambda r: r.objective)
    return results
