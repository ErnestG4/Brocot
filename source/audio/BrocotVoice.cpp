#include "BrocotVoice.h"

#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>

namespace brocot
{

void BrocotVoice::prepare (double sampleRate)
{
    invSampleRate = (float) (1.0 / sampleRate);
    ampEnv.prepare (sampleRate);
    smoothedLevel.reset (sampleRate, 0.03);
    smoothedLevel.setCurrentAndTargetValue (0.0f);
    for (auto& o : ops)
        o.prepare (sampleRate);
    resetPhases();
}

void BrocotVoice::resetPhases() noexcept
{
    carrierPhase = 0.0f;
    for (auto& o : ops)
        o.reset();
}

void BrocotVoice::noteOn (int midiNoteNumber, float /*velocity*/) noexcept
{
    activeNote = midiNoteNumber;
    carrierHz = (float) juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber);
    // Tell every operator how far the played note sits from middle C (MIDI
    // 60), in octaves, so per-op key-scaling can adjust the modulation index
    // with pitch. Done at note-on (env is at 0, so any depth step is masked).
    const float octavesFromMiddleC = (float) (midiNoteNumber - 60) / 12.0f;
    for (auto& o : ops)
        o.setNoteOctaves (octavesFromMiddleC);
    // Retrigger only — phases free-run, matching the prototype's continuous
    // FM character. Resetting phase on every note-on causes audible clicks
    // and changes the timbral attack; the prototype doesn't do this and
    // BROCOT-SPEC.md §4 doesn't require it.
    ampEnv.noteOn();
}

void BrocotVoice::noteOff (int midiNoteNumber) noexcept
{
    // BROCOT-SPEC.md §8: only the matching note-off releases the envelope;
    // other note-offs are ignored (last-note priority, no fallback stack).
    if (midiNoteNumber == activeNote)
    {
        ampEnv.noteOff();
        activeNote = kNoActiveNote;
    }
}

float BrocotVoice::renderSample (const PerSampleMod& mod) noexcept
{
    constexpr float twoPi = 6.2831853071795864769f;

    const float envSample = ampEnv.getNextSample();
    const float lev = smoothedLevel.getNextValue();

    float deltaSum = mod.carrierFmHz;
    for (size_t i = 0; i < ops.size(); ++i)
        deltaSum += ops[i].processSample (carrierHz, invSampleRate,
                                          mod.opDepthAdd[i]);

    const float instFreq = carrierHz + deltaSum;
    const float sample = std::sin (twoPi * carrierPhase)
                       * lev * envSample * mod.carrierAmGain;

    carrierPhase += instFreq * invSampleRate;
    if (carrierPhase >= 1.0f)      carrierPhase -= std::floor (carrierPhase);
    else if (carrierPhase < 0.0f)  carrierPhase -= std::floor (carrierPhase);

    return sample;
}

void BrocotVoice::render (float* out, int numSamples) noexcept
{
    const PerSampleMod neutral;
    for (int n = 0; n < numSamples; ++n)
        out[n] += renderSample (neutral);
}

} // namespace brocot
