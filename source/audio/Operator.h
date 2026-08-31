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

    void reset() noexcept { phase = 0.0f; fbPrev1 = 0.0f; fbPrev2 = 0.0f; }

    /** Operator oscillator waveform. A non-sine MODULATOR reshapes the FM
        sidebands — far more timbre per op than a sine (the "full FM program"
        move), at low op count. Order matches the `waveform` choice param. */
    enum class Wave { Sine = 0, Triangle, Saw, Square, Pulse };

    /** Unit-amplitude waveform sample for a phase given in TURNS (the argument
        to 2π). Sine is exact; the others wrap to [0,1). Naive shapes — fine for
        the modulator role (their aliasing barely reaches the output, unlike a
        non-sine carrier; the carrier in BrocotVoice stays a pure sine).
        `duty` only affects Pulse: high fraction of the cycle, so duty=0.5 is
        exactly Square and the n-th harmonic scales as |sin(πn·duty)| (the PW
        comb — e.g. duty 0.25 nulls every 4th harmonic).

        `evenSup` (Pulse only) is Fritz's double-pulse construction: the pulse
        minus evenSup times the same pulse half a period later, normalised by
        1 + evenSup so the odd harmonics hold level while the even ones scale
        by (1−β)/(1+β) — a continuous sweep from the full PW comb (0) to
        half-wave antisymmetry / odd-only (1). Measured orthogonal to the
        coincidence structure (COINCIDENCE-HORIZON.md, brocot_double_pulse
        sweep: 20 dB even-harmonic reach, ZERO ratios moved at any β): parity
        shapes amplitudes, never the ringing set — which is exactly why this
        knob is safe to add. evenSup = 0 reproduces the old pulse
        bit-exactly. */
    static inline float wave (Wave w, float turns, float duty = 0.25f,
                              float evenSup = 0.0f) noexcept
    {
        constexpr float twoPi = 6.2831853071795864769f;
        if (w == Wave::Sine) return std::sin (twoPi * turns);
        const float p = turns - std::floor (turns);          // [0,1)
        switch (w)
        {
            case Wave::Saw:    return 2.0f * p - 1.0f;
            case Wave::Square: return p < 0.5f ? 1.0f : -1.0f;
            case Wave::Pulse:
            {
                const float s1 = p < duty ? 1.0f : -1.0f;
                if (evenSup <= 0.0f) return s1;              // exact old path
                const float ps = p < 0.5f ? p + 0.5f : p - 0.5f;
                const float s2 = ps < duty ? 1.0f : -1.0f;
                return (s1 - evenSup * s2) / (1.0f + evenSup);
            }
            case Wave::Triangle:
            {
                float t = p + 0.25f; t -= std::floor (t);    // start at 0, rising
                return 1.0f - 4.0f * std::abs (t - 0.5f);
            }
            default: return std::sin (twoPi * turns);
        }
    }

    /** Set the operator waveform (0=Sine,1=Triangle,2=Saw,3=Square,4=Pulse).
        The v2 waveform choice param has 8 entries (engine shapes + reserved
        slots); indices beyond the engine's known shapes fall back to Sine —
        inert, rather than clamping onto the loudest shape. */
    void setWaveform (int idx) noexcept
    {
        waveform = (idx >= 0 && idx <= (int) Wave::Pulse) ? (Wave) idx
                                                          : Wave::Sine;
    }

    /** Pulse-wave duty cycle (high fraction of the cycle). Only audible when
        the waveform is Pulse; clamped to the param range 0.05..0.5 (0.5 =
        square; below ~0.05 the pulse thins toward an impulse train). */
    void setDuty (float d) noexcept
    {
        duty = juce::jlimit (0.05f, 0.5f, d);
    }

    /** Even-harmonic suppression (Fritz double-pulse β). Only audible when
        the waveform is Pulse; 0 = the plain pulse (bit-exact old path),
        1 = half-wave antisymmetric, odd harmonics only. */
    void setEvenSuppression (float beta) noexcept
    {
        evenSup = juce::jlimit (0.0f, 1.0f, beta);
    }

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
    /** Self-feedback amount in [0, 1]: the operator's sine is phase-modulated
        by its own (2-sample-averaged) previous output, the classic DX-style
        single-op feedback. 0 = clean sine; 1 = self-PM index of π radians
        (bright, sawtooth-like). Two-sample averaging is the standard trick to
        tame the runaway oscillation the bare 1-sample loop is prone to.
        Only op 1 (index 0) is driven by the host; see ParameterTree. */
    void setFeedback (float amount01) noexcept
    {
        constexpr float pi = 3.14159265358979324f;
        feedbackBeta = amount01 * pi;
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

        // Self-feedback: phase-modulate this oscillator by its own recent
        // output (2-sample averaged for stability). osc is the unit-amplitude
        // operator output; the carrier-frequency contribution is depth·fMod·osc
        // exactly as before, so feedback=0 reproduces the pure-sine path.
        float osc;
        if (feedbackBeta > 0.0f)
        {
            const float fb = 0.5f * (fbPrev1 + fbPrev2);
            // feedback term is in radians → convert to turns for wave().
            osc = wave (waveform, phase + feedbackBeta * fb * (1.0f / twoPi), duty, evenSup);
            fbPrev2 = fbPrev1;
            fbPrev1 = osc;
        }
        else
        {
            osc = wave (waveform, phase, duty, evenSup);
        }
        const float delta = currentDepth * fMod * osc;

        phase += fMod * invSampleRate;
        if (phase >= 1.0f) phase -= std::floor (phase);

        return delta;
    }

private:
    void updateDepthScale() noexcept
    {
        depthScale = std::exp2 (-keyScaleAmount * noteOctaves);  // exp2(0)==1
    }

    Wave  waveform    { Wave::Sine };  // operator oscillator shape
    float duty        { 0.25f }; // Pulse duty cycle (high fraction)
    float evenSup     { 0.0f  }; // double-pulse β (even suppression, Pulse only)
    float phase       { 0.0f };  // normalized [0,1)
    float targetDepth { 0.0f };  // the depth requested by the user / preset
    bool  enabled     { false };
    float baseRatio      { 1.0f };  // exact SB ratio, before detune
    float detuneMult     { 1.0f };  // 2^(detuneCents/1200)
    float keyScaleAmount { 0.0f };  // [-1, +1] depth-vs-pitch slope
    float noteOctaves    { 0.0f };  // octaves of active note above middle C
    float depthScale     { 1.0f };  // 2^(-keyScaleAmount · noteOctaves)
    float feedbackBeta   { 0.0f };  // self-PM index in radians (0..π)
    float fbPrev1        { 0.0f };  // last operator output (for feedback)
    float fbPrev2        { 0.0f };  // output before that (2-sample average)
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedDepth;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedRatio;
};

} // namespace brocot
