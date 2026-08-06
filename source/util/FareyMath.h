#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "../audio/Bessel.h"
#include "../audio/BrocotLattice.h"
#include "../audio/Path.h"

namespace brocot::viz
{

/**
    Pure decision/number logic for FareyView, split out of the component so it
    is unit-testable without JUCE GUI (same pattern as util/CloudMath.h): the
    view owns the pixels; this owns the numbers. juce_core only (Path).
*/

/** Longest prefix of `p` whose resolved denominator q stays ≤ qMax, so the
    navigation cursor sits on the drawn wing and its lattice math stays cheap.
    hardDepth bounds the scan for pathological inputs. */
inline Path cappedPrefix (const Path& p, int qMax, int hardDepth = 40)
{
    Path best;
    const int maxLen = std::min (p.depth(), hardDepth);
    for (int len = 1; len <= maxLen; ++len)
    {
        const Path cand = p.prefix (len);
        int pp, qq;
        cand.walk (pp, qq);
        if (qq > qMax) break;
        best = cand;
    }
    return best;
}

/** Fractional part of p/q — the butterfly's flux coordinate θ ∈ [0, 1). */
inline double fracTheta (int p, int q)
{
    const double r = (double) p / (double) q;
    return r - std::floor (r);
}

/** Diverging hue for a signed Chern integer: 0 → 0.5, ± spread → 0.25/0.75,
    clamped [0, 1]. `spread` is the |chern| that reaches the quarter-turn —
    FareyView passes its kQMax today, which compresses small |c| into
    neighbouring hues; widening is now a call-site change (the legend-
    compression note in FareyView::paint). */
inline float chernHue (int chern, int spread)
{
    const float t = 0.5f + (float) chern / (float) (2 * std::max (1, spread));
    return std::clamp (t, 0.0f, 1.0f);
}

/** pickProminentChild result. `valid` false = neither child is under the
    q guard (no pick — the caller hints instead of descending). */
struct ChildPick
{
    char   side { 'L' };        // 'L' or 'R'
    double prominence { 0.0 };  // widest-gap width of the picked child
    int    chern { 0 };         // Chern of that widest gap
    bool   valid { false };
};

/** The more prominent (widest widest-gap) L/R child of `cursor`, ONE
    lattice::gaps eigensolve per child, skipping children with q > qGuard
    (message-thread cost rule — see FareyView::kChildQMax). Ties and the
    all-zero-prominence case pick L (deterministic). */
inline ChildPick pickProminentChild (const Path& cursor, int qGuard = 64)
{
    ChildPick best;
    for (const char side : { 'L', 'R' })
    {
        const Path child = cursor.extended ((juce::juce_wchar) side);
        int p, q;
        child.walk (p, q);
        if (q > qGuard) continue;

        double prom = 0.0; int chern = 0;
        for (const auto& gp : lattice::gaps (p, q))
            if (gp.width > prom) { prom = gp.width; chern = gp.chern; }

        if (! best.valid || prom > best.prominence)
            best = { side, prom, chern, true };
    }
    return best;
}

/** First Bessel landmark strictly above `depth` (epsilon 1e-4 so stepping
    from exactly-on-a-landmark moves on), or nullptr past the table's end.
    bessel::kLandmarks is sorted ascending. */
inline const bessel::Landmark* nextLandmark (float depth)
{
    for (const auto& lm : bessel::kLandmarks)
        if (lm.value > depth + 1.0e-4f)
            return &lm;
    return nullptr;
}

/** Last Bessel landmark strictly below `depth`, or nullptr before the
    table's start. */
inline const bessel::Landmark* prevLandmark (float depth)
{
    const bessel::Landmark* best = nullptr;
    for (const auto& lm : bessel::kLandmarks)
        if (lm.value < depth - 1.0e-4f)
            best = &lm;
    return best;
}

/** Per-op state the partial plans decide over — plain struct so tests need
    no processor. `position` mirrors the op position IntParam (0..32,
    0 = root of the shared path). */
struct OpPartial
{
    bool enabled { false };
    int  position { 0 };
};

inline constexpr int kMaxPosition = 32;   // the position IntParam's ceiling

/** Add-a-partial plan (shared mode): enable the FIRST disabled op, at
    position = (max position among enabled ops) + 1, capped at kMaxPosition;
    with nothing enabled the first partial lands at position 1 (one step
    along the shared path — position 0 is the root). `valid` false = every
    op already enabled. */
struct AddPlan { int opIndex { -1 }; int position { 0 }; bool valid { false }; };

inline AddPlan addPartialPlan (const std::vector<OpPartial>& ops)
{
    AddPlan plan;
    int maxPos = 0;
    for (size_t i = 0; i < ops.size(); ++i)
    {
        if (ops[i].enabled)
            maxPos = std::max (maxPos, ops[i].position);
        else if (! plan.valid)
        {
            plan.opIndex = (int) i;
            plan.valid = true;
        }
    }
    plan.position = std::min (maxPos + 1, kMaxPosition);
    return plan;
}

/** Remove-a-partial plan: the enabled op with the HIGHEST position — the
    outermost partial (the same op the landmark-step gesture acts on; the
    two gestures deliberately share a target). Ties keep the first such op.
    `valid` false = nothing enabled. */
struct RemovePlan { int opIndex { -1 }; int position { 0 }; bool valid { false }; };

inline RemovePlan removePartialPlan (const std::vector<OpPartial>& ops)
{
    RemovePlan plan;
    for (size_t i = 0; i < ops.size(); ++i)
        if (ops[i].enabled && (! plan.valid || ops[i].position > plan.position))
            plan = { (int) i, ops[i].position, true };
    return plan;
}

} // namespace brocot::viz
