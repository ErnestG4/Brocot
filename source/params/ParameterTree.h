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
    // Reduced back to 8 for V2 build complexity (16-LFO bank is being added
    // on top; total mod surface is 8 ops + 16 LFOs = 24 panels). High-
    // density variants (32 / 64) remain a CMake-gated v1.x option.
    inline constexpr int kNumOperators = 8;

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

    // V2 LFO bank — 16 universal modulators.
    inline constexpr int kNumModulators = 16;

    inline juce::String modId (int idx, const char* suffix)
    {
        return juce::String ("mod") + juce::String (idx)
             + juce::String (".") + juce::String (suffix);
    }

    // Encoded lock-target kinds (matches the order in the APVTS choice list).
    enum class LockTargetKind : int
    {
        Free = 0,
        Carrier,
        Operator,
        Modulator,
        Count
    };

    // Encoded routing target kinds. Index = operator/modulator index for
    // the per-target kinds; ignored for the unique-target kinds.
    enum class TargetKind : int
    {
        FilterCutoff = 0,
        CarrierAm,
        CarrierFm,
        OpDepth,
        OpPosition,
        ModRate,
        ModDepth,
        Count
    };

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
