"""Spectral feature measurements — the "sound DNA" of a fingerprint.

Given an integer-grid amplitude vector (the same fingerprint Mode B and the
coverage bank use), compute musically-meaningful timbre descriptors. Uses:
  * tag presets in the steering UI ("bright · rough · inharmonic")
  * a secondary ranking / diversity signal alongside KL distance
  * just plain fun readouts for a toy people learn FM with

Everything is defined on the partial-position grid (n = 1..q_max), with the
absolute-frequency descriptors (roughness) referenced to f_carrier.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np


@dataclass
class SpectralFeatures:
    centroid: float        # brightness — mean partial position (1 = dark)
    spread: float          # bandwidth — std of partials about the centroid
    rolloff85: float       # partial below which 85% of energy sits
    flatness: float        # 0 tonal/peaky → 1 noisy/flat (Wiener entropy)
    tilt: float            # spectral slope (energy per partial, log) — <0 = HF rolloff
    odd_even: float        # odd:even harmonic energy ratio (>1 = clarinet-like)
    t1: float              # tristimulus: fundamental fraction
    t2: float              # tristimulus: harmonics 2–4 fraction
    t3: float              # tristimulus: harmonics 5+ fraction
    effective_count: float # number of "significant" partials (inverse participation)
    roughness: float       # sensory dissonance from close partial beating

    def describe(self) -> str:
        """A short adjective string for the toy UI."""
        adj = []
        adj.append("bright" if self.centroid > 6 else
                   "warm" if self.centroid > 3 else "dark")
        if self.roughness > 0.15:
            adj.append("rough")
        elif self.roughness < 0.04:
            adj.append("pure")
        if self.flatness > 0.45:
            adj.append("noisy")
        if self.odd_even > 2.0:
            adj.append("hollow")
        if self.effective_count > 12:
            adj.append("rich")
        elif self.effective_count < 3:
            adj.append("simple")
        return " · ".join(adj)


def _sethares_roughness(amps: np.ndarray, f_carrier: float) -> float:
    """Plomp-Levelt / Sethares dissonance summed over partial pairs, scaled by
    their amplitudes. Peaks when two partials are ~1/4 critical band apart."""
    idx = np.nonzero(amps > 1e-6)[0]
    if idx.size < 2:
        return 0.0
    f = (idx + 1) * f_carrier
    a = amps[idx]
    # Sethares dissonance kernel parameters.
    b1, b2, s1, s2 = 3.5, 5.75, 0.0207, 18.96
    total = 0.0
    norm = float((a.sum()) ** 2) + 1e-12
    for i in range(idx.size):
        fi, ai = f[i], a[i]
        for j in range(i + 1, idx.size):
            df = f[j] - fi
            s = 0.24 / (s1 * fi + s2)
            d = np.exp(-b1 * s * df) - np.exp(-b2 * s * df)
            total += ai * a[j] * d
    return float(total / norm)


def spectral_features(amps: np.ndarray, f_carrier: float = 220.0) -> SpectralFeatures:
    a = np.asarray(amps, dtype=np.float64)
    q = a.size
    n = np.arange(1, q + 1, dtype=np.float64)
    total = a.sum()
    if total <= 0:
        return SpectralFeatures(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)

    p = a / total
    centroid = float((n * p).sum())
    spread = float(np.sqrt(((n - centroid) ** 2 * p).sum()))
    cum = np.cumsum(a)
    rolloff85 = float(n[np.searchsorted(cum, 0.85 * total)] if total > 0 else 0.0)

    # Wiener entropy over the *full* grid (with a floor) — zeros pull a peaky
    # spectrum toward 0 (tonal); a flat spectrum gives 1 (noisy).
    eps = 1e-12
    flatness = float(np.exp(np.mean(np.log(p + eps))) / (np.mean(p) + eps))

    # Spectral tilt: slope of log-amplitude vs partial index (weighted lightly
    # to the partials that carry energy).
    mask = a > 1e-6
    if mask.sum() >= 2:
        tilt = float(np.polyfit(n[mask], np.log(a[mask]), 1)[0])
    else:
        tilt = 0.0

    odd = a[0::2].sum()
    even = a[1::2].sum()
    odd_even = float(odd / (even + 1e-12))

    t1 = float(a[0] / total)
    t2 = float(a[1:4].sum() / total)
    t3 = float(a[4:].sum() / total)

    effective_count = float((total ** 2) / ((a ** 2).sum() + 1e-12))
    roughness = _sethares_roughness(a, f_carrier)

    return SpectralFeatures(centroid, spread, rolloff85, flatness, tilt,
                            odd_even, t1, t2, t3, effective_count, roughness)
