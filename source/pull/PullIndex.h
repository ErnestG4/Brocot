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

    /** Convenience: the lowest-objective exemplar's ops for a category,
        or nullptr if the category is unknown / empty. */
    const std::vector<OpState>* getBestOps (const juce::String& category) const;

    /** Linear interpolation between two configurations.

        For each slot:
          - `depth` is `lerp(current.depth, target.depth, α)`.
          - `enabled` flips to target's value at α ≥ 0.5.
          - `path` flips to target's value at α ≥ 0.5.
        α is clamped to [0, 1]. Both inputs must be the same length;
        the output is that length too.

        The hop at α=0.5 is intentionally simple — audio-thread param
        smoothing absorbs the depth jump, and the path change happens
        once. Smoother path-morphing (intermediate SB mediants) is a
        future P3.10x improvement. */
    static std::vector<OpState> morph (const std::vector<OpState>& current,
                                        const std::vector<OpState>& target,
                                        float alpha);

private:
    int qMax { 32 };
    double fCarrierRef { 220.0 };
    std::vector<Category> categories;
};

} // namespace brocot
