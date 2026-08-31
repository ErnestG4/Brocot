#pragma once

#include "Horizon.h"
#include <cmath>

namespace brocot::field
{

/**
    The map field (MAP-FIELD.md): the perceptual decomposition of the ratio
    axis into basins around ringing rationals, and the beat-rate inversion —
    the horizon programme's primary perceptual deliverable.

    Everything here is the matched-index two-modulator frame (modulators at 1
    and α, both at index I, A = 2·order_bound(I)) the document derives in.
    Scope is inherited from COINCIDENCE-HORIZON.md unchanged: direct channel,
    two sine modulators, ε = 1e-3, α ∈ (1/A, A). Outside that interval the
    parent DEGENERATES — no in-box ringing ratio exists — and everything below
    reports it as p = q = 0 rather than fabricating a nearest ratio.

    The parent is the argmin of gap(α) = min |a₁ + a₂·α| over nonzero
    |a₁|,|a₂| ≤ A: the closest approach to a coincidence, a frequency
    difference between two REAL partials. It minimises denominator-weighted
    error — NOT |α − p/q| — and is provably a continued-fraction convergent of
    α (best approximation of the second kind; Lagrange), with ties broken to
    the smallest denominator, which is exactly what makes the tie land on a
    convergent too. Verified over 37,023 ratios in the ARS programme.

    Wording gates for anything displayed from this header (load-bearing):
    "nearest ringing ratio", "N Hz apart", "fuses" are earned; "heard as a
    detuned p/q" is NOT — separation crosses perceptual regimes within one
    basin, so perceptual language belongs on the SEPARATION (regime bands
    below), never on the region.
*/

struct Parent
{
    int    p { 0 }, q { 0 };   // 0/0 = degenerate (no in-box ringing ratio)
    double gap { 0.0 };        // lattice units: |q·α − p|; multiply by
                               // f_c·r1 for Hz between the two real partials
    bool valid() const noexcept { return p > 0 && q > 0; }
};

/** The argmin above with PER-SIDE bounds — |a₁| ≤ A1, |a₂| ≤ A2, the
    reachable-sideband box exactly as the coincidence derivation bounds it
    (a₁, a₂ are differences of the two operators' sideband orders, so each is
    capped by its own operator's 2·order_bound). The closest-approach
    DEFINITION carries to this box unchanged; the parent-is-a-convergent
    THEOREM is proved for the matched box only, so wording that leans on
    convergence belongs to the matched overload below. O(A2) per call: for
    each denominator the best numerator is round(q·α), and the reduced form of
    any minimiser strictly beats the unreduced one, so scanning q = 1..A2 with
    a strict tie-break to the smaller q lands on the argmin in lowest terms. */
inline Parent parentOf (double alpha, int A1, int A2) noexcept
{
    if (A1 < 1 || A2 < 1 || alpha * A2 <= 1.0 || alpha >= (double) A1)
        return {};

    Parent best;
    double bestErr = -1.0;
    for (int q = 1; q <= A2; ++q)
    {
        int p = (int) std::lround ((double) q * alpha);
        if (p < 1)  p = 1;              // a₁ must be nonzero…
        if (p > A1) p = A1;             // …and in the box: clamp, don't skip —
                                        // the box's best numerator for this q
                                        // is still a candidate
        const double err = std::abs ((double) q * alpha - (double) p);
        if (bestErr < 0.0 || err < bestErr)   // strict: ties keep smallest q
        {
            best = { p, q, err };
            bestErr = err;
        }
    }
    return best;
}

/** Matched box A1 = A2 = A — the frame MAP-FIELD.md derives and verifies. */
inline Parent parentOf (double alpha, int A) noexcept
{
    return parentOf (alpha, A, A);
}

/** Convenience: A from the modulation index, matched-frame. */
inline Parent parentAt (double alpha, double index) noexcept
{
    return parentOf (alpha, 2 * horizon::orderBound (index));
}

/** Separation of the parent's witness partials in Hz. The f_c·r1 factor is
    load-bearing: without it the number is in lattice units and off by a
    factor of the reference frequency (verified 89/89 in brocot_beat_target).
    fcr1 is the reference modulator's absolute frequency, f_carrier · r1. */
inline double separationHz (const Parent& parent, double fcr1) noexcept
{
    return parent.valid() ? parent.gap * fcr1 : 0.0;
}

/** Regime bands on the separation — the honest place for perceptual language
    (MAP-FIELD.md "What the display may say"). Thresholds are the document's
    published bands; they are display vocabulary, not measured constants. */
enum class Band { Fuses, SlowBeat, Rough, Separate };

inline Band bandOf (double sepHz) noexcept
{
    if (sepHz <= 0.0)  return Band::Fuses;
    if (sepHz < 8.0)   return Band::SlowBeat;
    if (sepHz < 20.0)  return Band::Rough;
    return Band::Separate;
}

/**
    Beat-rate targeting: for a parent p/q and a target of δ Hz,

        sep_Hz = f_c·r1·gap    ⟹    α = (p ± δ/(f_c·r1)) / q

    solved, not searched. Each side carries its own verdict because the
    refusal is per-side and fires earlier than intuition suggests (at
    f_c·r1 = 110 Hz, +12 Hz against 8/7 already lands under 7/6): a solution
    is REFUSED unless its basin membership — checked through parentOf, never
    assumed from the algebra — comes back as the requested parent. Refuse,
    don't clamp: a target under a different parent is a different sound.
*/
struct BeatSolution
{
    bool   upOk { false },  downOk { false };
    double up   { 0.0 },    down   { 0.0 };    // α above / below the parent
};

inline BeatSolution solveBeat (const Parent& parent, double deltaHz,
                               double fcr1, double index) noexcept
{
    BeatSolution out;
    if (! parent.valid() || deltaHz < 0.0 || fcr1 <= 0.0)
        return out;

    const int A = 2 * horizon::orderBound (index);
    const double dLattice = deltaHz / fcr1;

    const double cand[2] = { ((double) parent.p + dLattice) / (double) parent.q,
                             ((double) parent.p - dLattice) / (double) parent.q };
    for (int side = 0; side < 2; ++side)
    {
        const auto back = parentOf (cand[side], A);
        const bool ok = back.valid() && back.p == parent.p && back.q == parent.q;
        if (side == 0) { out.upOk = ok;   out.up = cand[side]; }
        else           { out.downOk = ok; out.down = cand[side]; }
    }
    return out;
}

} // namespace brocot::field
