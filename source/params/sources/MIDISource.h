#pragma once

#include "../ModSource.h"

#include <atomic>

namespace brocot
{

/** CC1 mod wheel, latched 0..1 across blocks. */
class MIDIModWheelSource : public ModulationSource
{
public:
    void setRaw14 (int rawCC) noexcept   // 0..127, kept as int for clarity
    {
        value.store (juce::jlimit (0.0f, 1.0f, (float) rawCC / 127.0f),
                     std::memory_order_relaxed);
    }
    void setNormalized (float v) noexcept
    {
        value.store (juce::jlimit (0.0f, 1.0f, v), std::memory_order_relaxed);
    }
    float getModulationValue() const noexcept override
    {
        return value.load (std::memory_order_relaxed);
    }
    juce::String getSourceId() const override { return "midi.modwheel"; }

private:
    std::atomic<float> value { 0.0f };
};

/** Pitch bend mapped to [-1, +1] (full ±range = ±2 semitones in v1 per §8). */
class MIDIPitchBendSource : public ModulationSource
{
public:
    void setBendRaw14 (int raw14) noexcept     // 0..16383, center 8192
    {
        const float normalized = (juce::jlimit (0, 16383, raw14) - 8192) / 8192.0f;
        value.store (juce::jlimit (-1.0f, 1.0f, normalized), std::memory_order_relaxed);
    }
    void setNormalized (float v) noexcept
    {
        value.store (juce::jlimit (-1.0f, 1.0f, v), std::memory_order_relaxed);
    }
    float getModulationValue() const noexcept override
    {
        return value.load (std::memory_order_relaxed);
    }
    juce::String getSourceId() const override { return "midi.pitchbend"; }

private:
    std::atomic<float> value { 0.0f };
};

/** Channel pressure (mono aftertouch), 0..1. */
class MIDIChannelPressureSource : public ModulationSource
{
public:
    void setRaw7 (int raw) noexcept
    {
        value.store (juce::jlimit (0.0f, 1.0f, (float) raw / 127.0f),
                     std::memory_order_relaxed);
    }
    float getModulationValue() const noexcept override
    {
        return value.load (std::memory_order_relaxed);
    }
    juce::String getSourceId() const override { return "midi.pressure"; }

private:
    std::atomic<float> value { 0.0f };
};

/** Per-voice MIDI velocity captured on the last note-on, 0..1. */
class MIDIVelocitySource : public ModulationSource
{
public:
    void setVelocity (float v) noexcept
    {
        value = juce::jlimit (0.0f, 1.0f, v);
    }
    float getModulationValue() const noexcept override { return value; }
    juce::String getSourceId() const override { return "midi.velocity"; }

private:
    float value { 1.0f };
};

} // namespace brocot
