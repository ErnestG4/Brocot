#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace brocot
{

/**
    APVTS parameter IDs and layout for Brocot v1.

    Path strings are NOT host-automatable parameters — they live in the
    APVTS state as a child ValueTree branch instead. Host automation here
    covers floats and choices only (see BROCOT-SPEC.md §6).

    Filter / LFO / polyphony parameters land in Slice 7 / Slice 15; this
    header gets extended in place.
*/
namespace params
{
    inline constexpr int kNumOperators = 8;

    inline juce::String opId (int idx, juce::StringRef suffix)
    {
        return juce::String::formatted ("op%d.%s", idx, suffix.text);
    }

    namespace ids
    {
        inline constexpr auto masterLevel = "master.level";
        inline constexpr auto octaveShift = "engine.octaveShift";
        inline constexpr auto pathMode    = "engine.pathMode";
        inline constexpr auto envAttack   = "env.attack";
        inline constexpr auto envDecay    = "env.decay";
        inline constexpr auto envSustain  = "env.sustain";
        inline constexpr auto envRelease  = "env.release";
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}

} // namespace brocot
