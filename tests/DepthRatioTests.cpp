#include <catch2/catch_test_macros.hpp>

#include "audio/DepthRatio.h"

using brocot::findDepthRatio;

TEST_CASE ("findDepthRatio: no active operators returns empty", "[DepthRatio]")
{
    REQUIRE (findDepthRatio ({ 0.0f, 0.0f, 0.0f }).isEmpty());
    REQUIRE (findDepthRatio ({ 0.01f, 0.001f }).isEmpty());
}

TEST_CASE ("findDepthRatio: single active op shows '1 : . : .'", "[DepthRatio]")
{
    REQUIRE (findDepthRatio ({ 0.5f, 0.0f, 0.0f }) == "1 : . : .");
    REQUIRE (findDepthRatio ({ 0.0f, 1.7f, 0.0f }) == ". : 1 : .");
}

TEST_CASE ("findDepthRatio: equal depths produce 1:1:1", "[DepthRatio]")
{
    const auto r = findDepthRatio ({ 1.0f, 1.0f, 1.0f });
    REQUIRE (r.startsWith ("1 : 1 : 1"));
}

TEST_CASE ("findDepthRatio: simple 2:1 ratio", "[DepthRatio]")
{
    const auto r = findDepthRatio ({ 1.0f, 2.0f });
    REQUIRE (r.startsWith ("1 : 2"));
}

TEST_CASE ("findDepthRatio: Avatar 1 depths approximate to 72:12:13",
           "[DepthRatio]")
{
    // 5.13 : 0.86 : 0.90 → normalized 5.965 : 1.0 : 1.047.
    // The prototype's findDepthRatio searches up to maxDenom=12 and lands
    // on 72:12:13 with residue ~0.024 — matching the spec §7 UI mock
    // ("72 : 12 : 13  raw 5.13:0.86:0.90").
    const auto r = findDepthRatio ({ 5.13f, 0.86f, 0.90f });
    REQUIRE (r.startsWith ("72 : 12 : 13"));
}

TEST_CASE ("findDepthRatio: disabled slots show as '.'", "[DepthRatio]")
{
    const auto r = findDepthRatio ({ 1.0f, 0.0f, 2.0f, 0.0f });
    REQUIRE (r.startsWith ("1 : . : 2 : ."));
}

TEST_CASE ("findDepthRatio: high-residue values append residue note when epsilon is tight",
           "[DepthRatio]")
{
    // A wide search (maxDenom=12) lets the algorithm find very close matches
    // for most reasonable inputs, so to demonstrate the residue note we use
    // a tight epsilon (0.001) on values that don't snap that close.
    const auto r = findDepthRatio ({ 1.0f, 1.37f, 2.18f }, 12, 0.001f);
    REQUIRE (r.contains ("residue"));
}
