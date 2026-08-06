#pragma once

#include <algorithm>
#include <cmath>

namespace brocot::params
{

/**
    Waveform-param v1 → v2 migration (the pulse-PW state-compat gate,
    phase3/modmap.md).

    v1: opId(i,"waveform") was a 4-entry AudioParameterChoice
        { sine, triangle, saw, square }, ParameterID version 1.
    v2: 8 entries { sine, triangle, saw, square, pulse, +3 reserved },
        ParameterID version 2. The headroom means the NEXT waveform addition
        needs no migration at all.

    What actually needs remapping — verified against this JUCE tree
    (juce_AudioProcessorValueTreeState.cpp: flushToTree stores
    `unnormalisedValue`; setNewState applies via setDenormalisedValue) AND
    empirically against a real saved state (~/.config/Brocot.settings decodes
    to <PARAM id="op1.position" value="3.0"/> — an IntParam 0..32 stored as
    3.0, not 3/32): the APVTS state tree stores DENORMALISED values, i.e. the
    raw choice INDEX for a Choice param. A v1 square is stored as 3.0 and
    loads into the 8-entry v2 param as index 3 — still square. So the
    stored-value migration is an identity for well-formed v1 states; its job
    is defensive (round + clamp into the v1 index range so a corrupt or
    out-of-range v1 value can never select pulse/reserved entries).

    The NORMALISED remap (round(v·3)/7) is also provided below, fully tested,
    for any representation that genuinely stores 0..1 fractions (host
    automation lanes / host-side normalized snapshots live there — outside
    setStateInformation's reach — and any future code path that handles
    normalized values must use it). It must NOT be applied to the APVTS tree
    values: treating a stored index 3.0 as "normalized" would clamp it to 1.0
    and reload square as sine.

    Both functions are pure so BrocotTests can cover them without linking the
    processor (see tests/WaveMigrationTests.cpp).
*/
namespace wavemigration
{
    /** Entry counts of the two waveform-choice generations. */
    inline constexpr int kNumWaveChoicesV1 = 4;
    inline constexpr int kNumWaveChoicesV2 = 8;

    /** State-version stamped on the APVTS root by getStateInformation.
        States without the property are v1 (pre-pulse). */
    inline constexpr int kWaveVersion = 2;
    inline constexpr const char* kWaveVersionProperty = "waveVersion";

    /** Migrate a v1 waveform value AS STORED in the APVTS state tree
        (denormalised = the raw choice index, as a float). Identity for the
        valid v1 indices 0..3; anything else rounds/clamps into that range so
        a malformed v1 state can never land on pulse or a reserved slot. */
    inline float migrateWaveformStored (float v1Stored) noexcept
    {
        const float idx = std::round (v1Stored);
        return std::clamp (idx, 0.0f, (float) (kNumWaveChoicesV1 - 1));
    }

    /** Migrate a v1 NORMALISED waveform value (0..1 over 4 entries) to the
        v2 normalised value (0..1 over 8 entries) selecting the same entry:
        v·(4−1) recovers the old index, /(8−1) renormalises. Old square
        (3/3 = 1.0) → 3/7, NOT the naive 1.0 → index 7.
        For normalized-representation channels only — the APVTS tree stores
        indices, use migrateWaveformStored there (see header comment). */
    inline float migrateWaveformNormalized (float v1Normalized) noexcept
    {
        const float clamped  = std::clamp (v1Normalized, 0.0f, 1.0f);
        const float oldIndex = std::round (clamped * (float) (kNumWaveChoicesV1 - 1));
        return oldIndex / (float) (kNumWaveChoicesV2 - 1);
    }
}

} // namespace brocot::params
