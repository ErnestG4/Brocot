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
        // Ratio smoothing — 20ms. Long enough that a single SB-tree
        // step jump (e.g., ratio 3/1 → 2/1 mid-morph) glides cleanly
        // instead of clicking; short enough that LFO-driven sweeps and
        // live path edits still feel responsive. (P3.13(b) bump from
        // 5ms — the original was tuned for solitary path edits, not
        // the rapid sequence of steps a slider-driven morph produces.)
        smoothedRatio.reset (sampleRate, 0.020);
        smoothedRatio.setCurrentAndTargetValue (1.0f);
    }

    void reset() noexcept { phase = 0.0f; }

    void setPath (const Path& p) { setRatio ((float) p.ratio()); }
    /** Set the *base* (Stern-Brocot, exact-rational) ratio. The effective
        ratio glided to by the smoother is base × detune (see setDetuneCents). */
    void setRatio (float r) noexcept
    {
        baseRatio = r;
        smoothedRatio.setTargetValue (r * detuneMult);
    }
    /** Snap the ratio (skip the smoothing ramp). Used at preset load / init. */
    void snapRatio (float r) noexcept
    {
        baseRatio = r;
        smoothedRatio.setCurrentAndTargetValue (r * detuneMult);
    }

    /** Per-op fine detune, in cents, applied multiplicatively on top of the
        exact Stern-Brocot ratio: effective ratio = base × 2^(cents/1200).
        The SB path picks *which* rational; detune sets *how far off* it the
        oscillator runs — the cents between rationals where analog beating /
        chorus / drift lives. 0 cents = the exact rational. */
    void setDetuneCents (float cents) noexcept
    {
        detuneMult = std::exp2 (cents / 1200.0f);   // exp2(0) == 1 exactly
        smoothedRatio.setTargetValue (baseRatio * detuneMult);
    }

    /** Key-scaling amount in [-1, +1]: how the modulation index tracks pitch.
        Effective depth is scaled by 2^(-keyScale · octavesFromMiddleC), so
        keyScale=+1 halves the index one octave above middle C (fewer
        sidebands up high → timbre stays coherent across the keyboard, the
        standard FM key-rate-scaling behaviour); keyScale=-1 doubles it. */
    void setKeyScale (float amount) noexcept
    {
        keyScaleAmount = amount;
        updateDepthScale();
    }
    /** Set by the voice at note-on: octaves of the played note above MIDI 60
        (middle C). Combines with keyScale to scale the effective depth. */
    void setNoteOctaves (float octavesFromMiddleC) noexcept
    {
        noteOctaves = octavesFromMiddleC;
        updateDepthScale();
    }
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
    float getRatio()  const noexcept { return smoothedRatio.getTargetValue(); }
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
            std::max (0.0f, smoothedDepth.getNextValue() * depthScale + depthOffset);
        const float currentRatio = smoothedRatio.getNextValue();
        if (currentDepth <= 1.0e-5f)
            return 0.0f;

        const float fMod = carrierHz * currentRatio;
        const float twoPi = 6.2831853071795864769f;
        const float delta = currentDepth * fMod * std::sin (twoPi * phase);

        phase += fMod * invSampleRate;
        if (phase >= 1.0f) phase -= std::floor (phase);

        return delta;
    }

private:
    void updateDepthScale() noexcept
    {
        depthScale = std::exp2 (-keyScaleAmount * noteOctaves);  // exp2(0)==1
    }

    float phase       { 0.0f };  // normalized [0,1)
    float targetDepth { 0.0f };  // the depth requested by the user / preset
    bool  enabled     { false };
    float baseRatio      { 1.0f };  // exact SB ratio, before detune
    float detuneMult     { 1.0f };  // 2^(detuneCents/1200)
    float keyScaleAmount { 0.0f };  // [-1, +1] depth-vs-pitch slope
    float noteOctaves    { 0.0f };  // octaves of active note above middle C
    float depthScale     { 1.0f };  // 2^(-keyScaleAmount · noteOctaves)
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedDepth;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedRatio;
};

} // namespace brocot
