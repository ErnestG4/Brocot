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

/** Minimal 2D point so the header stays JUCE-free; CloudView converts. */
struct Vec2 { float x { 0.0f }, y { 0.0f }; };

/** Control point for a quadratic wormhole arc between p1 and p2: offset from
    the chord midpoint, perpendicular to the chord, magnitude bulge·|chord|,
    bowing AWAY from `centre` (so arcs across the map bow outward instead of
    piling up in the middle). Degenerate p1 == p2 → p1. */
inline Vec2 arcControl (Vec2 p1, Vec2 p2, Vec2 centre, float bulge = 0.18f)
{
    const float dx = p2.x - p1.x, dy = p2.y - p1.y;
    const float len = std::sqrt (dx * dx + dy * dy);
    if (len < 1.0e-6f) return p1;

    const Vec2 mid { 0.5f * (p1.x + p2.x), 0.5f * (p1.y + p2.y) };
    float px = -dy / len, py = dx / len;
    if (px * (mid.x - centre.x) + py * (mid.y - centre.y) < 0.0f) { px = -px; py = -py; }
    return { mid.x + px * bulge * len, mid.y + py * bulge * len };
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

/** One operator's contribution to the simplicity descriptor: its resolved
    Stern-Brocot denominator q (the caller does the Path::walk) plus the
    depth/enabled state that gates and weights it. */
struct OpQ { int q { 1 }; float depth { 0.0f }; bool enabled { false }; };

/** Depth-weighted mean denominator q̄ over the ACTIVE ops (enabled, depth
    > 0) — the per-node "simplicity" descriptor. Config-derived only (q is
    read straight off each op's path); NOT a butterfly/prominence claim. −1
    when no op is active (colour that grey, don't scale it). */
inline float meanQOfOps (const std::vector<OpQ>& ops)
{
    double wSum = 0.0, qSum = 0.0;
    for (const auto& o : ops)
        if (o.enabled && o.depth > 0.0f)
        {
            wSum += (double) o.depth;
            qSum += (double) o.depth * (double) o.q;
        }
    return wSum > 0.0 ? (float) (qSum / wSum) : -1.0f;
}

} // namespace brocot::viz
