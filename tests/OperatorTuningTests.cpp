#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>

#include "audio/Operator.h"

using brocot::Operator;
using Catch::Approx;

namespace
{
    constexpr double kSampleRate = 48000.0;

    // Steady-state peak |delta| once depth smoothing has settled. Since
    // delta = depth · fMod · sin(2πφ), the peak over many cycles equals
    // (effective depth) · fMod — a direct read on the effective depth.
    float steadyPeakDelta (Operator& op, float carrierHz)
    {
        const float invSR = (float) (1.0 / kSampleRate);
        float peak = 0.0f;
        for (int i = 0; i < 8000; ++i)
        {
            const float d = op.processSample (carrierHz, invSR);
            if (i > 6000) peak = std::max (peak, std::abs (d));
        }
        return peak;
    }
}

TEST_CASE ("detune nudges the effective ratio off the exact rational")
{
    Operator op;
    op.prepare (kSampleRate);
    op.setEnabled (true);
    op.snapRatio (2.0f);                       // exact 2/1

    REQUIRE (op.getRatio() == Approx (2.0f));

    op.setDetuneCents (100.0f);                // +1 semitone
    REQUIRE (op.getRatio() == Approx (2.0f * std::pow (2.0f, 1.0f / 12.0f)));

    op.setDetuneCents (0.0f);                  // back to exact
    REQUIRE (op.getRatio() == Approx (2.0f));
}

TEST_CASE ("small detune produces a small ratio offset (beating region)")
{
    Operator op;
    op.prepare (kSampleRate);
    op.setEnabled (true);
    op.snapRatio (1.0f);
    op.setDetuneCents (6.0f);                  // ~0.76 Hz beat vs 1/1 at 220
    const float r = op.getRatio();
    REQUIRE (r > 1.0f);
    REQUIRE (r == Approx (std::pow (2.0f, 6.0f / 1200.0f)).margin (1e-6));
}

TEST_CASE ("key-scaling scales the effective modulation index with pitch")
{
    auto make = [] (float keyScale, float noteOct)
    {
        auto op = std::make_unique<Operator>();
        op->prepare (kSampleRate);
        op->setEnabled (true);
        op->snapRatio (1.0f);
        op->setDepth (2.0f);
        op->setKeyScale (keyScale);
        op->setNoteOctaves (noteOct);
        return op;
    };

    const float base = steadyPeakDelta (*make (0.0f, 1.0f), 100.0f);  // no scaling

    // keyScale=+1, one octave above middle C ⇒ depth halves.
    const float up = steadyPeakDelta (*make (1.0f, 1.0f), 100.0f);
    REQUIRE (up == Approx (0.5f * base).epsilon (0.05));

    // keyScale=-1, one octave above ⇒ depth doubles.
    const float dn = steadyPeakDelta (*make (-1.0f, 1.0f), 100.0f);
    REQUIRE (dn == Approx (2.0f * base).epsilon (0.05));

    // At middle C (0 octaves) key-scaling is a no-op regardless of amount.
    const float mid = steadyPeakDelta (*make (1.0f, 0.0f), 100.0f);
    REQUIRE (mid == Approx (base).epsilon (0.05));
}
