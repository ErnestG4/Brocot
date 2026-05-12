#include "AmpEnvelope.h"

namespace brocot
{

void AmpEnvelope::prepare (double sampleRate)
{
    adsr.setSampleRate (sampleRate);
    setParameters (params);
}

void AmpEnvelope::setParameters (const Parameters& p)
{
    params = p;
    juce::ADSR::Parameters jp;
    jp.attack  = params.attack;
    jp.decay   = params.decay;
    jp.sustain = params.sustain;
    jp.release = params.release;
    adsr.setParameters (jp);
}

void AmpEnvelope::noteOn()  noexcept { adsr.noteOn();  }
void AmpEnvelope::noteOff() noexcept { adsr.noteOff(); }

} // namespace brocot
