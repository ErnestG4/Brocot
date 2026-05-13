#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace brocot
{

class BrocotAudioProcessor;

/**
    BROCOT-SPEC.md §9 preset schema (.brocot files = JSON). Serialisation is
    a flat round-trip between the processor's APVTS + path state and a
    juce::var DynamicObject tree. Validation errors return an explanatory
    message via the optional juce::String& outError.
*/
namespace preset
{
    inline constexpr auto kFormatVersion = "2.0";

    juce::var serialise (const BrocotAudioProcessor&);

    /** Returns true and applies the preset to `processor` if `json` is a
        valid preset object; returns false and (when provided) fills
        `outError` with a short message otherwise.

        v1 presets (version "1.0", no provenance block) are auto-migrated:
        `author`/`description`/`category` are folded into a synthesised
        provenance block with kind="manual". Migrated presets re-save as v2. */
    bool applyTo (BrocotAudioProcessor&, const juce::var& json,
                  juce::String* outError = nullptr);
}

} // namespace brocot
