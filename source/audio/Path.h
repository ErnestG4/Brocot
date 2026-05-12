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
