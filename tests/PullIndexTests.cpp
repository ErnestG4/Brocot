/*
    Tests for the runtime PullIndex loader / lookup.

    The test fixtures synthesise a tiny pull_index.json in a temp dir
    rather than loading the full 2.6 MB factory index, so we control
    every value and can assert exactly on what the loader sees.

    Coverage:
      - load() returns false (and an error message) for missing or
        malformed input
      - top-level scalars (q_max, f_carrier_ref) round-trip
      - categories are sorted alphabetically
      - per-exemplar fields (preset / objective / amps / ops) all
        round-trip with the correct types and lengths
      - getBestOps() returns the lowest-objective exemplar's ops
      - morph() linearly interpolates depth, snaps path / enabled
        at the α=0.5 threshold, and clamps α into [0, 1]
*/

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <juce_core/juce_core.h>

#include "../source/pull/PullIndex.cpp"

namespace
{

juce::String makeFakeIndexJSON()
{
    // Two categories — Sawtooth (best obj 0.10, worst 0.30) and
    // Square (single exemplar). Op lists are length-3 for brevity;
    // the loader doesn't care that they're shorter than the real
    // kNumOperators.
    return R"json(
{
  "version": "1.0",
  "f_carrier_ref": 220.0,
  "q_max": 4,
  "categories": {
    "Sawtooth": {
      "preset_count": 2,
      "best_obj_preset": "saw/a.brocot",
      "centroid_amps": [0.5, 0.3, 0.15, 0.05],
      "exemplars": [
        {
          "preset": "saw/a.brocot", "objective": 0.10, "n_ops": 3,
          "amps":  [0.6, 0.25, 0.10, 0.05],
          "ops": [
            { "enabled": true,  "path": "R",   "depth": 1.5 },
            { "enabled": true,  "path": "RR",  "depth": 0.8 },
            { "enabled": false, "path": "",    "depth": 0.0 }
          ]
        },
        {
          "preset": "saw/b.brocot", "objective": 0.30, "n_ops": 2,
          "amps":  [0.4, 0.4, 0.15, 0.05],
          "ops": [
            { "enabled": true,  "path": "L",   "depth": 2.0 },
            { "enabled": true,  "path": "LL",  "depth": 1.0 },
            { "enabled": false, "path": "",    "depth": 0.0 }
          ]
        }
      ]
    },
    "Square": {
      "preset_count": 1,
      "best_obj_preset": "sq/a.brocot",
      "centroid_amps": [0.5, 0.0, 0.4, 0.1],
      "exemplars": [
        {
          "preset": "sq/a.brocot", "objective": 0.20, "n_ops": 3,
          "amps":  [0.5, 0.0, 0.4, 0.1],
          "ops": [
            { "enabled": true,  "path": "R",   "depth": 2.4 },
            { "enabled": true,  "path": "RR",  "depth": 1.84 },
            { "enabled": true,  "path": "L",   "depth": 1.2 }
          ]
        }
      ]
    }
  }
}
)json";
}

juce::File writeTempIndex (const juce::String& json)
{
    auto f = juce::File::createTempFile (".pull_index.json");
    f.replaceWithText (json);
    return f;
}

} // namespace

TEST_CASE ("PullIndex: load returns false for missing file")
{
    brocot::PullIndex idx;
    juce::String err;
    REQUIRE (! idx.load (juce::File ("/does/not/exist.json"), &err));
    REQUIRE (err.contains ("not found"));
}

TEST_CASE ("PullIndex: load returns false for non-object JSON")
{
    auto f = writeTempIndex ("\"a string, not an object\"");
    brocot::PullIndex idx;
    juce::String err;
    REQUIRE (! idx.load (f, &err));
    REQUIRE (err.isNotEmpty());
    f.deleteFile();
}

TEST_CASE ("PullIndex: parses top-level scalars + category count")
{
    auto f = writeTempIndex (makeFakeIndexJSON());
    brocot::PullIndex idx;
    REQUIRE (idx.load (f));
    REQUIRE (idx.getQMax() == 4);
    REQUIRE_THAT (idx.getFCarrierRef(), Catch::Matchers::WithinAbs (220.0, 1e-9));
    REQUIRE (idx.getCategoryCount() == 2);
    f.deleteFile();
}

TEST_CASE ("PullIndex: categories sorted alphabetically")
{
    auto f = writeTempIndex (makeFakeIndexJSON());
    brocot::PullIndex idx;
    REQUIRE (idx.load (f));
    const auto names = idx.getCategoryNames();
    REQUIRE (names.size() == 2);
    REQUIRE (names[0] == "Sawtooth");
    REQUIRE (names[1] == "Square");
}

TEST_CASE ("PullIndex: per-category fields round-trip")
{
    auto f = writeTempIndex (makeFakeIndexJSON());
    brocot::PullIndex idx;
    REQUIRE (idx.load (f));

    const auto* saw = idx.getCategory ("Sawtooth");
    REQUIRE (saw != nullptr);
    REQUIRE (saw->presetCount == 2);
    REQUIRE (saw->bestObjPreset == "saw/a.brocot");
    REQUIRE (saw->centroidAmps.size() == 4);
    REQUIRE_THAT (saw->centroidAmps[0], Catch::Matchers::WithinAbs (0.5f, 1e-6f));
    REQUIRE (saw->exemplars.size() == 2);

    // Exemplars come straight from the JSON in source order; the writer
    // produces them sorted by objective ascending.
    REQUIRE_THAT (saw->exemplars.front().objective, Catch::Matchers::WithinAbs (0.10, 1e-9));
    REQUIRE_THAT (saw->exemplars.back().objective,  Catch::Matchers::WithinAbs (0.30, 1e-9));

    const auto& a = saw->exemplars.front();
    REQUIRE (a.preset == "saw/a.brocot");
    REQUIRE (a.nOps   == 3);
    REQUIRE (a.amps.size() == 4);
    REQUIRE (a.ops.size()  == 3);

    REQUIRE (a.ops[0].enabled);
    REQUIRE (a.ops[0].path  == "R");
    REQUIRE_THAT (a.ops[0].depth, Catch::Matchers::WithinAbs (1.5f, 1e-6f));

    REQUIRE (! a.ops[2].enabled);
    REQUIRE (a.ops[2].path.isEmpty());
}

TEST_CASE ("PullIndex: getBestOps returns lowest-objective exemplar's ops")
{
    auto f = writeTempIndex (makeFakeIndexJSON());
    brocot::PullIndex idx;
    REQUIRE (idx.load (f));

    const auto* ops = idx.getBestOps ("Sawtooth");
    REQUIRE (ops != nullptr);
    REQUIRE (ops->size() == 3);
    REQUIRE ((*ops)[0].path == "R");           // matches saw/a.brocot
    REQUIRE ((*ops)[1].path == "RR");
}

TEST_CASE ("PullIndex: getBestOps returns null for unknown category")
{
    auto f = writeTempIndex (makeFakeIndexJSON());
    brocot::PullIndex idx;
    REQUIRE (idx.load (f));
    REQUIRE (idx.getBestOps ("Made-up") == nullptr);
}

// ---------------------------------------------------------------------------
// Morphing
// ---------------------------------------------------------------------------

TEST_CASE ("PullIndex::morph: α=0 returns current unchanged")
{
    using OpState = brocot::PullIndex::OpState;
    std::vector<OpState> cur { { "R",  1.0f, true }, { "",  0.0f, false } };
    std::vector<OpState> tgt { { "LL", 2.0f, true }, { "L", 0.5f, true  } };
    auto out = brocot::PullIndex::morph (cur, tgt, 0.0f);
    REQUIRE (out.size() == 2);
    REQUIRE (out[0].path == "R");
    REQUIRE_THAT (out[0].depth, Catch::Matchers::WithinAbs (1.0f, 1e-6f));
    REQUIRE (out[0].enabled);
    REQUIRE (out[1].path.isEmpty());
}

TEST_CASE ("PullIndex::morph: α=1 returns target unchanged")
{
    using OpState = brocot::PullIndex::OpState;
    std::vector<OpState> cur { { "R",  1.0f, true } };
    std::vector<OpState> tgt { { "LL", 2.0f, true } };
    auto out = brocot::PullIndex::morph (cur, tgt, 1.0f);
    REQUIRE (out[0].path == "LL");
    REQUIRE_THAT (out[0].depth, Catch::Matchers::WithinAbs (2.0f, 1e-6f));
}

TEST_CASE ("PullIndex::morph: depth lerps continuously")
{
    using OpState = brocot::PullIndex::OpState;
    std::vector<OpState> cur { { "R", 0.0f, true } };
    std::vector<OpState> tgt { { "R", 4.0f, true } };
    auto out = brocot::PullIndex::morph (cur, tgt, 0.25f);
    REQUIRE_THAT (out[0].depth, Catch::Matchers::WithinAbs (1.0f, 1e-6f));
    out = brocot::PullIndex::morph (cur, tgt, 0.75f);
    REQUIRE_THAT (out[0].depth, Catch::Matchers::WithinAbs (3.0f, 1e-6f));
}

TEST_CASE ("PullIndex::morph: path/enabled snap at α=0.5")
{
    using OpState = brocot::PullIndex::OpState;
    std::vector<OpState> cur { { "R", 1.0f, true  } };
    std::vector<OpState> tgt { { "L", 1.0f, false } };

    auto out = brocot::PullIndex::morph (cur, tgt, 0.49f);
    REQUIRE (out[0].path    == "R");
    REQUIRE (out[0].enabled == true);

    out = brocot::PullIndex::morph (cur, tgt, 0.50f);
    REQUIRE (out[0].path    == "L");
    REQUIRE (out[0].enabled == false);
}

TEST_CASE ("PullIndex::morph: α is clamped to [0, 1]")
{
    using OpState = brocot::PullIndex::OpState;
    std::vector<OpState> cur { { "R", 0.0f, true } };
    std::vector<OpState> tgt { { "R", 4.0f, true } };
    auto neg  = brocot::PullIndex::morph (cur, tgt, -0.5f);
    auto big  = brocot::PullIndex::morph (cur, tgt,  1.5f);
    REQUIRE_THAT (neg[0].depth, Catch::Matchers::WithinAbs (0.0f, 1e-6f));
    REQUIRE_THAT (big[0].depth, Catch::Matchers::WithinAbs (4.0f, 1e-6f));
}

TEST_CASE ("PullIndex::morph: output length is min(current, target)")
{
    using OpState = brocot::PullIndex::OpState;
    std::vector<OpState> cur (5, { "R", 0.0f, true });
    std::vector<OpState> tgt (3, { "L", 1.0f, true });
    auto out = brocot::PullIndex::morph (cur, tgt, 1.0f);
    REQUIRE (out.size() == 3);
}

// ---------------------------------------------------------------------------
// Integration test: load the real shipped index
// ---------------------------------------------------------------------------

TEST_CASE ("PullIndex: shipped resources/pull_index.json loads cleanly")
{
    const auto file = brocot::PullIndex::findIndexFile();
    if (! file.existsAsFile())
        SKIP ("pull_index.json not on disk — generate via "
              "phase3.build_pull_index");

    brocot::PullIndex idx;
    juce::String err;
    REQUIRE (idx.load (file, &err));
    REQUIRE (idx.getCategoryCount() > 0);
    REQUIRE (idx.getQMax() == 32);

    // Sawtooth and at least one Textured-Smooth category should be there
    // — those are the obvious targets for the steer-tab MVP.
    const auto names = idx.getCategoryNames();
    REQUIRE (names.contains ("Sawtooth"));

    const auto* saw = idx.getCategory ("Sawtooth");
    REQUIRE (saw != nullptr);
    REQUIRE (saw->presetCount >= 8);
    REQUIRE (! saw->exemplars.empty());
    REQUIRE (saw->exemplars.front().amps.size() == (size_t) idx.getQMax());
    REQUIRE (! saw->exemplars.front().ops.empty());
}
