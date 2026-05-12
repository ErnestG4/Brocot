#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>

#include "audio/BrocotVoice.h"

using brocot::BrocotVoice;

namespace
{
    constexpr double kSampleRate = 48000.0;

    float peakAbs (const std::vector<float>& buf)
    {
        float p = 0.0f;
        for (float x : buf) p = std::max (p, std::abs (x));
        return p;
    }

    float rms (const std::vector<float>& buf)
    {
        double s = 0.0;
        for (float x : buf) s += (double) x * x;
        return (float) std::sqrt (s / (double) buf.size());
    }

    BrocotVoice makeCarrierVoice()
    {
        BrocotVoice v;
        v.prepare (kSampleRate);
        v.setLevel (0.25f);
        // Use a short attack so envelope reaches sustain quickly in tests.
        brocot::AmpEnvelope::Parameters fast { 0.001f, 0.01f, 1.0f, 0.05f };
        v.envelope().setParameters (fast);
        return v;
    }
}

// ----------------------------------------------------------------------------
// Slice 5 — MIDI gating + amp envelope.
// ----------------------------------------------------------------------------

TEST_CASE ("Voice is silent before any noteOn", "[BrocotVoice][MIDI]")
{
    auto v = makeCarrierVoice();

    std::vector<float> buf (4800, 0.0f);
    v.render (buf.data(), (int) buf.size());

    REQUIRE (peakAbs (buf) < 1e-5f);
    REQUIRE_FALSE (v.isActive());
}

TEST_CASE ("noteOn produces audible sustained output", "[BrocotVoice][MIDI]")
{
    auto v = makeCarrierVoice();
    v.noteOn (69);  // A4 = 440 Hz

    // Skip the attack/decay transient before measuring sustained level.
    std::vector<float> attack (1200, 0.0f);
    v.render (attack.data(), (int) attack.size());

    std::vector<float> sustained (4800, 0.0f);
    v.render (sustained.data(), (int) sustained.size());

    REQUIRE (peakAbs (sustained) > 0.1f);
    REQUIRE (rms (sustained) > 0.05f);
    REQUIRE (v.isActive());
    REQUIRE (v.getActiveNote() == 69);
}

TEST_CASE ("Carrier frequency follows MIDI note", "[BrocotVoice][MIDI]")
{
    // Compare zero-crossings at A3 (220Hz) vs A4 (440Hz) over the same duration.
    auto countCrossings = [] (int note) {
        auto v = makeCarrierVoice();
        v.noteOn (note);

        // Skip transient.
        std::vector<float> warmup (480, 0.0f);
        v.render (warmup.data(), (int) warmup.size());

        constexpr int N = (int) kSampleRate;  // 1 s
        std::vector<float> buf (N, 0.0f);
        v.render (buf.data(), N);

        int crossings = 0;
        for (size_t i = 1; i < (size_t) N; ++i)
            if ((buf[i - 1] >= 0.0f) != (buf[i] >= 0.0f))
                ++crossings;
        return crossings;
    };

    const int a3 = countCrossings (57);   // 220 Hz → ~440 crossings
    const int a4 = countCrossings (69);   // 440 Hz → ~880 crossings
    REQUIRE (a3 >= 435);
    REQUIRE (a3 <= 445);
    REQUIRE (a4 >= 875);
    REQUIRE (a4 <= 885);
}

TEST_CASE ("Matching noteOff releases envelope; non-matching noteOff is ignored",
           "[BrocotVoice][MIDI]")
{
    auto v = makeCarrierVoice();
    v.noteOn (60);

    // Settle into sustain.
    std::vector<float> warmup (2400, 0.0f);
    v.render (warmup.data(), (int) warmup.size());
    REQUIRE (v.isActive());

    // A note-off for a different note must NOT affect the active envelope.
    v.noteOff (64);
    REQUIRE (v.isActive());
    REQUIRE (v.getActiveNote() == 60);

    std::vector<float> stillSustained (2400, 0.0f);
    v.render (stillSustained.data(), (int) stillSustained.size());
    REQUIRE (peakAbs (stillSustained) > 0.1f);

    // The matching note-off must release.
    v.noteOff (60);
    REQUIRE (v.getActiveNote() == BrocotVoice::kNoActiveNote);

    // Render past the 0.05s release time at 48kHz (~2400 samples).
    std::vector<float> releasing (4800, 0.0f);
    v.render (releasing.data(), (int) releasing.size());

    REQUIRE_FALSE (v.isActive());
    // Final samples should be at or near silence.
    float tailPeak = 0.0f;
    for (size_t i = releasing.size() - 480; i < releasing.size(); ++i)
        tailPeak = std::max (tailPeak, std::abs (releasing[i]));
    REQUIRE (tailPeak < 1e-3f);
}

TEST_CASE ("noteOn while playing retriggers and updates pitch", "[BrocotVoice][MIDI]")
{
    auto v = makeCarrierVoice();
    v.noteOn (60);
    std::vector<float> warmup (2400, 0.0f);
    v.render (warmup.data(), (int) warmup.size());

    // Overlap: a new note-on while another is held updates the active note
    // and retriggers the envelope (BROCOT-SPEC.md §8 mono behaviour).
    v.noteOn (72);
    REQUIRE (v.getActiveNote() == 72);
    REQUIRE (v.isActive());

    // The carrier should now be at C5 (~523 Hz), not C4 (~262 Hz).
    std::vector<float> warmupAfter (480, 0.0f);
    v.render (warmupAfter.data(), (int) warmupAfter.size());
    std::vector<float> samples ((int) kSampleRate, 0.0f);
    v.render (samples.data(), (int) samples.size());

    int crossings = 0;
    for (size_t i = 1; i < samples.size(); ++i)
        if ((samples[i - 1] >= 0.0f) != (samples[i] >= 0.0f))
            ++crossings;
    // C5 = 523.25 Hz → ~1046 crossings.
    REQUIRE (crossings >= 1040);
    REQUIRE (crossings <= 1055);
}
