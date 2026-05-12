#include "DepthRatio.h"

#include <algorithm>
#include <cmath>

namespace brocot
{

namespace
{
    constexpr float kMinAudibleDepth = 0.02f;

    int gcdRecursive (int a, int b) { return b == 0 ? a : gcdRecursive (b, a % b); }
}

juce::String findDepthRatio (const std::vector<float>& depths, int maxDenom, float epsilon)
{
    std::vector<bool> active (depths.size(), false);
    std::vector<float> normed (depths.size(), 0.0f);

    float minD = 1e9f;
    int activeCount = 0;
    for (size_t i = 0; i < depths.size(); ++i)
    {
        if (depths[i] > kMinAudibleDepth)
        {
            active[i] = true;
            ++activeCount;
            if (depths[i] < minD) minD = depths[i];
        }
    }
    if (activeCount == 0) return {};
    if (activeCount == 1)
    {
        juce::String out;
        for (size_t i = 0; i < depths.size(); ++i)
        {
            if (! out.isEmpty()) out += " : ";
            out += active[i] ? "1" : ".";
        }
        return out;
    }

    for (size_t i = 0; i < depths.size(); ++i)
        if (active[i]) normed[i] = depths[i] / minD;

    int bestScale = 1;
    float bestResidue = 1e9f;
    std::vector<int> bestInts (depths.size(), 0);

    for (int scale = 1; scale <= maxDenom; ++scale)
    {
        std::vector<int> ints (depths.size(), 0);
        float residueSum = 0.0f;
        for (size_t i = 0; i < depths.size(); ++i)
        {
            if (! active[i]) continue;
            const int v = std::max (1, (int) std::round (normed[i] * (float) scale));
            ints[i] = v;
            residueSum += std::fabs (normed[i] - (float) v / (float) scale);
        }
        const float residue = residueSum / (float) activeCount;

        if (residue < bestResidue)
        {
            bestResidue = residue;
            bestScale   = scale;
            bestInts    = ints;
        }
        if (residue < 0.005f) break;
    }
    juce::ignoreUnused (bestScale);

    int g = 0;
    for (int v : bestInts) if (v > 0) g = (g == 0) ? v : gcdRecursive (g, v);
    if (g <= 0) g = 1;

    juce::String out;
    for (size_t i = 0; i < depths.size(); ++i)
    {
        if (! out.isEmpty()) out += " : ";
        if (bestInts[i] == 0) out += ".";
        else                  out += juce::String (bestInts[i] / g);
    }
    if (bestResidue >= epsilon)
        out += juce::String::formatted ("   (residue %.3f)", (double) bestResidue);
    return out;
}

} // namespace brocot
