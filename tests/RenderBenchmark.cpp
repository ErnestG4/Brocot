// Render-cost benchmark for BrocotVoice. Hidden behind the [.bench] tag so it
// never runs in the normal suite; invoke explicitly:
//
//   ./BrocotTests "[.bench]" --reporter console
//
// It answers "how many 64-op voices fit the real-time budget on one core?" by
// timing how long it takes to render one voice-second of audio at 48 kHz. If
// rendering 1.0 s of one voice costs T seconds of wall time, then ~1/T such
// voices run in real time on a single core (the audio thread).

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>

#include "audio/BrocotVoice.h"

using brocot::BrocotVoice;

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr int    kSeconds    = 4;                       // audio-seconds per run
    constexpr int    kSamples    = (int) (kSampleRate) * kSeconds;

    // Configure a voice with `nActive` operators enabled at non-trivial depth
    // (so none hit the depth<=1e-5 early-out — worst-case full render), and
    // optionally self-feedback on op 0.
    void configure (BrocotVoice& v, int nActive, float feedback)
    {
        v.prepare (kSampleRate);
        v.envelope().setParameters ({ 0.001f, 0.0f, 1.0f, 0.1f });  // hold at sustain
        v.setLevel (0.5f);
        for (int i = 0; i < BrocotVoice::N_OPS; ++i)
        {
            auto& op = v.op (i);
            const bool on = i < nActive;
            op.setEnabled (on);
            if (on)
            {
                op.snapRatio ((float) ((i % 8) + 1));   // ratios 1..8, realistic spread
                op.setDepth (1.5f);
            }
        }
        v.op (0).setFeedback (feedback);
        v.noteOn (60);
    }

    // Returns nanoseconds of wall time per rendered sample (one voice), taking
    // the best of a few passes to shed scheduler noise.
    double nsPerSample (BrocotVoice& v)
    {
        const BrocotVoice::PerSampleMod psm {};
        // Warm-up so depth/ratio/level smoothers have settled.
        volatile float sink = 0.0f;
        for (int i = 0; i < 4096; ++i) sink += v.renderSample (psm);

        double best = 1e30;
        for (int pass = 0; pass < 3; ++pass)
        {
            const auto t0 = std::chrono::steady_clock::now();
            float acc = 0.0f;
            for (int i = 0; i < kSamples; ++i) acc += v.renderSample (psm);
            const auto t1 = std::chrono::steady_clock::now();
            sink += acc;   // defeat dead-code elimination
            const double ns = std::chrono::duration<double, std::nano> (t1 - t0).count();
            best = std::min (best, ns / kSamples);
        }
        (void) sink;
        return best;
    }

    void report (const char* label, int nActive, float feedback)
    {
        BrocotVoice v;
        configure (v, nActive, feedback);
        const double nsSamp = nsPerSample (v);
        // Real-time budget per voice per sample at 48 kHz = 1e9/48000 ns.
        const double budgetNs = 1.0e9 / kSampleRate;
        const double rtVoices = budgetNs / nsSamp;     // voices that fit one core
        std::printf ("  %-26s %7.1f ns/sample   ~%6.0f voices/core real-time\n",
                     label, nsSamp, rtVoices);
    }
}

TEST_CASE ("BrocotVoice render cost", "[.bench]")
{
    std::printf ("\nBrocotVoice render cost @ %.0f kHz "
                 "(best of 3 x %d s; lower ns is better):\n",
                 kSampleRate / 1000.0, kSeconds);
    report ("16 ops",            16, 0.0f);
    report ("32 ops",            32, 0.0f);
    report ("64 ops",            64, 0.0f);
    report ("64 ops + feedback", 64, 0.7f);
    std::printf ("\n  (mods are global/shared, not per-voice; duo = 2 voices max today)\n\n");
    SUCCEED();
}
