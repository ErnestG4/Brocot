#pragma once

#include <cmath>

namespace brocot
{

/**
    Single LFO with selectable shape, rate, depth, and optional per-note sync.
    BROCOT-SPEC.md §2: "one LFO routed to one target is the minimum useful
    modulation setup. Filter cutoff is the most common target." Multi-target
    routing is v2.
*/
class LFO
{
public:
    enum class Shape { Sine, Triangle, Square, Saw };

    void prepare (double sampleRateIn) noexcept
    {
        invSampleRate = (float) (1.0 / sampleRateIn);
        phase = 0.0f;
    }

    void setShape (Shape s) noexcept { shape = s; }
    void setRate  (float hz)    noexcept { rateHz = hz < 0.0f ? 0.0f : hz; }
    void setDepth (float d)     noexcept { depth  = d < 0.0f ? 0.0f : (d > 1.0f ? 1.0f : d); }
    void setKeySync (bool on)   noexcept { keySync = on; }
    bool isKeySync() const noexcept { return keySync; }

    void onNoteOn() noexcept
    {
        if (keySync) phase = 0.0f;
    }

    /** Returns the current bipolar value in [-1, +1] (before depth scaling)
        and advances the phase by one sample. */
    inline float tick() noexcept
    {
        const float p = phase;
        phase += rateHz * invSampleRate;
        if (phase >= 1.0f) phase -= std::floor (phase);

        switch (shape)
        {
            case Shape::Sine:     return std::sin (6.2831853071795864769f * p);
            case Shape::Triangle: return 4.0f * std::fabs (p - 0.5f) - 1.0f;
            case Shape::Square:   return p < 0.5f ? 1.0f : -1.0f;
            case Shape::Saw:      return 2.0f * p - 1.0f;
        }
        return 0.0f;
    }

    /** Output scaled by depth (still bipolar). */
    inline float tickScaled() noexcept { return tick() * depth; }

private:
    Shape shape { Shape::Sine };
    float rateHz { 1.0f };
    float depth  { 0.0f };
    float phase  { 0.0f };
    float invSampleRate { 1.0f / 44100.0f };
    bool  keySync { false };
};

} // namespace brocot
