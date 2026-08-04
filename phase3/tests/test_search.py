"""Tests for the two-phase search.

The big functional gate: searching for a target family produces
configurations whose family_distance to that target is meaningfully
lower than the median random config's. (Search > random.)
"""

from __future__ import annotations

import time

import numpy as np
import pytest

from phase3.config import BESSEL_LANDMARKS, Config, ModConfig, path_ratio, walk_path
from phase3.families import (
    family_organ,
    family_prime,
    family_sawtooth,
    family_square,
    family_triangle,
)
from phase3.family_distance import family_distance
from phase3.search import (
    _metropolis_accept,
    basin_hop,
    coordinate_descent,
    perturb,
    sample_random_config,
    two_phase_search,
)


# ---------------------------------------------------------------------------
# walk_path / path_ratio
# ---------------------------------------------------------------------------

def test_walk_path_empty_is_one_one():
    assert walk_path("") == (1, 1)


def test_walk_path_known_cases():
    assert walk_path("R") == (2, 1)
    assert walk_path("L") == (1, 2)
    assert walk_path("RLL") == (4, 3)
    assert walk_path("RLLLR") == (9, 7)


def test_path_ratio_consistent():
    assert path_ratio("RLLLR") == pytest.approx(9 / 7)


def test_walk_path_rejects_invalid_chars():
    with pytest.raises(ValueError):
        walk_path("RXL")


# ---------------------------------------------------------------------------
# Sampling
# ---------------------------------------------------------------------------

def test_sample_produces_valid_config():
    rng = np.random.default_rng(42)
    cfg = sample_random_config(rng)
    assert len(cfg.mods) == 64
    enabled_count = sum(1 for m in cfg.mods if m.enabled)
    # P3.7 sampler floors at 4 enabled; family-search sampler caps
    # max_enabled at 8 by default (high op counts use sample_exact_op_count).
    assert 4 <= enabled_count <= 8

    for m in cfg.mods:
        if m.enabled:
            # P3.7 raised max_path_depth to 16. The empty path (len 0,
            # ratio 1/1, the harmonic fundamental) is now sampleable too.
            assert 0 <= len(m.path) <= 16
            assert all(c in "LR" for c in m.path)
            # P3.7 floors enabled depth at MIN_DEPTH (0.04).
            from phase3.config import MIN_DEPTH
            assert MIN_DEPTH <= m.depth <= 8.0
        else:
            assert m.depth == 0.0


def test_sample_landmark_bias_clusters_near_landmarks():
    """With landmark_bias=1.0 and zero jitter, every depth should sit on
    a landmark exactly."""
    rng = np.random.default_rng(7)
    cfg = sample_random_config(
        rng, landmark_bias=1.0, landmark_jitter_std=0.0,
    )
    for m in cfg.mods:
        if m.enabled:
            assert any(abs(m.depth - lm) < 1e-6 for lm in BESSEL_LANDMARKS), \
                f"depth {m.depth} not at any landmark"


def test_sample_seed_is_reproducible():
    rng1 = np.random.default_rng(123)
    rng2 = np.random.default_rng(123)
    a = sample_random_config(rng1)
    b = sample_random_config(rng2)
    assert [(m.path, m.depth, m.enabled) for m in a.mods] == \
           [(m.path, m.depth, m.enabled) for m in b.mods]


# ---------------------------------------------------------------------------
# Coordinate descent
# ---------------------------------------------------------------------------

def test_coordinate_descent_never_increases_objective():
    rng = np.random.default_rng(99)
    cfg = sample_random_config(rng)
    fam = family_sawtooth()

    d_before = family_distance(cfg.predict(), fam, cfg.f_carrier, q_max=16)
    refined, d_after = coordinate_descent(
        cfg, fam, max_iterations=20, time_budget_s=3.0,
    )
    assert d_after <= d_before + 1e-7


def test_coordinate_descent_finds_some_improvement():
    """A randomly sampled config usually has *some* one-step move that
    reduces distance to a target family — coordinate descent should
    find at least one such move on most random starting points."""
    rng = np.random.default_rng(31)
    fam = family_sawtooth()

    # 5 trials, expect at least 3 to show improvement.
    improvements = 0
    for _ in range(5):
        cfg = sample_random_config(rng)
        d0 = family_distance(cfg.predict(), fam, cfg.f_carrier, q_max=16)
        _, d1 = coordinate_descent(cfg, fam, time_budget_s=3.0)
        if d1 < d0 - 1e-5:
            improvements += 1
    assert improvements >= 3, f"only {improvements}/5 trials improved"


def test_coordinate_descent_respects_time_budget():
    rng = np.random.default_rng(11)
    cfg = sample_random_config(rng)
    fam = family_sawtooth()

    start = time.time()
    coordinate_descent(cfg, fam, time_budget_s=0.5, max_iterations=10000)
    elapsed = time.time() - start
    # Allow a small fudge factor for the last in-flight iteration.
    assert elapsed < 1.5, f"budget exceeded: {elapsed}s"


# ---------------------------------------------------------------------------
# Two-phase search end-to-end
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("family_factory,family_name", [
    (family_sawtooth, "sawtooth"),
    (family_square, "square"),
    (family_triangle, "triangle"),
])
def test_search_beats_random_baseline(family_factory, family_name):
    """The top search result should clearly beat the median random
    sample for the same family target. Small budgets to keep tests
    fast — n_phase1 and time per candidate are tightened."""
    fam = family_factory()
    results = two_phase_search(
        fam,
        n_phase1=200,
        k_top=10,
        seed=314,
        time_budget_per_candidate_s=1.5,
    )

    assert len(results) == 10
    # Sorted ascending by objective
    objs = [r.objective for r in results]
    assert objs == sorted(objs)

    # Verify search-best is better than its own phase1 starting point.
    best = results[0]
    assert best.objective <= best.phase1_objective + 1e-7

    # And better than a random fresh sample's distance.
    rng = np.random.default_rng(0)
    random_d = []
    for _ in range(30):
        cfg = sample_random_config(rng)
        random_d.append(family_distance(cfg.predict(), fam, cfg.f_carrier, q_max=16))
    median_random = float(np.median(random_d))
    assert best.objective < median_random, (
        f"search best {best.objective:.4f} not better than median random "
        f"{median_random:.4f} for {family_name}"
    )


# ---------------------------------------------------------------------------
# P3.7 — canonicalisation, dedup, criticality
# ---------------------------------------------------------------------------

def test_canonicalised_puts_enabled_first_and_sorts():
    """Slot order is musically meaningless. canonicalised() should pack
    enabled mods into leading slots and sort them deterministically."""
    cfg = Config(
        mods=[
            ModConfig(path="",    depth=0.0, enabled=False),
            ModConfig(path="RR",  depth=1.5, enabled=True),
            ModConfig(path="L",   depth=2.0, enabled=True),
            ModConfig(path="",    depth=0.0, enabled=False),
            ModConfig(path="RLL", depth=0.5, enabled=True),
            ModConfig(path="",    depth=0.0, enabled=False),
            ModConfig(path="",    depth=0.0, enabled=False),
            ModConfig(path="",    depth=0.0, enabled=False),
        ],
        f_carrier=220.0,
    )
    c = cfg.canonicalised()
    enabled = [m for m in c.mods if m.enabled]
    assert len(enabled) == 3
    # Sorted by (path, depth) ascending.
    paths = [m.path for m in enabled]
    assert paths == sorted(paths)
    # Disabled mods fill the tail.
    assert all(not m.enabled for m in c.mods[3:])


def test_fingerprint_is_permutation_invariant():
    """Two configs that differ only by slot assignment have the same
    fingerprint — that's the whole point of the dedup pass."""
    a = Config(
        mods=[
            ModConfig(path="RR", depth=1.5, enabled=True),
            ModConfig(path="L",  depth=2.0, enabled=True),
        ] + [ModConfig() for _ in range(6)],
        f_carrier=220.0,
    )
    b = Config(
        mods=[
            ModConfig(path="L",  depth=2.0, enabled=True),
            ModConfig(path="RR", depth=1.5, enabled=True),
        ] + [ModConfig() for _ in range(6)],
        f_carrier=220.0,
    )
    assert a.fingerprint() == b.fingerprint()


def test_fingerprint_differs_for_different_patches():
    a = Config(
        mods=[
            ModConfig(path="R", depth=1.5, enabled=True),
            ModConfig(path="L", depth=2.0, enabled=True),
        ] + [ModConfig() for _ in range(6)],
        f_carrier=220.0,
    )
    b = Config(
        mods=[
            ModConfig(path="RR", depth=1.5, enabled=True),
            ModConfig(path="L",  depth=2.0, enabled=True),
        ] + [ModConfig() for _ in range(6)],
        f_carrier=220.0,
    )
    assert a.fingerprint() != b.fingerprint()


def test_two_phase_search_returns_unique_fingerprints():
    """The top-K results from a real search must all have distinct
    canonical fingerprints — no permutation-equivalent duplicates."""
    fam = family_sawtooth()
    results = two_phase_search(
        fam, n_phase1=200, k_top=10, seed=99,
        time_budget_per_candidate_s=1.0,
    )
    fps = [r.config.fingerprint() for r in results]
    assert len(set(fps)) == len(fps), \
        f"duplicate fingerprints in results: {len(fps)} total, {len(set(fps))} unique"


def test_search_respects_min_enabled_floor():
    fam = family_sawtooth()
    results = two_phase_search(
        fam, n_phase1=100, k_top=5, seed=42,
        time_budget_per_candidate_s=1.0,
        min_enabled=4,
    )
    for r in results:
        n_on = sum(1 for m in r.config.mods if m.enabled)
        assert n_on >= 4, f"only {n_on} enabled ops in result"


def test_search_enforces_min_depth():
    """Every enabled op in every search result should have depth ≥
    MIN_DEPTH — the floor is what prevents inert-but-enabled slots."""
    from phase3.config import MIN_DEPTH
    fam = family_sawtooth()
    results = two_phase_search(
        fam, n_phase1=100, k_top=5, seed=42,
        time_budget_per_candidate_s=1.0,
    )
    for r in results:
        for m in r.config.mods:
            if m.enabled:
                assert m.depth >= MIN_DEPTH - 1e-9, \
                    f"depth {m.depth} below MIN_DEPTH={MIN_DEPTH}"


def test_criticality_score_nonzero_for_typical_config():
    """A typical FM config sits somewhere on the spectral manifold;
    criticality should be > 0 (any perturbation moves the spectrum
    at least a little)."""
    from phase3.family_distance import criticality_score
    rng = np.random.default_rng(2)
    cfg = sample_random_config(rng)
    cs = criticality_score(cfg, family_sawtooth())
    assert cs > 0.0


def test_criticality_score_higher_near_bessel_zero():
    """A config sitting exactly on a Bessel zero (J_1 zero at 3.832
    on the n=1 sideband for ratio=1) should have higher criticality
    than the same config with depth nudged into the middle of a
    monotonic stretch."""
    from phase3.family_distance import criticality_score
    near_zero = Config(
        mods=[ModConfig(path="", depth=3.832, enabled=True)]
             + [ModConfig() for _ in range(7)],
    )
    plateau = Config(
        mods=[ModConfig(path="", depth=2.0, enabled=True)]
             + [ModConfig() for _ in range(7)],
    )
    # Pad to 4 enabled to mirror the production constraint.
    for c in (near_zero, plateau):
        c.mods[1] = ModConfig(path="R",   depth=1.0, enabled=True)
        c.mods[2] = ModConfig(path="L",   depth=1.0, enabled=True)
        c.mods[3] = ModConfig(path="RR",  depth=1.0, enabled=True)
    fam = family_sawtooth()
    cs_zero = criticality_score(near_zero, fam)
    cs_plat = criticality_score(plateau, fam)
    assert cs_zero > cs_plat, (
        f"expected near-Bessel-zero ({cs_zero}) more critical than "
        f"plateau ({cs_plat})"
    )


def test_composite_objective_with_zero_lambda_matches_family_distance():
    """λ_crit = 0 should make composite_objective indistinguishable
    from family_distance — same speed, same result."""
    from phase3.family_distance import composite_objective, family_distance
    rng = np.random.default_rng(5)
    cfg = sample_random_config(rng)
    fam = family_sawtooth()
    obj, fd, cs = composite_objective(
        cfg, fam, f_carrier=cfg.f_carrier, lambda_crit=0.0,
    )
    fd_direct = family_distance(cfg.predict(), fam, cfg.f_carrier, q_max=16)
    assert obj == pytest.approx(fd_direct, abs=1e-9)
    assert fd == pytest.approx(fd_direct, abs=1e-9)
    assert cs == 0.0


def test_search_for_sawtooth_lands_closer_to_sawtooth_than_square():
    """A sawtooth-search result should be closer to sawtooth than to
    square — same discrimination property as the per-target spectrum
    test, applied to a search-produced config."""
    saw = family_sawtooth()
    sq = family_square()

    results = two_phase_search(
        saw, n_phase1=200, k_top=5, seed=271,
        time_budget_per_candidate_s=1.5,
    )
    best = results[0]
    sp = best.config.predict()
    d_saw = family_distance(sp, saw, best.config.f_carrier, q_max=16)
    d_sq = family_distance(sp, sq, best.config.f_carrier, q_max=16)
    assert d_saw < d_sq, f"sawtooth-search produced d_saw={d_saw:.4f} > d_sq={d_sq:.4f}"


# ---------------------------------------------------------------------------
# S10/S11 — basin hopping
# ---------------------------------------------------------------------------

def test_perturb_respects_invariants_over_500_draws():
    """Property test: over 500 seeded draws, every perturbed config
    keeps the P3.7 invariants — min_enabled floor, MIN_DEPTH ≤ depth ≤
    8, path length ≤ max_path_depth, L/R-only paths, zeroed disabled
    slots — and the input config is never mutated."""
    from phase3.config import MIN_DEPTH
    rng = np.random.default_rng(4242)
    for _ in range(500):
        base = sample_random_config(rng)
        snapshot = [(m.path, m.depth, m.enabled) for m in base.mods]

        kicked = perturb(base, rng)

        assert [(m.path, m.depth, m.enabled) for m in base.mods] == snapshot, \
            "perturb mutated its input"
        n_on = sum(1 for m in kicked.mods if m.enabled)
        assert n_on >= 4, f"perturb dropped enabled count to {n_on}"
        for m in kicked.mods:
            if m.enabled:
                assert MIN_DEPTH - 1e-9 <= m.depth <= 8.0 + 1e-9, \
                    f"depth {m.depth} outside [MIN_DEPTH, 8]"
                assert len(m.path) <= 16, f"path too deep: {m.path!r}"
                assert all(c in "LR" for c in m.path), f"bad path {m.path!r}"
            else:
                assert m.depth == 0.0


def test_metropolis_acceptance_logic():
    """Better always accepted; worse never at T=0; worse at ~exp(-Δ/T)
    otherwise (statistical tolerance)."""
    rng = np.random.default_rng(0)
    # Improvements (Δ ≤ 0) are always accepted.
    assert all(_metropolis_accept(-0.1, 0.02, rng) for _ in range(100))
    assert _metropolis_accept(0.0, 0.02, rng)
    # T = 0 degenerates to strict descent.
    assert not any(_metropolis_accept(0.01, 0.0, rng) for _ in range(100))
    # Δ = T → acceptance rate ≈ e⁻¹ ≈ 0.368.
    n = 4000
    rate = sum(_metropolis_accept(0.02, 0.02, rng) for _ in range(n)) / n
    assert 0.32 < rate < 0.42, f"Metropolis rate {rate:.3f} far from e^-1"


def test_basin_hop_tracks_global_best_with_stubbed_descent(monkeypatch):
    """With a stubbed (scripted) descent, basin_hop must return the
    minimum score ever seen — the global best is tracked separately
    from the (here always-accepting, T huge) walker, so the running
    best is monotone non-increasing across hops."""
    import phase3.search as search_mod
    scores = [5.0, 3.0, 4.5, 2.0, 6.0, 1.5, 8.0]  # first = initial descent
    it = iter(scores)
    monkeypatch.setattr(
        search_mod, "coordinate_descent",
        lambda cfg, family, **kw: (cfg, next(it)),
    )
    cfg = sample_random_config(np.random.default_rng(1))
    _, best_d = basin_hop(
        cfg, family_sawtooth(), n_hops=len(scores) - 1, seed=2,
        temperature=1e9,
    )
    assert best_d == min(scores)


def test_basin_hop_cold_walker_keeps_initial_best(monkeypatch):
    """T=0: strictly-worse candidates are never accepted and never
    become the best — the returned score is the initial descent's."""
    import phase3.search as search_mod
    it = iter([5.0, 6.0, 7.0, 8.0])
    monkeypatch.setattr(
        search_mod, "coordinate_descent",
        lambda cfg, family, **kw: (cfg, next(it)),
    )
    cfg = sample_random_config(np.random.default_rng(1))
    _, best_d = basin_hop(
        cfg, family_sawtooth(), n_hops=3, seed=2, temperature=0.0,
    )
    assert best_d == 5.0


def test_basin_hop_deterministic_given_seed():
    """Same seed → identical result fingerprint and objective. The
    descent is iteration-bounded (max_iterations, generous wall clock)
    so the only stochastic element is the seeded rng."""
    cfg = sample_random_config(np.random.default_rng(8))
    fam = family_sawtooth()
    kwargs = dict(n_hops=3, seed=17, cd_budget_s=30.0, max_iterations=3)
    a, d_a = basin_hop(cfg, fam, **kwargs)
    b, d_b = basin_hop(cfg, fam, **kwargs)
    assert a.fingerprint() == b.fingerprint()
    assert d_a == d_b


def test_n_basin_hops_zero_matches_default_behaviour(monkeypatch):
    """Regression pin (the revert switch): n_basin_hops=0 must leave
    two_phase_search identical to the pre-S11 pipeline — same results
    as not passing the knob at all, for the same seed.

    The descent is iteration-bounded (real coordinate_descent, wrapped
    to kill the wall-clock cutoff) so run-to-run comparison is exact —
    the raw time budget truncates at nondeterministic points."""
    import phase3.search as search_mod
    real_cd = search_mod.coordinate_descent

    def bounded_cd(cfg, family, **kw):
        kw["max_iterations"] = 8
        kw["time_budget_s"] = 1e9
        return real_cd(cfg, family, **kw)

    monkeypatch.setattr(search_mod, "coordinate_descent", bounded_cd)

    fam = family_square()
    kwargs = dict(n_phase1=100, k_top=3, seed=777)
    base = two_phase_search(fam, **kwargs)
    pinned = two_phase_search(fam, n_basin_hops=0, **kwargs)
    assert [r.config.fingerprint() for r in base] == \
           [r.config.fingerprint() for r in pinned]
    assert [r.objective for r in base] == \
           pytest.approx([r.objective for r in pinned])


def test_basin_hop_matches_or_beats_plain_descent_on_synthetic_target():
    """End-to-end (slow-ish, a few seconds): a 2-op synthetic target
    where plain descent from a deliberately-off start plateaus in a
    local optimum. basin_hop from the same start must reach an
    objective ≤ the plain-descent result (fixed seed)."""
    from phase3.families import family_from_target
    from phase3.family_distance import quantize_to_integer_grid

    target = Config(
        mods=[
            ModConfig(path="R",  depth=2.405, enabled=True),
            ModConfig(path="RL", depth=1.841, enabled=True),
        ] + [ModConfig() for _ in range(6)],
        f_carrier=220.0,
    )
    amps = quantize_to_integer_grid(target.predict(), 220.0, 16)
    fam = family_from_target(amps, name="synthetic_2op", f_carrier=220.0)

    # Deliberately-off start: wrong first path, both depths well below
    # their landmarks.
    start = Config(
        mods=[
            ModConfig(path="RR", depth=0.7, enabled=True),
            ModConfig(path="L",  depth=0.7, enabled=True),
        ] + [ModConfig() for _ in range(6)],
        f_carrier=220.0,
    )

    _, d_plain = coordinate_descent(
        start, fam, time_budget_s=2.0, min_enabled=2,
    )
    _, d_hop = basin_hop(
        start, fam, n_hops=8, seed=3, cd_budget_s=1.0, min_enabled=2,
    )
    assert d_hop <= d_plain + 1e-7, (
        f"basin_hop ({d_hop:.5f}) worse than plain descent ({d_plain:.5f})"
    )
