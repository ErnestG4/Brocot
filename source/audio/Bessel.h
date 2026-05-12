#pragma once

#include <juce_core/juce_core.h>
#include <array>

namespace brocot::bessel
{

/**
    BROCOT-SPEC.md §7 calls Bessel landmarks "the scale degrees of FM
    amplitude space" — at each landmark a specific sideband order dominates
    or vanishes. The depth slider shows ticks at these values; the regime
    label and nearest-landmark display read from this table.

    Source of truth: resources/tables/bessel_landmarks.json. The numeric
    values here MUST match the JSON. Hardcoding them avoids parsing the
    JSON at startup but means the two need to stay in sync (a unit test
    enforces that in Slice 14 when the JSON file lands).
*/

struct Landmark
{
    enum class Kind { Peak, Zero };
    float       value;     // modulation index where this landmark occurs
    Kind        kind;
    int         n;         // J_n index
    const char* label;     // short display name, e.g. "J1+"
    bool        major;     // n ≤ 2 → drawn taller
};

// Major flag = `n ≤ 2` per the prototype's drawing rule — the first few
// sidebands are the perceptually dominant ones, so their landmarks get
// taller ticks.
inline constexpr std::array<Landmark, 10> kLandmarks {{
    { 1.8412f, Landmark::Kind::Peak, 1, "J1+",   true  },
    { 2.4048f, Landmark::Kind::Zero, 0, "J0_0",  true  },
    { 3.0542f, Landmark::Kind::Peak, 2, "J2+",   true  },
    { 3.8317f, Landmark::Kind::Zero, 1, "J1_0",  true  },
    { 4.2012f, Landmark::Kind::Peak, 3, "J3+",   false },
    { 5.1356f, Landmark::Kind::Zero, 2, "J2_0",  true  },
    { 5.3176f, Landmark::Kind::Peak, 4, "J4+",   false },
    { 5.5201f, Landmark::Kind::Zero, 0, "J0_0b", true  },
    { 6.4156f, Landmark::Kind::Peak, 5, "J5+",   false },
    { 7.0156f, Landmark::Kind::Zero, 1, "J1_0b", true  },
}};

inline constexpr float kDepthMax = 8.0f;

/** Returns a pointer to the closest landmark within `tolerance` of `depth`,
    or nullptr if none qualifies. */
inline const Landmark* nearest (float depth, float tolerance = 0.12f) noexcept
{
    const Landmark* best = nullptr;
    float bestDist = tolerance;
    for (const auto& lm : kLandmarks)
    {
        const float d = std::abs (depth - lm.value);
        if (d < bestDist)
        {
            bestDist = d;
            best = &lm;
        }
    }
    return best;
}

/** Coarse regime label per the spec's depth-range descriptions. */
inline juce::String regimeLabel (float depth) noexcept
{
    if (depth < 0.5f) return "tongue edge";
    if (depth < 3.0f) return "near-locked";
    if (depth < 5.0f) return "rich sidebands";
    return "clangy";
}

} // namespace brocot::bessel
