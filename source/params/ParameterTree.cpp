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

    return layout;
}

} // namespace brocot::params
