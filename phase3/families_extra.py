"""Extra target families — instrument & waveform flavours for Mode B to chase.

The 12 default families (sawtooth, square, prime-p, …) are the rigorous core.
These are the *fun* ones: spectral recipes for recognisable instrument timbres
that the FM search can approximate. They're targets, not synthesis — Mode B
finds a parallel-FM config whose spectrum is closest, so adding a family here
is adding a "discover me" sound to the menu.

Inharmonic families (bell, glass) project their non-integer partials onto the
integer grid with a Gaussian kernel (the bell_stretched precedent) and use
characteristic_q=None ⇒ KL-only distance. Harmonic-but-shaped families
(pluck, brass, reed, vowels) keep a characteristic_q for the RF term.

All amplitude recipes are referenced to f_carrier = 220 Hz where an absolute
frequency matters (vowels' formants), matching DEFAULT_F_REF elsewhere.
"""

from __future__ import annotations

import numpy as np

from phase3.families import Family

F_REF = 220.0


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _gaussian_project(positions: np.ndarray, weights: np.ndarray,
                      q_max: int, sigma: float = 0.3) -> np.ndarray:
    """Project (possibly non-integer) partial positions onto the integer grid
    via a Gaussian kernel — same idea as family_bell_stretched."""
    out = np.zeros(q_max, dtype=np.float64)
    keep = positions <= q_max + sigma * 5.0
    pos, w = positions[keep], weights[keep]
    if pos.size == 0:
        return out
    k = np.arange(1, q_max + 1, dtype=np.float64)[:, None]
    dx = k - pos[None, :]
    return (w[None, :] * np.exp(-(dx * dx) / (2.0 * sigma * sigma))).sum(axis=1)


# ---------------------------------------------------------------------------
# Harmonic-but-shaped families
# ---------------------------------------------------------------------------

def family_pluck(damping: float = 0.18) -> Family:
    """Plucked string / mallet: full harmonic series with an exponential
    high-frequency rolloff (1/n · e^{-damping·(n-1)}). Higher damping = duller."""
    def target(q_max: int) -> np.ndarray:
        n = np.arange(1, q_max + 1, dtype=np.float64)
        return (1.0 / n) * np.exp(-damping * (n - 1.0))
    return Family(f"pluck_{damping:.2f}".replace(".", "_"), target,
                  characteristic_q=1, params={"damping": damping})


def family_brass(center: float = 6.0, width: float = 4.0) -> Family:
    """Brass: harmonic series with a broad spectral-centroid bump in the
    upper-mid harmonics (the bright "blare" formant)."""
    def target(q_max: int) -> np.ndarray:
        n = np.arange(1, q_max + 1, dtype=np.float64)
        base = 1.0 / np.sqrt(n)
        bump = 1.0 + 1.5 * np.exp(-((n - center) ** 2) / (2.0 * width * width))
        return base * bump
    return Family("brass", target, characteristic_q=1,
                  params={"center": center, "width": width})


def family_reed(rolloff: float = 1.2) -> Family:
    """Reed / clarinet: odd harmonics dominate (like a square) but with the
    evens present and a gentler rolloff — woodier than a pure square."""
    def target(q_max: int) -> np.ndarray:
        n = np.arange(1, q_max + 1, dtype=np.float64)
        a = np.where(n % 2 == 1, 1.0 / n ** rolloff, 0.25 / n ** (rolloff + 0.5))
        return a
    return Family(f"reed_{rolloff:.1f}".replace(".", "_"), target,
                  characteristic_q=2, params={"rolloff": rolloff})


def family_hollow() -> Family:
    """Hollow / woody: only the first few odd harmonics, sharply limited —
    a stopped-pipe / pan-flute character."""
    def target(q_max: int) -> np.ndarray:
        a = np.zeros(q_max, dtype=np.float64)
        for n, w in ((1, 1.0), (3, 0.5), (5, 0.22), (7, 0.08)):
            if n <= q_max:
                a[n - 1] = w
        return a
    return Family("hollow", target, characteristic_q=2)


# ---------------------------------------------------------------------------
# Vowel / formant families (source-filter on a harmonic series)
# ---------------------------------------------------------------------------

# Classic sung-vowel formant centres (Hz) — F1/F2/F3.
_VOWEL_FORMANTS = {
    "a": (800.0, 1150.0, 2900.0),
    "e": (400.0, 1700.0, 2600.0),
    "i": (350.0, 2000.0, 2800.0),
    "o": (450.0, 800.0, 2830.0),
    "u": (325.0, 700.0, 2700.0),
}


def family_vowel(vowel: str = "a", bw: float = 120.0,
                 f_carrier: float = F_REF) -> Family:
    """Sung vowel: harmonic series (1/n source) shaped by three formant
    resonances (Lorentzian bandpass). Referenced to f_carrier for the
    absolute formant frequencies."""
    vowel = vowel.lower()
    if vowel not in _VOWEL_FORMANTS:
        raise ValueError(f"unknown vowel {vowel!r}; choose {list(_VOWEL_FORMANTS)}")
    formants = _VOWEL_FORMANTS[vowel]

    def target(q_max: int) -> np.ndarray:
        n = np.arange(1, q_max + 1, dtype=np.float64)
        freq = n * f_carrier
        source = 1.0 / n
        resp = np.zeros_like(freq)
        for fc in formants:
            resp += 1.0 / (1.0 + ((freq - fc) / bw) ** 2)
        return source * resp
    return Family(f"vowel_{vowel}", target, characteristic_q=1,
                  params={"vowel": vowel, "bw": bw})


# ---------------------------------------------------------------------------
# Inharmonic families (Gaussian-projected, KL-only)
# ---------------------------------------------------------------------------

# Tubular-bell partial ratios (relative to the strike tone) and rough weights.
_BELL_RATIOS = np.array([0.56, 0.92, 1.19, 1.71, 2.00, 2.74, 3.00, 3.76, 4.07])
_BELL_WEIGHTS = np.array([0.6, 1.0, 0.8, 0.5, 0.7, 0.4, 0.5, 0.3, 0.25])


def family_bell(sigma: float = 0.3) -> Family:
    """Tubular bell: classic inharmonic partial set with a strong hum + tap.
    Non-integer positions ⇒ KL-only distance."""
    def target(q_max: int) -> np.ndarray:
        return _gaussian_project(_BELL_RATIOS * 4.0, _BELL_WEIGHTS, q_max, sigma)
        # ×4 lifts the strike-tone onto a useful integer-grid span.
    return Family("bell", target, characteristic_q=None, params={"sigma": sigma})


def family_glass(stretch: float = 1.4, max_n: int = 24,
                 sigma: float = 0.3) -> Family:
    """Glassy / metallic: strongly stretched harmonics n^stretch with a 1/n
    envelope — brighter and more inharmonic than bell_stretched."""
    def target(q_max: int) -> np.ndarray:
        n = np.arange(1, max_n + 1, dtype=np.float64)
        return _gaussian_project(n ** stretch, 1.0 / n, q_max, sigma)
    return Family(f"glass_{stretch:.1f}".replace(".", "_"), target,
                  characteristic_q=None, params={"stretch": stretch})


# ---------------------------------------------------------------------------
# Registry
# ---------------------------------------------------------------------------

def extra_families() -> list[Family]:
    """All the flavour families, ready to drop into search / the bank."""
    return [
        family_pluck(),
        family_brass(),
        family_reed(),
        family_hollow(),
        *(family_vowel(v) for v in ("a", "e", "i", "o", "u")),
        family_bell(),
        family_glass(),
    ]
