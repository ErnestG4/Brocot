#pragma once

#include "EnvelopeShape.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace brocot
{

/**
    Per the unified-prototype: a modulator that smoothly bridges sub-audio
    LFO and audio-rate FM territory via a single rate parameter (0.1–800 Hz).
    Ownership of the lock-graph / target routing lives on the processor; this
    class is pure DSP state — phase accumulator, envelope shape, depth,
    PSK quantization, optional key-sync.

    The processor passes the *effective* rate (post-lock-resolution) into
    tick() each sample, so this class doesn't know about other modulators
    or carriers. Cycle guards and transitive resolution are processor concerns.
*/
class UniversalModulator
{
public:
    void prepare (double sampleRateIn) noexcept
    {
        sampleRate = (float) sampleRateIn;
        smoothedDepth.reset (sampleRateIn, 0.03);
        smoothedDepth.setCurrentAndTargetValue (0.0f);
    }

    void reset() noexcept { phase = 0.0f; }
    void onNoteOn() noexcept { if (keySync) phase = 0.0f; }

    void setShape (envelope::Shape s) noexcept { shape = s; }
    void setKappa (float k) noexcept           { kappa = k; }
    void setDepth (float d) noexcept           { smoothedDepth.setTargetValue (d); }
    void setKeySync (bool k) noexcept          { keySync = k; }
    void setPSKStates (int n) noexcept         { pskN = juce::jlimit (0, 16, n); }
    void setPSKSmooth (float s) noexcept       { pskSmooth = juce::jlimit (0.0f, 1.0f, s); }

    envelope::Shape getShape() const noexcept { return shape; }
    float getKappa() const noexcept           { return kappa; }
    float getDepth() const noexcept           { return smoothedDepth.getTargetValue(); }
    bool  isKeySync() const noexcept          { return keySync; }
    int   getPSKStates() const noexcept       { return pskN; }
    float getPhase() const noexcept           { return phase; }

    /** Advances phase by one sample at the given Hz and returns the bipolar
        output ([-1, +1]) scaled by depth. `depthOffset` is an additive
        contribution from V2.7 cascade chaining (one mod modulating another's
        depth); the effective depth is clamped at zero. */
    inline float tickBipolar (float rateHz, float depthOffset = 0.0f) noexcept
    {
        const float pUsed = pskN >= 2 ? envelope::pskQuantize (phase, pskN, pskSmooth) : phase;
        const float raw = envelope::bipolar (pUsed, shape, kappa);
        advance (rateHz);
        return raw * std::max (0.0f, smoothedDepth.getNextValue() + depthOffset);
    }

    /** Advances phase and returns the unipolar envelope value ([0, 1]) scaled
        by depth. */
    inline float tickUnipolar (float rateHz, float depthOffset = 0.0f) noexcept
    {
        const float pUsed = pskN >= 2 ? envelope::pskQuantize (phase, pskN, pskSmooth) : phase;
        const float raw = envelope::unipolar (pUsed, shape, kappa);
        advance (rateHz);
        return raw * std::max (0.0f, smoothedDepth.getNextValue() + depthOffset);
    }

private:
    inline void advance (float rateHz) noexcept
    {
        phase += envelope::kTwoPi * rateHz / sampleRate;
        if (phase >= envelope::kTwoPi) phase -= envelope::kTwoPi;
        else if (phase < 0.0f)         phase += envelope::kTwoPi;
    }

    float sampleRate { 44100.0f };
    float phase      { 0.0f };
    envelope::Shape shape { envelope::Shape::Sine };
    float kappa      { 8.0f };
    bool  keySync    { false };
    int   pskN       { 0 };       // 0 = off
    float pskSmooth  { 0.0f };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedDepth;
};

} // namespace brocot
