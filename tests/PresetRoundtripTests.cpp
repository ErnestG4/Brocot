#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <juce_core/juce_core.h>

#include "audio/GrooveLock.h"

// We're testing the JSON-shape contract here. The full apply/serialise
// roundtrip is tested at runtime through the plugin host; here we verify
// that hand-constructed JSON has the expected schema and is parsable.

TEST_CASE ("Sample v2 Avatar 1 JSON parses with provenance block", "[Preset]")
{
    static constexpr auto json = R"({
        "name": "Avatar 1",
        "version": "2.0",
        "provenance": {
            "kind": "manual",
            "author": "Combust",
            "description": "RLLLR shared path",
            "category": "discovered"
        },
        "engine": {
            "polyphony": "mono",
            "octaveShift": 0,
            "pathMode": "shared",
            "sharedPath": "RLLLR",
            "operators": [
                {"enabled": true,  "position": 1, "depth": 5.13},
                {"enabled": true,  "position": 3, "depth": 0.86},
                {"enabled": true,  "position": 5, "depth": 0.90}
            ]
        },
        "filter":   {"type": "lp",   "cutoff": 0.65, "resonance": 0.15},
        "envelope": {"attack": 0.01, "decay": 0.15, "sustain": 0.80, "release": 0.40},
        "lfo":      {"shape": "sine", "rate": 0.5, "depth": 0.0, "keySync": false},
        "master":   {"level": 0.5}
    })";

    juce::var parsed;
    auto result = juce::JSON::parse (json, parsed);
    REQUIRE (result.wasOk());
    REQUIRE (parsed.isObject());

    auto* root = parsed.getDynamicObject();
    REQUIRE (root != nullptr);
    REQUIRE (root->getProperty ("name").toString() == "Avatar 1");
    REQUIRE (root->getProperty ("version").toString() == "2.0");

    auto* prov = root->getProperty ("provenance").getDynamicObject();
    REQUIRE (prov != nullptr);
    REQUIRE (prov->getProperty ("kind").toString() == "manual");
    REQUIRE (prov->getProperty ("author").toString() == "Combust");
    REQUIRE (prov->getProperty ("category").toString() == "discovered");

    auto* engine = root->getProperty ("engine").getDynamicObject();
    REQUIRE (engine != nullptr);
    REQUIRE (engine->getProperty ("pathMode").toString() == "shared");
    REQUIRE (engine->getProperty ("sharedPath").toString() == "RLLLR");

    const auto* ops = engine->getProperty ("operators").getArray();
    REQUIRE (ops->size() == 3);
    const auto* firstOp = (*ops)[0].getDynamicObject();
    REQUIRE (firstOp != nullptr);
    REQUIRE ((bool) firstOp->getProperty ("enabled"));
    REQUIRE ((int) firstOp->getProperty ("position") == 1);
    REQUIRE ((double) firstOp->getProperty ("depth") == Catch::Approx (5.13).margin (1e-6));
}

TEST_CASE ("Sample v2 family-search preset carries search lineage", "[Preset]")
{
    // What an ARS-generated preset will look like (P3.5).
    static constexpr auto json = R"({
        "name": "sawtooth-1",
        "version": "2.0",
        "provenance": {
            "kind": "family-search",
            "author": "ARS Phase 3",
            "description": "Sawtooth family discovery.",
            "category": "Sawtooth",
            "ars_version": "0.x",
            "search": {
                "family": "sawtooth",
                "seed": 12345,
                "objective": 0.084,
                "params": { "n_samples": 1000, "n_refine": 20, "q_max": 16 },
                "timestamp": "2026-05-12T22:00:00Z"
            }
        },
        "engine": {"pathMode": "free", "operators": [{"enabled": true, "path": "R", "depth": 1.0}]}
    })";

    juce::var parsed;
    REQUIRE (juce::JSON::parse (json, parsed).wasOk());
    auto* prov = parsed.getDynamicObject()->getProperty ("provenance").getDynamicObject();
    REQUIRE (prov != nullptr);
    REQUIRE (prov->getProperty ("kind").toString() == "family-search");

    auto* search = prov->getProperty ("search").getDynamicObject();
    REQUIRE (search != nullptr);
    REQUIRE (search->getProperty ("family").toString() == "sawtooth");
    REQUIRE ((int) search->getProperty ("seed") == 12345);
    REQUIRE ((double) search->getProperty ("objective") == Catch::Approx (0.084));
}

TEST_CASE ("All factory .brocot files are v2 with provenance", "[Preset]")
{
    juce::File factoryDir (juce::String (BROCOT_SOURCE_DIR)
                           + "/resources/presets/factory");
    REQUIRE (factoryDir.isDirectory());

    // Recursive scan: hand-authored 32 live at top level, ARS-generated
    // discoveries live under factory/discovered/<family>/ — every .brocot
    // anywhere under factory/ must satisfy v2.
    auto files = factoryDir.findChildFiles (juce::File::findFiles, true, "*.brocot");
    REQUIRE (files.size() >= 32);

    for (const auto& f : files)
    {
        INFO ("file: " + f.getFileName());
        const auto json = juce::JSON::parse (f);
        REQUIRE (json.isObject());
        auto* root = json.getDynamicObject();
        REQUIRE (root != nullptr);
        REQUIRE (root->getProperty ("name").toString().isNotEmpty());
        REQUIRE (root->getProperty ("version").toString() == "2.0");
        REQUIRE (root->getProperty ("provenance").isObject());

        auto* prov = root->getProperty ("provenance").getDynamicObject();
        REQUIRE (prov != nullptr);
        // v2 always has `kind`; manual / family-search / criticality-search / reference-match
        REQUIRE (prov->getProperty ("kind").toString().isNotEmpty());

        REQUIRE (root->getProperty ("engine").isObject());
        auto* engine = root->getProperty ("engine").getDynamicObject();
        REQUIRE (engine != nullptr);
        REQUIRE (engine->getProperty ("operators").isArray());
        const auto* ops = engine->getProperty ("operators").getArray();
        // Writers ship different op-array lengths (hand-authored/P3.6/P3.7 = 8,
        // P3.8 = 16, P3.12 = 64, atlas/flavours = compact enabled-only lists).
        // The loader pads any length to kNumOperators (64), so the on-disk
        // invariant is just "at least one op, never more than the engine".
        REQUIRE (ops->size() >= 1);
        REQUIRE (ops->size() <= 64);
    }
}

TEST_CASE ("v1 preset (no provenance) is still parseable JSON", "[Preset]")
{
    // The loader's v1->v2 migration handles this in applyTo(); here we
    // just verify the JSON itself is valid input to the loader.
    static constexpr auto json = R"({
        "name": "old preset",
        "author": "Will",
        "description": "from v1",
        "version": "1.0",
        "category": "user",
        "engine": {
            "pathMode": "free",
            "operators": [{"enabled": true, "path": "R", "depth": 1.0}]
        }
    })";

    juce::var parsed;
    REQUIRE (juce::JSON::parse (json, parsed).wasOk());
    auto* root = parsed.getDynamicObject();
    REQUIRE (root != nullptr);
    REQUIRE (root->getProperty ("version").toString() == "1.0");
    REQUIRE_FALSE (root->hasProperty ("provenance"));
    // The loader synthesises provenance on applyTo. Tested via host
    // integration; here we just confirm the input shape.
}

TEST_CASE ("Preset without groove-feel keys leaves the S4 defaults intact", "[Preset]")
{
    // Presets predate the steer.groove* params (S4) and the schema neither
    // serialises nor applies them — PresetSchema.cpp has no groove keys — so
    // loading any old preset leaves the params at their layout defaults,
    // which are pinned to the pre-S4 hardcoded feel constants.
    static constexpr auto json = R"({
        "name": "pre-groove preset", "version": "2.0",
        "provenance": {"kind": "manual"},
        "engine": {
            "pathMode": "free",
            "operators": [{"enabled": true, "path": "RL", "depth": 1.0, "detune": 3.5}]
        }
    })";

    juce::var parsed;
    REQUIRE (juce::JSON::parse (json, parsed).wasOk());
    auto* root = parsed.getDynamicObject();
    REQUIRE (root != nullptr);
    REQUIRE_FALSE (root->hasProperty ("steer"));
    REQUIRE_FALSE (root->hasProperty ("groove"));
    auto* engine = root->getProperty ("engine").getDynamicObject();
    REQUIRE (engine != nullptr);
    REQUIRE_FALSE (engine->hasProperty ("grooveGravity"));
    REQUIRE_FALSE (engine->hasProperty ("grooveRestless"));
    REQUIRE_FALSE (engine->hasProperty ("grooveLock"));

    // The defaults such a load leaves in place — the pre-S4 constants.
    REQUIRE (brocot::groove::kDefaultGravity      == 0.6f);
    REQUIRE (brocot::groove::kDefaultRestlessness == 0.3f);
    REQUIRE (brocot::groove::kDefaultLock         == true);
}

TEST_CASE ("Free-mode v2 preset uses 'path' instead of 'position'", "[Preset]")
{
    static constexpr auto json = R"({
        "name": "free demo", "version": "2.0",
        "provenance": {"kind": "manual"},
        "engine": {
            "pathMode": "free",
            "operators": [
                {"enabled": true,  "path": "R",     "depth": 1.0},
                {"enabled": true,  "path": "RLL",   "depth": 0.5},
                {"enabled": false, "path": "",      "depth": 0.0}
            ]
        }
    })";

    juce::var parsed;
    REQUIRE (juce::JSON::parse (json, parsed).wasOk());
    auto* engine = parsed.getDynamicObject()->getProperty ("engine").getDynamicObject();
    REQUIRE (engine->getProperty ("pathMode").toString() == "free");
    const auto* ops = engine->getProperty ("operators").getArray();
    REQUIRE ((*ops)[1].getDynamicObject()->getProperty ("path").toString() == "RLL");
}
