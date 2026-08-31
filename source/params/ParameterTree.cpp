#include "ParameterTree.h"

#include "../audio/GrooveLock.h"

namespace brocot::params
{

namespace
{
    using FloatParam  = juce::AudioParameterFloat;
    using BoolParam   = juce::AudioParameterBool;
    using ChoiceParam = juce::AudioParameterChoice;
    using IntParam    = juce::AudioParameterInt;

    // setSkewForCentre takes a VALUE in the range, not a fraction — slider
    // midpoint outputs this value. Using midpoints in the perceptually
    // useful zone (most of the slider real estate covers the values you
    // actually use) gives the "log feel" without burying the bottom half
    // of the slider in near-zero values.
    juce::NormalisableRange<float> logRange (float min, float max, float centreValue)
    {
        juce::NormalisableRange<float> r (min, max);
        r.setSkewForCentre (centreValue);
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

    layout.add (std::make_unique<ChoiceParam> (
        juce::ParameterID (ids::polyphony, 1),
        "Polyphony",
        juce::StringArray { "mono", "duo", "poly" }, 0));

    // Voice count used in "poly" mode (ignored for mono/duo). Upper bound must
    // match BrocotAudioProcessor::kMaxVoices.
    layout.add (std::make_unique<IntParam> (
        juce::ParameterID (ids::maxVoices, 1),
        "Max Voices", 2, 16, 8));

    // ADSR per spec §2 ranges. Skew gives more resolution toward fast attacks
    // / decays, which is where the audible action is for percussive / pluck
    // sounds; sustain is linear; release uses similar log feel.
    // Envelope time params: slider midpoint at 0.5s for attack/decay (so
    // the upper half covers 0.5..5s pads, lower half covers 0..0.5s snappy
    // attacks). Release midpoint at 1.0s for the same feel on a 0..10s range.
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID (ids::envAttack,  1),
        "Env Attack",  logRange (0.0f, 5.0f, 0.5f),  0.01f));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID (ids::envDecay,   1),
        "Env Decay",   logRange (0.0f, 5.0f, 0.5f),  0.15f));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID (ids::envSustain, 1),
        "Env Sustain", juce::NormalisableRange<float> (0.0f, 1.0f), 0.8f));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID (ids::envRelease, 1),
        "Env Release", logRange (0.0f, 10.0f, 1.0f), 0.4f));

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

        // Position along the shared path. 32 is a generous upper bound on
        // path length; resolution caps at the actual sharedPath.length() when
        // applied to the voice.
        layout.add (std::make_unique<IntParam> (
            juce::ParameterID (opId (i, "position"), 1),
            "Op " + juce::String (i + 1) + " Position", 0, 32, 0));

        // Per-op fine detune in cents (offset from the exact SB ratio). ±100c
        // = ±1 semitone; the musically-useful drift/chorus region is the first
        // few cents. Default 0 = exact rational (perfectly harmonic).
        layout.add (std::make_unique<FloatParam> (
            juce::ParameterID (opId (i, "detune"), 1),
            "Op " + juce::String (i + 1) + " Detune",
            juce::NormalisableRange<float> (-100.0f, 100.0f), 0.0f));

        // Per-op key-scaling: how the modulation index tracks pitch. +1 halves
        // the index one octave above middle C (fewer sidebands up high); -1
        // doubles it. Default 0 = no scaling (depth constant across keyboard).
        layout.add (std::make_unique<FloatParam> (
            juce::ParameterID (opId (i, "keyScale"), 1),
            "Op " + juce::String (i + 1) + " Key Scale",
            juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f));

        // Operator waveform — a non-sine modulator reshapes the FM sidebands
        // (more timbre per op). Default sine = pre-existing behaviour.
        // v2 (ParameterID version 2): grown from 4 entries to 8 — "pulse"
        // plus three inert reserved slots, so the NEXT waveform addition
        // won't change the choice count (= won't renormalize host-side
        // values) again. v1 states are remapped in setStateInformation via
        // WaveMigration.h; entry count changes here must keep
        // wavemigration::kNumWaveChoicesV2 in sync.
        layout.add (std::make_unique<ChoiceParam> (
            juce::ParameterID (opId (i, "waveform"), 2),
            "Op " + juce::String (i + 1) + " Waveform",
            juce::StringArray { "sine", "triangle", "saw", "square", "pulse",
                                "(reserved 1)", "(reserved 2)", "(reserved 3)" },
            0));

        // Pulse duty cycle (high fraction). Audible only when the waveform
        // is "pulse". 0.5 = square; the n-th harmonic scales as
        // |sin(πn·duty)|, so sweeping duty combs the sideband recipe (the
        // classic PW timbre axis). A plain new param — state-safe: states
        // saved without it load at the 0.25 default.
        layout.add (std::make_unique<FloatParam> (
            juce::ParameterID (opId (i, "duty"), 1),
            "Op " + juce::String (i + 1) + " Pulse Duty",
            juce::NormalisableRange<float> (0.05f, 0.5f), 0.25f));

        // Even-harmonic suppression (Fritz double-pulse β). Audible only when
        // the waveform is "pulse": sweeps continuously from the plain pulse
        // (0) to half-wave antisymmetry / odd harmonics only (1). Parity is
        // measured orthogonal to the coincidence structure — this knob moves
        // ~20 dB of even-harmonic level and not one ratio in or out of the
        // ringing set. State-safe: old states load at the 0 default, which is
        // the bit-exact old pulse.
        layout.add (std::make_unique<FloatParam> (
            juce::ParameterID (opId (i, "evensup"), 1),
            "Op " + juce::String (i + 1) + " Even Suppression",
            juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

        // Wavefold β. On a sine modulator: an odd Bessel stack that extends
        // the coincidence horizon's denominator side only, monotonically
        // (census: phase3/wavefold.md, SHIP_CONTINUOUS_EXTENSION) — a
        // continuous q-resolution axis. 0 = exact bypass; old states load
        // at that default.
        layout.add (std::make_unique<FloatParam> (
            juce::ParameterID (opId (i, "fold"), 1),
            "Op " + juce::String (i + 1) + " Fold",
            juce::NormalisableRange<float> (0.0f, 2.5f), 0.0f));
    }

    // Single-op self-feedback on op 1 (index 0) only — the one topology the
    // parallel-FM model can't reach (phase3 topology study: ~70% of feedback
    // timbres are unreachable by any parallel config). Classic DX-style
    // feedback-sawtooth / bright character. 0..1 maps to a self-PM index of
    // 0..π radians (above ~π the loop tips into noise); default 0 = clean sine.
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID (opId (0, "feedback"), 1),
        "Op 1 Feedback",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

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

    // LFO (legacy single-LFO; V2.8 retires this once the LFO bank is wired
    // through completely). Kept for now so existing presets / state still load.
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

    // Groove feel (S4): the steer tab's Farey-gravity constants, UI-exposed.
    // Defaults are the pre-S4 hardcoded values (groove::kDefault*), so states
    // saved without these params steer exactly as before. grooveLock arms the
    // lock-to-groove-on-release behaviour (S5); off = the legacy settle.
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID (ids::grooveGravity, 1),
        "Groove Gravity",
        juce::NormalisableRange<float> (0.0f, 1.0f), groove::kDefaultGravity));
    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID (ids::grooveRestless, 1),
        "Groove Restlessness",
        juce::NormalisableRange<float> (0.0f, 1.0f), groove::kDefaultRestlessness));
    layout.add (std::make_unique<BoolParam> (
        juce::ParameterID (ids::grooveLock, 1),
        "Groove Lock", groove::kDefaultLock));

    // V2 universal-modulator bank — 16 LFOs, each with its own rate
    // (extralinear 0.1-800 Hz sweep), lock target, envelope shape, depth,
    // routing target, PSK quantization, and key-sync flag.
    const juce::StringArray shapeChoices {
        "Sine", "Von Mises", "Gaussian", "Square", "SawRise", "SawFall",
        "Triangle", "ExpDecay"
    };
    const juce::StringArray lockKindChoices {
        "Free", "Carrier", "Op", "Mod"
    };
    const juce::StringArray targetKindChoices {
        "Filter cutoff", "Carrier AM", "Carrier FM",
        "Op depth", "Op position", "Mod rate", "Mod depth",
        "Feedback (op 1)", "Env retrigger"
    };
    const juce::StringArray pskChoices { "Off", "2", "4", "8", "16" };

    for (int i = 0; i < kNumModulators; ++i)
    {
        const auto name = "Mod " + juce::String (i + 1);

        // Rate — log skew, midpoint at 5 Hz for natural feel across the
        // sub-audio / audio bridge (1 Hz LFO / 5 Hz / 50 Hz / 500 Hz all
        // reachable with decent resolution).
        layout.add (std::make_unique<FloatParam> (
            juce::ParameterID (modId (i, "rate"), 1),
            name + " Rate", logRange (0.1f, 800.0f, 5.0f), 4.0f));

        // Lock target = kind + index (Op or Mod). Index is ignored for
        // Free / Carrier kinds.
        layout.add (std::make_unique<ChoiceParam> (
            juce::ParameterID (modId (i, "lockKind"), 1),
            name + " Lock Kind", lockKindChoices, 0));
        layout.add (std::make_unique<IntParam> (
            juce::ParameterID (modId (i, "lockIndex"), 1),
            name + " Lock Index", 0, 15, 0));

        layout.add (std::make_unique<ChoiceParam> (
            juce::ParameterID (modId (i, "shape"), 1),
            name + " Shape", shapeChoices, 0));
        layout.add (std::make_unique<FloatParam> (
            juce::ParameterID (modId (i, "kappa"), 1),
            name + " Sharpness",
            juce::NormalisableRange<float> (0.0f, 50.0f), 8.0f));
        layout.add (std::make_unique<FloatParam> (
            juce::ParameterID (modId (i, "depth"), 1),
            name + " Depth",
            juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

        // Routing target = kind + index (Op or Mod). Index ignored for
        // FilterCutoff / CarrierAm / CarrierFm.
        layout.add (std::make_unique<ChoiceParam> (
            juce::ParameterID (modId (i, "targetKind"), 1),
            name + " Target", targetKindChoices, 0));
        layout.add (std::make_unique<IntParam> (
            juce::ParameterID (modId (i, "targetIndex"), 1),
            name + " Target Index", 0, 15, 0));

        layout.add (std::make_unique<ChoiceParam> (
            juce::ParameterID (modId (i, "psk"), 1),
            name + " PSK", pskChoices, 0));
        layout.add (std::make_unique<FloatParam> (
            juce::ParameterID (modId (i, "pskSmooth"), 1),
            name + " PSK Smooth",
            juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));

        layout.add (std::make_unique<BoolParam> (
            juce::ParameterID (modId (i, "keySync"), 1),
            name + " Key Sync", false));
    }

    return layout;
}

} // namespace brocot::params
