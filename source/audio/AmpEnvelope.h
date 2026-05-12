#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace brocot
{

/**
    ADSR amplitude envelope per BROCOT-SPEC.md §2 ("ADSR amp envelope, no
    filter envelope"). Thin wrapper around juce::ADSR — exists as its own
    type so the voice doesn't depend on a JUCE class directly and so v1.x
    can swap in alternative shapes without churning callers.

    Ranges per spec §2: A 0–5s, D 0–5s, S 0–1, R 0–10s.
*/
class AmpEnvelope
{
public:
    struct Parameters
    {
        float attack  { 0.01f };
        float decay   { 0.15f };
        float sustain { 0.80f };
        float release { 0.40f };
    };

    void prepare (double sampleRate);

    void setParameters (const Parameters& p);
    Parameters getParameters() const noexcept { return params; }

    void noteOn() noexcept;
    void noteOff() noexcept;

    bool isActive() const noexcept { return adsr.isActive(); }

    /** Returns the next envelope sample in [0, 1]. */
    inline float getNextSample() noexcept { return adsr.getNextSample(); }

private:
    juce::ADSR adsr;
    Parameters params;
};

} // namespace brocot
