#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace brocot
{

/**
    Runtime view of the offline pull-to-nearest table built by
    `phase3/build_pull_index.py`. The table groups factory exemplars by
    human-readable category (Sawtooth, Prime 7, Textured-Smooth · 8 ops,
    ...). For each exemplar we store its operator state (path / depth /
    enabled) plus its quantised, L1-normalised amplitude vector — the
    latter is the input the runtime needs for any
    "find-nearest-exemplar" lookup that lands later.

    For P3.10b we expose just the simplest shape-button surface: given
    a category, walk to its lowest-objective exemplar by linear
    interpolation. P3.10c will add the find-nearest variant once the
    audio engine can produce a current spectrum on demand.
*/
class PullIndex
{
public:
    /** One operator slot's saved state. Matches the JSON's
        `engine.operators[i]` shape. */
    struct OpState
    {
        juce::String path     {};
        float        depth    { 0.0f };
        bool         enabled  { false };
    };

    struct Exemplar
    {
        juce::String preset;
        double       objective { 0.0 };
        int          nOps      { 0 };
        std::vector<float>   amps;   // length = qMax
        std::vector<OpState> ops;    // length = kNumOperators
    };

    struct Category
    {
        juce::String name;
        int                presetCount { 0 };
        juce::String       bestObjPreset;
        std::vector<float> centroidAmps;
        std::vector<Exemplar> exemplars;
    };

    PullIndex() = default;

    /** Parse a `pull_index.json` file. Returns true on success.
        On failure `outError` carries a brief description if non-null. */
    bool load (const juce::File& file, juce::String* outError = nullptr);

    /** Try the dev-mode source dir first, then app-relative install
        fallbacks (same pattern as PresetManager). Returns the resolved
        File, or an empty File if none of the candidates exist. */
    static juce::File findIndexFile();

    bool isEmpty() const noexcept            { return categories.empty(); }
    int  getCategoryCount() const noexcept   { return (int) categories.size(); }
    int  getQMax() const noexcept            { return qMax; }
    double getFCarrierRef() const noexcept   { return fCarrierRef; }

    juce::StringArray getCategoryNames() const;
    const Category* getCategory (const juce::String& name) const;

    /** Return loaded category names whose UTF-8 byte sequence starts
        with `prefix`. Useful when the category names contain non-ASCII
        characters (e.g., the `·` in "Textured-Smooth · 8 ops"); a
        C++ source literal `"Textured-Smooth · "` is silently
        Latin-1-decoded by `juce::String(const char*)` and won't compare
        equal to the JSON-loaded UTF-8 key. A pure-ASCII prefix is safe
        because Latin-1 and UTF-8 agree byte-for-byte in 0x20..0x7e. */
    juce::StringArray getCategoryNamesStartingWith (juce::StringRef prefix) const;

    /** Convenience: the lowest-objective exemplar's ops for a category,
        or nullptr if the category is unknown / empty. */
    const std::vector<OpState>* getBestOps (const juce::String& category) const;

    /** Linear interpolation between two configurations.

        Per-slot semantics:
          - **Depth** lerps from the slot's *effective* current depth
            to its *effective* target depth, where a disabled slot is
            treated as depth=0. This means transitions through the
            enable boundary are *continuous* — a newly-enabling slot
            ramps up from 0 instead of popping on at half-target,
            and a fading-out slot decays to silence cleanly.
          - **Enabled** is true throughout any transition where either
            side was on. We only force enabled=false at α=1 exactly
            (so a target-off slot ends fully off). At α=0 exactly the
            current's enabled flag wins.
          - **Path** walks the Stern-Brocot tree: from current.path
            up to the LCA (longest common prefix of current.path and
            target.path), then down to target.path. Total walk has
            `(|cur|-|LCA|) + (|tgt|-|LCA|)` discrete steps; α is
            divided into that many segments. Each step is a one-char
            L/R add or parent-trim — a small ratio nudge the
            audio-thread smoothing absorbs.

        α is clamped to [0, 1]. Output length is min(current, target). */
    static std::vector<OpState> morph (const std::vector<OpState>& current,
                                        const std::vector<OpState>& target,
                                        float alpha);

    /** Walk the SB tree from `from` to `to` parametrised by α∈[0,1].
        α=0 returns `from`; α=1 returns `to`; intermediate α lands on
        one of the intermediate paths between them. Public so tests
        (and any future morph variant) can exercise it directly. */
    static juce::String pathWalk (const juce::String& from,
                                   const juce::String& to,
                                   float alpha);

private:
    int qMax { 32 };
    double fCarrierRef { 220.0 };
    std::vector<Category> categories;
};

} // namespace brocot
