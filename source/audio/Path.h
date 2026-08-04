#pragma once

#include <juce_core/juce_core.h>

namespace brocot
{

/**
    Stern-Brocot tree path.

    The path is a sequence of 'L' / 'R' steps from the root (1/1). The
    resolved rational p/q is the canonical representation of a modulator's
    frequency ratio — BROCOT-SPEC.md §2 calls this "the source of truth";
    p, q, and the decimal ratio are all derived.

    Paths constructed via the mediant algorithm are always in lowest terms
    by construction (no GCD reduction needed). Value semantics: every
    mutator returns a new Path; the original is unchanged.
*/
class Path
{
public:
    Path() = default;
    explicit Path (const juce::String& stepsIn);

    // Stern-Brocot descent from 1/1 to p/q, reduced to lowest terms first.
    // Returns false — leaving `out` untouched — for p or q ≤ 0, or when the
    // descent exceeds maxDepth; success with an empty `out` is therefore
    // only ever the genuine root 1/1.
    static bool fromRatio (int p, int q, Path& out, int maxDepth = 32);

    // Convenience overload: failure collapses to the empty path, which is
    // indistinguishable from root 1/1 — use the bool form when that matters.
    static Path fromRatio (int p, int q, int maxDepth = 32);

    Path extended (juce::juce_wchar step) const;
    Path parent() const;

    void walk (int& p, int& q) const;
    void walkWithChildren (int& p,  int& q,
                           int& lp, int& lq,
                           int& rp, int& rq) const;
    double ratio() const;

    int  depth()   const noexcept { return steps.length(); }
    bool isEmpty() const noexcept { return steps.isEmpty(); }
    const juce::String& toString() const noexcept { return steps; }

    Path prefix (int length) const;
    bool startsWith (const Path& other) const;
    Path commonPrefix (const Path& other) const;

    bool operator== (const Path& other) const noexcept { return steps == other.steps; }
    bool operator!= (const Path& other) const noexcept { return steps != other.steps; }

private:
    juce::String steps;
};

} // namespace brocot
