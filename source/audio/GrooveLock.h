#pragma once

#include <cmath>

#include "GrooveField.h"
#include "Path.h"

namespace brocot
{

namespace groove
{
    // Groove-feel defaults — the pre-S4 hardcoded SteerTab constants. The
    // steer.groove* parameters in ParameterTree.cpp take their defaults from
    // here, so old saved states (which lack the params) sound identical.
    // Regression-pinned in SteerLockTests.
    inline constexpr float kDefaultGravity      = 0.6f;
    inline constexpr float kDefaultRestlessness = 0.3f;
    inline constexpr bool  kDefaultLock         = true;

    // Must equal the opN.detune parameter range (±100 cents) — a lock whose
    // required detune exceeds this cannot be represented, so it is refused.
    inline constexpr double kMaxDetuneCents = 100.0;
}

/**
    Lock-to-groove math for steer release (S5), extracted from SteerTab so
    the UI-free test target can exercise it.

    Invariant when ok: walkRatio(path) × 2^(detuneCents/1200) == ratio to
    float precision — writing this path + detune then dropping the steered
    override produces zero audible discontinuity.
*/
struct LockResult
{
    Path  path;
    float detuneCents { 0.0f };
    bool  ok { false };
};

/** Resolve `ratio` to its dominant Farey groove plus an exact cents offset.
    ok=false (never clamped — clamping would break the exactness invariant)
    when: ratio ≤ 0, the dominant groove is further than maxDetuneCents away,
    or the Stern-Brocot descent to the groove's rational fails. */
inline LockResult lockRatioToGroove (const GrooveField& field,
                                     double ratio, double maxDetuneCents)
{
    LockResult result;
    if (! (ratio > 0.0))
        return result;

    const double logR = std::log2 (ratio);
    const auto&  g    = field.nearestGroove ((float) logR);

    // Cents from the exact integer rational (not the groove's float logRatio)
    // so the round trip path→ratio→detune is exact in double.
    const double cents = 1200.0 * (logR - std::log2 ((double) g.p / (double) g.q));
    if (std::abs (cents) > maxDetuneCents)
        return result;

    Path path;
    if (! Path::fromRatio (g.p, g.q, path))
        return result;

    result.path        = path;
    result.detuneCents = (float) cents;
    result.ok          = true;
    return result;
}

} // namespace brocot
