#pragma once

#include "ModSource.h"

#include <juce_core/juce_core.h>
#include <vector>

namespace brocot
{

/**
    BROCOT-SPEC.md §6.

    One modulation target = (base value) + Σ (source × per-source depth).
    Output is clamped to [minValue, maxValue]. Sources may be marked bipolar
    (contribute ±depth) or unipolar (0..depth) at hook-up time so the same
    underlying source can drive different consumers appropriately.
*/
struct ModulationConnection
{
    ModulationSource* source { nullptr };
    float depth   { 0.0f };
    bool  bipolar { true };
};

class ModulationTarget
{
public:
    void  setBase (float v) noexcept { baseValue = v; }
    float getBase() const noexcept    { return baseValue; }

    void setRange (float min, float max) noexcept
    {
        minValue = min;
        maxValue = max;
    }

    void addConnection (ModulationSource* source, float depth, bool bipolar = true)
    {
        if (source == nullptr) return;
        connections.push_back ({ source, depth, bipolar });
    }

    void removeConnection (ModulationSource* source)
    {
        connections.erase (
            std::remove_if (connections.begin(), connections.end(),
                            [source] (const ModulationConnection& c)
                            { return c.source == source; }),
            connections.end());
    }

    void clearConnections() { connections.clear(); }

    /** Returns the effective parameter value: base + Σ contributions,
        clamped to [minValue, maxValue]. */
    float getEffectiveValue() const noexcept
    {
        float v = baseValue;
        for (const auto& c : connections)
        {
            const float src = c.source != nullptr ? c.source->getModulationValue() : 0.0f;
            if (c.bipolar)
                v += src * c.depth;
            else
                v += juce::jmax (0.0f, src) * c.depth;
        }
        return juce::jlimit (minValue, maxValue, v);
    }

private:
    float baseValue { 0.0f };
    float minValue  { 0.0f };
    float maxValue  { 1.0f };
    std::vector<ModulationConnection> connections;
};

} // namespace brocot
