#pragma once

#include <cmath>
#include <numeric>
#include <vector>

namespace brocot
{

/**
    Farey "groove" field for continuous steering (see the groove-steering
    design): the Stern-Brocot / Farey rationals act as gravity wells in a
    continuous log-ratio space. A rational p/q's well depth ∝ its simplicity
    (weight = 1/(p·q) — 1/1, 2/1, 3/2 are deep; complex ratios shallow), so
    steering can glide continuously through relative frequency and be pulled
    into consonant grooves.

    Pure + header-only so it's trivially unit-testable. Works in log2-ratio
    (octaves): logRatio = log2(f_op / f_carrier).
*/
class GrooveField
{
public:
    struct Groove
    {
        float logRatio { 0.0f };   // log2(p/q)
        float weight   { 0.0f };   // 1/(p·q) — simplicity = well depth
        int   p { 1 }, q { 1 };
    };

    /** Enumerate coprime p/q with q∈[1,maxDenom] whose ratio lies in
        [2^logMin, 2^logMax]. kernelWidth is the well capture radius in octaves
        (~a semitone by default). */
    explicit GrooveField (int maxDenom = 16,
                          float logMin = -4.0f, float logMax = 4.0f,
                          float kernelWidthOct = 0.08f)
        : width (kernelWidthOct)
    {
        const float rLo = std::exp2 (logMin), rHi = std::exp2 (logMax);
        for (int q = 1; q <= maxDenom; ++q)
        {
            const int pLo = std::max (1, (int) std::ceil  ((double) q * rLo));
            const int pHi =            (int) std::floor ((double) q * rHi);
            for (int p = pLo; p <= pHi; ++p)
            {
                if (std::gcd (p, q) != 1)
                    continue;
                Groove g;
                g.p = p; g.q = q;
                g.logRatio = std::log2 ((float) p / (float) q);
                g.weight   = 1.0f / (float) (p * q);
                grooves.push_back (g);
            }
        }
    }

    const std::vector<Groove>& all() const noexcept { return grooves; }

    /** The dominant groove at `logRatio` — the one whose weighted proximity
        (weight × kernel(distance)) is greatest, i.e. the well pulling hardest
        here. */
    const Groove& nearestGroove (float logRatio) const
    {
        const Groove* best = &grooves[0];
        float bestScore = -1.0f;
        for (const auto& g : grooves)
        {
            const float s = g.weight * kernel (logRatio - g.logRatio);
            if (s > bestScore) { bestScore = s; best = &g; }
        }
        return *best;
    }

    /** Well depth at a point = the dominant groove's weighted proximity. */
    float depthAt (float logRatio) const
    {
        const auto& g = nearestGroove (logRatio);
        return g.weight * kernel (logRatio - g.logRatio);
    }

    /** Gravity step (Δ log-ratio) pulling `logRatio` toward its dominant
        groove. `gravity`∈[0,1] is overall strength; `restlessness`∈[0,1] gates
        out *shallow* wells (deep wells still hold) — the hybrid feel: mostly
        deterministic pull, but shallow grooves are escapable. Δ=0 exactly on a
        groove. */
    float pull (float logRatio, float gravity, float restlessness) const
    {
        const auto& g = nearestGroove (logRatio);
        const float d = g.logRatio - logRatio;          // toward the well
        const float depth = g.weight * kernel (d);
        // gate → 1 when the well is deep relative to restlessness; → 0 for
        // shallow wells under high restlessness (they get escaped).
        const float gate = depth / (depth + restlessness * kEscape + 1.0e-6f);
        return gravity * gate * d;
    }

private:
    static constexpr float kEscape = 0.2f;   // restlessness scale vs well depth

    float kernel (float d) const noexcept
    {
        const float x = d / width;
        return std::exp (-x * x);
    }

    std::vector<Groove> grooves;
    float width;
};

} // namespace brocot
