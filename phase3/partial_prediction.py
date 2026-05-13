"""Partial-set prediction for Brocot's parallel-FM topology.

For 8 modulators in parallel (each at ratio r_i and modulation index I_i)
summing into the carrier's frequency, the analytically predicted partial
set is the Cartesian-product expansion described in farey-fm 2.0 spec §2.2:

    P(C) = { ( f_c · (1 + Σ_i n_i r_i),  Π_i J_{n_i}(I_i) )
           : (n_1, …, n_N) ∈ ℤ^N }

with two prunings to keep the N=8 combinatorial explosion tractable:

  1. Per-modulator order bound. For each modulator i, retain only sideband
     orders n_i where |J_{n_i}(I_i)| > ε. At depth I=2 this typically caps
     |n_i| ≤ 4; at I=5 caps around |n_i| ≤ 8.

  2. Early-termination amplitude tracking. When generating the N-dimensional
     product recursively, prune subtrees whose running amplitude product
     has already dropped below threshold before all coordinates assigned.

Negative absolute frequencies reflect to their positive counterparts and
amplitudes sum at the reflected bin (Web Audio behaviour). Partials below
`f_min` (default 20 Hz) and above `f_max` (default 20 kHz) are dropped.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Sequence

import numpy as np
from scipy.special import jv


# ---------------------------------------------------------------------------
# Per-modulator order bound
# ---------------------------------------------------------------------------

def order_bound(depth: float, eps: float = 1e-3, n_cap: int = 50) -> int:
    """Largest |n| such that |J_n(depth)| > eps.

    Returns 0 for depth=0 (only carrier survives at zero modulation). The
    `n_cap` guard prevents pathological loops at very high depths.
    """
    if depth <= 0.0:
        return 0
    n = 1
    while n <= n_cap and abs(jv(n, depth)) > eps:
        n += 1
    return n - 1


# ---------------------------------------------------------------------------
# Partial-set recursion
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class Spectrum:
    """A spectral snapshot of a configuration."""
    freqs: np.ndarray   # ascending Hz
    amps: np.ndarray    # absolute amplitudes (positive)

    def __len__(self) -> int:
        return int(self.freqs.size)

    def loudest_non_carrier(self, f_carrier: float, tol_hz: float = 1.0) -> float:
        """Loudest amplitude excluding the carrier bin (used for the
        threshold-relative-to-spectrum filter)."""
        non_carrier = np.abs(self.freqs - f_carrier) > tol_hz
        if non_carrier.any():
            return float(self.amps[non_carrier].max())
        return float(self.amps.max()) if self.amps.size else 0.0


def predict_partials(
    ratios: Sequence[float],
    depths: Sequence[float],
    f_carrier: float = 220.0,
    threshold_db: float = -50.0,
    f_min: float = 20.0,
    f_max: float = 20000.0,
    eps: float = 1e-3,
    bin_resolution_hz: float = 0.5,
    max_partials: int | None = None,
) -> Spectrum:
    """Build the audible partial set for a parallel-FM configuration.

    Parameters
    ----------
    ratios, depths : same-length sequences, one per modulator
    f_carrier      : carrier fundamental in Hz
    threshold_db   : amplitude floor relative to the loudest non-carrier
                     partial (default −50 dB)
    f_min, f_max   : audible range (Hz); partials outside are dropped
    eps            : per-modulator order-bound tolerance for J_n
    bin_resolution_hz : quantisation step when summing partials that
                        collapse onto the same frequency (negative-reflected,
                        coincident sidebands, etc.)
    """
    if len(ratios) != len(depths):
        raise ValueError("ratios and depths must have the same length")

    n_mods = len(ratios)
    threshold = 10.0 ** (threshold_db / 20.0)

    # Per-modulator order bounds.
    n_max = [order_bound(I, eps=eps) for I in depths]

    # Buckets keyed by quantised frequency; values accumulate signed
    # amplitudes (so coincident partials with opposite signs cancel
    # correctly), and the final reading takes the absolute value.
    buckets: dict[int, float] = {}
    bin_inv = 1.0 / bin_resolution_hz

    def emit(freq: float, amp: float) -> None:
        if amp == 0.0:
            return
        # Reflect negative frequencies into the positive axis. For a
        # sine-basis output, sin(-2π·f·t) = -sin(2π·f·t), so reflection
        # flips the amplitude's sign — that's what produces the
        # constructive/destructive interference at frequencies where an
        # upper-side and a reflected-lower-side partial coincide. The
        # classic J_0-null trick at ratio=1 leaves a residual at the
        # carrier from the n=-2 reflection because of this.
        if freq < 0:
            f = -freq
            amp = -amp
        else:
            f = freq
        if f < f_min or f > f_max:
            return
        key = int(round(f * bin_inv))
        buckets[key] = buckets.get(key, 0.0) + amp

    # Recursion. partial_amp carries the product Π J_{n_k}(I_k) so far;
    # sum_n_r carries Σ n_k · r_k so far. Early-terminate when
    # partial_amp drops below a coarse threshold; also abort the whole
    # walk if `max_partials` is reached so pathological deep-multi-mod
    # configurations don't time out (typical for the search workload).
    early_cutoff = threshold * 0.1

    # Precompute J_n(I_k) for every (k, n) in the per-modulator range —
    # scipy's jv is expensive; we'd otherwise call it ~Π(2n_max[k]+1)
    # times across the recursion. With small per-mod ranges (often 3-9)
    # this is a sharp speedup.
    j_tables: list[np.ndarray] = []
    for k in range(n_mods):
        I = depths[k]
        n_k = n_max[k]
        if I == 0.0:
            t = np.zeros(2 * n_k + 1)
            t[n_k] = 1.0  # n=0 → J_0(0)=1
        else:
            t = jv(np.arange(-n_k, n_k + 1), I)
        j_tables.append(t)

    aborted = [False]

    def recurse(idx: int, partial_amp: float, sum_n_r: float) -> None:
        if aborted[0]:
            return
        if idx == n_mods:
            emit(f_carrier * (1.0 + sum_n_r), partial_amp)
            if max_partials is not None and len(buckets) > max_partials:
                aborted[0] = True
            return
        r = ratios[idx]
        n_k = n_max[idx]
        table = j_tables[idx]
        for offset, J in enumerate(table):
            next_amp = partial_amp * float(J)
            if abs(next_amp) < early_cutoff:
                continue
            recurse(idx + 1, next_amp, sum_n_r + (offset - n_k) * r)
            if aborted[0]:
                return

    recurse(0, 1.0, 0.0)

    if not buckets:
        return Spectrum(freqs=np.array([]), amps=np.array([]))

    # Sort by frequency, take absolute amplitudes.
    sorted_keys = sorted(buckets.keys())
    freqs = np.array([k * bin_resolution_hz for k in sorted_keys])
    amps = np.array([abs(buckets[k]) for k in sorted_keys])

    # Final threshold filter: relative to the loudest non-carrier partial,
    # since the carrier can dwarf everything at low modulation.
    non_carrier_mask = np.abs(freqs - f_carrier) > bin_resolution_hz * 2.0
    if non_carrier_mask.any() and amps[non_carrier_mask].size > 0:
        ref = amps[non_carrier_mask].max()
    else:
        ref = amps.max()
    keep = amps > ref * threshold
    return Spectrum(freqs=freqs[keep], amps=amps[keep])
