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

namespace
{
    // Magnitude of the operator's output at the k-th harmonic of its own
    // oscillation frequency, via direct sin/cos correlation. The operator
    // returns depth·fMod·osc; we normalise out depth·fMod to recover osc and
    // measure its harmonic content. ratio=1 ⇒ osc runs at carrierHz.
    float harmonicMag (Operator& op, float carrierHz, float depth, int harmonic)
    {
        const float invSR = (float) (1.0 / kSampleRate);
        const float twoPi = 6.2831853071795864769f;
        const float w = twoPi * (carrierHz * (float) harmonic) / (float) kSampleRate;
        const float norm = depth * carrierHz;          // depth·fMod (ratio=1)
        double sinAcc = 0.0, cosAcc = 0.0;
        const int settle = 4000, window = 16000;
        for (int i = 0; i < settle + window; ++i)
        {
            const float osc = op.processSample (carrierHz, invSR) / norm;
            if (i >= settle)
            {
                const float ph = w * (float) (i - settle);
                sinAcc += osc * std::sin (ph);
                cosAcc += osc * std::cos (ph);
            }
        }
        return (float) (std::sqrt (sinAcc * sinAcc + cosAcc * cosAcc) / window);
    }
}

TEST_CASE ("feedback adds harmonics absent from a clean sine operator")
{
    auto make = [] (float feedback)
    {
        auto op = std::make_unique<Operator>();
        op->prepare (kSampleRate);
        op->setEnabled (true);
        op->snapRatio (1.0f);
        op->setDepth (1.0f);
        op->setFeedback (feedback);
        return op;
    };

    // A pure sine has essentially no energy at its 2nd harmonic; self-feedback
    // injects it (the bright/sawtooth character). 3rd harmonic likewise grows.
    const float clean2 = harmonicMag (*make (0.0f), 220.0f, 1.0f, 2);
    const float fb2    = harmonicMag (*make (0.7f), 220.0f, 1.0f, 2);
    REQUIRE (clean2 < 0.02f);
    REQUIRE (fb2 > 0.05f);
    REQUIRE (fb2 > clean2 * 4.0f);
}

TEST_CASE ("feedback output stays bounded (2-sample averaging tames the loop)")
{
    Operator op;
    op.prepare (kSampleRate);
    op.setEnabled (true);
    op.snapRatio (1.0f);
    op.setDepth (1.0f);
    op.setFeedback (1.0f);                      // max self-PM index (π)

    const float invSR = (float) (1.0 / kSampleRate);
    const float fMod = 220.0f;                  // depth·fMod with depth=1
    float peak = 0.0f;
    for (int i = 0; i < 48000; ++i)
        peak = std::max (peak, std::abs (op.processSample (220.0f, invSR)));
    // osc is a sine ⇒ |delta| can never exceed depth·fMod, no runaway.
    REQUIRE (peak <= fMod * 1.001f);
    REQUIRE (std::isfinite (peak));
}

TEST_CASE ("operator waveform reshapes the oscillator spectrum")
{
    auto make = [] (int wf)
    {
        auto op = std::make_unique<Operator>();
        op->prepare (kSampleRate);
        op->setEnabled (true);
        op->snapRatio (1.0f);
        op->setDepth (1.0f);
        op->setWaveform (wf);
        return op;
    };
    // Sine (0) has ~no 3rd harmonic; square (3) is rich in odd harmonics.
    REQUIRE (harmonicMag (*make (0), 220.0f, 1.0f, 3) < 0.02f);
    REQUIRE (harmonicMag (*make (3), 220.0f, 1.0f, 3) > 0.1f);
    // Saw (2) carries a 2nd harmonic that the sine lacks.
    REQUIRE (harmonicMag (*make (2), 220.0f, 1.0f, 2) > 0.1f);
}

TEST_CASE ("feedback=0 reproduces the pure-sine path exactly")
{
    Operator clean, fbZero;
    for (auto* op : { &clean, &fbZero })
    {
        op->prepare (kSampleRate);
        op->setEnabled (true);
        op->snapRatio (1.0f);
        op->setDepth (1.0f);
    }
    fbZero.setFeedback (0.0f);

    const float invSR = (float) (1.0 / kSampleRate);
    for (int i = 0; i < 2000; ++i)
        REQUIRE (clean.processSample (220.0f, invSR)
                     == Approx (fbZero.processSample (220.0f, invSR)));
}
