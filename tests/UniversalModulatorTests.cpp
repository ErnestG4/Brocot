#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>

#include "audio/EnvelopeShape.h"
#include "audio/UniversalModulator.h"

using namespace brocot;

namespace
{
    constexpr float kPi = 3.14159265358979323846f;
}

// ----------------------------------------------------------------------------
// EnvelopeShape unipolar contract
// ----------------------------------------------------------------------------

TEST_CASE ("Unipolar envelopes are bounded in [0, 1]", "[Envelope]")
{
    for (int shapeIdx = 0; shapeIdx < (int) envelope::Shape::Count; ++shapeIdx)
    {
        const auto s = (envelope::Shape) shapeIdx;
        INFO ("shape: " << shapeIdx);
        for (int i = 0; i < 256; ++i)
        {
            const float p = (float) i / 256.0f * envelope::kTwoPi;
            const float v = envelope::unipolar (p, s, 8.0f);
            REQUIRE (v >= -1e-5f);
            REQUIRE (v <= 1.0f + 1e-5f);
        }
    }
}

TEST_CASE ("VonMises unipolar peaks at phase = 0", "[Envelope]")
{
    const float at0 = envelope::unipolar (0.0f, envelope::Shape::VonMises, 8.0f);
    const float atPi = envelope::unipolar (kPi, envelope::Shape::VonMises, 8.0f);
    REQUIRE (at0 == Catch::Approx (1.0f).margin (1e-5f));
    REQUIRE (atPi < 0.01f);   // strongly suppressed at antiphase with κ=8
}

TEST_CASE ("VonMises sharpness κ narrows the peak", "[Envelope]")
{
    // At small phase offsets, higher κ means the envelope falls off faster.
    const float offset = 0.5f;  // ~28 degrees
    const float lowK   = envelope::unipolar (offset, envelope::Shape::VonMises, 2.0f);
    const float highK  = envelope::unipolar (offset, envelope::Shape::VonMises, 30.0f);
    REQUIRE (highK < lowK);
}

TEST_CASE ("Square envelope follows κ-derived duty cycle", "[Envelope]")
{
    // κ = 25 → duty = 0.5. At phase ≈ 0.7×2π we should be in the OFF region.
    const float on  = envelope::unipolar (0.0f,                       envelope::Shape::Square, 25.0f);
    const float off = envelope::unipolar (0.7f * envelope::kTwoPi,    envelope::Shape::Square, 25.0f);
    REQUIRE (on  == Catch::Approx (1.0f));
    REQUIRE (off == Catch::Approx (0.0f));
}

TEST_CASE ("SawRise covers 0..1 monotonically", "[Envelope]")
{
    const float start  = envelope::unipolar (0.0f,                   envelope::Shape::SawRise, 0.0f);
    const float middle = envelope::unipolar (envelope::kTwoPi * 0.5f, envelope::Shape::SawRise, 0.0f);
    const float late   = envelope::unipolar (envelope::kTwoPi * 0.99f, envelope::Shape::SawRise, 0.0f);
    REQUIRE (start  == Catch::Approx (0.0f));
    REQUIRE (middle == Catch::Approx (0.5f).margin (0.01f));
    REQUIRE (late   > 0.95f);
}

// ----------------------------------------------------------------------------
// EnvelopeShape bipolar contract
// ----------------------------------------------------------------------------

TEST_CASE ("Bipolar shapes are bounded in [-1, +1]", "[Envelope]")
{
    for (int shapeIdx = 0; shapeIdx < (int) envelope::Shape::Count; ++shapeIdx)
    {
        const auto s = (envelope::Shape) shapeIdx;
        INFO ("shape: " << shapeIdx);
        for (int i = 0; i < 256; ++i)
        {
            const float p = (float) i / 256.0f * envelope::kTwoPi;
            const float v = envelope::bipolar (p, s, 8.0f);
            REQUIRE (v >= -1.0f - 1e-5f);
            REQUIRE (v <=  1.0f + 1e-5f);
        }
    }
}

TEST_CASE ("Bipolar sine peaks at π/2 and troughs at 3π/2", "[Envelope]")
{
    REQUIRE (envelope::bipolar (kPi * 0.5f, envelope::Shape::Sine, 0.0f) ==
             Catch::Approx (1.0f).margin (1e-5f));
    REQUIRE (envelope::bipolar (kPi * 1.5f, envelope::Shape::Sine, 0.0f) ==
             Catch::Approx (-1.0f).margin (1e-5f));
}

// ----------------------------------------------------------------------------
// PSK quantization
// ----------------------------------------------------------------------------

TEST_CASE ("PSK quantize with n=0/1 is a no-op", "[PSK]")
{
    REQUIRE (envelope::pskQuantize (1.234f, 0, 0.0f) == Catch::Approx (1.234f));
    REQUIRE (envelope::pskQuantize (1.234f, 1, 0.0f) == Catch::Approx (1.234f));
}

TEST_CASE ("PSK n=4 snaps to nearest π/2 step", "[PSK]")
{
    // Phase π/4 (45°) is exactly between 0 and π/2 — rounds up to π/2.
    REQUIRE (envelope::pskQuantize (kPi * 0.25f, 4, 0.0f) ==
             Catch::Approx (kPi * 0.5f).margin (1e-5f));
    // Phase 0.55 ≈ 31° rounds to 0.
    REQUIRE (envelope::pskQuantize (0.55f, 4, 0.0f) ==
             Catch::Approx (0.0f).margin (1e-5f));
}

TEST_CASE ("PSK smooth = 1.0 returns the input phase", "[PSK]")
{
    // smooth=1 → fully unquantized.
    REQUIRE (envelope::pskQuantize (1.234f, 4, 1.0f) == Catch::Approx (1.234f));
}

// ----------------------------------------------------------------------------
// UniversalModulator runtime behaviour
// ----------------------------------------------------------------------------

TEST_CASE ("UniversalModulator advances phase at the rate it's given", "[UniversalModulator]")
{
    constexpr float sr = 48000.0f;
    UniversalModulator m;
    m.prepare (sr);
    m.setDepth (1.0f);
    m.setShape (envelope::Shape::Sine);

    // Run depth smoothing to convergence.
    for (int i = 0; i < 5000; ++i) m.tickBipolar (1.0f);

    // Count zero crossings in 1s of 10 Hz output → ~20.
    int crossings = 0;
    float prev = m.tickBipolar (10.0f);
    for (int i = 1; i < (int) sr; ++i)
    {
        const float v = m.tickBipolar (10.0f);
        if ((prev >= 0.0f) != (v >= 0.0f)) ++crossings;
        prev = v;
    }
    REQUIRE (crossings >= 18);
    REQUIRE (crossings <= 22);
}

TEST_CASE ("UniversalModulator depth = 0 produces silent output", "[UniversalModulator]")
{
    UniversalModulator m;
    m.prepare (48000.0);
    m.setShape (envelope::Shape::Sine);
    // depth defaults to 0; smoothed value also starts at 0
    for (int i = 0; i < 10000; ++i)
    {
        const float v = m.tickBipolar (5.0f);
        REQUIRE (std::fabs (v) < 1e-4f);
    }
}

TEST_CASE ("UniversalModulator keySync resets phase on noteOn", "[UniversalModulator]")
{
    UniversalModulator m;
    m.prepare (48000.0);
    m.setKeySync (true);
    m.setDepth (1.0f);
    m.setShape (envelope::Shape::Sine);

    // Advance phase part-way through a cycle.
    for (int i = 0; i < 2400; ++i) m.tickBipolar (10.0f);
    REQUIRE (m.getPhase() > 0.0f);

    m.onNoteOn();
    REQUIRE (m.getPhase() == Catch::Approx (0.0f).margin (1e-5f));
}

TEST_CASE ("UniversalModulator without keySync keeps phase across noteOn",
           "[UniversalModulator]")
{
    UniversalModulator m;
    m.prepare (48000.0);
    m.setKeySync (false);
    m.setDepth (1.0f);

    for (int i = 0; i < 2400; ++i) m.tickBipolar (10.0f);
    const float phaseBefore = m.getPhase();
    m.onNoteOn();
    REQUIRE (m.getPhase() == Catch::Approx (phaseBefore));
}

TEST_CASE ("UniversalModulator depth scales the output", "[UniversalModulator]")
{
    UniversalModulator m;
    m.prepare (48000.0);
    m.setDepth (0.25f);
    m.setShape (envelope::Shape::Sine);

    // Let depth smoother converge then check peak amplitude.
    for (int i = 0; i < 5000; ++i) m.tickBipolar (5.0f);

    float peak = 0.0f;
    for (int i = 0; i < (int) (48000.0f / 5.0f) + 100; ++i)
        peak = std::max (peak, std::fabs (m.tickBipolar (5.0f)));
    REQUIRE (peak == Catch::Approx (0.25f).margin (0.005f));
}
