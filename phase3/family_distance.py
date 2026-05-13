"""Family-distance metric — how far a configuration's predicted spectrum
sits from a target musical-family manifold.

Per farey-fm 2.0 §2.3, the integer-supported metric is a hybrid:

    d(P, family) = D_KL(â, ŵ)  +  λ · |rf(P) − rf(family)|

where:
- â is the empirical partial-spectrum quantised to the integer
  multiples-of-fundamental grid and normalised to sum to 1
- ŵ is the family's normalised target amplitude vector
- rf(·) is the Ramanujan-Fourier amplitude at the family's
  characteristic q, computed via ARS's `ramanujan_sum_array` and
  Euler-φ. For prime-p families, rf(·) instead uses the prime-power
  aggregation that mirrors ARS's `padic_amplitude_v4`.
- λ ≈ 0.5 by default (tunable).

For bell-stretched (non-integer positions), the RF term is dropped
and the metric reduces to KL on Gaussian-smoothed spectra.

The math primitives (ramanujan_sum_array, _phi) are imported from
ARS. We feed them an *amplitude vector* directly rather than going
through ARS's event-time → indicator-bin path, which matches our
synthesis-side static-spectrum use case while staying numerically
identical to what RESULTS.md's RF engine produces.
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

# Make ARS importable from phase3/ars/.
_ARS_DIR = Path(__file__).parent / "ars"
if str(_ARS_DIR) not in sys.path:
    sys.path.insert(0, str(_ARS_DIR))

# These are the canonical implementations from arithmetic_toolkit.py.
from arithmetic_toolkit import ramanujan_sum_array, _phi  # noqa: E402

from .families import Family  # noqa: E402
from .partial_prediction import Spectrum  # noqa: E402


# ---------------------------------------------------------------------------
# Grid projection
# ---------------------------------------------------------------------------

def quantize_to_integer_grid(
    spectrum: Spectrum,
    f_carrier: float,
    q_max: int,
) -> np.ndarray:
    """Project a partial set onto the integer-multiple-of-carrier grid.

    Position 1 = the fundamental at 1·f_carrier (the carrier itself).
    Position 2 = 2·f_carrier. Etc. Out-of-range partials are dropped;
    non-integer-multiple partials bin to the nearest integer.
    """
    out = np.zeros(q_max, dtype=np.float64)
    if len(spectrum) == 0:
        return out
    for f, a in zip(spectrum.freqs, spectrum.amps):
        n = int(round(f / f_carrier))
        if 1 <= n <= q_max:
            out[n - 1] += a
    return out


# ---------------------------------------------------------------------------
# RF on an amplitude vector (consumes ARS primitives, no event-time API)
# ---------------------------------------------------------------------------

def rf_amplitude_vector(amplitudes: np.ndarray, q_max: int) -> np.ndarray:
    """Compute |a_q| for q=1..q_max from an amplitude vector indexed by
    integer position (one bin per position).

    Equivalent to `ramanujan_fourier(t_k, q_max, normalize=False)` when
    the input event times produce an indicator function equal to this
    amplitude vector — but bypasses the events→bins conversion since
    we already have the bins.
    """
    n = len(amplitudes)
    if n == 0:
        return np.zeros(q_max, dtype=np.float64)
    a = np.zeros(q_max, dtype=np.float64)
    for q in range(1, q_max + 1):
        c = ramanujan_sum_array(q, n)
        a[q - 1] = float(np.mean(amplitudes * c)) / _phi(q)
    return np.abs(a)


def padic_aggregate(
    rf_amps: np.ndarray,
    primes: list[int],
    q_max: int,
) -> dict[int, float]:
    """Per-prime power-aggregation of |a_q| over q ∈ {p, p², p³, …}.

    Mirrors ARS's `padic_amplitude_v4.normalised_per_q` — the *mean*
    amplitude over the prime-power bands divided by the global mean
    amplitude across q ≥ 2. The per-band mean normalisation cancels
    the "more bands → bigger sum" confound that biases the
    sum-normalised version toward small primes (which have more
    pure-power bands ≤ q_max). Higher value = more RF power
    concentrated at this prime's powers relative to the baseline.
    """
    if len(rf_amps) < 2:
        return {p: 0.0 for p in primes}
    mean_baseline = float(np.mean(rf_amps[1:])) + 1e-12
    out: dict[int, float] = {}
    for p in primes:
        q_pows: list[int] = []
        q_pow = p
        while q_pow <= q_max:
            q_pows.append(q_pow)
            q_pow *= p
        if not q_pows:
            out[p] = 0.0
            continue
        mean_at_p = float(sum(rf_amps[q - 1] for q in q_pows)) / len(q_pows)
        out[p] = mean_at_p / mean_baseline
    return out


# ---------------------------------------------------------------------------
# Distance computation
# ---------------------------------------------------------------------------

def _kl_divergence(p: np.ndarray, q: np.ndarray, eps: float = 1e-9) -> float:
    """D_KL(p || q) for two non-negative vectors. Each is re-normalised
    inside; `eps` smooths zeros to avoid log(0)."""
    p = np.asarray(p, dtype=np.float64) + eps
    q = np.asarray(q, dtype=np.float64) + eps
    p /= p.sum()
    q /= q.sum()
    return float(np.sum(p * np.log(p / q)))


def family_distance(
    spectrum: Spectrum,
    family: Family,
    f_carrier: float,
    q_max: int = 16,
    lambda_rf: float = 0.5,
) -> float:
    """Compute d(P, family). Lower is closer to the family manifold.

    Parameters
    ----------
    spectrum   : predicted partial set (from partial_prediction)
    family     : the target family (see phase3.families)
    f_carrier  : carrier fundamental in Hz (used to quantise to the
                 integer grid; only the *ratios* of partial freqs to
                 f_carrier matter, not the absolute frequency)
    q_max      : grid size for both KL and RF terms; default 16 (the
                 audible-musical-family range; spec §6 of 2.0 recommends
                 16 over the inherited 30, since useful families don't
                 reach past q=13).
    lambda_rf  : weight on the RF mismatch term.
    """
    emp = quantize_to_integer_grid(spectrum, f_carrier, q_max)
    tgt = family.target_amplitudes(q_max)

    # KL on normalised spectra.
    kl = _kl_divergence(emp, tgt)

    # If the family doesn't have a characteristic q (e.g. bell-stretched),
    # KL is the whole metric.
    if family.characteristic_q is None:
        return kl

    # RF amplitudes for both empirical and target.
    emp_rf = rf_amplitude_vector(emp, q_max)
    tgt_rf = rf_amplitude_vector(tgt, q_max)

    # Prime-p families use prime-power aggregation (padic_amplitude_v4
    # equivalent on our amplitude-vector RF). Compares normalised
    # power-at-p-powers, not single-q amplitude.
    if family.name.startswith("prime_"):
        p = family.params.get("p", family.characteristic_q)
        emp_agg = padic_aggregate(emp_rf, [p], q_max)
        tgt_agg = padic_aggregate(tgt_rf, [p], q_max)
        rf_term = abs(emp_agg[p] - tgt_agg[p])
    else:
        q = family.characteristic_q
        if 1 <= q <= q_max:
            # Normalise the RF amplitude at q by total RF power so the
            # term is dimensionless and comparable across families.
            emp_total = float(np.sum(emp_rf[1:])) + 1e-12
            tgt_total = float(np.sum(tgt_rf[1:])) + 1e-12
            emp_q = emp_rf[q - 1] / emp_total
            tgt_q = tgt_rf[q - 1] / tgt_total
            rf_term = abs(emp_q - tgt_q)
        else:
            rf_term = 0.0

    return kl + lambda_rf * rf_term
