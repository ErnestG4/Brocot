#pragma once

#include "Path.h"

#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>

namespace brocot
{

/**
    One modulator slot — a sine oscillator whose frequency tracks
    (carrier × Path-derived ratio), summed into the carrier's
    instantaneous frequency at modulation index I (the depth value).

    BROCOT-SPEC.md §2 fixes two invariants:
      - The user-facing depth is modulation index I, not gain in Hz;
        the actual frequency deviation is I × f_modulator.
      - Modulators are parallel (sum into the carrier), never nested.
*/
class Operator
{
public:
    void prepare (double sampleRate)
    {
        // ~30ms smoothing — fast enough that param tweaks feel responsive,
        // slow enough to kill the zipper noise on per-block depth updates.
        smoothedDepth.reset (sampleRate, 0.03);
        smoothedDepth.setCurrentAndTargetValue (0.0f);
    }

    void reset() noexcept { phase = 0.0f; }

    void setPath (const Path& p) { ratio = (float) p.ratio(); }
    void setRatio (float r) noexcept { ratio = r; }
    void setDepth (float depthI) noexcept
    {
        targetDepth = depthI;
        smoothedDepth.setTargetValue (enabled ? targetDepth : 0.0f);
    }
    void setEnabled (bool e) noexcept
    {
        enabled = e;
        // Fade depth in / out over the smoothing window so enable/disable
        // toggles don't pop. The phase keeps running so a re-enable picks
        // up where it left off — no phase reset jolt either.
        smoothedDepth.setTargetValue (enabled ? targetDepth : 0.0f);
    }

    bool  isEnabled() const noexcept { return enabled; }
    float getRatio()  const noexcept { return ratio; }
    float getDepth()  const noexcept { return targetDepth; }

    /** Returns this operator's contribution to the carrier's instantaneous
        frequency in Hz, and advances internal phase. Caller passes the
        carrier's base frequency in Hz and the inverse sample rate.
        `depthOffset` is an additive contribution from the V2 LFO bank's
        per-sample routing — negative values that drive effective depth
        below zero are clamped at zero (the FM math expects non-negative I). */
    inline float processSample (float carrierHz, float invSampleRate,
                                float depthOffset = 0.0f) noexcept
    {
        const float currentDepth =
            std::max (0.0f, smoothedDepth.getNextValue() + depthOffset);
        if (currentDepth <= 1.0e-5f)
            return 0.0f;

        const float fMod = carrierHz * ratio;
        const float twoPi = 6.2831853071795864769f;
        const float delta = currentDepth * fMod * std::sin (twoPi * phase);

        phase += fMod * invSampleRate;
        if (phase >= 1.0f) phase -= std::floor (phase);

        return delta;
    }

private:
    float ratio       { 1.0f };  // f_mod / f_carrier
    float phase       { 0.0f };  // normalized [0,1)
    float targetDepth { 0.0f };  // the depth requested by the user / preset
    bool  enabled     { false };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedDepth;
};

} // namespace brocot
