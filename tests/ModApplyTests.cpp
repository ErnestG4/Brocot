/*
    Tests for the mod-map → universal-modulator-bank mapping (M3b).

    Pure decision logic only: modParamPlan / resetPlan emit (paramID,
    denormalized value) pairs pinned here against the REAL ParameterTree
    ranges (rate logRange 0.1–800 Hz, shape choice order Sine..ExpDecay,
    depth 0..1, targetIndex 0..15). applyModsToAPVTS is a thin
    setValueNotifyingHost loop exercised at runtime through the plugin —
    the tests link neither juce_audio_processors nor an APVTS.
*/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "../source/pull/ModApply.h"

using brocot::Landscape;
namespace modapply = brocot::modapply;

namespace
{
    using Plan = std::vector<std::pair<juce::String, float>>;

    // The planned value for one param id, or nullopt-ish via found flag.
    float valueOf (const Plan& plan, const juce::String& id, bool& found)
    {
        found = false;
        float out = 0.0f;
        for (const auto& [pid, v] : plan)
            if (pid == id) { found = true; out = v; }
        return out;
    }
    float requireValue (const Plan& plan, const juce::String& id)
    {
        bool found = false;
        const float v = valueOf (plan, id, found);
        REQUIRE (found);
        return v;
    }
}

TEST_CASE ("modParamPlan realizes a depth-target sine spec", "[ModApply]")
{
    // kind 0 (op depth), op 3, sine, 1.7 Hz, mod depth 0.8 — the canonical
    // shipped-map shape. Slot 0.
    Landscape::ModSpec spec { 0, 3, 0, 1.7f, 0.8f };
    const auto plan = modapply::modParamPlan (spec, 0);

    // Full 11-param slot coverage — a leftover lock/PSK from the previous
    // patch must never deform the planned LFO.
    REQUIRE (plan.size() == 11);
    for (const auto& [id, v] : plan)
        CHECK (id.startsWith ("mod0."));

    CHECK (requireValue (plan, "mod0.rate")  == Catch::Approx (1.7f));
    CHECK (requireValue (plan, "mod0.shape") == Catch::Approx (0.0f));   // "Sine"
    CHECK (requireValue (plan, "mod0.depth")
               == Catch::Approx (0.8f * modapply::kDepthSwing));
    CHECK (requireValue (plan, "mod0.targetKind")
               == Catch::Approx ((float) modapply::kTargetKindOpDepth));  // 3
    CHECK (requireValue (plan, "mod0.targetIndex") == Catch::Approx (3.0f));
    // Neutralized fields: free-running, unquantized, not key-synced.
    CHECK (requireValue (plan, "mod0.lockKind")  == Catch::Approx (0.0f));
    CHECK (requireValue (plan, "mod0.lockIndex") == Catch::Approx (0.0f));
    CHECK (requireValue (plan, "mod0.psk")       == Catch::Approx (0.0f));
    CHECK (requireValue (plan, "mod0.pskSmooth") == Catch::Approx (0.0f));
    CHECK (requireValue (plan, "mod0.keySync")   == Catch::Approx (0.0f));
    CHECK (requireValue (plan, "mod0.kappa")     == Catch::Approx (8.0f));
}

TEST_CASE ("modParamPlan maps a feedback-target spec onto op-0 depth", "[ModApply]")
{
    // kind 1 = op-0 feedback. The bank has no feedback route (TargetKind
    // has no Feedback member) — the documented approximation is OpDepth on
    // op 0, whatever the spec's op field says.
    Landscape::ModSpec spec { 1, 5, 0, 0.33f, 0.45f };
    const auto plan = modapply::modParamPlan (spec, 2);

    for (const auto& [id, v] : plan)
        CHECK (id.startsWith ("mod2."));
    CHECK (requireValue (plan, "mod2.targetKind")
               == Catch::Approx ((float) modapply::kTargetKindOpDepth));
    CHECK (requireValue (plan, "mod2.targetIndex") == Catch::Approx (0.0f));
    CHECK (requireValue (plan, "mod2.rate")  == Catch::Approx (0.33f));
    CHECK (requireValue (plan, "mod2.depth")
               == Catch::Approx (0.45f * modapply::kDepthSwing));
}

TEST_CASE ("modParamPlan maps tri onto the bank's Triangle choice", "[ModApply]")
{
    // ParameterTree shapeChoices: Sine, VonMises, Gaussian, Square, SawRise,
    // SawFall, Triangle, ExpDecay — tri is index 6, NOT 1.
    Landscape::ModSpec spec { 0, 1, 1, 2.0f, 0.5f };
    const auto plan = modapply::modParamPlan (spec, 0);
    CHECK (requireValue (plan, "mod0.shape") == Catch::Approx (6.0f));
}

TEST_CASE ("modParamPlan clamps rate into the bank's 0.1-800 Hz range", "[ModApply]")
{
    Landscape::ModSpec slow { 0, 0, 0, 0.001f, 0.5f };
    CHECK (requireValue (modapply::modParamPlan (slow, 0), "mod0.rate")
               == Catch::Approx (0.1f));

    Landscape::ModSpec fast { 0, 0, 0, 5000.0f, 0.5f };
    CHECK (requireValue (modapply::modParamPlan (fast, 0), "mod0.rate")
               == Catch::Approx (800.0f));

    // Depth clamps to 0..1 before scaling; target index to the param's 0..15.
    Landscape::ModSpec deep { 0, 40, 0, 1.0f, 3.0f };
    const auto plan = modapply::modParamPlan (deep, 0);
    CHECK (requireValue (plan, "mod0.depth")
               == Catch::Approx (modapply::kDepthSwing));
    CHECK (requireValue (plan, "mod0.targetIndex") == Catch::Approx (15.0f));

    // Out-of-range slots plan nothing at all.
    CHECK (modapply::modParamPlan (slow, -1).empty());
    CHECK (modapply::modParamPlan (slow, modapply::kNumSlots).empty());
}

TEST_CASE ("resetPlan zeroes all 16 slots' depths", "[ModApply]")
{
    const auto plan = modapply::resetPlan();
    REQUIRE ((int) plan.size() == modapply::kNumSlots);
    for (int i = 0; i < modapply::kNumSlots; ++i)
    {
        CHECK (plan[(size_t) i].first == modapply::slotParamId (i, "depth"));
        CHECK (plan[(size_t) i].second == Catch::Approx (0.0f));
    }
}
