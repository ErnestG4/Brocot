/*
    Tests for the runtime Landscape loader / locate / gradient-walk.

    Like PullIndexTests, this synthesises a tiny landscape_graph.json in a temp
    dir so we control every value. Graph: 3 nodes on a single chain toward one
    family "Bright", with next_hop 0→1→2(arrived).
*/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "../source/pull/Landscape.cpp"   // unity-include, matches PullIndexTests

using brocot::Landscape;
using brocot::PullIndex;

namespace
{
    // node k = a single op at SB path ("R"=2/1, "RR"=3/1, "RRR"=4/1), depth 1.
    const char* kJson = R"JSON(
    {
      "version": "1.0",
      "f_carrier_ref": 220.0,
      "n_nodes": 3,
      "n_families": 1,
      "family_names": ["Bright"],
      "nodes": [
        {"family": 0, "ops": [{"path": "R",   "depth": 1.0, "enabled": true}]},
        {"family": 0, "ops": [{"path": "RR",  "depth": 1.0, "enabled": true}]},
        {"family": 0, "ops": [{"path": "RRR", "depth": 1.0, "enabled": true}]}
      ],
      "next_hop": { "Bright": [1, 2, -1] }
    })JSON";

    juce::File writeTemp (const juce::String& json)
    {
        auto f = juce::File::createTempFile (".json");
        f.replaceWithText (json);
        return f;
    }
}

TEST_CASE ("Landscape loads nodes + family fields", "[Landscape]")
{
    auto f = writeTemp (kJson);
    Landscape ls;
    juce::String err;
    REQUIRE (ls.load (f, &err));
    REQUIRE (ls.getNodeCount() == 3);
    REQUIRE (ls.getFamilyNames().contains ("Bright"));
    REQUIRE (ls.hasFamily ("Bright"));
    REQUIRE_FALSE (ls.hasFamily ("Nope"));
    f.deleteFile();
}

TEST_CASE ("ratioHistogram is depth-weighted, L1-normalised, ratio-placed",
           "[Landscape]")
{
    std::vector<PullIndex::OpState> ops {
        { "R", 1.0f, true },        // 2/1
        { "L", 1.0f, true },        // 1/2
        { "RR", 0.0f, true },       // disabled by zero depth → ignored
    };
    const auto h = Landscape::ratioHistogram (ops);
    REQUIRE ((int) h.size() == Landscape::kHistBins);
    float sum = 0.0f; for (float v : h) sum += v;
    REQUIRE (sum == Catch::Approx (1.0f));
    // 2/1 and 1/2 land symmetrically about the centre (log2 = +1 and -1).
    // With two equal-depth active ops, two bins carry 0.5 each.
    int nonzero = 0; for (float v : h) if (v > 0.0f) ++nonzero;
    REQUIRE (nonzero == 2);
}

TEST_CASE ("nearestNode matches the closest config by params", "[Landscape]")
{
    auto f = writeTemp (kJson);
    Landscape ls; ls.load (f);

    // A patch that is exactly node 1 (single op at "RR") locates to node 1.
    std::vector<PullIndex::OpState> patch { { "RR", 1.0f, true } };
    REQUIRE (ls.nearestNode (patch) == 1);

    // A patch at "R" locates to node 0.
    REQUIRE (ls.nearestNode ({ { "R", 1.0f, true } }) == 0);
    f.deleteFile();
}

TEST_CASE ("route follows next-hop pointers to the family", "[Landscape]")
{
    auto f = writeTemp (kJson);
    Landscape ls; ls.load (f);

    const auto r0 = ls.route (0, "Bright");
    REQUIRE (r0 == std::vector<int> { 0, 1, 2 });

    const auto r1 = ls.route (1, "Bright");
    REQUIRE (r1 == std::vector<int> { 1, 2 });

    // The arriving node only.
    const auto r2 = ls.route (2, "Bright");
    REQUIRE (r2 == std::vector<int> { 2 });

    // Unknown family → empty.
    REQUIRE (ls.route (0, "Nope").empty());

    // Waypoints carry each node's ops in order.
    const auto wp = ls.waypoints (0, "Bright");
    REQUIRE (wp.size() == 3);
    REQUIRE (wp.front().front().path == "R");
    REQUIRE (wp.back().front().path == "RRR");
    f.deleteFile();
}
