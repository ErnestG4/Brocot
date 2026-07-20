"""Brocot ↔ Hofstadter-butterfly lattice math (BROCOT-SPEC.md §2).

Python mirror of the C++ ``source/audio/BrocotLattice.h`` (the two share the
§6 regression *facts*, not a module — C++ and Python can't literally share
code). This is the offline surface: the map-vs-butterfly study
(``butterfly_map_study.py``) calls it.

The navigation map's ratio axis θ = p/q is the almost-Mathieu (Harper)
butterfly's flux axis. Four quantities are kept strictly distinct (§4.4):

- ``simplicity(q)`` = q — lower is simpler/more consonant. NOT the
  1/(p·q) groove weight.
- ``cf_depth(p, q)`` = sum of continued-fraction partial quotients — the
  CF-sum criticality/approximability proxy. Deliberately the CF-sum and
  nothing else (NOT liminf-K / Borel–Bernstein, §4.2).
- ``prominence(p, q, lam)`` = width of the widest almost-Mathieu gap — the
  stability/plateau proxy.
- ``gaps(...).chern`` = the Wannier/Středa topological integer of a gap.

STRUCTURAL, NOT LITERAL (§1, §4.1): band/gap structure is an organizing /
annotation layer over ratio space and a prominence proxy — a played FM ratio
is not itself "a band or a gap". λ is a parameter (default 1), not a
constant (§4.5).
"""

from __future__ import annotations

from dataclasses import dataclass
from math import gcd
from typing import List

import numpy as np


# ---------------------------------------------------------------------------
# Number-theoretic helpers
# ---------------------------------------------------------------------------

def simplicity(q: int) -> int:
    """simplicity = q (the denominator). Lower = simpler = more consonant."""
    return q


def cf_terms(p: int, q: int) -> List[int]:
    """Continued-fraction partial quotients of p/q via the Euclidean algorithm.

    e.g. 4/3 → [1, 3], 9/7 → [1, 3, 2], 1/2 → [0, 2].
    """
    terms: List[int] = []
    while q != 0:
        terms.append(p // q)
        p, q = q, p % q
    return terms


def cf_depth(p: int, q: int) -> int:
    """Sum of continued-fraction partial quotients (the CF-sum criticality
    proxy). Convention-independent — [.., a] and [.., a-1, 1] have equal sums —
    and equals a proper Stern-Brocot path's L/R depth + 1."""
    return sum(cf_terms(p, q))


def modinv(a: int, m: int) -> int:
    """Modular inverse of a mod m (a, m coprime), in [0, m). 0 for m == 1."""
    if m == 1:
        return 0
    return pow(a % m, -1, m)


def fold_chern(t: int, q: int) -> int:
    """Fold a Wannier index t ∈ [0, q) into (−q/2, q/2] (upper inclusive)."""
    return t - q if 2 * t > q else t


# ---------------------------------------------------------------------------
# Almost-Mathieu (Harper) spectrum
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class Band:
    lo: float
    hi: float


@dataclass(frozen=True)
class Gap:
    lo: float
    hi: float
    width: float
    chern: int


def _harper_corner(p: int, q: int, lam: float, offset: float, wrap: float) -> np.ndarray:
    """Sorted eigenvalues of the q×q Harper matrix at one magnetic-BZ corner:
    diagonal 2λ cos(2π m p/q + offset), unit off-diagonals, periodic corner
    element = wrap (±1)."""
    m = np.arange(q)
    diag = 2.0 * lam * np.cos(2.0 * np.pi * m * p / q + offset)
    h = np.diag(diag).astype(np.float64)
    if q == 1:
        return np.sort(np.linalg.eigvalsh(h))
    idx = np.arange(q - 1)
    h[idx, idx + 1] += 1.0
    h[idx + 1, idx] += 1.0
    h[0, q - 1] += wrap
    h[q - 1, 0] += wrap
    return np.sort(np.linalg.eigvalsh(h))


def almost_mathieu_bands(p: int, q: int, lam: float = 1.0) -> List[Band]:
    """The q bands of the θ = p/q almost-Mathieu spectrum at coupling λ.
    Band j = [min, max] over the 4 magnetic-BZ corners of the j-th sorted
    eigenvalue (BROCOT-SPEC.md §6)."""
    corners = [(0.0, +1.0), (0.0, -1.0), (np.pi, +1.0), (np.pi, -1.0)]
    ev = np.stack([_harper_corner(p, q, lam, off, wr) for off, wr in corners])  # (4, q)
    lo = ev.min(axis=0)
    hi = ev.max(axis=0)
    return [Band(float(lo[j]), float(hi[j])) for j in range(q)]


def gaps(p: int, q: int, lam: float = 1.0, eps: float = 1e-6) -> List[Gap]:
    """Spectral gaps of θ = p/q: for j = 1…q−1, gap = [bands[j-1].hi,
    bands[j].lo] with zero-width gaps dropped. Each carries its width and
    chern = fold((j · modinv(p, q)) mod q)."""
    if q < 2:
        return []
    bands = almost_mathieu_bands(p, q, lam)
    inv = modinv(p, q)
    out: List[Gap] = []
    for j in range(1, q):
        lo = bands[j - 1].hi
        hi = bands[j].lo
        width = hi - lo
        if width <= eps:
            continue
        out.append(Gap(lo, hi, width, fold_chern((j * inv) % q, q)))
    return out


def chern_labels(p: int, q: int, lam: float = 1.0) -> List[int]:
    """Convenience: the Chern integers of the nonzero-width gaps, in order."""
    return [g.chern for g in gaps(p, q, lam)]


def prominence(p: int, q: int, lam: float = 1.0) -> float:
    """prominence = width of the widest gap (0 if none). A single per-ratio
    scalar; the stable-plateau proxy. Structural only (§4.1)."""
    gs = gaps(p, q, lam)
    return max((g.width for g in gs), default=0.0)
