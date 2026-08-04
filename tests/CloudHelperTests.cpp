#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>

#include "util/CloudMath.h"

using namespace brocot::viz;
using Catch::Approx;

// ----------------------------------------------------------------------------
// shimmerAlpha — the criticality-twinkle math
// ----------------------------------------------------------------------------

TEST_CASE ("shimmerAlpha is bounded to [0, 0.7]", "[CloudMath]")
{
    for (float crit : { 0.0f, 0.3f, 1.0f, 1.5f, 2.0f })
        for (int step = 0; step < 100; ++step)
        {
            const float a = shimmerAlpha (crit, 0.07f * (float) step,
                                          (uint32_t) step * 2654435761u);
            REQUIRE (a >= 0.0f);
            REQUIRE (a <= 0.7f);
        }
}

TEST_CASE ("shimmerAlpha is monotone in crit at fixed phase", "[CloudMath]")
{
    for (float phase : { 0.0f, 0.6f, 1.3f, 2.1f })
    {
        float prev = -1.0f;
        for (float crit = 0.0f; crit <= 1.001f; crit += 0.05f)
        {
            const float a = shimmerAlpha (crit, phase, 7u * 2654435761u);
            REQUIRE (a >= prev);
            prev = a;
        }
    }
}

TEST_CASE ("shimmerAlpha period is 2.5 seconds (0.4 Hz)", "[CloudMath]")
{
    for (int step = 0; step < 25; ++step)
    {
        const float t = 0.1f * (float) step;
        REQUIRE (shimmerAlpha (0.8f, t + 2.5f, 42u)
                 == Approx (shimmerAlpha (0.8f, t, 42u)).margin (1.0e-4));
    }
    // ...and half a period is visibly elsewhere in the cycle.
    REQUIRE (std::abs (shimmerAlpha (0.8f, 0.0f, 42u)
                       - shimmerAlpha (0.8f, 1.25f, 42u)) > 0.01f);
}

TEST_CASE ("two salts decorrelate the twinkle", "[CloudMath]")
{
    const uint32_t saltA = 1u * 2654435761u;
    const uint32_t saltB = 2u * 2654435761u;
    bool differed = false;
    for (int step = 0; step < 50 && ! differed; ++step)
    {
        const float t = 0.05f * (float) step;
        differed = std::abs (shimmerAlpha (1.0f, t, saltA)
                             - shimmerAlpha (1.0f, t, saltB)) > 0.05f;
    }
    REQUIRE (differed);
}

// ----------------------------------------------------------------------------
// topCritIndices — bake-time dot selection
// ----------------------------------------------------------------------------

TEST_CASE ("topCritIndices returns N indices in descending crit", "[CloudMath]")
{
    const std::vector<float> crit { 0.1f, 0.9f, 0.4f, 0.7f, 0.2f, 0.95f };
    const auto top = topCritIndices (crit, 3);
    REQUIRE (top == std::vector<int> { 5, 1, 3 });
}

TEST_CASE ("topCritIndices ties are stable (original order)", "[CloudMath]")
{
    const std::vector<float> crit { 0.5f, 0.9f, 0.5f, 0.5f, 0.9f };
    const auto top = topCritIndices (crit, 5);
    REQUIRE (top == std::vector<int> { 1, 4, 0, 2, 3 });
}

TEST_CASE ("topCritIndices clamps N to the population", "[CloudMath]")
{
    const std::vector<float> crit { 0.3f, 0.6f };
    REQUIRE (topCritIndices (crit, 400) == std::vector<int> { 1, 0 });
    REQUIRE (topCritIndices (crit, 0).empty());
    REQUIRE (topCritIndices ({}, 400).empty());
}
