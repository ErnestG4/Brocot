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
// arcControl — wormhole-arc geometry
// ----------------------------------------------------------------------------

TEST_CASE ("arcControl offset is perpendicular to the chord", "[CloudMath]")
{
    const Vec2 p1 { 10.0f, 20.0f }, p2 { 110.0f, 60.0f }, centre { 50.0f, 50.0f };
    const auto c = arcControl (p1, p2, centre);
    const Vec2 mid { 60.0f, 40.0f };
    const float dot = (c.x - mid.x) * (p2.x - p1.x) + (c.y - mid.y) * (p2.y - p1.y);
    REQUIRE (dot == Approx (0.0f).margin (1.0e-3));
}

TEST_CASE ("arcControl offset magnitude is bulge times the chord length", "[CloudMath]")
{
    const Vec2 p1 { 0.0f, 0.0f }, p2 { 30.0f, 40.0f }, centre { 100.0f, 100.0f };
    for (float bulge : { 0.18f, 0.5f })
    {
        const auto c = arcControl (p1, p2, centre, bulge);
        const Vec2 mid { 15.0f, 20.0f };
        const float mag = std::sqrt ((c.x - mid.x) * (c.x - mid.x)
                                     + (c.y - mid.y) * (c.y - mid.y));
        REQUIRE (mag == Approx (bulge * 50.0f).epsilon (1.0e-4));   // |chord| = 50
    }
}

TEST_CASE ("arcControl bows away from the centre, flipping as it crosses the chord", "[CloudMath]")
{
    // Horizontal chord along y = 10: centre below → bow up; centre above → bow down.
    const Vec2 p1 { 0.0f, 10.0f }, p2 { 100.0f, 10.0f };
    const auto below = arcControl (p1, p2, { 50.0f, -40.0f });
    const auto above = arcControl (p1, p2, { 50.0f,  60.0f });
    REQUIRE (below.y > 10.0f);
    REQUIRE (above.y < 10.0f);
    REQUIRE (below.x == Approx (50.0f));
    REQUIRE (below.y == Approx (2.0f * 10.0f - above.y));   // mirror images
}

TEST_CASE ("arcControl degenerates to p1 when the chord vanishes", "[CloudMath]")
{
    const Vec2 p { 7.0f, -3.0f };
    const auto c = arcControl (p, p, { 0.0f, 0.0f });
    REQUIRE (c.x == Approx (7.0f));
    REQUIRE (c.y == Approx (-3.0f));
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
