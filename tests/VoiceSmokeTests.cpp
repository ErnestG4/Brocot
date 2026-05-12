#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <numeric>
#include <vector>

#include "audio/BrocotVoice.h"
#include "audio/Path.h"

using brocot::BrocotVoice;
using brocot::Operator;
using brocot::Path;

namespace
{
    constexpr double kSampleRate = 48000.0;

    struct AudioStats
    {
        bool  anyNaN;
        bool  anyInf;
        float peakAbs;
        float rms;
    };

    AudioStats measure (const std::vector<float>& buf)
    {
        AudioStats s { false, false, 0.0f, 0.0f };
        double sumSq = 0.0;
        for (float x : buf)
        {
            if (std::isnan (x)) s.anyNaN = true;
            if (std::isinf (x)) s.anyInf = true;
            s.peakAbs = std::max (s.peakAbs, std::abs (x));
            sumSq += (double) x * (double) x;
        }
        s.rms = (float) std::sqrt (sumSq / (double) buf.size());
        return s;
    }
}

TEST_CASE ("octave·J1 hard-coded preset renders bounded, non-zero, non-NaN audio", "[BrocotVoice]")
{
    BrocotVoice v;
    v.prepare (kSampleRate);
    v.setCarrierHz (220.0f);
    v.setLevel (0.2f);

    auto& m0 = v.op (0);
    m0.setEnabled (true);
    m0.setPath (Path ("R"));
    m0.setDepth (1.841f);

    constexpr int N = 4800;  // 0.1 s @ 48 kHz
    std::vector<float> buf (N, 0.0f);
    v.render (buf.data(), N);

    auto s = measure (buf);
    REQUIRE_FALSE (s.anyNaN);
    REQUIRE_FALSE (s.anyInf);
    REQUIRE (s.peakAbs <= 1.0f);                  // no clipping
    REQUIRE (s.peakAbs > 0.05f);                  // actually producing sound
    REQUIRE (s.rms > 0.01f);                      // sustained energy, not impulse
}

TEST_CASE ("Silent when no operators are enabled (carrier only)", "[BrocotVoice]")
{
    // With no FM contribution the carrier is a pure sine — bounded by `level`,
    // RMS = level/sqrt(2) ≈ 0.141 at level=0.2. This verifies the carrier
    // itself behaves and doesn't depend on the modulator path being valid.
    BrocotVoice v;
    v.prepare (kSampleRate);
    v.setCarrierHz (220.0f);
    v.setLevel (0.2f);
    // ops default to disabled

    constexpr int N = 4800;
    std::vector<float> buf (N, 0.0f);
    v.render (buf.data(), N);

    auto s = measure (buf);
    REQUIRE_FALSE (s.anyNaN);
    REQUIRE (s.peakAbs <= 0.2f + 1e-3f);
    REQUIRE (s.rms == Catch::Approx (0.2f / std::sqrt (2.0f)).margin (0.005f));
}

TEST_CASE ("Carrier frequency is approximately 220 Hz (zero-crossing count)", "[BrocotVoice]")
{
    // Quick frequency sanity check: count zero-crossings of the carrier-only
    // output over 1 second. For a 220 Hz sine that's ~440 crossings.
    BrocotVoice v;
    v.prepare (kSampleRate);
    v.setCarrierHz (220.0f);
    v.setLevel (0.2f);

    constexpr int N = (int) kSampleRate;  // 1 s
    std::vector<float> buf (N, 0.0f);
    v.render (buf.data(), N);

    int crossings = 0;
    for (size_t i = 1; i < (size_t) N; ++i)
        if ((buf[i - 1] >= 0.0f) != (buf[i] >= 0.0f))
            ++crossings;

    // ~440 ± a few from sampling boundary effects.
    REQUIRE (crossings >= 435);
    REQUIRE (crossings <= 445);
}
