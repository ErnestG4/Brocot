"""Musical-family definitions for Phase 3 family-distance search.

Each Family is a triple `(L, w, name)` per farey-fm 2.0 §2.1:
    L: set of integer (or stretched) partial positions populated
    w: amplitude envelope assigning weight to each populated position
    name: player-facing label

Family targets are represented as a Python closure that maps `q_max`
(grid size) to an amplitude vector of length q_max, with index i
corresponding to integer partial position i+1 (so out[0] is the
fundamental at 1·f_carrier). The amplitude vector is NOT normalised
inside the family — `family_distance.family_distance` does that.

Each family carries an optional `characteristic_q`: the integer
position whose RF amplitude carries the most family-discriminating
signal. None for non-integer families (bell-stretched), which use
KL-only distance.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from functools import reduce
from math import gcd
from typing import Callable

import numpy as np


@dataclass(frozen=True)
class Family:
    name: str
    target_amplitudes: Callable[[int], np.ndarray]
    characteristic_q: int | None
    params: dict = field(default_factory=dict)


# ---------------------------------------------------------------------------
# Integer-supported families
# ---------------------------------------------------------------------------

def family_sawtooth() -> Family:
    """All integer multiples with 1/n envelope. Classical analogue sawtooth."""
    def target(q_max: int) -> np.ndarray:
        return 1.0 / np.arange(1, q_max + 1)
    return Family("sawtooth", target, characteristic_q=1)


def family_square() -> Family:
    """Odd integer multiples with 1/n envelope."""
    def target(q_max: int) -> np.ndarray:
        a = np.zeros(q_max, dtype=np.float64)
        a[::2] = 1.0 / np.arange(1, q_max + 1, 2)
        return a
    return Family("square", target, characteristic_q=2)


def family_triangle() -> Family:
    """Odd integer multiples with 1/n² envelope."""
    def target(q_max: int) -> np.ndarray:
        a = np.zeros(q_max, dtype=np.float64)
        n = np.arange(1, q_max + 1, 2)
        a[::2] = 1.0 / (n * n)
        return a
    return Family("triangle", target, characteristic_q=2)


def family_pulse_pw(N: int) -> Family:
    """Pulse-width N: all integer positions except multiples of N, with
    sinc(πn/N)/n envelope. Classic naive-PWM spectral shape."""
    if N < 2:
        raise ValueError(f"pulse-PW N must be ≥ 2, got {N}")

    def target(q_max: int) -> np.ndarray:
        a = np.zeros(q_max, dtype=np.float64)
        for n in range(1, q_max + 1):
            if n % N == 0:
                continue  # spectral null at multiples of N
            x = np.pi * n / N
            sinc = np.sin(x) / x
            a[n - 1] = abs(sinc) / n
        return a
    return Family(f"pulse_pw_{N}", target, characteristic_q=N,
                  params={"N": N})


def family_organ(stops: tuple[int, ...]) -> Family:
    """Drawbar-organ additive: weight 1/s at each stop position s."""
    if not stops:
        raise ValueError("organ family needs at least one stop")
    stops_sorted = tuple(sorted(set(stops)))

    def target(q_max: int) -> np.ndarray:
        a = np.zeros(q_max, dtype=np.float64)
        for s in stops_sorted:
            if 1 <= s <= q_max:
                a[s - 1] = 1.0 / s
        return a

    # Characteristic q: the GCD of consecutive differences between stops
    # (the "spacing period" of the harmonic stack). For {1,3,5}: gcd(2,2)=2.
    # For {1,2,4}: gcd(1,2)=1. Single-stop case: 1.
    if len(stops_sorted) > 1:
        diffs = [b - a for a, b in zip(stops_sorted, stops_sorted[1:])]
        char_q = reduce(gcd, diffs)
        if char_q <= 0:
            char_q = 1
    else:
        char_q = 1

    return Family(f"organ_{'_'.join(map(str, stops_sorted))}", target,
                  characteristic_q=char_q,
                  params={"stops": list(stops_sorted)})


# ---------------------------------------------------------------------------
# Non-integer-supported families
# ---------------------------------------------------------------------------

def family_bell_stretched(beta: float, max_n: int = 30, sigma: float = 0.3) -> Family:
    """Stretched-harmonic: positions n^β for n=1..max_n with 1/n envelope.

    Since the positions are non-integer, we project onto the integer
    grid via a Gaussian kernel centred at each n^β. characteristic_q is
    None — the family-distance metric drops the RF term and uses only
    KL on the smoothed spectra (2.0 §2.3).
    """
    if beta <= 0.999:
        raise ValueError(f"bell-stretched β must be > 1 (got {beta})")

    def target(q_max: int) -> np.ndarray:
        # Vectorised — the nested Python-loop version was ~4× slower than
        # other families' targets and dominated bell-stretched's per-eval
        # cost (target() is called for every search-distance evaluation).
        n = np.arange(1, max_n + 1, dtype=np.float64)
        pos = n ** beta
        mask = pos <= q_max + sigma * 5.0
        n_keep = n[mask]
        pos_keep = pos[mask]
        if n_keep.size == 0:
            return np.zeros(q_max, dtype=np.float64)
        weights = 1.0 / n_keep
        two_sigma_sq = 2.0 * sigma * sigma
        k = np.arange(1, q_max + 1, dtype=np.float64)[:, None]
        dx = k - pos_keep[None, :]
        return (weights[None, :] * np.exp(-(dx * dx) / two_sigma_sq)).sum(axis=1)

    return Family(f"bell_beta_{beta:.2f}".replace(".", "_"),
                  target, characteristic_q=None,
                  params={"beta": beta, "sigma": sigma})


# ---------------------------------------------------------------------------
# External-target family (Mode B, P0.2)
# ---------------------------------------------------------------------------

def family_from_target(
    amplitudes: np.ndarray,
    name: str = "external",
    f_carrier: float = 220.0,
) -> Family:
    """Wrap an externally-measured amplitude vector as a Family.

    This is the Mode B entry point: `amplitudes` is the integer-partial
    spectrum extracted from a recording by
    `audio_spectrum.audio_to_spectrum`. The returned Family plugs into the
    existing `family_distance` / `two_phase_search` machinery with no core
    changes.

    `characteristic_q=None` — there is no a-priori "signature q" for an
    arbitrary external target, so the distance is KL-only on the smoothed
    spectra, reusing the bell_stretched precedent (see family_distance:
    the RF term is dropped when characteristic_q is None).

    `target_amplitudes(q_max)` returns the supplied vector truncated or
    zero-padded to length q_max, cached per q_max (the search calls it
    once per distance evaluation).
    """
    base = np.asarray(amplitudes, dtype=np.float64).ravel()
    cache: dict[int, np.ndarray] = {}

    def target(q_max: int) -> np.ndarray:
        cached = cache.get(q_max)
        if cached is not None:
            return cached
        out = np.zeros(q_max, dtype=np.float64)
        m = min(q_max, base.size)
        out[:m] = base[:m]
        cache[q_max] = out
        return out

    return Family(name, target, characteristic_q=None,
                  params={"source": "external", "f_carrier": f_carrier,
                          "n_partials": int(base.size)})


# ---------------------------------------------------------------------------
# Prime-p families
# ---------------------------------------------------------------------------

def family_prime(p: int) -> Family:
    """Prime-p lattice: position 1 (fundamental) plus all multiples of p,
    each with 1/n envelope.

    p ∈ {2, 3, 5, 7, 11, 13}. Position 1 is always present (the carrier-
    fundamental); positions {p, 2p, 3p, ...} carry the prime-p signature
    that ARS's padic_amplitude_v4 detects.
    """
    if p < 2:
        raise ValueError(f"prime must be ≥ 2 (got {p})")

    def target(q_max: int) -> np.ndarray:
        a = np.zeros(q_max, dtype=np.float64)
        if q_max >= 1:
            a[0] = 1.0  # fundamental
        for n in range(p, q_max + 1, p):
            a[n - 1] = 1.0 / n
        return a

    return Family(f"prime_{p}", target, characteristic_q=p, params={"p": p})


# ---------------------------------------------------------------------------
# Default 12-family list shipped in Phase 3 v1
# ---------------------------------------------------------------------------

DEFAULT_PRIMES = (2, 3, 5, 7, 11, 13)


def default_families() -> list[Family]:
    """The 12 families generated for Phase 3 v1.

    - sawtooth, square, triangle (3 staples)
    - pulse_pw_4 (parametric — defaulted to N=4)
    - organ_1_3_5 (parametric — defaulted to principal-stop drawbar)
    - bell_beta_1.05 (parametric — defaulted to mild stretching)
    - prime_2, prime_3, prime_5, prime_7, prime_11, prime_13 (6)
    """
    return [
        family_sawtooth(),
        family_square(),
        family_triangle(),
        family_pulse_pw(4),
        family_organ((1, 3, 5)),
        family_bell_stretched(1.05),
        *(family_prime(p) for p in DEFAULT_PRIMES),
    ]
