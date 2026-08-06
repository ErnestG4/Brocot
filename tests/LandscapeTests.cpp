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

TEST_CASE ("jumps block parses, validates, and is optional", "[Landscape]")
{
    // kJson has no "jumps" key → empty.
    auto f0 = writeTemp (kJson);
    Landscape none;
    REQUIRE (none.load (f0));
    REQUIRE (none.getJumps().empty());
    REQUIRE (none.getJumpTau() == Catch::Approx (0.0f));
    f0.deleteFile();

    // A jumps block with one valid edge (given a>b, must be swapped), one
    // out-of-range edge and one self-edge (both dropped silently).
    const char* json = R"JSON(
    {
      "version": "1.0",
      "family_names": ["Bright"],
      "nodes": [
        {"family": 0, "ops": [{"path": "R",   "depth": 1.0, "enabled": true}]},
        {"family": 0, "ops": [{"path": "RR",  "depth": 1.0, "enabled": true}]},
        {"family": 0, "ops": [{"path": "RRR", "depth": 1.0, "enabled": true}]}
      ],
      "next_hop": { "Bright": [1, 2, -1] },
      "jumps": {
        "version": 1, "metric": "maxstep_fp_l1", "tau": 0.18,
        "edges": [
          {"a": 2, "b": 0, "r": 0.11, "t": 1.92, "g": 23.4, "w": 0.42},
          {"a": 0, "b": 99, "r": 0.05, "t": 1.0, "g": 9.0, "w": 0.5},
          {"a": 1, "b": 1, "r": 0.05, "t": 1.0, "g": 9.0, "w": 0.5}
        ]
      }
    })JSON";
    auto f = writeTemp (json);
    Landscape ls;
    REQUIRE (ls.load (f));
    REQUIRE (ls.getJumpTau() == Catch::Approx (0.18f));
    REQUIRE (ls.getJumps().size() == 1);
    const auto& j = ls.getJumps()[0];
    CHECK (j.a == 0);           // swapped into a < b
    CHECK (j.b == 2);
    CHECK (j.rough == Catch::Approx (0.11f));
    CHECK (j.tv == Catch::Approx (1.92f));
    CHECK (j.graphDist == Catch::Approx (23.4f));
    CHECK (j.worstAlpha == Catch::Approx (0.42f));

    // Incidence lookup finds the edge from both endpoints, not from node 1.
    CHECK (ls.jumpsFrom (0) == std::vector<int> { 0 });
    CHECK (ls.jumpsFrom (2) == std::vector<int> { 0 });
    CHECK (ls.jumpsFrom (1).empty());
    f.deleteFile();
}

TEST_CASE ("bestCritNode picks by criticality with a distance penalty", "[Landscape]")
{
    // Three nodes: a high-crit node just inside the radius, a mid-crit node
    // at the centre, a max-crit node far outside.
    const char* json = R"JSON(
    {
      "version": "1.0",
      "family_names": ["Bright"],
      "nodes": [
        {"family": 0, "x": 0.0,  "y": 0.0, "crit": 0.40,
         "ops": [{"path": "R", "depth": 1.0, "enabled": true}]},
        {"family": 0, "x": 0.09, "y": 0.0, "crit": 0.90,
         "ops": [{"path": "RR", "depth": 1.0, "enabled": true}]},
        {"family": 0, "x": 0.9,  "y": 0.9, "crit": 1.00,
         "ops": [{"path": "RRR", "depth": 1.0, "enabled": true}]}
      ],
      "next_hop": { "Bright": [1, 2, -1] }
    })JSON";
    auto f = writeTemp (json);
    Landscape ls;
    REQUIRE (ls.load (f));

    // Radius 0.1 around origin: node 1 wins (0.90 - 0.5*0.9 = 0.45 beats
    // node 0's 0.40 - 0 = 0.40); the far max-crit node is out of range.
    CHECK (ls.bestCritNode (0.0f, 0.0f, 0.1f) == 1);

    // A harsh distance penalty flips the choice to the centre node.
    CHECK (ls.bestCritNode (0.0f, 0.0f, 0.1f, 6.0f) == 0);

    // No node within a tiny radius elsewhere → -1; nonpositive radius → -1.
    CHECK (ls.bestCritNode (-0.9f, 0.9f, 0.05f) == -1);
    CHECK (ls.bestCritNode (0.0f, 0.0f, 0.0f) == -1);
    f.deleteFile();
}

TEST_CASE ("mod-map fields parse: mods / motion / rate_n / doc flag", "[Landscape]")
{
    // A doc with "mod_map": 1 and one node carrying every mod-map extra,
    // plus tolerant-parse fodder: an unknown-kind entry and an unknown-shape
    // entry (both must be dropped, never misapplied), and "triangle" as an
    // accepted alias for "tri".
    const char* json = R"JSON(
    {
      "version": "1.0", "name": "6op mod", "n_ops": 6, "mod_map": 1,
      "family_names": ["Bright"],
      "nodes": [
        {"family": 0, "feedback": 0.5, "motion": 0.62, "rate_n": 0.31,
         "mods": [
           {"kind": "depth",    "op": 3, "shape": "sine",     "rate": 1.7,  "depth": 0.8},
           {"kind": "feedback", "op": 0, "shape": "triangle", "rate": 0.33, "depth": 0.45},
           {"kind": "warp",     "op": 1, "shape": "sine",     "rate": 1.0,  "depth": 0.5},
           {"kind": "depth",    "op": 1, "shape": "spiral",   "rate": 1.0,  "depth": 0.5}
         ],
         "ops": [{"path": "R", "depth": 1.0, "enabled": true}]},
        {"family": 0, "feedback": 0.0,
         "ops": [{"path": "RR", "depth": 1.0, "enabled": true}]}
      ],
      "next_hop": { "Bright": [1, -1] }
    })JSON";
    auto f = writeTemp (json);
    Landscape ls;
    REQUIRE (ls.load (f));
    REQUIRE (ls.isModulationMap());
    REQUIRE (ls.hasMotion());
    REQUIRE (ls.getName() == "6-op mod");

    const auto& n0 = ls.getNodes()[0];
    CHECK (n0.motion == Catch::Approx (0.62f));
    CHECK (n0.rateN  == Catch::Approx (0.31f));
    REQUIRE (n0.mods.size() == 2);          // unknown kind + shape dropped
    CHECK (n0.mods[0].kind  == 0);
    CHECK (n0.mods[0].op    == 3);
    CHECK (n0.mods[0].shape == 0);
    CHECK (n0.mods[0].rate  == Catch::Approx (1.7f));
    CHECK (n0.mods[0].depth == Catch::Approx (0.8f));
    CHECK (n0.mods[1].kind  == 1);
    CHECK (n0.mods[1].shape == 1);          // "triangle" alias
    CHECK (n0.mods[1].rate  == Catch::Approx (0.33f));

    // Node 1 ships no extras → the absent defaults.
    const auto& n1 = ls.getNodes()[1];
    CHECK (n1.motion == Catch::Approx (-1.0f));
    CHECK (n1.rateN  == Catch::Approx (-1.0f));
    CHECK (n1.mods.empty());
    f.deleteFile();
}

TEST_CASE ("non-mod maps stay mod-free: no flag, no motion, no mods", "[Landscape]")
{
    // kJson predates the mod map entirely — every mod accessor must report
    // absence (and a stale mod map's state must not leak across load()).
    auto f = writeTemp (kJson);
    Landscape ls;
    REQUIRE (ls.load (f));
    CHECK_FALSE (ls.isModulationMap());
    CHECK_FALSE (ls.hasMotion());
    for (const auto& n : ls.getNodes())
    {
        CHECK (n.motion == Catch::Approx (-1.0f));
        CHECK (n.rateN  == Catch::Approx (-1.0f));
        CHECK (n.mods.empty());
    }
    f.deleteFile();
}

TEST_CASE ("shipped mod map's mod layer loads when present", "[Landscape]")
{
    // Guarded end-to-end check of the real artifact (phase3/gpu_modbank.py
    // map): every node of the shipped mod map must carry exactly one parsed
    // ModSpec inside the mined ranges, plus 0..1 motion/rate descriptors.
    const juce::File f (juce::String (BROCOT_SOURCE_DIR)
                            + "/resources/landscape_graph_6mod.json.gz");
    if (! f.existsAsFile())
        SKIP ("shipped mod map not present");
    Landscape ls;
    REQUIRE (ls.load (f));
    REQUIRE (ls.isModulationMap());
    REQUIRE (ls.hasMotion());
    REQUIRE (ls.getName() == "6-op mod");
    REQUIRE (ls.getJumps().empty());   // deliberately ships without wormholes
    for (const auto& n : ls.getNodes())
    {
        REQUIRE (n.mods.size() == 1);  // exactly one assignment this pass
        const auto& m = n.mods.front();
        CHECK ((m.kind == 0 || m.kind == 1));
        CHECK ((m.shape == 0 || m.shape == 1));
        CHECK (m.rate >= 0.3f);  CHECK (m.rate <= 7.0f);
        CHECK (m.depth >= 0.3f); CHECK (m.depth <= 1.0f);
        CHECK (n.motion >= 0.0f); CHECK (n.motion <= 1.0f);
        CHECK (n.rateN >= 0.0f);  CHECK (n.rateN <= 1.0f);
    }
}

TEST_CASE ("shipped base map's jumps block loads when present", "[Landscape]")
{
    // Guarded end-to-end check of the real artifact: if the shipped 6-op map
    // carries a jumps block (mined by phase3/gpu_jumps.py), it must parse
    // with in-range indices and rendered roughness below tau.
    const juce::File f (juce::String (BROCOT_SOURCE_DIR)
                            + "/resources/landscape_graph.json.gz");
    if (! f.existsAsFile())
        SKIP ("shipped base map not present");
    Landscape ls;
    REQUIRE (ls.load (f));
    if (ls.getJumps().empty())
        SKIP ("map carries no jumps block");
    REQUIRE (ls.getJumpTau() > 0.0f);
    for (const auto& j : ls.getJumps())
    {
        CHECK (j.a < j.b);
        CHECK (j.b < ls.getNodeCount());
        CHECK (j.rough <= ls.getJumpTau() + 1e-4f);
    }
    CHECK (! ls.jumpsFrom (ls.getJumps().front().a).empty());
}
