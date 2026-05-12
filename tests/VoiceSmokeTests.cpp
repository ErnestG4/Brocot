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

// ----------------------------------------------------------------------------
// Slice 4 — multi-modulator engine (parallel summation).
// ----------------------------------------------------------------------------

TEST_CASE ("Avatar·1 (3 ops on shared path RLLLR) renders bounded multi-op audio", "[BrocotVoice]")
{
    // BROCOT-SPEC.md §4, verification case 3:
    //   3 ops on shared path RLLLR at positions 1/3/5 (ratios 2/1, 4/3, 9/7)
    //   depths 5.13, 0.86, 0.90; carrier 130.81 Hz.
    BrocotVoice v;
    v.prepare (kSampleRate);
    v.setCarrierHz (130.81f);
    v.setLevel (0.18f);

    const Path shared ("RLLLR");
    struct S { int pos; float d; };
    const S setups[] = { {1, 5.13f}, {3, 0.86f}, {5, 0.90f} };
    for (size_t i = 0; i < std::size (setups); ++i)
    {
        auto& o = v.op ((int) i);
        o.setEnabled (true);
        o.setPath (shared.prefix (setups[i].pos));
        o.setDepth (setups[i].d);
    }

    constexpr int N = 9600;  // 0.2 s
    std::vector<float> buf (N, 0.0f);
    v.render (buf.data(), N);

    auto s = measure (buf);
    REQUIRE_FALSE (s.anyNaN);
    REQUIRE_FALSE (s.anyInf);
    REQUIRE (s.peakAbs <= 1.0f);
    REQUIRE (s.peakAbs > 0.05f);
    REQUIRE (s.rms > 0.01f);

    // Sanity check: op 0 is at 2/1 = 261.62 Hz, op 1 at 4/3 ≈ 174.41,
    // op 2 at 9/7 ≈ 168.18. The ratios resolved correctly:
    REQUIRE (v.op (0).getRatio() == Catch::Approx (2.0f).margin (1e-5f));
    REQUIRE (v.op (1).getRatio() == Catch::Approx (4.0f / 3.0f).margin (1e-5f));
    REQUIRE (v.op (2).getRatio() == Catch::Approx (9.0f / 7.0f).margin (1e-5f));
}

TEST_CASE ("All-ops-enabled-but-depth-zero is identical to carrier-only", "[BrocotVoice]")
{
    // Pure math check: an enabled modulator with I=0 contributes nothing
    // (Δf = 0·f_mod·sin(...) = 0), so 8 zero-depth ops must produce the
    // same output as no ops at all. Catches the case where the FM loop
    // accidentally writes garbage for enabled-but-zero-depth ops.
    auto renderCarrierOnly = [] {
        BrocotVoice v;
        v.prepare (kSampleRate);
        v.setCarrierHz (220.0f);
        v.setLevel (0.2f);
        std::vector<float> buf (4096, 0.0f);
        v.render (buf.data(), (int) buf.size());
        return buf;
    };

    auto renderZeroDepthOps = [] {
        BrocotVoice v;
        v.prepare (kSampleRate);
        v.setCarrierHz (220.0f);
        v.setLevel (0.2f);
        for (int i = 0; i < BrocotVoice::N_OPS; ++i)
        {
            auto& o = v.op (i);
            o.setEnabled (true);
            o.setPath (Path ("R"));
            o.setDepth (0.0f);
        }
        std::vector<float> buf (4096, 0.0f);
        v.render (buf.data(), (int) buf.size());
        return buf;
    };

    auto a = renderCarrierOnly();
    auto b = renderZeroDepthOps();
    REQUIRE (a.size() == b.size());

    float maxDiff = 0.0f;
    for (size_t i = 0; i < a.size(); ++i)
        maxDiff = std::max (maxDiff, std::abs (a[i] - b[i]));

    REQUIRE (maxDiff < 1e-6f);
}

TEST_CASE ("Multi-op output differs from single-op output (summation is real)", "[BrocotVoice]")
{
    // If parallel summation isn't working, adding ops 2 and 3 wouldn't change
    // anything. This test demonstrates that adding extra modulators with
    // non-zero depth changes the rendered waveform.
    auto renderWithOpCount = [] (int numOps) {
        BrocotVoice v;
        v.prepare (kSampleRate);
        v.setCarrierHz (130.81f);
        v.setLevel (0.18f);
        const Path shared ("RLLLR");
        struct S { int pos; float d; };
        const S setups[] = { {1, 5.13f}, {3, 0.86f}, {5, 0.90f} };
        for (int i = 0; i < numOps; ++i)
        {
            auto& o = v.op (i);
            o.setEnabled (true);
            o.setPath (shared.prefix (setups[(size_t) i].pos));
            o.setDepth (setups[(size_t) i].d);
        }
        std::vector<float> buf (4096, 0.0f);
        v.render (buf.data(), (int) buf.size());
        return buf;
    };

    auto one   = renderWithOpCount (1);
    auto two   = renderWithOpCount (2);
    auto three = renderWithOpCount (3);

    auto rmsDiff = [] (const std::vector<float>& a, const std::vector<float>& b) {
        double s = 0.0;
        for (size_t i = 0; i < a.size(); ++i) s += (a[i] - b[i]) * (a[i] - b[i]);
        return std::sqrt (s / (double) a.size());
    };

    // Each successive op should produce a measurably different waveform.
    REQUIRE (rmsDiff (one,   two)   > 0.001);
    REQUIRE (rmsDiff (two,   three) > 0.0005);
    REQUIRE (rmsDiff (one,   three) > 0.001);
}

TEST_CASE ("J₀ null preset (single mod at 1/1, I=2.405) renders cleanly", "[BrocotVoice]")
{
    // BROCOT-SPEC.md §4, verification case 2 — carrier at f_c with one mod
    // at the same frequency and I = 2.405 (the first zero of J₀). The
    // carrier-frequency component should vanish from the spectrum; total
    // signal energy stays bounded. We can't FFT here, but we can verify
    // the engine doesn't blow up at this notable depth value.
    BrocotVoice v;
    v.prepare (kSampleRate);
    v.setCarrierHz (220.0f);
    v.setLevel (0.2f);
    auto& m0 = v.op (0);
    m0.setEnabled (true);
    m0.setPath (Path (""));      // 1/1
    m0.setDepth (2.4048f);

    constexpr int N = 9600;
    std::vector<float> buf (N, 0.0f);
    v.render (buf.data(), N);

    auto s = measure (buf);
    REQUIRE_FALSE (s.anyNaN);
    REQUIRE_FALSE (s.anyInf);
    REQUIRE (s.peakAbs <= 1.0f);
    REQUIRE (s.rms > 0.01f);  // total energy roughly unchanged (Parseval)
}
