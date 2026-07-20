#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>

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

/** Bessel function of the first kind J_m(x) for integer order m.

    FM sideband amplitudes are exactly these: a carrier phase-modulated at
    index I puts a component of weight J_m(I) at carrier ± m·f_mod. The landmark
    table above records where individual J_m peak/vanish; this evaluates them so
    a coherence meter can weight sidebands by their real energy.

    Miller's algorithm: downward recurrence from a high order (stable for all
    m, unlike upward recurrence which blows up for m > x), normalised by the
    sum rule J_0 + 2(J_2 + J_4 + …) = 1. Accurate to well past what a meter
    needs across the audible index range (x ≲ 8). Self-contained — no external
    special-function dependency. */
inline float J (int m, float xf) noexcept
{
    const int n = std::abs (m);
    const double x = (double) xf;

    if (x == 0.0)
        return n == 0 ? 1.0f : 0.0f;

    // Start order comfortably above both n and x, and even (the sum rule pairs
    // even indices).
    int start = n + 16 + 2 * (int) std::ceil (std::sqrt (40.0 * (n + 1)));
    start += (start & 1);

    double bjp = 0.0;    // f_{k+1}
    double bj  = 1.0;    // f_k, seeded arbitrarily; normalised out at the end
    double sum = 0.0;    // Σ even-index f (includes f_0)
    double ans = 0.0;    // f_n

    for (int k = start; k >= 1; --k)
    {
        const double bjm = (2.0 * k / x) * bj - bjp;   // f_{k-1}
        bjp = bj;
        bj  = bjm;
        if (std::abs (bj) > 1.0e10)                     // rescale to avoid overflow
        {
            bj  *= 1.0e-10;
            bjp *= 1.0e-10;
            sum *= 1.0e-10;
            ans *= 1.0e-10;
        }
        if (((k - 1) & 1) == 0) sum += bj;             // f_0, f_2, f_4, …
        if ((k - 1) == n)       ans = bj;
    }

    const double norm = 2.0 * sum - bj;                // bj == f_0 here
    double result = ans / norm;
    if (m < 0 && (n & 1)) result = -result;            // J_{-n} = (-1)^n J_n
    return (float) result;
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
