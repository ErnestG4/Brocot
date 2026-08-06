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
    /** One modulator assignment shipped on a mod-map node (phase3/modmap.md):
        an LFO of `shape` at `rate` Hz driving either op-`op`'s depth
        (kind 0) or op-0 feedback (kind 1) with modulation depth `depth`.
        Exactly one entry per node in the first mod-map pass; parsed
        tolerantly — entries with an unknown kind/shape string are dropped. */
    struct ModSpec
    {
        int   kind  { 0 };      // 0 = op depth, 1 = op-0 feedback
        int   op    { 0 };      // target operator index (kind 0)
        int   shape { 0 };      // 0 = sine, 1 = tri
        float rate  { 0.0f };   // LFO rate, Hz
        float depth { 0.0f };   // modulation depth, 0..1
    };

    struct Node
    {
        std::vector<PullIndex::OpState> ops;     // reconstructable synth state
        float              feedback  { 0.0f };   // op-0 self-feedback, 0..1 param
        int                family    { -1 };     // index into familyNames
        float              x { 0.0f }, y { 0.0f }; // 2D cloud position, [-1,1]
        float              bright { 0.0f };      // spectral centroid, 0..1
        float              dense  { 0.0f };      // ~partial count, 0..1
        float              odd    { 0.0f };      // odd-harmonic fraction, 0..1
        float              tonal  { 0.0f };      // musical(1) ↔ chaotic(0)
        float              crit   { 0.0f };      // criticality: lines/junctions/rims
        // Mod-map extras (absent on ordinary maps): motion = mean
        // frame-to-frame L1 of the trajectory fingerprint (p98-normalised),
        // rateN = log-mapped LFO rate. −1 = the field wasn't shipped.
        float              motion { -1.0f };
        float              rateN  { -1.0f };
        std::vector<ModSpec> mods;               // modulator assignments
        std::vector<float> ratioHist;            // param-space locate signature
    };

    /** A waypoint = a node's operator state plus its op-0 feedback amount.
        SteerTab morphs the ops (PullIndex::morph) and lerps the feedback. */
    struct Waypoint
    {
        std::vector<PullIndex::OpState> ops;
        float feedback { 0.0f };
    };

    /** A verified smooth cross-basin jump ("wormhole") mined offline by
        phase3/gpu_jumps.py — see phase3/jumps.md for the contract. All
        numbers are RENDER-VERIFIED, never model predictions. */
    struct Jump
    {
        int   a { 0 }, b { 0 };      // node indices, a < b (undirected)
        float rough { 0.0f };        // rendered R_max (worst morph bump, fp L1)
        float tv { 0.0f };           // rendered total variation of the morph
        float graphDist { 0.0f };    // k-NN graph distance at mine time
        float worstAlpha { 0.0f };   // where along the morph the bump sits
    };

    static constexpr int kHistBins = 40;         // log2-ratio bins over [-5, +5]

    Landscape() = default;

    /** Parse a landscape_graph.json file. Returns true on success. */
    bool load (const juce::File& file, juce::String* outError = nullptr);

    /** Dev-source dir then app-relative fallbacks (same pattern as PullIndex). */
    static juce::File findFile();

    /** All per-depth map files (landscape_graph*.json.gz), sorted by depth. */
    static juce::Array<juce::File> findAllFiles();

    /** Cheap header-only read: pull the map depth (n_ops, default 6) and the
        normalised selector name out of a graph file WITHOUT parsing its (huge)
        node array. Lets the selector show every map's label while the full
        node load is deferred until the map is actually navigated. Decompresses
        only the file's preamble. Returns false if the file can't be opened. */
    static bool peekHeader (const juce::File& file, int& nOpsOut,
                            juce::String& nameOut);

    /** Normalise an embedded "<N>op <desc>" graph name to the uniform
        "<N>-op[ <desc>]" selector label. Shared by load() and peekHeader(). */
    static juce::String makeMapName (int nOps, const juce::String& rawName);

    bool isEmpty()       const noexcept { return nodes.empty(); }
    int  getNodeCount()  const noexcept { return (int) nodes.size(); }
    /** This map's operator depth: it edits ops 0..nOps-1 (nested prefix). */
    int  getNOps()       const noexcept { return nOps; }
    /** Display label for the map selector (e.g. "6-op saw", "4-op mix"). */
    juce::String getName() const { return mapName; }
    /** True when the doc carries "mod_map": 1 — nodes ship modulator
        assignments and the mod-specific runtime UI is enabled. */
    bool isModulationMap() const noexcept { return isModMap; }
    /** True when any node shipped a motion descriptor (≥ 0) — gates the
        motion/rate colour modes. */
    bool hasMotion() const noexcept { return anyMotion; }
    juce::StringArray getFamilyNames() const;
    int  getFamilyCount() const noexcept { return familyNames.size(); }
    bool hasFamily (const juce::String& name) const;

    /** All nodes (for the cloud visualiser — read their x/y + family). */
    const std::vector<Node>& getNodes() const noexcept { return nodes; }

    /** Nearest node to a current patch, by depth-weighted ratio-histogram L1.
        Returns -1 if the landscape is empty or the patch's map slice has no
        active ops (an all-zero signature locates nowhere). */
    int nearestNode (const std::vector<PullIndex::OpState>& current) const;

    /** Node-index route from `startNode` following `family`'s next-hop field
        to the family (inclusive of start and the arriving family node). Empty
        if the family is unknown or the walk never arrives (start unreachable,
        corrupt field, or a cycle) — never a partial route to the wrong place. */
    std::vector<int> route (int startNode, const juce::String& family) const;

    /** The OpState waypoint sequence for `route()` — what SteerTab morphs
        through toward the family. */
    std::vector<Waypoint>
        waypoints (int startNode, const juce::String& family) const;

    const Node* node (int i) const noexcept
    {
        return (i >= 0 && i < (int) nodes.size()) ? &nodes[(size_t) i] : nullptr;
    }

    /** Verified wormholes, sorted smoothest-first as shipped. Empty for maps
        mined before the jumps pass (the "jumps" key is optional). */
    const std::vector<Jump>& getJumps() const noexcept { return jumps; }

    /** τ the miner used (25th pct of k-NN-edge roughness), 0 if no jumps. */
    float getJumpTau() const noexcept { return jumpTau; }

    /** Indices into getJumps() of the jumps incident to `nodeIdx`. Linear
        scan — jumps are capped at 2048, callers poll at UI rates. */
    std::vector<int> jumpsFrom (int nodeIdx) const;

    /** Highest-criticality node within `radius` of (cx, cy), scored
        crit − distPenalty·(dist/radius) so a marginally-less-critical nearer
        node beats a rim across the map. Returns -1 if none in radius. The
        criticality-seek navigation aid; pure so tests can reach it. */
    int bestCritNode (float cx, float cy, float radius,
                      float distPenalty = 0.5f) const;

    /** Depth-weighted log2-ratio histogram (length kHistBins, L1-normalised)
        — the param-space signature used for localisation. Public for tests. */
    static std::vector<float> ratioHistogram (const std::vector<PullIndex::OpState>& ops);

private:
    std::vector<Node>             nodes;
    std::vector<Jump>             jumps;
    float                         jumpTau { 0.0f };
    int                           nOps { 6 };   // map depth (nested prefix)
    bool                          isModMap { false };
    bool                          anyMotion { false };
    juce::String                  mapName;      // selector label
    juce::StringArray             familyNames;
    std::vector<std::vector<int>> nextHop;   // [familyIdx][node] → next node
                                             // (-1 = arrived, -2 = unreachable)
    int familyIndex (const juce::String& name) const;
};

} // namespace brocot
