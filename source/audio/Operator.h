#pragma once

#include "Path.h"

#include <cmath>

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
    void reset() noexcept { phase = 0.0f; }

    void setPath (const Path& p) { ratio = (float) p.ratio(); }
    void setRatio (float r) noexcept { ratio = r; }
    void setDepth (float depthI) noexcept { depth = depthI; }
    void setEnabled (bool e) noexcept { enabled = e; }

    bool  isEnabled() const noexcept { return enabled; }
    float getRatio()  const noexcept { return ratio; }
    float getDepth()  const noexcept { return depth; }

    /** Returns this operator's contribution to the carrier's instantaneous
        frequency in Hz, and advances internal phase. Caller passes the
        carrier's base frequency in Hz and the inverse sample rate. */
    inline float processSample (float carrierHz, float invSampleRate) noexcept
    {
        if (! enabled || depth <= 0.0f)
            return 0.0f;

        const float fMod = carrierHz * ratio;
        const float twoPi = 6.2831853071795864769f;
        const float delta = depth * fMod * std::sin (twoPi * phase);

        phase += fMod * invSampleRate;
        if (phase >= 1.0f) phase -= std::floor (phase);

        return delta;
    }

private:
    float ratio   { 1.0f };  // f_mod / f_carrier
    float depth   { 0.0f };  // modulation index I
    float phase   { 0.0f };  // normalized [0,1)
    bool  enabled { false };
};

} // namespace brocot
