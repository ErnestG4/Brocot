#pragma once

#include "Bessel.h"
#include <numeric>
#include <cmath>

namespace brocot::horizon
{

/**
    The coincidence horizon (COINCIDENCE-HORIZON.md, root of the repo).

    For the parallel-FM topology with two sine modulators at ratios r1, r2 and
    modulation indices I1, I2, write alpha = r2/r1 = p/q in lowest terms. A
    DIRECT sideband coincidence is reachable

        ⟺   p ≤ 2·order_bound(I1)   AND   q ≤ 2·order_bound(I2)

    Proved (witness constructed), verified 508/508 exact at matched indices and
    4784/4784 for the asymmetric corollary. Everything here is that document's
    machinery made available to the engine and UI; nothing is new science.

    SCOPE — load-bearing, stated in the UI, never discovered by a player:
      · DIRECT (unfolded) channel. The folded channel's only horizon-free
        content is the universal ±m·alpha family, which fires for every alpha
        (rational or irrational) and distinguishes nothing.
      · Exact at TWO sine modulators. Harmonic-stack waveforms extend the
        q-side only (measured: everything sine-ringable stays ringable, a few
        deeper denominators join). N = 3 degrades (17.3% of above-horizon sets
        coincide anyway); N ≥ 4 dissolves (44.5% / 65.9% at N = 4 / 5 — the
        solution lattice has dimension N−1, and a nonzero in-box solution
        becomes generic). Use scopeFor() and draw the annotation only where
        the theorem holds.
      · ε = 1e-3 is the order_bound convention, inherited from
        phase3/partial_prediction.py. The horizon ORDERING of ratios is
        ε-invariant; absolute thresholds are not, so no UI wording may treat
        an ε-derived index threshold as a perceptual fact.
      · Structure, not audibility. Above-horizon ratios are perfectly distinct
        from one another — they simply cannot fuse. Captions say COINCIDENCE
        STRUCTURE ("these sidebands fuse"), never "alive/dead", never "jumps".
*/

/** Largest |n| with |J_n(I)| > eps — bit-faithful mirror of
    phase3/partial_prediction.py order_bound (same walk-up-and-stop semantics,
    double precision throughout; a unit test pins both against scipy goldens).
    Returns 0 at I ≤ 0: only the carrier survives. */
inline int orderBound (double index, double eps = 1.0e-3, int nCap = 50) noexcept
{
    if (index <= 0.0)
        return 0;
    int n = 1;
    while (n <= nCap && std::abs (bessel::Jd (n, index)) > eps)
        ++n;
    return n - 1;
}

/** alpha = r2/r1 in lowest terms, from two exact rationals p1/q1, p2/q2
    (each as Path::walk yields them). False for non-positive inputs. */
struct Alpha { int p { 0 }, q { 0 }; };

inline bool alphaOf (int p1, int q1, int p2, int q2, Alpha& out) noexcept
{
    if (p1 <= 0 || q1 <= 0 || p2 <= 0 || q2 <= 0)
        return false;
    const long long num = (long long) p2 * q1;
    const long long den = (long long) q2 * p1;
    const long long g = std::gcd (num, den);
    out.p = (int) (num / g);
    out.q = (int) (den / g);
    return true;
}

/** Direct-channel reachability, asymmetric corollary. I1 belongs to the
    modulator whose ratio is the DENOMINATOR side of alpha's derivation
    (r1, the reference), I2 to r2 — i.e. exactly the order the alphaOf
    arguments were given in. Swapping indices without swapping ratios is a
    different question with a different answer (6.1% of the shipped map's
    enabled pairs flip verdict under exchange). */
inline bool ringable (Alpha a, double index1, double index2, double eps = 1.0e-3) noexcept
{
    if (a.p <= 0 || a.q <= 0)
        return false;
    return a.p <= 2 * orderBound (index1, eps)
        && a.q <= 2 * orderBound (index2, eps);
}

/** The proof's sufficiency witness: two distinct index pairs (n1,n2), (n1',n2')
    inside the box that land on the same frequency, valid whenever the
    predicate holds. Exposed so a display can show WHICH sidebands fuse rather
    than assert that some do. */
struct Witness { int n1, n2, n1p, n2p; };

inline Witness witness (Alpha a) noexcept
{
    return { (a.p + 1) / 2, -(a.q / 2),      // ( ⌈p/2⌉, −⌊q/2⌋ )
             -(a.p / 2),    (a.q + 1) / 2 }; // ( −⌊p/2⌋, ⌈q/2⌉ )
}

/** Where the theorem stands for a given patch configuration. Ordered from
    strongest to weakest so callers may compare with <. */
enum class Scope
{
    Exact,          // 2 effective sine modulators — theorem-grade
    StackExtended,  // 2 effective modulators, at least one non-sine: the
                    // sine predicate is a LOWER bound on the ringing set
                    // (support extension only adds; measured 13 → 17 with
                    // no removals). Draw sine-exact marks, label the extension.
    Degraded,       // 3 effective modulators — 17.3% of above-horizon sets
                    // coincide anyway; annotation is advisory at best
    Dissolved       // ≥ 4 — 44.5% (N=4), 65.9% (N=5): the predicate predicts
                    // nothing; the annotation must not be drawn
};

/** An operator only spans a lattice dimension if it can emit a sideband at
    all: order_bound(I) = 0 pins its index to n = 0 and removes it from the
    coincidence equation entirely. So the N that governs dissolution is the
    count of enabled modulators with order_bound(I) ≥ 1 — a consequence of the
    theorem's own box, not an extra empirical claim. Caution: at ε = 1e-3 that
    floor sits near I ≈ 0.002 (J_1(I) ≈ I/2), so in practice every enabled
    modulator with a nonzero depth counts; do not conflate this ε-support
    floor with Coherence.h's audibility floor, which is a different (and
    higher) bar making a perceptual claim this predicate does not. */
inline Scope scopeFor (int effectiveOps, bool allSine) noexcept
{
    if (effectiveOps >= 4) return Scope::Dissolved;
    if (effectiveOps == 3) return Scope::Degraded;
    return allSine ? Scope::Exact : Scope::StackExtended;
}

} // namespace brocot::horizon
