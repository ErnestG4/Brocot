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
    // Must match BrocotVoice::N_OPS — bumping one without the other will
    // mismatch the APVTS layout against the voice's operator array.
    // v1.x candidates: 32 / 64 as a "Brocot High Density" build variant
    // gated by a CMake option; the audio thread budget can comfortably
    // accommodate either with sin() per op.
    inline constexpr int kNumOperators = 16;

    inline juce::String opId (int idx, const char* suffix)
    {
        // Plain string concatenation — do NOT use juce::String::formatted
        // with "%s" here. On Windows, String::formatted converts the format
        // to wide-char and calls _vsnwprintf, which then interprets %s as
        // wchar_t* and reads our const char* two bytes at a time as garbage
        // wide chars. The result is corrupted parameter IDs that don't match
        // any APVTS parameter — every Attachment in the editor then silently
        // fails to bind, and the audio thread sees default values forever.
        return juce::String ("op") + juce::String (idx)
             + juce::String (".") + juce::String (suffix);
    }

    namespace ids
    {
        inline constexpr auto masterLevel = "master.level";
        inline constexpr auto octaveShift = "engine.octaveShift";
        inline constexpr auto pathMode    = "engine.pathMode";
        inline constexpr auto polyphony   = "engine.polyphony";
        inline constexpr auto envAttack   = "env.attack";
        inline constexpr auto envDecay    = "env.decay";
        inline constexpr auto envSustain  = "env.sustain";
        inline constexpr auto envRelease  = "env.release";
        inline constexpr auto filterType      = "filter.type";
        inline constexpr auto filterCutoff    = "filter.cutoff";
        inline constexpr auto filterResonance = "filter.resonance";
        inline constexpr auto lfoShape   = "lfo.shape";
        inline constexpr auto lfoRate    = "lfo.rate";
        inline constexpr auto lfoDepth   = "lfo.depth";
        inline constexpr auto lfoKeySync = "lfo.keySync";
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}

} // namespace brocot
