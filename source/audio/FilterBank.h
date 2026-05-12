#pragma once

#include <cmath>

namespace brocot
{

/**
    Cytomic-style topology-preserving transform state-variable filter.
    Outputs LP / BP / HP / Notch simultaneously; the consumer picks one
    via setMode().

    BROCOT-SPEC.md §2: TPT-SVF, stable at high resonance, "matters because
    the LFO will be sweeping the cutoff". Four modes match the spec's UI.

    Reference: Andy Simper, "Linear Trapezoidal State Variable Filter
    SVF in state increment form".
*/
class FilterBank
{
public:
    enum class Mode { LP, BP, HP, Notch };

    void prepare (double sampleRateIn) noexcept
    {
        sampleRate = (float) sampleRateIn;
        ic1eq = ic2eq = 0.0f;
        updateCoeffs();
    }

    void reset() noexcept { ic1eq = ic2eq = 0.0f; }

    void setMode (Mode m) noexcept { mode = m; }

    void setCutoff (float hz) noexcept
    {
        // Clamp away from 0 (cot(0) → ∞) and from Nyquist (tan(π/2) → ∞).
        const float maxHz = sampleRate * 0.49f;
        cutoffHz = hz < 20.0f ? 20.0f : (hz > maxHz ? maxHz : hz);
        updateCoeffs();
    }

    void setResonance (float q) noexcept    // 0..1 perceptual; internally Q ≥ 0.5
    {
        const float clamped = q < 0.0f ? 0.0f : (q > 1.0f ? 1.0f : q);
        // Map 0..1 to a useful Q range. q=0 → Q=0.5 (Butterworth-ish);
        // q=1 → Q=20 (resonant peak).
        const float Q = 0.5f + 19.5f * clamped;
        k = 1.0f / Q;
        updateCoeffs();
    }

    inline float processSample (float x) noexcept
    {
        const float v3 = x - ic2eq;
        const float v1 = a1 * ic1eq + a2 * v3;
        const float v2 = ic2eq + a2 * ic1eq + a3 * v3;
        ic1eq = 2.0f * v1 - ic1eq;
        ic2eq = 2.0f * v2 - ic2eq;

        switch (mode)
        {
            case Mode::LP:    return v2;
            case Mode::BP:    return v1;
            case Mode::HP:    return x - k * v1 - v2;
            case Mode::Notch: return x - k * v1;
        }
        return x;
    }

private:
    void updateCoeffs() noexcept
    {
        const float g = std::tan (3.14159265358979323846f * cutoffHz / sampleRate);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    Mode  mode { Mode::LP };
    float sampleRate { 44100.0f };
    float cutoffHz   { 8000.0f };
    float k          { 1.0f / 0.7071f };  // ~Q = 0.707
    float a1 { 0.0f }, a2 { 0.0f }, a3 { 0.0f };
    float ic1eq { 0.0f }, ic2eq { 0.0f };
};

} // namespace brocot
