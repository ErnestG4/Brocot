#include "BrocotVoice.h"

#include <cmath>

namespace brocot
{

void BrocotVoice::prepare (double sampleRate)
{
    invSampleRate = (float) (1.0 / sampleRate);
    resetPhases();
}

void BrocotVoice::resetPhases() noexcept
{
    carrierPhase = 0.0f;
    for (auto& o : ops)
        o.reset();
}

void BrocotVoice::render (float* out, int numSamples) noexcept
{
    constexpr float twoPi = 6.2831853071795864769f;

    for (int n = 0; n < numSamples; ++n)
    {
        float deltaSum = 0.0f;
        for (auto& o : ops)
            deltaSum += o.processSample (carrierHz, invSampleRate);

        const float instFreq = carrierHz + deltaSum;
        const float sample = std::sin (twoPi * carrierPhase) * level;

        carrierPhase += instFreq * invSampleRate;
        if (carrierPhase >= 1.0f) carrierPhase -= std::floor (carrierPhase);
        else if (carrierPhase < 0.0f) carrierPhase -= std::floor (carrierPhase);

        out[n] += sample;
    }
}

} // namespace brocot
