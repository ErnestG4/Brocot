#pragma once

#include <algorithm>
#include <vector>

#include "audio/Coherence.h"
#include "audio/Horizon.h"
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

    THE RANKING DOES INHERIT IT — settled 2026-08-25, and the answer went the
    way this comment did not expect (brocot_suggest_score_census.json).
    Coherence.h bins partials at 12 cents, which looked like it might make the
    score a NEAR-coincidence detector the horizon has no jurisdiction over. It
    does not: **64.9%** of the energy the score counts as "shared" comes from
    partials that are EXACTLY equal. So the horizon governs this ranking.

    Consequences, measured over 300 sampled patches:
      - **57.7%** of top-1 suggestions cannot coincide with ANY operator
        already in the patch.
      - A horizon-reachability filter changes the shipped top-4 substantially:
        mean Jaccard **0.329**.

    So a reachability filter is warranted, not merely tidy. It needs the indices
    of `current`, which this function does not currently receive.

    THE FILTER IS NOW WRITTEN (2026-08-31). OpSpec carries each current op's
    index and exact rational, and every candidate must be horizon-reachable
    (Horizon.h ringable, exact arithmetic) from AT LEAST ONE current operator
    at that operator's own index — the index-blindness the census flagged is
    exactly the mistake the per-op I1 argument closes. Scope of the filter,
    stated: reachability against SOME current op is the census's own criterion
    ("cannot coincide with ANY operator already in the patch"); it is a
    necessary condition for the coherence gain to be measuring coincidence
    rather than 12-cent binning noise, not a sufficient one. Legacy callers
    that leave OpSpec::p/q at 0 get the unfiltered enumeration — the filter
    refuses to guess a rational from a double.

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

/** True when a modulator at exact ratio candP/candQ and index `candIndex` can
    reach a direct sideband coincidence with at least one operator in
    `current`, each at its own stored index. Ops without exact rationals
    (p/q = 0) cannot vote either way. Exposed separately so a UI can
    distinguish "no extension improves coherence" from "no extension can
    coincide at these depths". */
inline bool reachableFromAny (const std::vector<OpSpec>& current,
                              int candP, int candQ, double candIndex)
{
    for (const auto& o : current)
    {
        horizon::Alpha a;
        if (o.p > 0 && o.q > 0
            && horizon::alphaOf (o.p, o.q, candP, candQ, a)
            && horizon::ringable (a, o.index, candIndex))
            return true;
    }
    return false;
}

/** True when at least one prefix-extension of `base` (length 1…maxExtra,
    exact-duplicate ratios excluded) is horizon-reachable from some current
    operator. Lets a UI caption an empty suggestion set honestly: "no
    extension can coincide at these depths" is a different fact from "none
    improves coherence". Vacuously true when no current op carries an exact
    rational (the filter is off there). */
inline bool anyReachableExtension (const std::vector<OpSpec>& current,
                                   const Path& base,
                                   double newIndex,
                                   int maxExtra = 3)
{
    const bool haveExact =
        std::any_of (current.begin(), current.end(),
                     [] (const OpSpec& o) { return o.p > 0 && o.q > 0; });
    if (! haveExact)
        return true;

    std::vector<Path> frontier { base };
    for (int depth = 1; depth <= maxExtra; ++depth)
    {
        std::vector<Path> next;
        for (const auto& t : frontier)
            for (auto step : { 'L', 'R' })
            {
                const Path cand = t.extended ((juce::juce_wchar) step);
                next.push_back (cand);

                const double r = cand.ratio();
                bool duplicate = false;
                for (const auto& o : current)
                    if (std::abs (o.ratio - r) < 1.0e-9)
                    {
                        duplicate = true;
                        break;
                    }
                if (duplicate) continue;

                int cp = 0, cq = 0;
                cand.walk (cp, cq);
                if (reachableFromAny (current, cp, cq, newIndex))
                    return true;
            }
        frontier = std::move (next);
    }
    return false;
}

/** Enumerate prefix-extensions of `base` (base + every L/R string of length
    1…maxExtra), score each as an added modulator at `newIndex`, and return the
    top `topN` by resulting whole-config coherence. Candidates whose ratio
    duplicates an existing modulator are skipped (an exact duplicate is
    trivially "coherent" but musically redundant), and — when the current ops
    carry exact rationals — candidates that cannot coincide with any current
    operator are dropped (the reachability filter above). */
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

    const bool haveExactRationals =
        std::any_of (current.begin(), current.end(),
                     [] (const OpSpec& o) { return o.p > 0 && o.q > 0; });

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

        int cp = 0, cq = 0;
        cand.walk (cp, cq);
        if (haveExactRationals && ! reachableFromAny (current, cp, cq, newIndex))
            continue;

        auto trial = current;
        trial.push_back ({ r, newIndex, cp, cq });
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
