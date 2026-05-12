#pragma once

#include "Operator.h"

#include <array>

namespace brocot
{

/**
    One FM voice: a sine carrier plus up to N_OPS parallel modulators.

    Per BROCOT-SPEC.md §2/§4 — modulators are parallel and sum into the
    carrier's instantaneous frequency. Each modulator's contribution is
    I_k × f_mk × sin(2π · φ_k); the carrier's effective frequency is
    f_c + Σ Δf_k, and the output is sin(2π · φ_c).

    Slice 3 uses N=1 op; Slice 4 lights up all 8.
*/
class BrocotVoice
{
public:
    static constexpr int N_OPS = 8;

    void prepare (double sampleRate);
    void setCarrierHz (float hz) noexcept { carrierHz = hz; }
    void setLevel (float linearGain) noexcept { level = linearGain; }

    Operator& op (int idx) noexcept { return ops[(size_t) idx]; }

    void resetPhases() noexcept;

    /** Renders `numSamples` of mono audio into `out`, summing onto whatever
        is already there. Uses the per-sample FM loop from spec §4. */
    void render (float* out, int numSamples) noexcept;

private:
    std::array<Operator, N_OPS> ops {};
    float carrierHz     { 220.0f };
    float carrierPhase  { 0.0f };
    float invSampleRate { 1.0f / 44100.0f };
    float level         { 0.2f };
};

} // namespace brocot
