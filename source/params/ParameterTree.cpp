#include "ParameterTree.h"

namespace brocot::params
{

namespace
{
    using FloatParam  = juce::AudioParameterFloat;
    using BoolParam   = juce::AudioParameterBool;
    using ChoiceParam = juce::AudioParameterChoice;
    using IntParam    = juce::AudioParameterInt;

    juce::NormalisableRange<float> logRange (float min, float max, float skew)
    {
        // skew < 1 biases toward `min`; this gives slider feel matching
        // perceptual time-scale (short A/D feels different from long).
        juce::NormalisableRange<float> r (min, max);
        r.setSkewForCentre (skew);
        return r;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID (ids::masterLevel, 1),
        "Master Level",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    layout.add (std::make_unique<IntParam> (
        juce::ParameterID (ids::octaveShift, 1),
        "Octave Shift", -3, 3, 0));

    layout.add (std::make_unique<ChoiceParam> (
        juce::ParameterID (ids::pathMode, 1),
        "Path Mode",
        juce::StringArray { "shared", "free" }, 0));

    // ADSR per spec §2 ranges. Skew gives more resolution toward fast attacks
    // / decays, which is where the audible action is for percussive / pluck
    // sounds; sustain is linear; release uses similar log feel.
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID (ids::envAttack,  1),
        "Env Attack",  logRange (0.0f, 5.0f, 0.3f),  0.01f));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID (ids::envDecay,   1),
        "Env Decay",   logRange (0.0f, 5.0f, 0.3f),  0.15f));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID (ids::envSustain, 1),
        "Env Sustain", juce::NormalisableRange<float> (0.0f, 1.0f), 0.8f));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID (ids::envRelease, 1),
        "Env Release", logRange (0.0f, 10.0f, 0.3f), 0.4f));

    for (int i = 0; i < kNumOperators; ++i)
    {
        layout.add (std::make_unique<BoolParam> (
            juce::ParameterID (opId (i, "enabled"), 1),
            "Op " + juce::String (i + 1) + " Enabled",
            i < 3));  // first three on by default per spec §2

        layout.add (std::make_unique<FloatParam> (
            juce::ParameterID (opId (i, "depth"), 1),
            "Op " + juce::String (i + 1) + " Depth",
            juce::NormalisableRange<float> (0.0f, 8.0f), 0.5f));
    }

    // Filter (spec §2). Cutoff is in normalized 0..1 here — PluginProcessor
    // maps that to a logarithmic 20 Hz..20 kHz sweep when reading.
    layout.add (std::make_unique<ChoiceParam> (
        juce::ParameterID (ids::filterType, 1),
        "Filter Type",
        juce::StringArray { "LP", "BP", "HP", "Notch" }, 0));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID (ids::filterCutoff, 1),
        "Filter Cutoff", juce::NormalisableRange<float> (0.0f, 1.0f), 0.85f));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID (ids::filterResonance, 1),
        "Filter Resonance", juce::NormalisableRange<float> (0.0f, 1.0f), 0.15f));

    // LFO (spec §2). Rate is in Hz with log skew; depth is normalized.
    layout.add (std::make_unique<ChoiceParam> (
        juce::ParameterID (ids::lfoShape, 1),
        "LFO Shape",
        juce::StringArray { "Sine", "Triangle", "Square", "Saw" }, 0));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID (ids::lfoRate, 1),
        "LFO Rate", logRange (0.01f, 20.0f, 0.3f), 0.5f));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID (ids::lfoDepth, 1),
        "LFO Depth", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));
    layout.add (std::make_unique<BoolParam> (
        juce::ParameterID (ids::lfoKeySync, 1),
        "LFO Key Sync", false));

    return layout;
}

} // namespace brocot::params
