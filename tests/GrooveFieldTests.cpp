#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>

#include "audio/GrooveField.h"

using brocot::GrooveField;
using Catch::Approx;

namespace
{
    constexpr float log2f_ (double r) { return (float) std::log2 (r); }
}

TEST_CASE ("GrooveField enumerates simple rationals with simplicity weights")
{
    GrooveField gf (16, -4.0f, 4.0f);
    // 1/1 is present and is the heaviest well (weight 1).
    float wUnison = 0.0f, w32 = 0.0f;
    for (const auto& g : gf.all())
    {
        if (g.p == 1 && g.q == 1) wUnison = g.weight;
        if (g.p == 3 && g.q == 2) w32 = g.weight;
    }
    REQUIRE (wUnison == Approx (1.0f));        // 1/(1·1)
    REQUIRE (w32 == Approx (1.0f / 6.0f));      // 1/(3·2)
    REQUIRE (wUnison > w32);                    // simpler = deeper
}

TEST_CASE ("nearestGroove resolves to the dominant well")
{
    GrooveField gf;
    auto at = [&] (double r) { return gf.nearestGroove (log2f_ (r)); };
    REQUIRE ((at (1.0).p == 1 && at (1.0).q == 1));
    REQUIRE ((at (2.0).p == 2 && at (2.0).q == 1));
    REQUIRE ((at (1.5).p == 3 && at (1.5).q == 2));

    // Just above unison, the deep 1/1 well dominates any nearby complex ratio.
    const auto& g = at (1.02);
    REQUIRE ((g.p == 1 && g.q == 1));
}

TEST_CASE ("pull moves toward the groove and vanishes on it")
{
    GrooveField gf;

    // On a groove → ~no pull.
    REQUIRE (std::abs (gf.pull (0.0f, 1.0f, 0.0f)) < 1e-3f);

    // Slightly sharp of unison → pull is negative (back down toward 1/1).
    REQUIRE (gf.pull (0.04f, 1.0f, 0.0f) < 0.0f);
    // Slightly flat → pull positive (up toward 1/1).
    REQUIRE (gf.pull (-0.04f, 1.0f, 0.0f) > 0.0f);

    // Pull scales with gravity.
    const float p_half = gf.pull (0.04f, 0.5f, 0.0f);
    const float p_full = gf.pull (0.04f, 1.0f, 0.0f);
    REQUIRE (p_full == Approx (2.0f * p_half).margin (1e-6));
}

TEST_CASE ("restlessness escapes shallow wells more than deep ones")
{
    GrooveField gf;

    // A point near a shallow well (close to a complex ratio, far from any
    // simple one) loses most of its pull under high restlessness.
    const float near54 = log2f_ (5.0 / 4.0) + 0.02f;   // near 5/4 (weight 1/20)
    const float shallowCalm    = std::abs (gf.pull (near54, 1.0f, 0.0f));
    const float shallowRestful = std::abs (gf.pull (near54, 1.0f, 1.0f));
    REQUIRE (shallowRestful < shallowCalm);

    // A deep well (near 1/1) keeps most of its pull even when restless.
    const float deepCalm    = std::abs (gf.pull (0.04f, 1.0f, 0.0f));
    const float deepRestful = std::abs (gf.pull (0.04f, 1.0f, 1.0f));
    REQUIRE (deepRestful > 0.5f * deepCalm);

    // The deep well retains a larger fraction of its pull than the shallow one.
    REQUIRE ((deepRestful / deepCalm) > (shallowRestful / shallowCalm));
}
