#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

namespace brocot::viz
{

/**
    Pure math for CloudView's dynamic layer, split out of the component so it
    is unit-testable without JUCE GUI (same pattern as DensityGrid.h /
    BrocotLattice.h). CloudView owns the pixels; this owns the numbers.
*/

inline constexpr float kTwoPi = 6.28318530717958647692f;

/** Twinkle alpha for a criticality dot: a slow 0.4 Hz sine (period 2.5 s),
    amplitude scaled by the node's crit, dephased per dot by a deterministic
    salt (no RNG — rank * 2654435761u at bake time) so the field shimmers
    instead of pulsing in lockstep. Bounded [0, 0.7]. */
inline float shimmerAlpha (float crit, float phaseSeconds, uint32_t salt)
{
    const float saltPhase = (float) (salt % 1024u) / 1024.0f * kTwoPi;
    const float wave  = 0.5f + 0.5f * std::sin (kTwoPi * 0.4f * phaseSeconds + saltPhase);
    const float alpha = crit * (0.35f + 0.30f * wave);
    return std::clamp (alpha, 0.0f, 0.7f);
}

/** Indices of the n largest values in `crit`, descending; ties keep their
    original index order (stable). n past the end returns everything. */
inline std::vector<int> topCritIndices (const std::vector<float>& crit, int n)
{
    std::vector<int> idx (crit.size());
    std::iota (idx.begin(), idx.end(), 0);
    std::stable_sort (idx.begin(), idx.end(), [&crit] (int a, int b)
                      { return crit[(size_t) a] > crit[(size_t) b]; });
    idx.resize ((size_t) std::clamp (n, 0, (int) crit.size()));
    return idx;
}

} // namespace brocot::viz
