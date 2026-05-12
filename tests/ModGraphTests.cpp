#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "params/ModTarget.h"
#include "params/sources/MIDISource.h"
#include "params/sources/EnvSource.h"

using namespace brocot;

namespace
{
    // Trivial stub source for testing the target's combination logic in
    // isolation. Returning a fixed value avoids dependencies on MIDI state.
    class FixedSource : public ModulationSource
    {
    public:
        explicit FixedSource (float v) : value (v) {}
        void set (float v) noexcept { value = v; }
        float getModulationValue() const noexcept override { return value; }
        juce::String getSourceId() const override { return "fixed"; }
    private:
        float value;
    };
}

// ----------------------------------------------------------------------------
// ModulationTarget — base + Σ source × depth, clamped.
// ----------------------------------------------------------------------------

TEST_CASE ("ModulationTarget with no sources returns base", "[ModTarget]")
{
    ModulationTarget t;
    t.setRange (0.0f, 1.0f);
    t.setBase (0.3f);
    REQUIRE (t.getEffectiveValue() == Catch::Approx (0.3f));
}

TEST_CASE ("Single bipolar source adds source × depth", "[ModTarget]")
{
    FixedSource s (0.5f);
    ModulationTarget t;
    t.setRange (-1.0f, 1.0f);
    t.setBase (0.0f);
    t.addConnection (&s, 0.4f, true);
    REQUIRE (t.getEffectiveValue() == Catch::Approx (0.2f));

    s.set (-1.0f);
    REQUIRE (t.getEffectiveValue() == Catch::Approx (-0.4f));
}

TEST_CASE ("Unipolar connection clamps negative source to 0", "[ModTarget]")
{
    FixedSource s (-0.5f);
    ModulationTarget t;
    t.setRange (0.0f, 1.0f);
    t.setBase (0.5f);
    t.addConnection (&s, 0.4f, false);
    // -0.5 -> max(0, -0.5) * 0.4 = 0, so effective stays at base.
    REQUIRE (t.getEffectiveValue() == Catch::Approx (0.5f));

    s.set (0.75f);
    REQUIRE (t.getEffectiveValue() == Catch::Approx (0.5f + 0.75f * 0.4f));
}

TEST_CASE ("Multiple sources sum into the same target", "[ModTarget]")
{
    FixedSource lfo (0.5f);
    FixedSource env (1.0f);
    ModulationTarget t;
    t.setRange (-2.0f, 2.0f);
    t.setBase (0.1f);
    t.addConnection (&lfo, 0.2f, true);    // contributes +0.1
    t.addConnection (&env, 0.4f, false);   // contributes +0.4
    REQUIRE (t.getEffectiveValue() == Catch::Approx (0.6f));
}

TEST_CASE ("Effective value clamps to configured range", "[ModTarget]")
{
    FixedSource s (1.0f);
    ModulationTarget t;
    t.setRange (0.0f, 1.0f);
    t.setBase (0.8f);
    t.addConnection (&s, 0.5f, true);  // would be 1.3, clamps to 1.0
    REQUIRE (t.getEffectiveValue() == Catch::Approx (1.0f));
}

TEST_CASE ("removeConnection drops a specific source", "[ModTarget]")
{
    FixedSource a (1.0f), b (1.0f);
    ModulationTarget t;
    t.setRange (-10.0f, 10.0f);
    t.setBase (0.0f);
    t.addConnection (&a, 0.5f, true);
    t.addConnection (&b, 0.3f, true);
    REQUIRE (t.getEffectiveValue() == Catch::Approx (0.8f));

    t.removeConnection (&a);
    REQUIRE (t.getEffectiveValue() == Catch::Approx (0.3f));
}

// ----------------------------------------------------------------------------
// MIDI source mapping.
// ----------------------------------------------------------------------------

TEST_CASE ("MIDIModWheelSource maps 0..127 → 0..1", "[ModSource][MIDI]")
{
    MIDIModWheelSource w;
    REQUIRE (w.getModulationValue() == Catch::Approx (0.0f));
    w.setRaw14 (0);     REQUIRE (w.getModulationValue() == Catch::Approx (0.0f));
    w.setRaw14 (64);    REQUIRE (w.getModulationValue() == Catch::Approx (64.0f / 127.0f));
    w.setRaw14 (127);   REQUIRE (w.getModulationValue() == Catch::Approx (1.0f));
}

TEST_CASE ("MIDIPitchBendSource maps 14-bit raw → [-1, +1]", "[ModSource][MIDI]")
{
    MIDIPitchBendSource pb;
    REQUIRE (pb.getModulationValue() == Catch::Approx (0.0f));

    pb.setBendRaw14 (8192);    // center
    REQUIRE (pb.getModulationValue() == Catch::Approx (0.0f));

    pb.setBendRaw14 (16383);   // max
    REQUIRE (pb.getModulationValue() == Catch::Approx ((16383 - 8192) / 8192.0f).margin (0.001f));

    pb.setBendRaw14 (0);       // min
    REQUIRE (pb.getModulationValue() == Catch::Approx (-1.0f));
}

TEST_CASE ("MIDIChannelPressureSource maps 0..127 → 0..1", "[ModSource][MIDI]")
{
    MIDIChannelPressureSource cp;
    cp.setRaw7 (0);     REQUIRE (cp.getModulationValue() == Catch::Approx (0.0f));
    cp.setRaw7 (127);   REQUIRE (cp.getModulationValue() == Catch::Approx (1.0f));
}

TEST_CASE ("AmpEnvSource passes the current value through", "[ModSource]")
{
    AmpEnvSource e;
    REQUIRE (e.getModulationValue() == Catch::Approx (0.0f));
    e.setCurrent (0.42f);
    REQUIRE (e.getModulationValue() == Catch::Approx (0.42f));
}
