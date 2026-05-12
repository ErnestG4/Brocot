#pragma once

#include <juce_core/juce_core.h>

namespace brocot
{

/**
    BROCOT-SPEC.md §6.

    A ModulationSource exposes a single current value, normalized to a
    documented range. Channel-wide sources (mod wheel, pitch bend) typically
    map their hardware range onto [-1, +1] or [0, 1]. Per-voice sources
    (velocity, envelope) hold per-voice state.

    The interface stays deliberately tiny — one virtual function — so v2 can
    add new source kinds (FareyPLLBank, NNSClassifier, SidechainEnvelope,
    SpectralCentroid) without churning v1 callers.
*/
class ModulationSource
{
public:
    virtual ~ModulationSource() = default;

    /** Current source value. Conventions:
        - Bipolar sources (pitch bend, LFO, audio envelope follower): [-1, +1].
        - Unipolar sources (mod wheel, velocity, amp envelope, pressure): [0, +1].
        The ModulationTarget consumes whichever is appropriate; sources do
        not need to know their consumers' bipolarity preference. */
    virtual float getModulationValue() const noexcept = 0;

    /** Stable identifier used by preset serialization in v1.x. */
    virtual juce::String getSourceId() const = 0;
};

} // namespace brocot
