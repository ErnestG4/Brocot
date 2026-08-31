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

TEST_CASE ("pulse at duty 0.5 is exactly the square wave")
{
    // Same comparator (p < 0.5), so sample-for-sample identity, not just
    // spectral similarity.
    for (int i = 0; i < 1000; ++i)
    {
        const float turns = (float) i * 0.0173f;             // irrational-ish sweep
        REQUIRE (Operator::wave (Operator::Wave::Pulse, turns, 0.5f)
                     == Operator::wave (Operator::Wave::Square, turns));
    }

    // And through the full operator path: identical odd-harmonic content.
    auto make = [] (int wf, float duty)
    {
        auto op = std::make_unique<Operator>();
        op->prepare (kSampleRate);
        op->setEnabled (true);
        op->snapRatio (1.0f);
        op->setDepth (1.0f);
        op->setWaveform (wf);
        op->setDuty (duty);
        return op;
    };
    const float sq3 = harmonicMag (*make (3, 0.25f), 220.0f, 1.0f, 3);
    const float pl3 = harmonicMag (*make (4, 0.5f),  220.0f, 1.0f, 3);
    REQUIRE (pl3 == Approx (sq3).epsilon (0.02));
}

TEST_CASE ("pulse duty 0.25 nulls the 4th harmonic (the sin(pi*n*duty) comb)")
{
    // Pulse harmonic amplitudes go as (4/(pi*n))*|sin(pi*n*duty)|: at duty
    // 0.25 the n=4 term is sin(pi) = 0 while the n=3/n=5 neighbours sit at
    // sin(0.75pi) = sin(1.25pi) = 0.707 — the audible PW comb.
    auto make = [] (float duty)
    {
        auto op = std::make_unique<Operator>();
        op->prepare (kSampleRate);
        op->setEnabled (true);
        op->snapRatio (1.0f);
        op->setDepth (1.0f);
        op->setWaveform (4);                        // pulse
        op->setDuty (duty);
        return op;
    };
    const float m3 = harmonicMag (*make (0.25f), 220.0f, 1.0f, 3);
    const float m4 = harmonicMag (*make (0.25f), 220.0f, 1.0f, 4);
    const float m5 = harmonicMag (*make (0.25f), 220.0f, 1.0f, 5);
    REQUIRE (m3 > 0.1f);                            // ~0.15 predicted
    REQUIRE (m5 > 0.06f);                           // ~0.09 predicted
    REQUIRE (m4 < 0.02f);                           // nulled
    REQUIRE (m4 < m3 / 4.0f);
    REQUIRE (m4 < m5 / 3.0f);

    // Square (duty 0.5) has NO such null — its 4th harmonic is absent too
    // (even), so contrast against duty where n=4 is live: duty 0.375 gives
    // |sin(1.5pi)| = 1, the strongest possible 4th-harmonic weighting.
    const float live4 = harmonicMag (*make (0.375f), 220.0f, 1.0f, 4);
    REQUIRE (live4 > 0.05f);
    REQUIRE (m4 < live4 / 3.0f);
}

TEST_CASE ("setWaveform maps reserved/unknown indices to sine")
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
    // Reserved choice slots (5..7) and garbage must play as a pure sine —
    // no 3rd harmonic — not clamp onto square/pulse.
    for (int wf : { 5, 6, 7, 42, -3 })
        REQUIRE (harmonicMag (*make (wf), 220.0f, 1.0f, 3) < 0.02f);
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

// ----------------------------------------------------------------------------
// Double-pulse even suppression (COINCIDENCE-HORIZON.md, brocot_double_pulse)
// ----------------------------------------------------------------------------

TEST_CASE ("evenSup = 0 is the old pulse bit-exactly", "[Operator]")
{
    for (float t = 0.0f; t < 1.0f; t += 0.013f)
        for (float duty : { 0.05f, 0.25f, 0.5f })
            REQUIRE (Operator::wave (Operator::Wave::Pulse, t, duty, 0.0f)
                     == Operator::wave (Operator::Wave::Pulse, t, duty));
}

TEST_CASE ("evenSup = 1 is half-wave antisymmetric", "[Operator]")
{
    // f(t + T/2) == -f(t): the property that cancels the even harmonics.
    for (float t = 0.0f; t < 1.0f; t += 0.017f)
    {
        const float a = Operator::wave (Operator::Wave::Pulse, t,        0.25f, 1.0f);
        const float b = Operator::wave (Operator::Wave::Pulse, t + 0.5f, 0.25f, 1.0f);
        REQUIRE (a == Catch::Approx (-b).margin (1e-6f));
    }
}

TEST_CASE ("evenSup scales even harmonics by (1-b)/(1+b) and holds odd", "[Operator]")
{
    // 1024-point DFT of one cycle at duty 0.25. Fn = Pn * (1 - b*(-1)^n),
    // normalised by (1+b): odd harmonics hold level, even scale by
    // (1-b)/(1+b) — the doc's formula, checked through the sweep, not only
    // at its ends.
    constexpr int N = 1024;
    auto mag = [] (float beta, int harmonic)
    {
        double re = 0.0, im = 0.0;
        for (int i = 0; i < N; ++i)
        {
            const float t = (float) i / (float) N;
            const float s = Operator::wave (Operator::Wave::Pulse, t, 0.25f, beta);
            re += s * std::cos (2.0 * juce::MathConstants<double>::pi * harmonic * t);
            im += s * std::sin (2.0 * juce::MathConstants<double>::pi * harmonic * t);
        }
        return std::sqrt (re * re + im * im) / N;
    };

    for (float beta : { 0.25f, 0.5f, 0.75f })
    {
        const double expected = (1.0 - beta) / (1.0 + beta);
        for (int n : { 2, 6 })   // duty 0.25 nulls n = 4, 8, ... — skip those
            REQUIRE (mag (beta, n) / mag (0.0f, n) == Catch::Approx (expected).margin (2e-3));
        for (int n : { 1, 3, 5 })
            REQUIRE (mag (beta, n) / mag (0.0f, n) == Catch::Approx (1.0).margin (2e-3));
    }
}

// ----------------------------------------------------------------------------
// Wavefold (phase3/wavefold.md, SHIP_CONTINUOUS_EXTENSION)
// ----------------------------------------------------------------------------

TEST_CASE ("fold = 0 is an exact bypass", "[Operator]")
{
    Operator op;
    op.setFold (0.0f);
    for (float s : { -1.0f, -0.4f, 0.0f, 0.7f, 1.0f })
        REQUIRE (op.applyFold (s) == s);
}

TEST_CASE ("fold produces the odd Bessel stack on a sine", "[Operator]")
{
    // DFT of sin(beta*sin t)/c: harmonic k magnitude should be
    // |2 J_k(beta)|/c for odd k and ~0 for even k. Golden J values from
    // scipy (beta = 1.5): J1 = 0.5579365, J3 = 0.0605097, J5 = 0.0017994.
    constexpr int N = 2048;
    const float beta = 1.5f;
    Operator op;
    op.setFold (beta);

    auto mag = [&op] (int harmonic)
    {
        double re = 0.0, im = 0.0;
        for (int i = 0; i < N; ++i)
        {
            const double t = 2.0 * juce::MathConstants<double>::pi * i / N;
            const float s = op.applyFold ((float) std::sin (t));
            re += s * std::cos (harmonic * t);
            im += s * std::sin (harmonic * t);
        }
        return 2.0 * std::sqrt (re * re + im * im) / N;
    };

    const double c = std::sin ((double) beta);   // beta <= pi/2: peak = sin(beta)
    REQUIRE (mag (1) == Catch::Approx (2.0 * 0.5579365 / c).margin (1e-3));
    REQUIRE (mag (3) == Catch::Approx (2.0 * 0.0605097 / c).margin (1e-3));
    REQUIRE (mag (5) == Catch::Approx (2.0 * 0.0017994 / c).margin (1e-3));
    REQUIRE (mag (2) < 1e-6);
    REQUIRE (mag (4) < 1e-6);
}

TEST_CASE ("fold output stays within unit peak", "[Operator]")
{
    Operator op;
    for (float beta : { 0.5f, 1.5707f, 2.0f, 2.5f })
    {
        op.setFold (beta);
        float peak = 0.0f;
        for (float s = -1.0f; s <= 1.0f; s += 0.001f)
            peak = std::max (peak, std::abs (op.applyFold (s)));
        REQUIRE (peak <= 1.0f + 1e-5f);
        REQUIRE (peak >= 0.98f);   // the normalisation actually reaches 1
    }
}
