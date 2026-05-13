#pragma once

#include "Operator.h"
#include "AmpEnvelope.h"

#include <array>
#include <juce_audio_basics/juce_audio_basics.h>

namespace brocot
{

/**
    One FM voice: a sine carrier plus up to N_OPS parallel modulators,
    gated by an ADSR amplitude envelope.

    Per BROCOT-SPEC.md §2/§4 — modulators are parallel and sum into the
    carrier's instantaneous frequency. Each modulator's contribution is
    I_k × f_mk × sin(2π · φ_k); the carrier's effective frequency is
    f_c + Σ Δf_k, and the output is sin(2π · φ_c) · env(t).

    MIDI handling per spec §8 (monophonic, latest-note priority):
      - noteOn(n) sets the carrier from MIDI note n and retriggers env.
      - noteOff(n) releases the env only if n matches the active note;
        other note-offs are ignored.
*/
class BrocotVoice
{
public:
    static constexpr int N_OPS = 8;  // must match params::kNumOperators
    static constexpr int kNoActiveNote = -1;

    void prepare (double sampleRate);
    void setCarrierHz (float hz) noexcept { carrierHz = hz; }
    void setLevel (float linearGain) noexcept { smoothedLevel.setTargetValue (linearGain); }

    Operator& op (int idx) noexcept { return ops[(size_t) idx]; }
    AmpEnvelope& envelope() noexcept { return ampEnv; }

    void resetPhases() noexcept;

    void noteOn (int midiNoteNumber, float velocity = 1.0f) noexcept;
    void noteOff (int midiNoteNumber) noexcept;

    int  getActiveNote() const noexcept { return activeNote; }
    bool isActive() const noexcept { return ampEnv.isActive(); }

    /** Renders `numSamples` of mono audio summed onto `out`. */
    void render (float* out, int numSamples) noexcept;

private:
    std::array<Operator, N_OPS> ops {};
    AmpEnvelope ampEnv;
    float carrierHz     { 220.0f };
    float carrierPhase  { 0.0f };
    float invSampleRate { 1.0f / 44100.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedLevel;
    int   activeNote    { kNoActiveNote };
};

} // namespace brocot
