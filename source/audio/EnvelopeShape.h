#pragma once

#include <cmath>

namespace brocot::envelope
{

/**
    Envelope and modulator shape registry. Ported from the unified-prototype
    HTML / CFC v5: each shape has a bipolar variant (–1..+1, for FM/PM/RM
    signal use) and a unipolar variant (0..1, for amplitude shaping / CFC
    envelopes). The κ (kappa) parameter controls sharpness for the shapes
    that use it (vonmises, gaussian, square→duty, expdecay).

    Phase convention: [0, 2π). Callers should normalise before passing in.
*/

inline constexpr float kTwoPi = 6.2831853071795864769f;

enum class Shape : int
{
    Sine = 0,
    VonMises,
    Gaussian,
    Square,
    SawRise,
    SawFall,
    Triangle,
    ExpDecay,
    Count
};

inline float wrapPhase (float p) noexcept
{
    p = std::fmod (p, kTwoPi);
    if (p < 0.0f) p += kTwoPi;
    return p;
}

inline float unipolar (float phase, Shape s, float kappa) noexcept
{
    const float p = wrapPhase (phase);

    switch (s)
    {
        case Shape::Sine:
            return 0.5f + 0.5f * std::sin (p - 1.5707963267948966f);
        case Shape::VonMises:
            return std::exp (kappa * (std::cos (p) - 1.0f));
        case Shape::Gaussian:
        {
            const float d = std::min (p, kTwoPi - p);
            const float sigma = 3.14159265358979323846f / (1.0f + kappa * 0.5f);
            return std::exp (-d * d / (2.0f * sigma * sigma));
        }
        case Shape::Square:
        {
            // duty maps κ ∈ [0,50] → [~0.02, ~0.98]
            const float duty = std::max (0.02f, std::min (0.98f, 0.5f + (kappa - 25.0f) / 52.0f));
            return p < duty * kTwoPi ? 1.0f : 0.0f;
        }
        case Shape::SawRise:  return p / kTwoPi;
        case Shape::SawFall:  return 1.0f - p / kTwoPi;
        case Shape::Triangle: return p < 3.14159265358979323846f
                                       ? p / 3.14159265358979323846f
                                       : 2.0f - p / 3.14159265358979323846f;
        case Shape::ExpDecay: return std::exp (-(1.0f + kappa * 0.4f) * p / kTwoPi);
        case Shape::Count:    break;
    }
    return 0.0f;
}

inline float bipolar (float phase, Shape s, float kappa) noexcept
{
    const float p = wrapPhase (phase);

    switch (s)
    {
        case Shape::Sine:     return std::sin (p);
        case Shape::VonMises: return std::exp (kappa * (std::cos (p) - 1.0f)) * 2.0f - 1.0f;
        case Shape::Gaussian:
        {
            const float d = std::min (p, kTwoPi - p);
            const float sigma = 3.14159265358979323846f / (1.0f + kappa * 0.5f);
            return std::exp (-d * d / (2.0f * sigma * sigma)) * 2.0f - 1.0f;
        }
        case Shape::Square:   return p < 3.14159265358979323846f ? 1.0f : -1.0f;
        case Shape::SawRise:  return 2.0f * (p / kTwoPi) - 1.0f;
        case Shape::SawFall:  return 1.0f - 2.0f * (p / kTwoPi);
        case Shape::Triangle: return p < 3.14159265358979323846f
                                       ? (2.0f * p / 3.14159265358979323846f - 1.0f)
                                       : (3.0f - 2.0f * p / 3.14159265358979323846f);
        case Shape::ExpDecay: return std::exp (-(1.0f + kappa * 0.4f) * p / kTwoPi) * 2.0f - 1.0f;
        case Shape::Count:    break;
    }
    return 0.0f;
}

/** PSK phase quantization: snap to N evenly spaced phase states; blend
    between exact and quantized phase by `smooth` ∈ [0, 1].
    `n < 2` is a no-op (returns the input phase unchanged). */
inline float pskQuantize (float phase, int n, float smooth) noexcept
{
    if (n < 2) return phase;
    const float step = kTwoPi / (float) n;
    const float quant = std::round (phase / step) * step;
    return phase * smooth + quant * (1.0f - smooth);
}

} // namespace brocot::envelope
