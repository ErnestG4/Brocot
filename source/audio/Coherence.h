#pragma once

#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

#include "audio/Bessel.h"

namespace brocot::coherence
{

/**
    Path-prefix coherence meter (BROCOT-SPEC.md §P0-a — Will's banked
    discovery). Modulators that share a Stern-Brocot path prefix are consecutive
    Farey mediants / continued-fraction convergents; their FM sideband combs
    coincide on shared partials, giving a coherent, distinctive timbre. This
    turns that qualitative discovery into a number you can steer by.

    Model (parallel modulators, per Operator.h §2/§4 — each is its own Bessel
    expansion around the carrier). For a modulator at ratio r = f_mod/f_carrier
    and index I, the sidebands sit at f_carrier·|1 ± m·r| with energy J_m(I)²
    (the lower sideband reflects through DC via the absolute value). Working in
    carrier units (f_carrier ≡ 1) keeps the score scale-invariant — it depends
    only on the ratios and indices, never on the played note.

    Score = the fraction of total sideband energy that lands on frequencies
    SHARED by two or more modulators, within a cents tolerance. Commensurate /
    prefix-related combs overlap heavily → high score; unrelated ratios don't.
    Empirically the banked triple (R, RLL, RLLLR = 2/1, 4/3, 9/7) scores several
    times higher than unrelated triples at matched depths (tests/CoherenceTests).

    STRUCTURAL, not literal (§4.1): this measures spectral coincidence, an
    organizing/annotation quantity. It is not a claim that a note "is a band".

    GENERATIVE MODEL — CORRECTION, 2026-08-27. The rationale above says "each is
    its own Bessel expansion around the carrier", i.e. the spectrum is a SUM of
    N independent combs. For SIMULTANEOUS modulators that is not the spectrum
    this engine produces. The true spectrum is a PRODUCT lattice, at
    f_carrier*(1 + n1*r1 + n2*r2 + ...) with energy J_n1(I1)*J_n2(I2)*..., and
    the comb model keeps only the lattice points where every OTHER index is
    zero. The omitted cross-partials carry (1 - J0(I)^2)^2 of the total energy
    at matched indices — verified by formula and by direct lattice summation:

        I = 0.9  ->  12.1%        I = 1.5  ->  54.5%        I = 2.0  ->  90.2%

    So at I >= 1.5 this score is computed over a minority of the spectrum. At
    the spec's own typical I ~ 0.9 it is a fair first-order proxy.

    WHETHER THE RANKING INHERITS THIS IS UNRESOLVED, and deliberately not
    overstated. Scoring the same candidates on the true lattice — with "shared"
    given the horizon's meaning, a bin holding two or more DISTINCT index
    vectors — changes the top-4 at Jaccard 0.517, 95% CI [0.468, 0.566]. The
    0.50 bar sits inside that interval, so the measurement separates the two
    rankings from neither "unchanged" nor "different". About 1,170 sampled
    cases would resolve it; 140 do not
    (criticality_tool/cross_substrate/brocot_coherence_model.json).

    So: this comment is corrected because the model is provably wrong. No C++ is
    changed, because the arm that would warrant a change is unresolved rather
    than negative.
*/

struct OpSpec
{
    double ratio { 0.0 };   // f_mod / f_carrier (exact p/q from Path::ratio)
    double index { 0.0 };   // FM modulation index = operator depth
    int    p     { 0 };     // exact rational, as Path::walk yields it;
    int    q     { 0 };     // 0/0 = unknown (legacy callers) — horizon
                            // reachability checks are skipped without it
};

struct Result
{
    float coherence { 0.0f };   // 0..1 — shared-partial energy fraction
    int   activeOps { 0 };      // modulators that contributed sidebands
};

/** Minimum index below which a modulator makes no audible sidebands (mirrors
    Bessel/DepthRatio's audibility floor). */
inline constexpr double kMinAudibleIndex = 0.02;

/** Score the coincidence of the given modulators' sideband combs.
    `centsTolerance` bins nearby partials together; `maxOrder` caps the sideband
    order considered (per-op order also scales with the index). */
inline Result score (const std::vector<OpSpec>& ops,
                     double centsTolerance = 12.0,
                     int maxOrder = 24)
{
    Result r;

    // Gather active modulators.
    std::vector<OpSpec> active;
    for (const auto& o : ops)
        if (o.index > kMinAudibleIndex && o.ratio > 0.0)
            active.push_back (o);
    r.activeOps = (int) active.size();

    if (active.size() < 2 || active.size() > 63)
        return r;   // nothing to share (or too many to bit-mask; not a real case)

    // Bin: cents key -> { summed energy, bitmask of contributing op indices }.
    std::map<long long, std::pair<double, std::uint64_t>> bins;
    double total = 0.0;
    const double invCents = 1.0 / centsTolerance;

    for (std::size_t k = 0; k < active.size(); ++k)
    {
        const double r0 = active[k].ratio;
        const double I  = active[k].index;
        int order = (int) std::ceil (I) + 4;
        order = std::min (order, maxOrder);
        order = std::max (order, 1);

        for (int m = 1; m <= order; ++m)
        {
            const double w = (double) bessel::J (m, (float) I);
            const double energy = w * w;
            if (energy <= 0.0) continue;

            const double nus[2] = { 1.0 + m * r0, std::abs (1.0 - m * r0) };
            for (double nu : nus)
            {
                if (nu <= 1.0e-4) continue;   // lands on DC/carrier
                total += energy;
                const long long key = (long long) std::llround (1200.0 * std::log2 (nu) * invCents);
                auto& b = bins[key];
                b.first += energy;
                b.second |= (std::uint64_t) 1 << k;
            }
        }
    }

    if (total <= 0.0)
        return r;

    double shared = 0.0;
    for (const auto& [key, b] : bins)
    {
        (void) key;
        // ≥2 distinct ops present ⇔ more than one bit set.
        if (b.second != 0 && (b.second & (b.second - 1)) != 0)
            shared += b.first;
    }

    r.coherence = (float) (shared / total);
    return r;
}

} // namespace brocot::coherence
