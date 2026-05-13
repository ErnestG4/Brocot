"""Configuration representation for Phase 3 search.

A Config is the 8-operator FM state Brocot's audio engine consumes —
each modulator has a Stern-Brocot path (L/R string), a modulation
index (depth), and an enabled flag. Search operates over the space of
Configs; .brocot v2 JSON is generated from search results.

This is intentionally a different shape from Brocot's APVTS state.
The synth doesn't run Python; this struct is the offline tool's
canonical type for "an 8-op FM patch", convertible to .brocot JSON by
the writer (P3.5).
"""

from __future__ import annotations

from dataclasses import dataclass, field, replace
from typing import Sequence

import numpy as np

from phase3.partial_prediction import Spectrum, predict_partials


# ---------------------------------------------------------------------------
# Stern-Brocot path -> rational
# ---------------------------------------------------------------------------

def walk_path(path: str) -> tuple[int, int]:
    """Return (p, q) for the rational at the end of a Stern-Brocot path.
    The empty path is 1/1; 'R' is 2/1; 'L' is 1/2; 'RLL' is 4/3; etc.
    Always returns coprime p, q in lowest terms (by mediant construction).
    """
    left_p, left_q = 0, 1
    right_p, right_q = 1, 0
    cur_p, cur_q = 1, 1
    for s in path:
        if s == 'L':
            right_p, right_q = cur_p, cur_q
        elif s == 'R':
            left_p, left_q = cur_p, cur_q
        else:
            raise ValueError(f"path must contain only L/R chars, got {s!r}")
        cur_p = left_p + right_p
        cur_q = left_q + right_q
    return cur_p, cur_q


def path_ratio(path: str) -> float:
    p, q = walk_path(path)
    return p / q


# ---------------------------------------------------------------------------
# Operator + Config dataclasses
# ---------------------------------------------------------------------------

@dataclass
class ModConfig:
    path: str = ""        # Stern-Brocot L/R path; "" = 1/1
    depth: float = 0.0    # modulation index I, range [0, 8]
    enabled: bool = False


@dataclass
class Config:
    mods: list[ModConfig] = field(default_factory=list)
    f_carrier: float = 220.0

    @classmethod
    def silent(cls, n_mods: int = 8, f_carrier: float = 220.0) -> "Config":
        return cls(mods=[ModConfig() for _ in range(n_mods)],
                   f_carrier=f_carrier)

    def copy(self) -> "Config":
        return Config(
            mods=[replace(m) for m in self.mods],
            f_carrier=self.f_carrier,
        )

    def ratios_and_depths(self) -> tuple[list[float], list[float]]:
        """Only the enabled mods' (ratio, depth) pairs."""
        rs, ds = [], []
        for m in self.mods:
            if m.enabled:
                rs.append(path_ratio(m.path))
                ds.append(m.depth)
        return rs, ds

    def predict(
        self,
        threshold_db: float = -50.0,
        eps: float = 1e-3,
        max_partials: int | None = None,
    ) -> Spectrum:
        rs, ds = self.ratios_and_depths()
        return predict_partials(
            ratios=rs, depths=ds, f_carrier=self.f_carrier,
            threshold_db=threshold_db,
            eps=eps,
            max_partials=max_partials,
        )


# ---------------------------------------------------------------------------
# Bessel landmarks — used by the depth-sampling bias
# ---------------------------------------------------------------------------

BESSEL_LANDMARKS = (
    1.841,  # J1 peak
    2.405,  # J0 zero
    3.054,  # J2 peak
    3.832,  # J1 zero
    4.201,  # J3 peak
    5.136,  # J2 zero
    5.318,  # J4 peak
    5.520,  # J0 zero #2
    6.416,  # J5 peak
    7.016,  # J1 zero #2
)
