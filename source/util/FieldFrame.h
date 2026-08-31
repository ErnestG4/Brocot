#pragma once

#include <cmath>
#include <vector>

#include "../audio/MapField.h"

namespace brocot::fieldframe
{

/**
    The pair-field frame: MAP-FIELD.h machinery applied to a real patch.

    A pair readout takes two operators as (reference, other) and reports the
    nearest reachable coincidence of THAT PAIR — parent, separation in Hz at
    the played carrier, regime band. Per-pair statements stay true at any
    operator count: "these two operators' sidebands land together / miss by
    5.5 Hz" is a claim about a real pair of partials, and adding more
    operators adds MORE coincidences, never removes these. What does not
    survive N > 2 is the negative ("nothing above the pair horizon
    coincides"), which nothing here asserts.

    Waveforms: the box below is the sine-channel box. Triangle/square/saw
    modulators add harmonic-multiple channels on top (support extension —
    measured, only ever additive), so for them the readout names one real
    coincidence channel, not necessarily the nearest. Amplitudes are out of
    scope throughout: separation predicts the beat rate, not its salience.

    Wording gate (inherited): "nearest ringing ratio", "N Hz", regime band —
    yes; "heard as a detuned p/q" — no.
*/

struct OpState
{
    int    p { 0 }, q { 0 };      // exact base rational (Path::walk)
    double detuneCents { 0.0 };
    double index { 0.0 };         // FM modulation index (depth)

    bool valid() const noexcept { return p > 0 && q > 0; }
    double effectiveRatio() const noexcept
    {
        return ((double) p / (double) q) * std::exp2 (detuneCents / 1200.0);
    }
};

/** The reference operator of a patch: simplest base ratio (smallest q, ties
    to smallest p) — the same "family anchor" idea SteerTab uses, stated on
    the rational rather than the path string. Returns -1 when nothing valid. */
inline int referenceIndex (const std::vector<OpState>& ops) noexcept
{
    int best = -1;
    for (int i = 0; i < (int) ops.size(); ++i)
    {
        if (! ops[(size_t) i].valid()) continue;
        if (best < 0
            || ops[(size_t) i].q <  ops[(size_t) best].q
            || (ops[(size_t) i].q == ops[(size_t) best].q
                && ops[(size_t) i].p < ops[(size_t) best].p))
            best = i;
    }
    return best;
}

struct PairReadout
{
    bool          valid { false };   // false = degenerate (no reachable pair coincidence in range)
    field::Parent parent;            // nearest reachable coincidence, as p/q of alpha
    double        alpha { 0.0 };     // effective r_other / r_ref
    double        sepHz { 0.0 };     // witness-partial separation at carrierHz
    field::Band   band  { field::Band::Fuses };
};

/** The readout for one (reference, other) pair at a given carrier frequency.
    Box bounds are per-side — each operator's own 2·order_bound — exactly as
    the coincidence derivation caps the order differences. */
inline PairReadout pairReadout (const OpState& ref, const OpState& other,
                                double carrierHz) noexcept
{
    PairReadout out;
    if (! ref.valid() || ! other.valid() || carrierHz <= 0.0)
        return out;

    const int A1 = 2 * horizon::orderBound (ref.index);
    const int A2 = 2 * horizon::orderBound (other.index);
    const double r1 = ref.effectiveRatio();
    out.alpha = other.effectiveRatio() / r1;

    out.parent = field::parentOf (out.alpha, A1, A2);
    if (! out.parent.valid())
        return out;

    out.sepHz = field::separationHz (out.parent, carrierHz * r1);
    out.band  = field::bandOf (out.sepHz);
    out.valid = true;
    return out;
}

/** Beat-rate targeting, solved as a detune for `other` (the reference stays
    put): which detune-cents values give the pair a δ Hz beat against its
    CURRENT parent? Closed form, per-side basin refusal through parentOf —
    refuse, don't clamp: a target landing under a different parent is a
    different sound (MAP-FIELD.md item 5). */
struct BeatCents
{
    bool   upOk { false },  downOk { false };
    double upCents { 0.0 }, downCents { 0.0 };
};

inline BeatCents solveBeatCents (const OpState& ref, const OpState& other,
                                 double carrierHz, double deltaHz) noexcept
{
    BeatCents out;
    const auto now = pairReadout (ref, other, carrierHz);
    if (! now.valid || deltaHz < 0.0)
        return out;

    const int A1 = 2 * horizon::orderBound (ref.index);
    const int A2 = 2 * horizon::orderBound (other.index);
    const double r1 = ref.effectiveRatio();
    const double dLattice = deltaHz / (carrierHz * r1);
    const double baseRatio = (double) other.p / (double) other.q;

    const double candAlpha[2] = {
        ((double) now.parent.p + dLattice) / (double) now.parent.q,
        ((double) now.parent.p - dLattice) / (double) now.parent.q
    };
    for (int side = 0; side < 2; ++side)
    {
        const auto back = field::parentOf (candAlpha[side], A1, A2);
        const bool ok = back.valid()
                     && back.p == now.parent.p && back.q == now.parent.q;
        const double cents = 1200.0 * std::log2 (candAlpha[side] * r1 / baseRatio);
        if (side == 0) { out.upOk = ok;   out.upCents = cents; }
        else           { out.downOk = ok; out.downCents = cents; }
    }
    return out;
}

} // namespace brocot::fieldframe
