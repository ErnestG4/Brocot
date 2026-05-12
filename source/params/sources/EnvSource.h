#pragma once

#include "../ModSource.h"

namespace brocot
{

/**
    Adaptor exposing the most recent envelope sample (cached by the audio
    thread) as a ModulationSource. The actual envelope is owned by the
    voice; this just provides a stable [0, 1] view for any ModTarget that
    wants to listen.

    The cached value is updated by the audio thread once per block (or per
    sample, depending on caller); UI/host reads see the latest committed
    value with relaxed memory ordering — drift is tolerable, torn writes
    aren't.
*/
class AmpEnvSource : public ModulationSource
{
public:
    void setCurrent (float v) noexcept { value.store (v, std::memory_order_relaxed); }
    float getModulationValue() const noexcept override
    {
        return value.load (std::memory_order_relaxed);
    }
    juce::String getSourceId() const override { return "env.amp"; }

private:
    std::atomic<float> value { 0.0f };
};

} // namespace brocot
