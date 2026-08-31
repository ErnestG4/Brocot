"""Sealed census: does a per-modulator sine fold act as a continuous
q-side horizon extension?

THE QUESTION BEING SETTLED (pre-registered; this file is committed before
any output of it exists, per the ARS sealed-generator discipline):

    A wavefolder on a sine modulator, s -> sin(beta * s), turns the
    modulator into an odd harmonic stack with amplitudes 2*J_k(beta)
    (k odd).  COINCIDENCE-HORIZON.md measured, for DISCRETE stacks, that
    a stack extends the coincidence horizon on the DENOMINATOR side only
    (p <= 2B always; q grows with stack extent).  If that carries to the
    fold's continuous, amplitude-tapered stack, fold depth is a
    CONTINUOUS resolution control on the q-side of ratio space -- a
    reason to ship a fold knob.  If it does not carry, the knob is not
    the feature it claims to be and must not ship on this rationale.

SCOPE, stated before running:
  * Support-level enumeration in exact integer arithmetic -- the same
    methodological layer as the horizon doc's ratiopinned enumeration,
    extended to amplitude-tapered stacks.  NOT a rendered-audio result;
    render verification is future work and this census cannot license
    audibility claims.
  * Two-operator frame: a sine reference modulator at ratio 1, the
    folded modulator at alpha = p/q, both at modulation index I.
  * The fold is normalised to unit time-domain peak before the index is
    applied (as an engine knob would be), so harmonic k sees an
    effective index I * a_k / c(beta).
  * Coincidences are counted on the (n_ref, M) lattice, where M is the
    folded modulator's total harmonic-weighted order.  This QUOTIENTS
    the universal self-cancellation family (3*alpha - 3*(1*alpha) = 0
    for any alpha) by construction -- the defect the ARS programme hit
    three times is unrepresentable here, and arm A4 red-paths it anyway.

THE ARMS (each can fail; bars fixed here, before any run):

  A1  ASYMMETRY.  At every beta on the grid, the maximum ringing
      numerator is <= 2*order_bound(I).  Bar: zero violations.
  A1r RIVAL (must FAIL for the shipping claim to mean anything): "the
      fold does not extend the horizon at all" -- max ringing
      denominator <= 2*order_bound(I) at every beta >= 1.0.  If this
      rival SURVIVES, the fold is inert and the verdict is NO_SHIP
      regardless of the other arms.
  A2  CONTINUOUS EXTENSION.  The maximum ringing denominator takes at
      least 3 distinct values across the beta grid at I = 0.9.  Bar:
      >= 3 (a knob that flips once is a switch, not an axis).
  A3  NESTED GROWTH.  For consecutive grid betas, the ringing set at
      the larger beta contains the set at the smaller, at every step
      and both indices.  This arm is allowed to fail without killing
      the ship verdict: monotonicity bounds the KNOB RANGE (verdict
      SHIP_RESTRICTED_RANGE, with the largest monotone prefix named),
      because a knob that un-rings ratios mid-sweep is a different UI
      contract.  (J_k(beta) is not monotone, so this is a real risk,
      not a formality.)
  A4  RED PATH.  alpha = 10946/6765 (a golden-ratio convergent, q far
      beyond any reachable order difference) must not ring at any beta.
      A universal-family style bug fires at every alpha; this catches
      it.  Bar: zero rings.

  Not an arm, and why: "same harmonic support => same ringing set" was
  considered and dropped as INERT -- with amplitude-tapered stacks the
  per-harmonic order bounds depend on the amplitudes, so the premise
  cannot be matched across different amplitude vectors; an arm that
  cannot fire is not evidence (non-evidence-as-verdict rule).

VERDICT vocabulary (fixed here): SHIP_CONTINUOUS_EXTENSION (A1 pass,
A1r fails as required, A2 pass, A3 pass, A4 pass); SHIP_RESTRICTED_RANGE
(same but A3 fails; monotone prefix reported); NO_SHIP (anything else).

Grid (fixed here, not scanned): beta in {0.0, 0.25, ..., 2.5} (11
points), I in {0.9, 2.0}, Stern-Brocot nodes with q <= 39 in
[0.20, 1.40] (the horizon doc's widened stack window).  epsilon = 1e-3
throughout, order_bound's own convention.

Output: derived/wavefold_census.json.  Exit code 0 only if the verdict
is a SHIP_* verdict AND every REQUIRED arm scored as its bar demands.
"""

from __future__ import annotations

import itertools
import json
import sys
from fractions import Fraction
from math import gcd
from pathlib import Path

import numpy as np
from scipy.special import jv

sys.path.insert(0, str(Path(__file__).resolve().parent))
from partial_prediction import order_bound  # noqa: E402

EPS = 1e-3
BETAS = [0.25 * i for i in range(11)]          # 0.0 .. 2.5
INDICES = [0.9, 2.0]
Q_MAX = 39
ALPHA_LO, ALPHA_HI = 0.20, 1.40
K_MAX = 19                                     # odd harmonics considered
RED_ALPHA = (10946, 6765)                      # F_21/F_20, convergent of phi


def fold_harmonics(beta: float) -> dict[int, float]:
    """Odd-harmonic amplitudes of sin(beta*sin(t)), unit time-domain peak.

    sin(beta*sin t) = 2*sum_{k odd} J_k(beta) sin(k t); peak c(beta) =
    max_t |sin(beta sin t)| = sin(beta) for beta <= pi/2 else 1.
    beta = 0 is the identity fold: a pure sine, amplitude 1.
    """
    if beta <= 0.0:
        return {1: 1.0}
    c = np.sin(beta) if beta <= np.pi / 2.0 else 1.0
    out: dict[int, float] = {}
    for k in range(1, K_MAX + 1, 2):
        a = abs(2.0 * jv(k, beta)) / c
        if a > 0.0:
            out[k] = a
    return out


def reachable_orders(I: float, harmonics: dict[int, float]) -> set[int]:
    """All M = sum_k m_k * k with |m_k| <= order_bound(I * a_k)."""
    sets: list[list[int]] = []
    for k, a in sorted(harmonics.items()):
        bk = order_bound(I * a, eps=EPS)
        if bk > 0:
            sets.append([m * k for m in range(-bk, bk + 1)])
    reach = {0}
    for contrib in sets:
        reach = {r + c for r in reach for c in contrib}
    return reach


def ring(p: int, q: int, b_ref: int, delta_m: set[int]) -> bool:
    """Exact: exists (dn, dM) != (0,0), |dn| <= 2*b_ref, dM in delta_m,
    with dn*q == -dM*p  <=>  exists m != 0: |m*p| <= 2*b_ref and m*q in
    delta_m (gcd(p,q)=1 forces dn = m*p, dM = -m*q)."""
    m = 1
    while m * p <= 2 * b_ref:
        if m * q in delta_m or -m * q in delta_m:
            return True
        m += 1
    return False


def sb_nodes() -> list[tuple[int, int]]:
    nodes = []
    for q in range(1, Q_MAX + 1):
        for p in range(1, int(ALPHA_HI * q) + 1):
            if ALPHA_LO * q <= p <= ALPHA_HI * q and gcd(p, q) == 1:
                nodes.append((p, q))
    return nodes


def main() -> int:
    nodes = sb_nodes()
    result: dict = {
        "sealed": {
            "betas": BETAS, "indices": INDICES, "q_max": Q_MAX,
            "alpha_range": [ALPHA_LO, ALPHA_HI], "eps": EPS,
            "k_max": K_MAX, "n_nodes": len(nodes),
            "red_alpha": list(RED_ALPHA),
        },
        "per_cell": [],
    }

    ring_sets: dict[tuple[float, float], frozenset] = {}
    for I in INDICES:
        b_ref = order_bound(I, eps=EPS)
        for beta in BETAS:
            harm = fold_harmonics(beta)
            reach = reachable_orders(I, harm)
            delta_m = {a - b for a in reach for b in reach if a != b}
            rs = frozenset((p, q) for (p, q) in nodes
                           if ring(p, q, b_ref, delta_m))
            ring_sets[(I, beta)] = rs
            result["per_cell"].append({
                "I": I, "beta": beta, "b_ref": b_ref,
                "harmonic_support": sorted(harm),
                "n_ringing": len(rs),
                "max_num": max((p for p, _ in rs), default=0),
                "max_den": max((q for _, q in rs), default=0),
                "red_alpha_rings": ring(*RED_ALPHA, b_ref, delta_m),
            })

    cells = result["per_cell"]

    # A1 — asymmetry: max ringing numerator <= 2*b_ref everywhere.
    a1_viol = [c for c in cells if c["max_num"] > 2 * c["b_ref"]]
    a1_pass = len(a1_viol) == 0

    # A1r — rival "fold is inert": max den <= 2*b_ref at every beta >= 1.
    rival_cells = [c for c in cells if c["beta"] >= 1.0]
    a1r_survives = all(c["max_den"] <= 2 * c["b_ref"] for c in rival_cells)

    # A2 — continuous extension: >= 3 distinct max_den at I = 0.9.
    dens_09 = sorted({c["max_den"] for c in cells if c["I"] == 0.9})
    a2_pass = len(dens_09) >= 3

    # A3 — nested growth per consecutive beta, both indices.
    a3_breaks: list[dict] = []
    for I in INDICES:
        for b_prev, b_next in itertools.pairwise(BETAS):
            missing = ring_sets[(I, b_prev)] - ring_sets[(I, b_next)]
            if missing:
                a3_breaks.append({"I": I, "from_beta": b_prev,
                                  "to_beta": b_next,
                                  "n_lost": len(missing),
                                  "lost_sample": sorted(missing)[:5]})
    a3_pass = len(a3_breaks) == 0
    # Largest beta prefix monotone at BOTH indices (for RESTRICTED_RANGE).
    first_break = min((b["to_beta"] for b in a3_breaks), default=None)
    monotone_prefix = (BETAS[-1] if first_break is None
                       else max(b for b in BETAS if b < first_break))

    # A4 — red path.
    a4_pass = not any(c["red_alpha_rings"] for c in cells)

    if a1_pass and (not a1r_survives) and a2_pass and a4_pass:
        verdict = ("SHIP_CONTINUOUS_EXTENSION" if a3_pass
                   else "SHIP_RESTRICTED_RANGE")
    else:
        verdict = "NO_SHIP"

    result["arms"] = {
        "A1_asymmetry": {"pass": a1_pass, "violations": a1_viol},
        "A1r_rival_inert_fold": {"survives": a1r_survives,
                                 "required": "must fail"},
        "A2_continuous_extension": {"pass": a2_pass,
                                    "distinct_max_den_at_0.9": dens_09},
        "A3_nested_growth": {"pass": a3_pass, "breaks": a3_breaks,
                             "monotone_beta_prefix": monotone_prefix},
        "A4_red_path": {"pass": a4_pass},
    }
    result["verdict"] = verdict

    out = Path(__file__).resolve().parent / "derived" / "wavefold_census.json"
    out.parent.mkdir(exist_ok=True)
    out.write_text(json.dumps(result, indent=1))
    print(f"verdict: {verdict}")
    for name, arm in result["arms"].items():
        print(f"  {name}: {arm.get('pass', arm.get('survives'))}")
    print(f"  monotone beta prefix: {monotone_prefix}")
    print(f"wrote {out}")
    return 0 if verdict.startswith("SHIP") else 1


if __name__ == "__main__":
    raise SystemExit(main())
