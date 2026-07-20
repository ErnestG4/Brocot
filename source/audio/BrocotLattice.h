#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <utility>
#include <vector>

namespace brocot::lattice
{

/**
    Shared lattice math for the Brocot ↔ Hofstadter-butterfly correspondence
    (BROCOT-SPEC.md §2). The navigation map's ratio axis θ = p/q is the
    almost-Mathieu (Harper) butterfly's flux axis: the Farey mediants that
    place map nodes are the mediants that open the butterfly's spectral gaps.

    This module is the single source of truth for the FOUR distinct quantities
    the spec insists must never be conflated (§4.4). Each owns exactly one
    meaning and one name:

      - simplicity(q)      = q. Lower = simpler = more consonant. (NB: this is
                             a different quantity from GrooveField's 1/(p·q)
                             well weight — do not substitute one for the other.)
      - cfDepth(p,q)       = sum of continued-fraction partial quotients. The
                             CF-SUM criticality / approximability proxy. This
                             is deliberately the CF-sum and nothing else — it is
                             NOT the liminf-K or Borel–Bernstein notion (§4.2),
                             which are pairwise non-equivalent in the analysis
                             lineage. Label it CF-based wherever it surfaces.
      - prominence(p,q,λ)  = width of the widest spectral gap of the θ=p/q
                             almost-Mathieu operator. The stability/plateau
                             proxy that makes wide-gap "footings" visible in
                             ratio space.
      - gaps(...).chern    = the Wannier/Středa topological integer of a gap
                             (slope of the IDS vs θ through that gap).

    STRUCTURAL, NOT LITERAL (§1, §4.1): the band/gap structure is used here as
    an organizing / annotation layer over ratio space and a prominence/stability
    proxy. A played FM ratio is not itself "a band or a gap"; do not let any
    caller assert a literal sonic identity.

    Pure + header-only so it is trivially unit-testable, matching the
    GrooveField.h / Bessel.h pattern. Consumes a reduced (p, q) — typically from
    Path::walk — and never re-derives the mediant walk.

    λ (lambda) is a PARAMETER, not a constant (§4.5): default 1 is the self-dual
    critical almost-Mathieu operator; the near-unity work lives at λ < 1.

    Validated against the BROCOT-SPEC.md §6 regression fixtures
    (see tests/BrocotLatticeTests.cpp).
*/

// ============================================================================
// Number-theoretic helpers
// ============================================================================

/** simplicity = q (the denominator). Lower = simpler = more consonant. */
inline int simplicity (int q) noexcept { return q; }

/** Continued-fraction partial quotients of p/q via the Euclidean algorithm,
    e.g. 4/3 → [1, 3], 9/7 → [1, 3, 2], 1/2 → [0, 2]. */
inline std::vector<int> cfTerms (int p, int q)
{
    std::vector<int> terms;
    while (q != 0)
    {
        terms.push_back (p / q);
        const int r = p % q;
        p = q;
        q = r;
    }
    return terms;
}

/** cfDepth = sum of continued-fraction partial quotients (the CF-sum
    criticality proxy). Convention-independent: the two canonical CF forms
    [..., a] and [..., a-1, 1] have equal term sums, so this is well defined.
    Equals a proper Stern-Brocot path's L/R depth + 1. */
inline int cfDepth (int p, int q)
{
    const auto t = cfTerms (p, q);
    return std::accumulate (t.begin(), t.end(), 0);
}

/** Modular inverse of a mod m (a, m assumed coprime), returned in [0, m).
    Extended Euclidean. Returns 0 for m == 1. */
inline int modinv (int a, int m)
{
    if (m == 1) return 0;
    int t = 0, newT = 1;
    int r = m, newR = ((a % m) + m) % m;
    while (newR != 0)
    {
        const int quot = r / newR;
        t = std::exchange (newT, t - quot * newT);
        r = std::exchange (newR, r - quot * newR);
    }
    if (t < 0) t += m;
    return t;
}

/** Fold a Wannier index t ∈ [0, q) into the symmetric range (−q/2, q/2].
    Upper bound inclusive so t == q/2 (even q) stays +q/2. */
inline int foldChern (int t, int q) noexcept
{
    return (2 * t > q) ? t - q : t;
}

// ============================================================================
// Almost-Mathieu (Harper) spectrum
// ============================================================================

struct Band { double lo, hi; };
struct Gap  { double lo, hi, width; int chern; };

// Local pi so the module stays JUCE-free and MSVC-safe (M_PI is non-standard
// and needs _USE_MATH_DEFINES).
inline constexpr double kPi = 3.14159265358979323846;

namespace detail
{
    /** Eigenvalues (ascending) of a real symmetric matrix by cyclic Jacobi.
        `a` is a flat n×n row-major buffer, mutated in place. n is small
        (q ≤ a few dozen for audible ratios) so O(n³) sweeps are cheap. */
    inline std::vector<double> symmetricEigenvalues (std::vector<double>& a, int n)
    {
        if (n == 1) return { a[0] };

        for (int sweep = 0; sweep < 100; ++sweep)
        {
            double off = 0.0;
            for (int p = 0; p < n; ++p)
                for (int qi = p + 1; qi < n; ++qi)
                    off += a[p * n + qi] * a[p * n + qi];
            if (off < 1.0e-24) break;

            for (int p = 0; p < n; ++p)
            {
                for (int qi = p + 1; qi < n; ++qi)
                {
                    const double apq = a[p * n + qi];
                    if (std::abs (apq) < 1.0e-300) continue;

                    const double app = a[p * n + p];
                    const double aqq = a[qi * n + qi];
                    const double theta = (aqq - app) / (2.0 * apq);
                    const double t = (theta >= 0.0 ? 1.0 : -1.0)
                                   / (std::abs (theta) + std::sqrt (theta * theta + 1.0));
                    const double c = 1.0 / std::sqrt (t * t + 1.0);
                    const double s = t * c;

                    for (int k = 0; k < n; ++k)
                    {
                        const double akp = a[k * n + p];
                        const double akq = a[k * n + qi];
                        a[k * n + p]  = c * akp - s * akq;
                        a[k * n + qi] = s * akp + c * akq;
                    }
                    for (int k = 0; k < n; ++k)
                    {
                        const double apk = a[p * n + k];
                        const double aqk = a[qi * n + k];
                        a[p * n + k]  = c * apk - s * aqk;
                        a[qi * n + k] = s * apk + c * aqk;
                    }
                }
            }
        }

        std::vector<double> ev (static_cast<size_t> (n));
        for (int i = 0; i < n; ++i) ev[static_cast<size_t> (i)] = a[i * n + i];
        std::sort (ev.begin(), ev.end());
        return ev;
    }

    /** Sorted eigenvalues of the q×q Harper matrix at one magnetic-BZ corner:
        diagonal 2λ cos(2π m p/q + offset), unit off-diagonals, periodic corner
        element = wrap (±1). */
    inline std::vector<double> harperCorner (int p, int q, double lambda,
                                             double offset, double wrap)
    {
        std::vector<double> a (static_cast<size_t> (q * q), 0.0);
        const double twoPi = 2.0 * kPi;
        for (int m = 0; m < q; ++m)
            a[m * q + m] = 2.0 * lambda * std::cos (twoPi * m * p / (double) q + offset);

        if (q == 1)
        {
            // Degenerate: the single site's hop wraps onto itself. No gap
            // structure at integer flux; return the bare diagonal.
            return { a[0] };
        }

        for (int m = 0; m + 1 < q; ++m)
        {
            a[m * q + (m + 1)] += 1.0;
            a[(m + 1) * q + m] += 1.0;
        }
        a[0 * q + (q - 1)] += wrap;
        a[(q - 1) * q + 0] += wrap;

        return symmetricEigenvalues (a, q);
    }
}

/** The q bands of the θ = p/q almost-Mathieu spectrum at coupling λ.
    Band j = [min, max] over the 4 magnetic-BZ corners (wrap ∈ {+1,−1},
    offset ∈ {0, π}) of the j-th sorted eigenvalue (BROCOT-SPEC.md §6). */
inline std::vector<Band> almostMathieuBands (int p, int q, double lambda = 1.0)
{
    const std::array<std::pair<double, double>, 4> corners {{
        { 0.0,   +1.0 }, { 0.0,   -1.0 },
        { kPi,  +1.0 }, { kPi,  -1.0 },
    }};

    std::vector<std::vector<double>> ev;
    ev.reserve (4);
    for (const auto& [offset, wrap] : corners)
        ev.push_back (detail::harperCorner (p, q, lambda, offset, wrap));

    std::vector<Band> bands (static_cast<size_t> (q));
    for (int j = 0; j < q; ++j)
    {
        double lo =  1.0e300, hi = -1.0e300;
        for (const auto& e : ev)
        {
            lo = std::min (lo, e[static_cast<size_t> (j)]);
            hi = std::max (hi, e[static_cast<size_t> (j)]);
        }
        bands[static_cast<size_t> (j)] = { lo, hi };
    }
    return bands;
}

/** The spectral gaps of θ = p/q: for j = 1…q−1, gap = [bands[j-1].hi,
    bands[j].lo] with zero-width gaps dropped. Each gap carries its width
    (prominence of that gap) and chern = fold((j · modinv(p,q)) mod q). */
inline std::vector<Gap> gaps (int p, int q, double lambda = 1.0)
{
    std::vector<Gap> out;
    if (q < 2) return out;   // integer flux: single band, no gaps

    const auto bands = almostMathieuBands (p, q, lambda);
    const int inv = modinv (p, q);

    for (int j = 1; j < q; ++j)
    {
        const double lo = bands[static_cast<size_t> (j - 1)].hi;
        const double hi = bands[static_cast<size_t> (j)].lo;
        const double width = hi - lo;
        if (width <= 1.0e-6) continue;   // closed gap
        const int chern = foldChern ((j * inv) % q, q);
        out.push_back ({ lo, hi, width, chern });
    }
    return out;
}

/** prominence = width of the widest gap (0 if none). A single scalar per node
    for a navigator: what makes stable, consonant "plateaus" visible in ratio
    space. STRUCTURAL only (§4.1). */
inline double prominence (int p, int q, double lambda = 1.0)
{
    double best = 0.0;
    for (const auto& g : gaps (p, q, lambda))
        best = std::max (best, g.width);
    return best;
}

} // namespace brocot::lattice
