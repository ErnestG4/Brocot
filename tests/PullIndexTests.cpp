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

TEST_CASE ("PullIndex::morph: enabled stays on through transitions, off only at α=1")
{
    using OpState = brocot::PullIndex::OpState;
    std::vector<OpState> cur { { "R", 1.0f, true  } };
    std::vector<OpState> tgt { { "L", 1.0f, false } };

    // Mid-transition the slot is still on so depth can fade.
    auto out = brocot::PullIndex::morph (cur, tgt, 0.5f);
    REQUIRE (out[0].enabled == true);

    // Only at α=1 do we honour target's off-flag.
    out = brocot::PullIndex::morph (cur, tgt, 1.0f);
    REQUIRE (out[0].enabled == false);
}

TEST_CASE ("PullIndex::morph: depth ramps from 0 when transitioning off→on")
{
    // The pre-P3.10d morph would pop the slot on at α=0.5 with
    // half-target-depth. Now it should stay enabled throughout and
    // depth should lerp from 0 to target.depth smoothly.
    using OpState = brocot::PullIndex::OpState;
    std::vector<OpState> cur { { "",  0.0f, false } };
    std::vector<OpState> tgt { { "R", 4.0f, true  } };

    auto out = brocot::PullIndex::morph (cur, tgt, 0.0f);
    REQUIRE_THAT (out[0].depth, Catch::Matchers::WithinAbs (0.0f, 1e-6f));

    out = brocot::PullIndex::morph (cur, tgt, 0.5f);
    REQUIRE_THAT (out[0].depth, Catch::Matchers::WithinAbs (2.0f, 1e-6f));
    REQUIRE (out[0].enabled);   // pre-emptively on so audio thread can ramp

    out = brocot::PullIndex::morph (cur, tgt, 1.0f);
    REQUIRE_THAT (out[0].depth, Catch::Matchers::WithinAbs (4.0f, 1e-6f));
    REQUIRE (out[0].enabled);
}

TEST_CASE ("PullIndex::morph: depth fades to 0 when transitioning on→off")
{
    using OpState = brocot::PullIndex::OpState;
    std::vector<OpState> cur { { "R", 4.0f, true  } };
    std::vector<OpState> tgt { { "",  0.0f, false } };

    auto out = brocot::PullIndex::morph (cur, tgt, 0.25f);
    REQUIRE_THAT (out[0].depth, Catch::Matchers::WithinAbs (3.0f, 1e-6f));
    REQUIRE (out[0].enabled);

    out = brocot::PullIndex::morph (cur, tgt, 1.0f);
    REQUIRE_THAT (out[0].depth, Catch::Matchers::WithinAbs (0.0f, 1e-6f));
    REQUIRE (! out[0].enabled);
}

// ---------------------------------------------------------------------------
// SB-tree path walk
// ---------------------------------------------------------------------------

TEST_CASE ("PullIndex::pathWalk: identity at endpoints")
{
    REQUIRE (brocot::PullIndex::pathWalk ("R",  "L",  0.0f) == "R");
    REQUIRE (brocot::PullIndex::pathWalk ("R",  "L",  1.0f) == "L");
    REQUIRE (brocot::PullIndex::pathWalk ("RR", "RR", 0.5f) == "RR");
}

TEST_CASE ("PullIndex::pathWalk: walks via LCA when paths diverge")
{
    // from = "RR", to = "LL". LCA = "" (no common prefix).
    // Walk: RR → R → "" → L → LL.  4 steps total.
    // α values are picked safely inside each segment so half-step
    // rounding doesn't bite at the boundaries.
    using PI = brocot::PullIndex;
    REQUIRE (PI::pathWalk ("RR", "LL", 0.0f) == "RR");
    REQUIRE (PI::pathWalk ("RR", "LL", 0.2f) == "R");
    REQUIRE (PI::pathWalk ("RR", "LL", 0.5f) == "");   // at LCA
    REQUIRE (PI::pathWalk ("RR", "LL", 0.8f) == "L");
    REQUIRE (PI::pathWalk ("RR", "LL", 1.0f) == "LL");
}

TEST_CASE ("PullIndex::pathWalk: shared prefix means shorter walk")
{
    // from = "RRL", to = "RRR". LCA = "RR". Walk: RRL → RR → RRR. 2 steps.
    using PI = brocot::PullIndex;
    REQUIRE (PI::pathWalk ("RRL", "RRR", 0.0f) == "RRL");
    REQUIRE (PI::pathWalk ("RRL", "RRR", 0.5f) == "RR");
    REQUIRE (PI::pathWalk ("RRL", "RRR", 1.0f) == "RRR");
}

TEST_CASE ("PullIndex::pathWalk: from-empty path grows toward target")
{
    // from = "", to = "RL". Walk: "" → R → RL. 2 steps.
    using PI = brocot::PullIndex;
    REQUIRE (PI::pathWalk ("", "RL", 0.0f) == "");
    REQUIRE (PI::pathWalk ("", "RL", 0.5f) == "R");
    REQUIRE (PI::pathWalk ("", "RL", 1.0f) == "RL");
}

TEST_CASE ("PullIndex::pathWalk: long walks step granularly")
{
    // from = "RRRR", to = "LLLL". LCA = "". 8 steps total.
    using PI = brocot::PullIndex;
    REQUIRE (PI::pathWalk ("RRRR", "LLLL", 0.0f).length() == 4);
    REQUIRE (PI::pathWalk ("RRRR", "LLLL", 0.5f) == "");
    REQUIRE (PI::pathWalk ("RRRR", "LLLL", 1.0f).length() == 4);

    // Strictly monotonic: as α grows the path moves toward `to`.
    // No path repeats unless the round-to-nearest segments hit the
    // same step bucket — which only happens within a single segment.
    auto a = PI::pathWalk ("RRRR", "LLLL", 0.1f);
    auto b = PI::pathWalk ("RRRR", "LLLL", 0.3f);
    REQUIRE (a != b);   // different α, far enough apart, different paths
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

TEST_CASE ("PullIndex::morph: output length is max(current, target) — pads shorter side")
{
    using OpState = brocot::PullIndex::OpState;
    std::vector<OpState> cur (5, { "R", 4.0f, true });
    std::vector<OpState> tgt (3, { "L", 1.0f, true });
    auto out = brocot::PullIndex::morph (cur, tgt, 1.0f);
    // Output covers every slot in cur so the engine can fade out the
    // trailing slots that target leaves unspecified.
    REQUIRE (out.size() == 5);

    // The trailing-current slots (no matching target) fade to disabled
    // at α=1 — implicit "target is off here".
    REQUIRE (! out[3].enabled);
    REQUIRE_THAT (out[3].depth, Catch::Matchers::WithinAbs (0.0f, 1e-6f));
    REQUIRE (! out[4].enabled);
}

TEST_CASE ("PullIndex::morph: 64-current → 8-target audibly thins the carpet")
{
    // The bug Will hit on the steer tab's Textured-Smooth row: the
    // default 64-op carpet pulled toward a Textured-Smooth · 8 ops
    // target only morphed the first 8 slots — slots 8..63 stayed loud
    // at their original depths because morph iterated min(cur, tgt).
    // After P3.12(c) the iteration is max(cur, tgt) with implicit-off
    // padding. This test enforces that *most* of the 64 slots see a
    // depth drop after a single click, not just the first 8.
    using OpState = brocot::PullIndex::OpState;

    std::vector<OpState> cur;
    for (int i = 0; i < 64; ++i)
        cur.push_back ({ "R", 0.35f, true });   // every slot enabled at depth 0.35

    std::vector<OpState> tgt;
    for (int i = 0; i < 8; ++i)
        tgt.push_back ({ "L", 0.35f, true });

    auto nudged = brocot::PullIndex::morph (cur, tgt, 0.15f);
    REQUIRE (nudged.size() == 64);

    // Slots 8..63 should have lower depth than cur (target says off
    // → depth lerps toward 0).
    int dropped = 0;
    for (size_t i = 8; i < nudged.size(); ++i)
        if (nudged[i].depth < 0.35f - 1e-4f) ++dropped;
    REQUIRE (dropped >= 50);   // most of the 56 trailing slots fade
}

TEST_CASE ("PullIndex::morph: extra target slots beyond current fade IN from silence")
{
    using OpState = brocot::PullIndex::OpState;
    std::vector<OpState> cur (2, { "R", 1.0f, true });
    std::vector<OpState> tgt (5, { "L", 3.0f, true });

    // At α=0.5 the extra target slots should be half-faded-in.
    auto out = brocot::PullIndex::morph (cur, tgt, 0.5f);
    REQUIRE (out.size() == 5);
    REQUIRE (out[3].enabled);   // kept on through the transition
    REQUIRE_THAT (out[3].depth, Catch::Matchers::WithinAbs (1.5f, 1e-6f));
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
