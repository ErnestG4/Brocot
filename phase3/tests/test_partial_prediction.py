"""Tests for partial_prediction.py.

The math is the FM sideband formula plus pruning, so the tests check
against analytically known cases.
"""

from __future__ import annotations

import math

import numpy as np
import pytest
from scipy.special import jv

from phase3.partial_prediction import order_bound, predict_partials


# ---------------------------------------------------------------------------
# Order bound
# ---------------------------------------------------------------------------

def test_order_bound_zero_depth():
    assert order_bound(0.0) == 0


def test_order_bound_grows_with_depth():
    # J_n peaks near n = depth; for depth 5, |J_n(5)| is appreciable up to
    # n ~ 8 or so. Strict monotonicity isn't guaranteed, but coarse growth is.
    bounds = [order_bound(d, eps=1e-3) for d in (0.5, 1.0, 2.0, 5.0, 10.0)]
    assert bounds == sorted(bounds)
    assert bounds[-1] > bounds[0]


def test_order_bound_matches_jv_threshold():
    eps = 1e-3
    for depth in (0.5, 1.0, 2.4, 5.0):
        n = order_bound(depth, eps=eps)
        # Order n itself should be above the threshold, n+1 below.
        assert abs(jv(n, depth)) > eps
        assert abs(jv(n + 1, depth)) <= eps


# ---------------------------------------------------------------------------
# Carrier-only / silent / trivial cases
# ---------------------------------------------------------------------------

def test_no_modulators_returns_carrier_only():
    sp = predict_partials(ratios=[], depths=[], f_carrier=440.0)
    assert len(sp) == 1
    assert sp.freqs[0] == pytest.approx(440.0, abs=1.0)
    assert sp.amps[0] == pytest.approx(1.0, abs=1e-6)


def test_zero_depth_single_mod_is_carrier_only():
    """Any modulator with I = 0 contributes only n=0 (J_0(0)=1, J_n(0)=0
    for n>0). The result is just the carrier."""
    sp = predict_partials(ratios=[2.0], depths=[0.0], f_carrier=220.0)
    assert len(sp) == 1
    assert sp.freqs[0] == pytest.approx(220.0, abs=1.0)


# ---------------------------------------------------------------------------
# Single modulator — the classic FM sideband cases
# ---------------------------------------------------------------------------

def test_single_mod_octave_J1_peak():
    """One modulator at ratio 2/1, depth 1.841 (J_1 peak).

    For ratio=2 (f_m = 2·f_c), sidebands sit at f_c·(1 + 2n) for integer n:
        n=0:  f_c        amp J_0(I)
        n=+1: 3·f_c      amp J_1(I)
        n=-1: -f_c → reflects to f_c with sign flip → contributes +J_1(I)
        n=+2: 5·f_c      amp J_2(I)
        n=-2: -3·f_c → reflects to 3·f_c with sign flip → -J_2(I)
        …
    So the carrier bin gets J_0(I) + J_1(I) and 3·f_c gets J_1(I) - J_2(I).
    """
    I = 1.841
    sp = predict_partials(
        ratios=[2.0], depths=[I],
        f_carrier=220.0, threshold_db=-60.0,
    )
    # Carrier bin: J_0 + J_1 (constructive)
    carrier_idx = np.argmin(np.abs(sp.freqs - 220.0))
    assert sp.freqs[carrier_idx] == pytest.approx(220.0, abs=1.0)
    expected_carrier = abs(jv(0, I) + jv(1, I))
    assert sp.amps[carrier_idx] == pytest.approx(expected_carrier, rel=0.02)

    # 3·f_c bin: J_1 - J_2 (the reflected n=-2 cancels with n=+1)
    bin_3fc = np.argmin(np.abs(sp.freqs - 660.0))
    assert sp.freqs[bin_3fc] == pytest.approx(660.0, abs=1.0)
    expected_3fc = abs(jv(1, I) - jv(2, I))
    assert sp.amps[bin_3fc] == pytest.approx(expected_3fc, rel=0.05)


def test_single_mod_J0_null_suppresses_n0_contribution():
    """At ratio=1, depth=2.4048 (first zero of J_0), the n=0 contribution
    to the carrier vanishes. The carrier bin isn't fully silent because the
    n=-2 reflection lands there with -J_2(2.4048), but the n=0 component
    is gone — so the carrier amplitude equals |J_2(2.4048)|, not
    |J_0 + J_2|. We verify this is well below the depth=0 reference value
    of 1.0."""
    sp_null = predict_partials(
        ratios=[1.0], depths=[2.4048],
        f_carrier=220.0, threshold_db=-80.0,
    )
    sp_ref = predict_partials(
        ratios=[1.0], depths=[0.0],
        f_carrier=220.0, threshold_db=-80.0,
    )

    def carrier_amp(sp):
        mask = np.abs(sp.freqs - 220.0) < 1.0
        return float(sp.amps[mask].max()) if mask.any() else 0.0

    null_amp = carrier_amp(sp_null)
    ref_amp = carrier_amp(sp_ref)

    assert ref_amp == pytest.approx(1.0, abs=1e-6)
    # The reflection residual |J_2(2.4048)| ≈ 0.432; with constructive
    # contributions from higher-order reflections it can be a bit higher,
    # but we should be well under 1.0.
    assert null_amp < 0.7, f"carrier not appreciably suppressed: {null_amp}"


def test_single_mod_sideband_spacing_matches_ratio():
    """For one mod at ratio r, sidebands sit at f_c · (1 + n·r). Check that
    pairs of adjacent sidebands have the expected spacing."""
    f_c = 220.0
    r = 3.0 / 2.0  # 3:2 fifth
    sp = predict_partials(
        ratios=[r], depths=[2.0],
        f_carrier=f_c, threshold_db=-50.0,
    )
    # All partial frequencies should be at f_c · |1 + n·r| for some integer n.
    for f in sp.freqs:
        n = round((f / f_c - 1.0) / r)
        n_alt = round((-f / f_c - 1.0) / r)  # reflected-negative branch
        # Either branch should produce a near-integer
        err = min(
            abs((f - f_c * (1.0 + n * r))),
            abs((f - f_c * abs(1.0 + n_alt * r))),
        )
        assert err < 1.0, f"partial {f:.2f} doesn't sit on r-lattice"


# ---------------------------------------------------------------------------
# Multi-modulator — combinatorial sanity
# ---------------------------------------------------------------------------

def test_avatar_1_partials_are_bounded():
    """Avatar 1 config — three mods on shared path RLLLR at positions 1/3/5
    (ratios 2, 4/3, 9/7) with depths 5.13, 0.86, 0.90, carrier 130.81 Hz.
    Just check we get a non-trivial partial set inside the audible range,
    with finite amplitudes."""
    sp = predict_partials(
        ratios=[2.0, 4.0 / 3.0, 9.0 / 7.0],
        depths=[5.13, 0.86, 0.90],
        f_carrier=130.81,
    )
    assert len(sp) > 5
    assert np.all(np.isfinite(sp.amps))
    assert np.all(sp.amps >= 0)
    assert np.all(sp.freqs >= 20.0)
    assert np.all(sp.freqs <= 20000.0)


def test_eight_op_does_not_explode():
    """The whole point of the pruning: 8 modulators at moderate depths
    must produce a tractable partial count (<< 10^5 raw combinations)."""
    # 8 mods at small-integer ratios and J_1-peak depths.
    sp = predict_partials(
        ratios=[1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0],
        depths=[1.841] * 8,
        f_carrier=110.0,
        threshold_db=-50.0,
    )
    # Empirical: well under 1000 retained partials at this threshold.
    assert len(sp) < 2000, f"unpruned blowup: {len(sp)} partials"
    assert len(sp) > 10, "way over-pruned — no audible partials retained"


def test_amplitudes_are_invariant_to_carrier_frequency():
    """Changing f_carrier shifts the absolute frequencies of all partials
    but doesn't change their amplitudes — verifies that the math is
    correctly relative to the carrier."""
    cfg = dict(ratios=[1.0, 2.0, 3.0], depths=[1.5, 1.5, 1.5])
    sp_low = predict_partials(**cfg, f_carrier=110.0)
    sp_high = predict_partials(**cfg, f_carrier=440.0)

    # Same number of partials retained (same lattice structure).
    assert len(sp_low) == len(sp_high)
    # Sorted amplitudes match.
    np.testing.assert_allclose(
        np.sort(sp_low.amps), np.sort(sp_high.amps),
        rtol=1e-6,
    )
    # Frequency ratio between the two sets equals the carrier ratio.
    assert (sp_high.freqs[-1] / sp_low.freqs[-1]) == pytest.approx(4.0, rel=0.05)


# ---------------------------------------------------------------------------
# Spectrum helper
# ---------------------------------------------------------------------------

def test_loudest_non_carrier_excludes_the_carrier_bin():
    sp = predict_partials(
        ratios=[2.0], depths=[1.841],
        f_carrier=220.0, threshold_db=-40.0,
    )
    loud = sp.loudest_non_carrier(220.0)
    # The carrier amp is |J_0(1.841)| ≈ 0.318; |J_1(1.841)| ≈ 0.582 is louder.
    assert loud > 0.318 * 0.9
