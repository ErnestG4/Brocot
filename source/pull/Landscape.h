#pragma once

#include "PullIndex.h"   // reuse OpState (path / depth / enabled)

#include <juce_core/juce_core.h>
#include <vector>

namespace brocot
{

/**
    Runtime view of the offline FM-soundspace graph built by
    `phase3/gpu_paths.py gradients` (resources/landscape_graph.json).

    The landscape is fixed; the synth tracks smoothly across it without any
    real-time spectral analysis:
      - nodes are dense FM configs (each reconstructable to operator state),
        labelled with the timbre family they fall in;
      - for each family we ship a *gradient field* — a next-hop pointer from
        every node toward that family (a shortest-path tree). Following the
        pointers from any node gives the shortest smooth route to the family
        with no runtime search.

    "Where we are" is found by **parameter distance** (a depth-weighted ratio
    histogram of the current patch vs each node) — no FFT, audio-thread-free.
    Waypoints along a route are walked with PullIndex::morph.
*/
class Landscape
{
public:
    struct Node
    {
        std::vector<PullIndex::OpState> ops;     // reconstructable synth state
        int                family    { -1 };     // index into familyNames
        std::vector<float> ratioHist;            // param-space locate signature
    };

    static constexpr int kHistBins = 40;         // log2-ratio bins over [-5, +5]

    Landscape() = default;

    /** Parse a landscape_graph.json file. Returns true on success. */
    bool load (const juce::File& file, juce::String* outError = nullptr);

    /** Dev-source dir then app-relative fallbacks (same pattern as PullIndex). */
    static juce::File findFile();

    bool isEmpty()       const noexcept { return nodes.empty(); }
    int  getNodeCount()  const noexcept { return (int) nodes.size(); }
    juce::StringArray getFamilyNames() const;
    bool hasFamily (const juce::String& name) const;

    /** Nearest node to a current patch, by depth-weighted ratio-histogram L1.
        Returns -1 if the landscape is empty. */
    int nearestNode (const std::vector<PullIndex::OpState>& current) const;

    /** Node-index route from `startNode` following `family`'s next-hop field
        to the family (inclusive of start and the arriving family node). Empty
        if the family is unknown or the node can't reach it. */
    std::vector<int> route (int startNode, const juce::String& family) const;

    /** The OpState waypoint sequence for `route()` — what SteerTab morphs
        through toward the family. */
    std::vector<std::vector<PullIndex::OpState>>
        waypoints (int startNode, const juce::String& family) const;

    const Node* node (int i) const noexcept
    {
        return (i >= 0 && i < (int) nodes.size()) ? &nodes[(size_t) i] : nullptr;
    }

    /** Depth-weighted log2-ratio histogram (length kHistBins, L1-normalised)
        — the param-space signature used for localisation. Public for tests. */
    static std::vector<float> ratioHistogram (const std::vector<PullIndex::OpState>& ops);

private:
    std::vector<Node>             nodes;
    juce::StringArray             familyNames;
    std::vector<std::vector<int>> nextHop;   // [familyIdx][node] → next node
                                             // (-1 = arrived, -2 = unreachable)
    int familyIndex (const juce::String& name) const;
};

} // namespace brocot
