#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>

#include "params/WaveMigration.h"

// Waveform-param v1→v2 migration (the pulse-PW state-compat gate). The
// processor's setStateInformation can't run in BrocotTests (no
// juce_audio_processors link), so the migration lives in pure functions in
// params/WaveMigration.h and is covered here. Two representations exist:
//
//   - The APVTS state tree stores DENORMALISED values (raw choice indices)
//     — verified against JUCE's flushToTree/setNewState and a decoded real
//     saved state (see the WaveMigration.h header comment). That's what
//     setStateInformation remaps, via migrateWaveformStored.
//   - Normalized 0..1 values (host-side representations) remap via
//     migrateWaveformNormalized: old index = round(v·3), new value = idx/7.

using namespace brocot::params::wavemigration;
using Catch::Approx;

TEST_CASE ("waveform choice-count constants match the v1/v2 layouts", "[WaveMigration]")
{
    REQUIRE (kNumWaveChoicesV1 == 4);
    REQUIRE (kNumWaveChoicesV2 == 8);
    REQUIRE (kWaveVersion == 2);
}

TEST_CASE ("stored-value migration preserves every v1 waveform index", "[WaveMigration]")
{
    // The APVTS tree stores the raw index; a well-formed v1 state holds
    // exactly 0.0 / 1.0 / 2.0 / 3.0 and must reload as the SAME entry of the
    // 8-entry v2 param — i.e. pass through unchanged.
    for (int idx = 0; idx < kNumWaveChoicesV1; ++idx)
        REQUIRE (migrateWaveformStored ((float) idx) == Approx ((float) idx));

    // Float fuzz around an index snaps back to it.
    REQUIRE (migrateWaveformStored (2.00001f) == Approx (2.0f));
    REQUIRE (migrateWaveformStored (0.99998f) == Approx (1.0f));
}

TEST_CASE ("stored-value migration clamps malformed v1 values into the v1 range", "[WaveMigration]")
{
    // A v1 state can't legitimately hold index >3 — never let a corrupt one
    // land on pulse (4) or a reserved slot (5..7).
    REQUIRE (migrateWaveformStored (4.0f)  == Approx (3.0f));
    REQUIRE (migrateWaveformStored (7.0f)  == Approx (3.0f));
    REQUIRE (migrateWaveformStored (-1.0f) == Approx (0.0f));
}

TEST_CASE ("normalized migration maps each v1 entry onto the same v2 entry", "[WaveMigration]")
{
    // v1 normalized = idx/3 (4 entries); v2 normalized = idx/7 (8 entries).
    REQUIRE (migrateWaveformNormalized (0.0f)        == Approx (0.0f));         // sine
    REQUIRE (migrateWaveformNormalized (1.0f / 3.0f) == Approx (1.0f / 7.0f));  // triangle
    REQUIRE (migrateWaveformNormalized (2.0f / 3.0f) == Approx (2.0f / 7.0f));  // saw
    REQUIRE (migrateWaveformNormalized (1.0f)        == Approx (3.0f / 7.0f));  // square — NOT index 7
}

TEST_CASE ("normalized migration round-trips every v1 index through the v2 range", "[WaveMigration]")
{
    for (int idx = 0; idx < kNumWaveChoicesV1; ++idx)
    {
        const float v1 = (float) idx / (float) (kNumWaveChoicesV1 - 1);
        const float v2 = migrateWaveformNormalized (v1);
        REQUIRE ((int) std::lround (v2 * (float) (kNumWaveChoicesV2 - 1)) == idx);
    }
}

TEST_CASE ("normalized migration is robust to fuzzed and out-of-range input", "[WaveMigration]")
{
    // Values that don't sit exactly on a v1 grid point (dithered automation,
    // float drift) snap to the nearest v1 entry before renormalising.
    REQUIRE (migrateWaveformNormalized (0.34f)  == Approx (1.0f / 7.0f));  // ≈ triangle
    REQUIRE (migrateWaveformNormalized (0.95f)  == Approx (3.0f / 7.0f));  // ≈ square
    REQUIRE (migrateWaveformNormalized (-0.5f)  == Approx (0.0f));
    REQUIRE (migrateWaveformNormalized (1.5f)   == Approx (3.0f / 7.0f));
}

TEST_CASE ("v2 stored values would pass the stored-value migration untouched", "[WaveMigration]")
{
    // setStateInformation only migrates v1 states (waveVersion absent). This
    // pins the invariant that matters if that gate ever regressed: for the
    // sine..square indices shared by both generations the stored-value remap
    // is an identity, so double-migration cannot corrupt them.
    for (int idx = 0; idx < kNumWaveChoicesV1; ++idx)
        REQUIRE (migrateWaveformStored (migrateWaveformStored ((float) idx))
                     == Approx ((float) idx));
}
