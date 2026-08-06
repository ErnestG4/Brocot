#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <vector>

#include "audio/BrocotLattice.h"
#include "audio/Path.h"
#include "util/FareyMath.h"

using namespace brocot::viz;
using brocot::Path;
using Catch::Approx;

// ----------------------------------------------------------------------------
// cappedPrefix
// ----------------------------------------------------------------------------

TEST_CASE ("cappedPrefix caps at the q ceiling", "[FareyMath]")
{
    // "L"*n resolves to 1/(n+1): prefixes of LLLLLL hit q = 2,3,4,5,6,7.
    REQUIRE (cappedPrefix (Path ("LLLLLL"), 4).toString() == "LLL");     // 1/4
    REQUIRE (cappedPrefix (Path ("LLLLLL"), 7).toString() == "LLLLLL");  // 1/7

    // q ceiling below the first step → empty (the root).
    REQUIRE (cappedPrefix (Path ("LLLLLL"), 1).isEmpty());

    // "R"*n stays q = 1, so no cap ever bites.
    REQUIRE (cappedPrefix (Path ("RRRR"), 1).toString() == "RRRR");
}

TEST_CASE ("cappedPrefix respects hardDepth", "[FareyMath]")
{
    REQUIRE (cappedPrefix (Path ("LLLLLL"), 100, 2).toString() == "LL");
    REQUIRE (cappedPrefix (Path ("RRRR"), 100, 3).toString() == "RRR");
}

// ----------------------------------------------------------------------------
// fracTheta
// ----------------------------------------------------------------------------

TEST_CASE ("fracTheta is the fractional part of p/q", "[FareyMath]")
{
    REQUIRE (fracTheta (1, 2) == Approx (0.5));
    REQUIRE (fracTheta (3, 2) == Approx (0.5));   // wraps past 1
    REQUIRE (fracTheta (2, 1) == Approx (0.0));
    REQUIRE (fracTheta (7, 4) == Approx (0.75));

    for (int q = 1; q <= 8; ++q)
        for (int p = 1; p <= 16; ++p)
        {
            const double t = fracTheta (p, q);
            REQUIRE (t >= 0.0);
            REQUIRE (t < 1.0);
        }
}

// ----------------------------------------------------------------------------
// chernHue
// ----------------------------------------------------------------------------

TEST_CASE ("chernHue diverges around 0.5 and clamps", "[FareyMath]")
{
    REQUIRE (chernHue (0, 32) == Approx (0.5f));
    REQUIRE (chernHue (1, 32) > 0.5f);
    REQUIRE (chernHue (-1, 32) < 0.5f);
    REQUIRE (chernHue (1, 32) == Approx (0.5f + 1.0f / 64.0f));

    // Spread widens/narrows the slope — the legend-compression knob.
    REQUIRE (chernHue (2, 4) == Approx (0.75f));
    REQUIRE (chernHue (2, 32) == Approx (0.53125f));

    // Out-of-spread magnitudes clamp to [0, 1].
    REQUIRE (chernHue (100, 4) == 1.0f);
    REQUIRE (chernHue (-100, 4) == 0.0f);
}

// ----------------------------------------------------------------------------
// pickProminentChild
// ----------------------------------------------------------------------------

TEST_CASE ("pickProminentChild matches direct lattice::prominence", "[FareyMath]")
{
    // Cursor 1/3 (path LL): children L = 1/4, R = 2/5 — distinct q, both
    // under the guard, both with real gap structure.
    const auto pick = pickProminentChild (Path ("LL"));
    REQUIRE (pick.valid);

    const double promL = brocot::lattice::prominence (1, 4);
    const double promR = brocot::lattice::prominence (2, 5);
    REQUIRE (promL > 0.0);
    REQUIRE (promR > 0.0);

    const char expected = promL >= promR ? 'L' : 'R';
    REQUIRE (pick.side == expected);
    REQUIRE (pick.prominence == Approx (std::max (promL, promR)));
}

TEST_CASE ("pickProminentChild at the root picks the gapped side", "[FareyMath]")
{
    // Children of the root: L = 1/2, R = 2/1. Integer flux (q=1) has no gap
    // structure at all, while the q=2 approximant carries a wide central gap
    // — so L wins on genuine prominence, matching lattice::prominence.
    const auto pick = pickProminentChild (Path (""));
    REQUIRE (pick.valid);
    REQUIRE (pick.side == 'L');
    REQUIRE (brocot::lattice::prominence (2, 1) == Approx (0.0));
    REQUIRE (pick.prominence == Approx (brocot::lattice::prominence (1, 2)));
    REQUIRE (pick.prominence > 0.0);
}

TEST_CASE ("pickProminentChild honours the q guard", "[FareyMath]")
{
    // Children of 1/4: L = 1/5, R = 2/7 — both beyond a guard of 4.
    REQUIRE_FALSE (pickProminentChild (Path ("LLL"), 4).valid);

    // One-sided: at the root with guard 1 only R (2/1, q=1) qualifies.
    const auto pick = pickProminentChild (Path (""), 1);
    REQUIRE (pick.valid);
    REQUIRE (pick.side == 'R');
}

// ----------------------------------------------------------------------------
// landmark stepping
// ----------------------------------------------------------------------------

TEST_CASE ("nextLandmark / prevLandmark step through the Bessel table", "[FareyMath]")
{
    // From below the table: the first landmark (J1+ at 1.8412).
    const auto* first = nextLandmark (0.0f);
    REQUIRE (first != nullptr);
    REQUIRE (first->value == Approx (1.8412f));

    // Sitting exactly ON a landmark steps to the neighbour (strict + epsilon).
    const auto* second = nextLandmark (1.8412f);
    REQUIRE (second != nullptr);
    REQUIRE (second->value == Approx (2.4048f));
    REQUIRE (prevLandmark (2.4048f)->value == Approx (1.8412f));

    // Mid-table from an off-landmark depth.
    REQUIRE (nextLandmark (5.2f)->value == Approx (5.3176f));
    REQUIRE (prevLandmark (5.2f)->value == Approx (5.1356f));

    // Ends: nothing below the first, nothing above the last (7.0156).
    REQUIRE (prevLandmark (1.8412f) == nullptr);
    REQUIRE (prevLandmark (0.0f) == nullptr);
    REQUIRE (nextLandmark (7.0156f) == nullptr);
    REQUIRE (nextLandmark (brocot::bessel::kDepthMax) == nullptr);
    REQUIRE (prevLandmark (brocot::bessel::kDepthMax)->value == Approx (7.0156f));

    // A full ascending walk visits all 10 landmarks in order.
    int count = 0;
    float depth = 0.0f;
    while (const auto* lm = nextLandmark (depth))
    {
        REQUIRE (lm->value > depth);
        depth = lm->value;
        ++count;
    }
    REQUIRE (count == (int) brocot::bessel::kLandmarks.size());
}

// ----------------------------------------------------------------------------
// add/remove partial plans
// ----------------------------------------------------------------------------

TEST_CASE ("addPartialPlan with no enabled ops starts at position 1", "[FareyMath]")
{
    const std::vector<OpPartial> ops (4);   // all disabled
    const auto plan = addPartialPlan (ops);
    REQUIRE (plan.valid);
    REQUIRE (plan.opIndex == 0);
    REQUIRE (plan.position == 1);
}

TEST_CASE ("addPartialPlan fills the first disabled slot past the outermost partial", "[FareyMath]")
{
    // Gaps in positions: enabled at 0, 3, 5 with disabled slots interleaved.
    const std::vector<OpPartial> ops {
        { true, 0 }, { false, 0 }, { true, 3 }, { true, 5 }, { false, 0 } };
    const auto plan = addPartialPlan (ops);
    REQUIRE (plan.valid);
    REQUIRE (plan.opIndex == 1);     // first disabled
    REQUIRE (plan.position == 6);    // max enabled position + 1
}

TEST_CASE ("addPartialPlan caps position at 32 and refuses when full", "[FareyMath]")
{
    const std::vector<OpPartial> capped { { true, 32 }, { false, 0 } };
    REQUIRE (addPartialPlan (capped).position == 32);

    const std::vector<OpPartial> full { { true, 1 }, { true, 2 } };
    REQUIRE_FALSE (addPartialPlan (full).valid);
}

TEST_CASE ("removePartialPlan takes the outermost partial", "[FareyMath]")
{
    const std::vector<OpPartial> ops {
        { true, 0 }, { false, 7 }, { true, 5 }, { true, 3 } };
    const auto plan = removePartialPlan (ops);
    REQUIRE (plan.valid);
    REQUIRE (plan.opIndex == 2);     // enabled at the highest position (5);
    REQUIRE (plan.position == 5);    // the disabled 7 doesn't count

    // Ties keep the first such op.
    const std::vector<OpPartial> tied { { true, 4 }, { true, 4 } };
    REQUIRE (removePartialPlan (tied).opIndex == 0);

    // Nothing enabled → no pick.
    REQUIRE_FALSE (removePartialPlan ({ { false, 0 }, { false, 0 } }).valid);
}
