/*
    Tests for the runtime Landscape loader / locate / gradient-walk.

    Like PullIndexTests, this synthesises a tiny landscape_graph.json in a temp
    dir so we control every value. Graph: 3 nodes on a single chain toward one
    family "Bright", with next_hop 0→1→2(arrived).
*/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cstring>

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
        {"family": 0, "feedback": 0.0, "ops": [{"path": "R",   "depth": 1.0, "enabled": true}]},
        {"family": 0, "feedback": 0.0, "ops": [{"path": "RR",  "depth": 1.0, "enabled": true}]},
        {"family": 0, "feedback": 0.5, "ops": [{"path": "RRR", "depth": 1.0, "enabled": true}]}
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

TEST_CASE ("Landscape loads a gzipped graph (.json.gz)", "[Landscape]")
{
    // Write the same JSON gzip-compressed to a .json.gz temp file and confirm
    // the decompress-on-load path produces an identical result.
    auto f = juce::File::createTempFile (".json.gz");
    {
        auto raw = f.createOutputStream();
        REQUIRE (raw != nullptr);
        // windowBits 31 (MAX_WBITS|16) = gzip format, matching the reader and
        // the Python-gzip artifact (default 15 would be zlib).
        juce::GZIPCompressorOutputStream gz (*raw, 6, 31);
        gz.write (kJson, std::strlen (kJson));
        gz.flush();
    }

    Landscape ls;
    juce::String err;
    REQUIRE (ls.load (f, &err));
    REQUIRE (ls.getNodeCount() == 3);
    REQUIRE (ls.route (0, "Bright") == std::vector<int> { 0, 1, 2 });
    f.deleteFile();
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

TEST_CASE ("peekHeader reads name + depth without parsing nodes", "[Landscape]")
{
    // A header with a name + n_ops, followed by a big node array. peekHeader
    // must return the normalised label and depth WITHOUT touching the nodes
    // (write it gzipped to exercise the streamed-decompress path).
    const char* named = R"JSON(
    {
      "version": "1.0", "name": "8op mix", "n_ops": 8,
      "n_nodes": 1, "n_families": 1, "family_names": ["Bright"],
      "nodes": [ {"family": 0, "feedback": 0.0,
                  "ops": [{"path": "R", "depth": 1.0, "enabled": true, "wave": 2}]} ]
    })JSON";
    auto f = juce::File::createTempFile (".json.gz");
    {
        auto raw = f.createOutputStream();
        REQUIRE (raw != nullptr);
        juce::GZIPCompressorOutputStream gz (*raw, 6, 31);
        gz.write (named, std::strlen (named));
        gz.flush();
    }
    int nOps = -1; juce::String name;
    REQUIRE (Landscape::peekHeader (f, nOps, name));
    REQUIRE (nOps == 8);
    REQUIRE (name == "8-op mix");
    f.deleteFile();

    // A header with no name field → plain "<N>-op".
    int n2 = -1; juce::String name2;
    auto g = writeTemp (kJson);   // kJson has no name / no n_ops
    REQUIRE (Landscape::peekHeader (g, n2, name2));
    REQUIRE (n2 == 6);            // default when n_ops absent
    REQUIRE (name2 == "6-op");
    g.deleteFile();
}

TEST_CASE ("map name normalises '<N>op <desc>' to '<N>-op <desc>'", "[Landscape]")
{
    const char* named = R"JSON(
    {
      "version": "1.0", "name": "6op sine", "n_ops": 6,
      "n_nodes": 1, "n_families": 1, "family_names": ["Bright"],
      "nodes": [ {"family": 0, "feedback": 0.0,
                  "ops": [{"path": "R", "depth": 1.0, "enabled": true, "wave": 2}]} ]
    })JSON";
    auto f = writeTemp (named);
    Landscape ls; juce::String err;
    REQUIRE (ls.load (f, &err));
    REQUIRE (ls.getName() == "6-op sine");
    // per-op waveform survives the parse into the node (saw == 2)
    REQUIRE (ls.getNodeCount() == 1);
    REQUIRE (ls.getNodes().front().ops.front().waveform == 2);
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

    // No active ops → all-zero signature → locates nowhere (-1), not node 0.
    REQUIRE (ls.nearestNode ({ { "R", 1.0f, false } }) == -1);
    REQUIRE (ls.nearestNode ({ { "R", 0.0f, true } }) == -1);
    REQUIRE (ls.nearestNode ({}) == -1);
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

    // Waypoints carry each node's ops + op-0 feedback in order.
    const auto wp = ls.waypoints (0, "Bright");
    REQUIRE (wp.size() == 3);
    REQUIRE (wp.front().ops.front().path == "R");
    REQUIRE (wp.back().ops.front().path == "RRR");
    REQUIRE (wp.front().feedback == Catch::Approx (0.0f));
    REQUIRE (wp.back().feedback == Catch::Approx (0.5f));  // node 2's feedback
    f.deleteFile();
}

TEST_CASE ("route that never arrives is empty, not partial", "[Landscape]")
{
    // node 0 is unreachable (-2); nodes 1 and 2 form a cycle that never hits
    // the family (-1). Both walks must yield NO route — a partial path would
    // steer toward a node of some unrelated family.
    const char* json = R"JSON(
    {
      "version": "1.0",
      "f_carrier_ref": 220.0,
      "n_nodes": 3,
      "n_families": 1,
      "family_names": ["Bright"],
      "nodes": [
        {"family": 0, "feedback": 0.0, "ops": [{"path": "R",   "depth": 1.0, "enabled": true}]},
        {"family": 0, "feedback": 0.0, "ops": [{"path": "RR",  "depth": 1.0, "enabled": true}]},
        {"family": 0, "feedback": 0.0, "ops": [{"path": "RRR", "depth": 1.0, "enabled": true}]}
      ],
      "next_hop": { "Bright": [-2, 2, 1] }
    })JSON";
    auto f = writeTemp (json);
    Landscape ls;
    REQUIRE (ls.load (f));
    REQUIRE (ls.route (0, "Bright").empty());   // -2 unreachable
    REQUIRE (ls.route (1, "Bright").empty());   // 1↔2 cycle, guard exhausts
    REQUIRE (ls.waypoints (1, "Bright").empty());
    f.deleteFile();
}
