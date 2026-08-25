#pragma once

#include <algorithm>
#include <vector>

#include "audio/Coherence.h"
#include "audio/Path.h"

namespace brocot::coherence
{

/**
    Coherence "suggest mode" (BROCOT-SPEC.md §P0-a #1): given the current
    modulators and a chosen base Stern-Brocot path, recommend the
    prefix-EXTENSIONS of that base for a new secondary modulator that most
    raise the whole-config path-prefix coherence.

    CORRECTION, 2026-08-25. This comment previously justified the candidate set
    with "shared prefix ⇒ coincident sideband combs". That implication is
    FALSE, and measurably so. Sharing a prefix says nothing about whether a
    coincidence is REACHABLE: for two operators at ratios r1, r2 with indices
    I1, I2, writing alpha = r2/r1 = p/q in lowest terms, a direct coincidence
    exists iff

        p <= 2 * order_bound(I1)   AND   q <= 2 * order_bound(I2)

    (the asymmetric corollary in COINCIDENCE-HORIZON.md, verified 4784/4784
    against exact enumeration). Extending a path drives p and q up quickly while
    the horizon stays where the player's index left it, so an extension can
    share five characters of path and still be silent.

    HOW WRONG, measured rather than asserted (brocot_suggest_census.json): of
    the candidates this function enumerates at I = 0.9, **97.5%** share a prefix
    and cannot coincide — and restricted to the near-unity ratios BROCOT-SPEC
    defines the instrument by, **99.7–100%**. The criterion is essentially
    always wrong exactly where the instrument lives.

    WHAT IS AND IS NOT ESTABLISHED. That census measured this implication only.
    It did not call Coherence.h's score and did not evaluate the topN a caller
    actually sees, so whether the RANKING inherits the defect is open
    (criticality_tool queue.py: suggest-score-census). Do not read the numbers
    above as a measurement of the recommendations; they are a measurement of the
    justification this comment used to give for them.

    KNOWN GAP: suggestExtensions takes `newIndex` for the candidate and never
    consults the indices of `current`, while the horizon depends on both. A
    reachability filter would need them.

    Pure + header-only so the recommendation is unit-testable independently of
    any navigator UI. STRUCTURAL only (§4.1): a recommendation about spectral
    coincidence, not a literal band/gap claim.
*/

struct Suggestion
{
    juce::String path;         // the recommended full extension path
    float        coherence;    // whole-config coherence if this op is added
    float        gain;         // coherence − coherenceWithoutIt
};

/** Enumerate prefix-extensions of `base` (base + every L/R string of length
    1…maxExtra), score each as an added modulator at `newIndex`, and return the
    top `topN` by resulting whole-config coherence. Candidates whose ratio
    duplicates an existing modulator are skipped (an exact duplicate is
    trivially "coherent" but musically redundant). */
inline std::vector<Suggestion> suggestExtensions (const std::vector<OpSpec>& current,
                                                  const Path& base,
                                                  double newIndex,
                                                  int maxExtra = 3,
                                                  int topN = 4)
{
    const float baseline = score (current).coherence;

    // All L/R extension strings of length 1..maxExtra, breadth-first.
    std::vector<juce::String> tails { "" };
    std::vector<juce::String> candidates;
    for (int depth = 1; depth <= maxExtra; ++depth)
    {
        std::vector<juce::String> next;
        for (const auto& t : tails)
            for (auto step : { 'L', 'R' })
            {
                const juce::String s = t + juce::String::charToString ((juce::juce_wchar) step);
                next.push_back (s);
                candidates.push_back (base.toString() + s);
            }
        tails = std::move (next);
    }

    std::vector<Suggestion> out;
    for (const auto& candStr : candidates)
    {
        const Path cand (candStr);
        const double r = cand.ratio();

        bool duplicate = false;
        for (const auto& o : current)
            if (std::abs (o.ratio - r) < 1.0e-9)
            {
                duplicate = true;
                break;
            }
        if (duplicate) continue;

        auto trial = current;
        trial.push_back ({ r, newIndex });
        const float coh = score (trial).coherence;
        out.push_back ({ candStr, coh, coh - baseline });
    }

    std::sort (out.begin(), out.end(),
               [] (const Suggestion& a, const Suggestion& b) { return a.coherence > b.coherence; });
    if ((int) out.size() > topN)
        out.resize ((size_t) topN);
    return out;
}

} // namespace brocot::coherence
