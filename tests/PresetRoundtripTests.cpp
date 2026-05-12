#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <juce_core/juce_core.h>

// We're testing the JSON-shape contract here. The full apply/serialise
// roundtrip is tested at runtime through the plugin host; here we verify
// that hand-constructed JSON has the expected schema and is parsable.

TEST_CASE ("Sample Avatar 1 JSON parses and has expected fields", "[Preset]")
{
    static constexpr auto json = R"({
        "name": "Avatar 1",
        "author": "Combust",
        "description": "RLLLR shared path",
        "version": "1.0",
        "category": "discovered",
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
    REQUIRE (root->getProperty ("version").toString() == "1.0");

    auto* engine = root->getProperty ("engine").getDynamicObject();
    REQUIRE (engine != nullptr);
    REQUIRE (engine->getProperty ("pathMode").toString() == "shared");
    REQUIRE (engine->getProperty ("sharedPath").toString() == "RLLLR");
    REQUIRE (engine->getProperty ("operators").isArray());

    const auto* ops = engine->getProperty ("operators").getArray();
    REQUIRE (ops->size() == 3);
    const auto* firstOp = (*ops)[0].getDynamicObject();
    REQUIRE (firstOp != nullptr);
    REQUIRE ((bool) firstOp->getProperty ("enabled"));
    REQUIRE ((int) firstOp->getProperty ("position") == 1);
    REQUIRE ((double) firstOp->getProperty ("depth") == Catch::Approx (5.13).margin (1e-6));
}

TEST_CASE ("Free-mode preset uses 'path' instead of 'position'", "[Preset]")
{
    static constexpr auto json = R"({
        "name": "free demo", "version": "1.0",
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
