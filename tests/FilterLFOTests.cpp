#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>

#include "audio/FilterBank.h"
#include "audio/LFO.h"

using brocot::FilterBank;
using brocot::LFO;

namespace
{
    constexpr double kSampleRate = 48000.0;

    float rms (const std::vector<float>& buf)
    {
        double s = 0.0;
        for (float x : buf) s += (double) x * x;
        return (float) std::sqrt (s / (double) buf.size());
    }

    // Generate a sine at `hz` over `n` samples, then run through the filter.
    std::vector<float> renderSinThroughFilter (FilterBank& f, float hz, int n)
    {
        std::vector<float> buf ((size_t) n);
        const double twoPi = 6.283185307179586;
        for (int i = 0; i < n; ++i)
            buf[(size_t) i] = (float) std::sin (twoPi * (double) hz * i / kSampleRate);
        for (int i = 0; i < n; ++i)
            buf[(size_t) i] = f.processSample (buf[(size_t) i]);
        return buf;
    }
}

// ----------------------------------------------------------------------------
// FilterBank (TPT SVF) — gain at well-separated frequencies.
// ----------------------------------------------------------------------------

TEST_CASE ("LP: passes lows, attenuates highs", "[FilterBank]")
{
    FilterBank f;
    f.prepare (kSampleRate);
    f.setMode (FilterBank::Mode::LP);
    f.setResonance (0.0f);
    f.setCutoff (1000.0f);

    auto pass = renderSinThroughFilter (f, 100.0f, 9600);
    f.reset();
    auto stop = renderSinThroughFilter (f, 10000.0f, 9600);

    REQUIRE (rms (pass) > 0.5f);   // ~unity passband
    REQUIRE (rms (stop) < 0.1f);   // strongly attenuated stopband
    REQUIRE (rms (pass) > rms (stop) * 5.0f);
}

TEST_CASE ("HP: passes highs, attenuates lows", "[FilterBank]")
{
    FilterBank f;
    f.prepare (kSampleRate);
    f.setMode (FilterBank::Mode::HP);
    f.setResonance (0.0f);
    f.setCutoff (1000.0f);

    auto pass = renderSinThroughFilter (f, 10000.0f, 9600);
    f.reset();
    auto stop = renderSinThroughFilter (f, 100.0f, 9600);

    REQUIRE (rms (pass) > 0.5f);
    REQUIRE (rms (stop) < 0.1f);
}

TEST_CASE ("BP: peaks near cutoff, attenuates above and below", "[FilterBank]")
{
    FilterBank f;
    f.prepare (kSampleRate);
    f.setMode (FilterBank::Mode::BP);
    f.setResonance (0.8f);   // some Q so peak is pronounced
    f.setCutoff (1000.0f);

    auto pass = renderSinThroughFilter (f, 1000.0f, 9600);
    f.reset();
    auto belowStop = renderSinThroughFilter (f, 100.0f, 9600);
    f.reset();
    auto aboveStop = renderSinThroughFilter (f, 10000.0f, 9600);

    REQUIRE (rms (pass) > rms (belowStop) * 3.0f);
    REQUIRE (rms (pass) > rms (aboveStop) * 3.0f);
}

TEST_CASE ("Notch: passes far-from-cutoff, attenuates at cutoff", "[FilterBank]")
{
    FilterBank f;
    f.prepare (kSampleRate);
    f.setMode (FilterBank::Mode::Notch);
    f.setResonance (0.8f);
    f.setCutoff (1000.0f);

    auto inside = renderSinThroughFilter (f, 1000.0f, 9600);
    f.reset();
    auto outside = renderSinThroughFilter (f, 100.0f, 9600);

    REQUIRE (rms (outside) > rms (inside) * 2.0f);
}

// ----------------------------------------------------------------------------
// LFO — shape and rate properties.
// ----------------------------------------------------------------------------

TEST_CASE ("Sine LFO is bounded [-1, +1] and has near-zero DC over a cycle", "[LFO]")
{
    LFO lfo;
    lfo.prepare (kSampleRate);
    lfo.setShape (LFO::Shape::Sine);
    lfo.setRate (1.0f);   // 1 Hz → exactly kSampleRate samples per cycle
    lfo.setDepth (1.0f);

    double sum = 0.0;
    float peak = 0.0f;
    constexpr int N = (int) kSampleRate;  // one cycle
    for (int i = 0; i < N; ++i)
    {
        float v = lfo.tickScaled();
        sum += v;
        peak = std::max (peak, std::fabs (v));
    }
    REQUIRE (peak <= 1.0f + 1e-5f);
    REQUIRE (peak > 0.99f);
    REQUIRE (std::fabs (sum / (double) N) < 0.01f);
}

TEST_CASE ("Square LFO emits only ±1 values", "[LFO]")
{
    LFO lfo;
    lfo.prepare (kSampleRate);
    lfo.setShape (LFO::Shape::Square);
    lfo.setRate (2.0f);
    lfo.setDepth (1.0f);

    bool seenHigh = false, seenLow = false;
    for (int i = 0; i < (int) kSampleRate; ++i)
    {
        float v = lfo.tickScaled();
        REQUIRE (std::fabs (std::fabs (v) - 1.0f) < 1e-6f);
        if (v > 0.0f) seenHigh = true;
        if (v < 0.0f) seenLow  = true;
    }
    REQUIRE (seenHigh);
    REQUIRE (seenLow);
}

TEST_CASE ("Saw LFO sweeps -1 → +1 across a cycle", "[LFO]")
{
    LFO lfo;
    lfo.prepare (kSampleRate);
    lfo.setShape (LFO::Shape::Saw);
    lfo.setRate (1.0f);
    lfo.setDepth (1.0f);

    float first = lfo.tickScaled();           // very close to -1
    REQUIRE (first < -0.99f);

    // Skip half cycle, sample → near 0
    for (int i = 0; i < (int) (kSampleRate / 2) - 1; ++i) lfo.tickScaled();
    float mid = lfo.tickScaled();
    REQUIRE (mid > -0.05f);
    REQUIRE (mid <  0.05f);
}

TEST_CASE ("Triangle LFO peaks at ±1 mid-segment", "[LFO]")
{
    LFO lfo;
    lfo.prepare (kSampleRate);
    lfo.setShape (LFO::Shape::Triangle);
    lfo.setRate (1.0f);
    lfo.setDepth (1.0f);

    float peakPos = 0.0f, peakNeg = 0.0f;
    for (int i = 0; i < (int) kSampleRate; ++i)
    {
        float v = lfo.tickScaled();
        peakPos = std::max (peakPos, v);
        peakNeg = std::min (peakNeg, v);
    }
    REQUIRE (peakPos > 0.99f);
    REQUIRE (peakNeg < -0.99f);
}

TEST_CASE ("Key sync resets LFO phase on onNoteOn", "[LFO]")
{
    LFO lfo;
    lfo.prepare (kSampleRate);
    lfo.setShape (LFO::Shape::Saw);
    lfo.setRate (1.0f);
    lfo.setDepth (1.0f);
    lfo.setKeySync (true);

    // Walk halfway through a cycle.
    for (int i = 0; i < (int) (kSampleRate / 2); ++i) lfo.tick();

    // Without sync we'd be near 0 next sample; with sync we restart at phase 0
    // (saw output 2·0 - 1 = -1).
    lfo.onNoteOn();
    REQUIRE (lfo.tickScaled() < -0.99f);
}
