"""ARS arithmetic fingerprint of an FM config — the deep number-theoretic
signature beyond the raw spectrum.

We already use ARS's Ramanujan-Fourier (`rf_amplitude_vector`) and p-adic
aggregation (`padic_aggregate`) for the family-distance RF term. This module
turns them into a per-config *arithmetic tag*:

  * rf_top_q      — the dominant Ramanujan period (which q ≥ 2 carries the
                    most RF power): the integer periodicity the partials sit on.
  * padic_prime   — the prime whose powers carry the most RF power (2/3/5/7/
                    11/13): the config's prime-lattice character.
  * spacing_class — GUE / GOE / Poisson of the partial *spacing* (only when a
                    config is dense enough, ≥20 partials, for the statistic to
                    mean anything — e.g. the textured-smooth carpets). This is
                    ARS's full_analysis on the partial positions as a point
                    process: are the partials repulsive (random-matrix-like) or
                    clustered (Poisson)?

Use: arithmetic tags for the preset library / steering UI, and an arithmetic
atlas of FM space (where configs fall in the RF / p-adic / spacing landscape).
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from phase3.family_distance import padic_aggregate, rf_amplitude_vector

_PRIMES = [2, 3, 5, 7, 11, 13]


@dataclass
class ArsFingerprint:
    rf_top_q: int           # dominant Ramanujan q (≥2)
    rf_top_strength: float  # its normalised RF amplitude
    padic_prime: int        # dominant prime
    padic_strength: float   # its p-adic aggregate value
    spacing_class: str      # 'GUE' | 'GOE' | 'Poisson' | 'sparse'

    def describe(self) -> str:
        s = f"q{self.rf_top_q}·p{self.padic_prime}"
        if self.spacing_class != "sparse":
            s += f"·{self.spacing_class}"
        return s


def ars_fingerprint(amps: np.ndarray, q_max: int | None = None) -> ArsFingerprint:
    a = np.asarray(amps, dtype=np.float64)
    if q_max is None:
        q_max = a.size
    rf = rf_amplitude_vector(a, q_max)
    # Dominant Ramanujan q among q≥2 (q=1 is the DC/fundamental term).
    if rf.size > 2 and rf[1:].sum() > 0:
        top = int(np.argmax(rf[1:])) + 2
        strength = float(rf[top - 1] / (rf[1:].sum() + 1e-12))
    else:
        top, strength = 1, 0.0

    pa = padic_aggregate(rf, _PRIMES, q_max)
    pprime = max(pa, key=pa.get)
    pstr = float(pa[pprime])

    spacing = _spacing_class(a, q_max)
    return ArsFingerprint(top, round(strength, 4), pprime, round(pstr, 4), spacing)


def _spacing_class(amps: np.ndarray, q_max: int) -> str:
    """ARS spacing-statistic class of the partial positions (as a point
    process). Only meaningful with enough partials; 'sparse' otherwise."""
    pos = np.nonzero(amps[:q_max] > 1e-4 * (amps.max() + 1e-12))[0].astype(float) + 1.0
    if pos.size < 20:
        return "sparse"
    try:
        from phase3.ars.arithmetic_toolkit import full_analysis
        fp = full_analysis(pos, label="fm")
        nns = fp.get("primary_nns", {})
        return str(nns.get("best", "sparse"))
    except Exception:
        return "sparse"
