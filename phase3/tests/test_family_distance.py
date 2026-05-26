"""Tests for the family-distance metric.

The falsification discipline from RESULTS §7.ter.19 (carried over from
the ARS toolkit) requires that *before* the metric is trusted on real
configurations, it produces sensible results on synthesised inputs whose
family membership is known a priori:

- A sawtooth-amplitude input should produce d ≈ 0 against the sawtooth
  family.
- A sawtooth-amplitude input perturbed by Gaussian noise should produce
  d > 0 scaling with noise magnitude.
- A sawtooth-amplitude input should produce LARGER d against the square
  family than against the sawtooth family.
- The prime-p aggregation should peak at the correct p when fed a
  prime-p amplitude vector.

These are the gates this test file enforces.
"""

from __future__ import annotations

import numpy as np
import pytest

from phase3.families import (
    family_bell_stretched,
    family_prime,
    family_sawtooth,
    family_square,
    family_triangle,
)
from phase3.family_distance import (
    _kl_divergence,
    family_distance,
    padic_aggregate,
    quantize_to_integer_grid,
    rf_amplitude_vector,
)
from phase3.partial_prediction import Spectrum


# ---------------------------------------------------------------------------
# Quantisation helper
# ---------------------------------------------------------------------------

def test_quantize_to_integer_grid_simple():
    sp = Spectrum(
        freqs=np.array([100.0, 200.0, 300.0]),
        amps=np.array([1.0, 0.5, 0.25]),
    )
    a = quantize_to_integer_grid(sp, f_carrier=100.0, q_max=8)
    assert a[0] == pytest.approx(1.0)
    assert a[1] == pytest.approx(0.5)
    assert a[2] == pytest.approx(0.25)
    assert (a[3:] == 0).all()


def test_quantize_drops_out_of_range():
    sp = Spectrum(
        freqs=np.array([100.0, 1000.0]),  # position 1 and 10
        amps=np.array([1.0, 0.1]),
    )
    a = quantize_to_integer_grid(sp, f_carrier=100.0, q_max=8)
    assert a[0] == pytest.approx(1.0)
    # Position 10 is out of the q_max=8 grid.
    assert a.sum() == pytest.approx(1.0)


# ---------------------------------------------------------------------------
# RF on amplitude vector
# ---------------------------------------------------------------------------

def test_rf_uniform_amplitudes_have_low_non_dc():
    """A uniform amplitude vector (no periodic structure) should produce
    low |a_q| for q ≥ 2."""
    a = np.ones(32)
    rf = rf_amplitude_vector(a, q_max=12)
    # q=1 (DC) carries most of the energy; q≥2 should be small.
    assert rf[0] > rf[1:].max() * 5


def test_rf_period_2_pattern_spikes_at_q_2():
    """Amplitude vector that's positive at even positions, zero at odd
    — should have a strong |a_q| at q=2."""
    n = 32
    a = np.zeros(n)
    a[1::2] = 1.0  # positions 2, 4, 6, ...
    rf = rf_amplitude_vector(a, q_max=12)
    # |a_2| should be the loudest non-DC component.
    assert np.argmax(rf[1:]) + 2 == 2


def test_rf_period_3_pattern_spikes_at_q_3():
    n = 60
    a = np.zeros(n)
    a[2::3] = 1.0  # positions 3, 6, 9, ...
    rf = rf_amplitude_vector(a, q_max=12)
    assert np.argmax(rf[1:]) + 2 == 3


# ---------------------------------------------------------------------------
# Padic aggregation
# ---------------------------------------------------------------------------

def test_padic_aggregation_peaks_at_correct_prime():
    """For a fundamental-free prime-7 lattice (just multiples of 7, no
    position-1 spike), the padic-aggregate's bias-corrected per-q-mean
    should produce its maximum at p=7.

    The full family_prime(7) target also includes position 1 (the
    carrier-fundamental), which contributes ~μ(q)/φ(q)/N to every a_q
    and inflates small primes' aggregates. The family-distance metric
    handles that by comparing empirical vs target aggregates (bias
    cancels); the isolated dominance property only holds cleanly on
    a fundamental-free lattice.
    """
    q_max = 64
    a = np.zeros(q_max)
    for n in range(7, q_max + 1, 7):
        a[n - 1] = 1.0 / n
    rf = rf_amplitude_vector(a, q_max=q_max)
    agg = padic_aggregate(rf, [2, 3, 5, 7, 11, 13], q_max=q_max)
    dominant = max(agg.items(), key=lambda kv: kv[1])
    assert dominant[0] == 7, f"expected p=7 dominant, got {dominant}"


# ---------------------------------------------------------------------------
# KL helper
# ---------------------------------------------------------------------------

def test_kl_zero_when_distributions_identical():
    a = np.array([1.0, 2.0, 3.0, 4.0])
    assert _kl_divergence(a, a) == pytest.approx(0.0, abs=1e-6)


def test_kl_positive_when_distributions_differ():
    a = np.array([1.0, 0.0, 0.0, 0.0])
    b = np.array([0.25, 0.25, 0.25, 0.25])
    assert _kl_divergence(a, b) > 0.0


# ---------------------------------------------------------------------------
# Family-distance — falsification discipline
# ---------------------------------------------------------------------------

def _make_target_spectrum(family, q_max: int, f_carrier: float) -> Spectrum:
    """Synthesise a spectrum that exactly matches a family target — used
    to validate the d ≈ 0 behaviour."""
    a = family.target_amplitudes(q_max)
    freqs = []
    amps = []
    for i, w in enumerate(a):
        if w > 0:
            freqs.append((i + 1) * f_carrier)
            amps.append(float(w))
    return Spectrum(freqs=np.array(freqs), amps=np.array(amps))


def test_sawtooth_input_zero_distance_to_sawtooth():
    fam = family_sawtooth()
    sp = _make_target_spectrum(fam, q_max=16, f_carrier=110.0)
    d = family_distance(sp, fam, f_carrier=110.0, q_max=16)
    assert d == pytest.approx(0.0, abs=1e-4)


def test_square_input_zero_distance_to_square():
    fam = family_square()
    sp = _make_target_spectrum(fam, q_max=16, f_carrier=110.0)
    d = family_distance(sp, fam, f_carrier=110.0, q_max=16)
    assert d == pytest.approx(0.0, abs=1e-4)


def test_sawtooth_perturbed_has_distance_scaling_with_noise():
    """Adding noise to a sawtooth spectrum should increase its distance
    to the sawtooth family monotonically."""
    fam = family_sawtooth()
    rng = np.random.default_rng(seed=42)
    base = fam.target_amplitudes(16)
    f_c = 110.0
    distances = []
    for noise_amp in (0.0, 0.05, 0.10, 0.20):
        noisy = base + rng.standard_normal(16) * noise_amp
        noisy = np.maximum(noisy, 0)  # keep non-negative
        freqs = np.array([(i + 1) * f_c for i in range(16) if noisy[i] > 1e-6])
        amps = np.array([noisy[i] for i in range(16) if noisy[i] > 1e-6])
        sp = Spectrum(freqs=freqs, amps=amps)
        distances.append(family_distance(sp, fam, f_c, q_max=16))
    # Strict monotonic increase: d(noise=0) < d(noise=0.05) < ... < d(noise=0.20)
    for a, b in zip(distances, distances[1:]):
        assert a < b, f"distance not increasing: {distances}"


def test_sawtooth_farther_from_square_than_from_sawtooth():
    """A sawtooth-shaped input should be closer to the sawtooth family
    than to the square family (the discriminator works)."""
    saw = family_sawtooth()
    sq = family_square()
    sp = _make_target_spectrum(saw, q_max=16, f_carrier=110.0)
    d_saw = family_distance(sp, saw, 110.0, q_max=16)
    d_sq = family_distance(sp, sq, 110.0, q_max=16)
    assert d_saw < d_sq, f"saw input closer to square ({d_sq}) than saw ({d_saw})"


def test_square_farther_from_sawtooth_than_from_square():
    saw = family_sawtooth()
    sq = family_square()
    sp = _make_target_spectrum(sq, q_max=16, f_carrier=110.0)
    d_sq = family_distance(sp, sq, 110.0, q_max=16)
    d_saw = family_distance(sp, saw, 110.0, q_max=16)
    assert d_sq < d_saw


def test_prime_7_lattice_closer_to_prime_7_than_prime_5():
    p7 = family_prime(7)
    p5 = family_prime(5)
    sp = _make_target_spectrum(p7, q_max=64, f_carrier=110.0)
    d_p7 = family_distance(sp, p7, 110.0, q_max=64)
    d_p5 = family_distance(sp, p5, 110.0, q_max=64)
    assert d_p7 < d_p5


def test_bell_stretched_kl_only_no_rf_term():
    """Bell-stretched has characteristic_q = None, so the metric is pure
    KL — and a bell-stretched target spectrum should produce d ≈ 0
    against the bell family at the same β."""
    fam = family_bell_stretched(1.05)
    sp = _make_target_spectrum(fam, q_max=24, f_carrier=110.0)
    d = family_distance(sp, fam, 110.0, q_max=24)
    assert d == pytest.approx(0.0, abs=1e-3)


def test_triangle_input_closer_to_triangle_than_square():
    """Triangle and square share their support (odd-only positions) but
    differ in envelope (1/n² vs 1/n). The distance metric must
    discriminate them despite the overlap."""
    tri = family_triangle()
    sq = family_square()
    sp = _make_target_spectrum(tri, q_max=16, f_carrier=110.0)
    d_tri = family_distance(sp, tri, 110.0, q_max=16)
    d_sq = family_distance(sp, sq, 110.0, q_max=16)
    assert d_tri < d_sq


# ---------------------------------------------------------------------------
# Vectorised RF (Ramanujan-matrix LUT) — must equal the per-q reference (P1.2)
# ---------------------------------------------------------------------------

def _rf_reference(amps, q_max):
    """Original per-q implementation, kept as the correctness oracle."""
    from phase3.ars.arithmetic_toolkit import ramanujan_sum_array, _phi
    n = len(amps)
    a = np.zeros(q_max)
    for q in range(1, q_max + 1):
        c = ramanujan_sum_array(q, n)
        a[q - 1] = float(np.mean(amps * c)) / _phi(q)
    return np.abs(a)


@pytest.mark.parametrize("q_max", [8, 16, 64, 200])
def test_rf_vectorised_matches_reference(q_max):
    rng = np.random.default_rng(q_max)
    amps = np.abs(rng.standard_normal(q_max))
    np.testing.assert_allclose(
        rf_amplitude_vector(amps, q_max), _rf_reference(amps, q_max), atol=1e-12)


def test_phi_mu_sieve_matches_ars():
    from phase3.ars.arithmetic_toolkit import euler_phi, mobius
    from phase3.family_distance import _phi_mu_sieve
    phi, mu = _phi_mu_sieve(200)
    for n in range(1, 201):
        assert phi[n] == euler_phi(n), n
        assert mu[n] == mobius(n), n


# ---------------------------------------------------------------------------
# Interpolated grid projection (smoothing, S1)
# ---------------------------------------------------------------------------

def test_interpolated_grid_splits_half_integer():
    # A single partial at exactly 2.5·f_c splits evenly between bins 2 and 3.
    sp = Spectrum(freqs=np.array([2.5 * 220.0]), amps=np.array([1.0]))
    out = quantize_to_integer_grid(sp, 220.0, 8, interpolate=True)
    assert out[1] == pytest.approx(0.5)
    assert out[2] == pytest.approx(0.5)


def test_interpolated_grid_continuous_in_frequency():
    # Sliding a partial from 2.0→3.0·f_c moves weight smoothly bin2→bin3,
    # with no discontinuous jump (the property that smooths the landscape).
    prev = None
    for x in np.linspace(2.0, 3.0, 11):
        sp = Spectrum(freqs=np.array([x * 220.0]), amps=np.array([1.0]))
        out = quantize_to_integer_grid(sp, 220.0, 8, interpolate=True)
        if prev is not None:
            assert abs(out[2] - prev) <= 0.11    # ≤ one linspace step, no jump
        prev = out[2]


def test_interpolated_integer_partial_unchanged():
    # Exact-integer partials land wholly in their bin (no leakage).
    sp = Spectrum(freqs=np.array([3.0 * 220.0]), amps=np.array([1.0]))
    out = quantize_to_integer_grid(sp, 220.0, 8, interpolate=True)
    assert out[2] == pytest.approx(1.0)
    assert out.sum() == pytest.approx(1.0)


def test_hard_round_still_available():
    sp = Spectrum(freqs=np.array([2.4 * 220.0]), amps=np.array([1.0]))
    out = quantize_to_integer_grid(sp, 220.0, 8, interpolate=False)
    assert out[1] == pytest.approx(1.0)   # 2.4 → nearest int 2
