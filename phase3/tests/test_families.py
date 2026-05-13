"""Tests for the 12-family definitions."""

from __future__ import annotations

import numpy as np
import pytest

from phase3.families import (
    DEFAULT_PRIMES,
    Family,
    default_families,
    family_bell_stretched,
    family_organ,
    family_prime,
    family_pulse_pw,
    family_sawtooth,
    family_square,
    family_triangle,
)


def test_default_families_has_12():
    fams = default_families()
    assert len(fams) == 12
    names = [f.name for f in fams]
    # Tail must include all six prime families.
    assert all(f"prime_{p}" in names for p in DEFAULT_PRIMES)


def test_sawtooth_target_is_1_over_n():
    f = family_sawtooth()
    a = f.target_amplitudes(8)
    np.testing.assert_allclose(a, 1.0 / np.arange(1, 9))
    assert f.characteristic_q == 1


def test_square_target_is_odd_only_1_over_n():
    f = family_square()
    a = f.target_amplitudes(8)
    # Positions 1, 3, 5, 7 (i.e. indices 0, 2, 4, 6) should be 1/n; others 0.
    expected = np.zeros(8)
    for n in (1, 3, 5, 7):
        expected[n - 1] = 1.0 / n
    np.testing.assert_allclose(a, expected)
    assert f.characteristic_q == 2


def test_triangle_target_is_odd_only_1_over_n_squared():
    f = family_triangle()
    a = f.target_amplitudes(8)
    expected = np.zeros(8)
    for n in (1, 3, 5, 7):
        expected[n - 1] = 1.0 / (n * n)
    np.testing.assert_allclose(a, expected)
    assert f.characteristic_q == 2


def test_pulse_pw_has_nulls_at_multiples_of_N():
    f = family_pulse_pw(4)
    a = f.target_amplitudes(12)
    # Positions 4, 8, 12 should be null.
    for n in (4, 8, 12):
        assert a[n - 1] == 0.0
    # Non-multiple positions should be non-zero.
    for n in (1, 2, 3, 5, 6, 7, 9, 10, 11):
        assert a[n - 1] > 0.0
    assert f.characteristic_q == 4


def test_pulse_pw_rejects_invalid_N():
    with pytest.raises(ValueError):
        family_pulse_pw(1)


def test_organ_only_has_stops():
    f = family_organ((1, 3, 5))
    a = f.target_amplitudes(8)
    for n in (1, 3, 5):
        assert a[n - 1] == pytest.approx(1.0 / n)
    for n in (2, 4, 6, 7, 8):
        assert a[n - 1] == 0.0
    # GCD of differences (3-1)=2 and (5-3)=2 is 2.
    assert f.characteristic_q == 2


def test_organ_octave_stops_have_char_q_1():
    f = family_organ((1, 2, 4))
    # GCD of (2-1)=1 and (4-2)=2 is 1.
    assert f.characteristic_q == 1


def test_bell_stretched_has_no_characteristic_q():
    f = family_bell_stretched(1.05)
    assert f.characteristic_q is None
    a = f.target_amplitudes(16)
    # Should have non-zero amplitude near each n^1.05 position.
    assert (a > 0).any()
    # Fundamental position is at 1^1.05 = 1, so a[0] should be substantial.
    assert a[0] > 0.5


def test_bell_stretched_rejects_beta_le_1():
    with pytest.raises(ValueError):
        family_bell_stretched(0.99)


def test_prime_p_lattice():
    f = family_prime(7)
    a = f.target_amplitudes(21)
    # Position 1 (fundamental) and multiples of 7 are non-zero.
    assert a[0] == 1.0          # carrier-fundamental
    assert a[6] == pytest.approx(1.0 / 7)
    assert a[13] == pytest.approx(1.0 / 14)
    assert a[20] == pytest.approx(1.0 / 21)
    # Non-multiples-of-7 (other than position 1) are zero.
    for n in (2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13):
        assert a[n - 1] == 0.0
    assert f.characteristic_q == 7
    assert f.params == {"p": 7}


def test_prime_p_rejects_p_lt_2():
    with pytest.raises(ValueError):
        family_prime(1)


def test_all_families_produce_finite_non_negative_targets():
    for f in default_families():
        a = f.target_amplitudes(20)
        assert np.all(np.isfinite(a)), f"{f.name} target has NaN/inf"
        assert np.all(a >= 0), f"{f.name} target has negative weights"
        # At least one position must be populated.
        assert (a > 0).any(), f"{f.name} target is all zeros"
