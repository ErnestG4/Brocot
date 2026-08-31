#include <catch2/catch_test_macros.hpp>

#include <numeric>
#include <vector>

#include "audio/Horizon.h"

using namespace brocot::horizon;

// ----------------------------------------------------------------------------
// orderBound mirrors phase3/partial_prediction.py order_bound
// ----------------------------------------------------------------------------

TEST_CASE ("orderBound matches the Python source of truth", "[Horizon]")
{
    // Goldens generated 2026-08-31 from partial_prediction.order_bound
    // (scipy jv, eps = 1e-3). The 0.9/1.5/2.0/3.0 rows are the theorem's own
    // verification table in COINCIDENCE-HORIZON.md.
    REQUIRE (orderBound (0.0) == 0);
    REQUIRE (orderBound (0.1) == 2);
    REQUIRE (orderBound (0.5) == 3);
    REQUIRE (orderBound (0.9) == 4);
    REQUIRE (orderBound (1.5) == 5);
    REQUIRE (orderBound (2.0) == 6);
    REQUIRE (orderBound (3.0) == 7);
    REQUIRE (orderBound (5.0) == 10);
    REQUIRE (orderBound (8.0) == 14);
}

TEST_CASE ("Jd agrees with scipy at the eps boundary", "[Horizon]")
{
    // The pairs straddling eps = 1e-3 at each verification index — exactly the
    // comparisons orderBound's verdict hangs on. scipy goldens, 10 sig figs.
    struct Row { int n; double x, golden; };
    const Row rows[] = {
        { 4, 0.9, 1.6405522310e-03 }, { 5, 0.9, 1.4865802167e-04 },
        { 5, 1.5, 1.7994217674e-03 }, { 6, 1.5, 2.2801269539e-04 },
        { 6, 2.0, 1.2024289718e-03 }, { 7, 2.0, 1.7494407487e-04 },
        { 7, 3.0, 2.5472944518e-03 }, { 8, 3.0, 4.9344177621e-04 },
    };
    for (const auto& r : rows)
    {
        const double got = brocot::bessel::Jd (r.n, r.x);
        REQUIRE (std::abs (got - r.golden) < 1.0e-9);
        // And the verdict each golden implies survives the round trip.
        REQUIRE ((std::abs (got) > 1.0e-3) == (std::abs (r.golden) > 1.0e-3));
    }
}

// ----------------------------------------------------------------------------
// The theorem, re-verified against brute-force enumeration (not the formula)
// ----------------------------------------------------------------------------

namespace
{
    // Ground truth by enumeration: does any coincidence a·q == b·p exist with
    // a = n1−n1' ∈ [−2B1, 2B1], b = n2'−n2 ∈ [−2B2, 2B2], (a,b) ≠ (0,0)?
    // This enumerates the difference lattice directly and never consults the
    // max(p,q) formula, so predicate and truth can disagree if either is wrong.
    bool coincidenceByEnumeration (int p, int q, int B1, int B2)
    {
        for (int a = -2 * B1; a <= 2 * B1; ++a)
            for (int b = -2 * B2; b <= 2 * B2; ++b)
                if ((a != 0 || b != 0) && a * q == b * p)
                    return true;
        return false;
    }

    // The 127 Stern-Brocot rationals in [0.70, 1.40] with q ≤ 24 — the node
    // population COINCIDENCE-HORIZON.md verified over. The count is pinned so
    // a drifted enumeration cannot silently shrink the test.
    std::vector<Alpha> verificationNodes()
    {
        std::vector<Alpha> nodes;
        for (int q = 1; q <= 24; ++q)
            for (int p = 1; p <= 2 * q; ++p)
                if (10 * p >= 7 * q && 10 * p <= 14 * q && std::gcd (p, q) == 1)
                    nodes.push_back ({ p, q });
        return nodes;
    }
}

TEST_CASE ("matched-index horizon: 508/508 against enumeration", "[Horizon]")
{
    const auto nodes = verificationNodes();
    REQUIRE (nodes.size() == 127);

    int cells = 0;
    for (double I : { 0.9, 1.5, 2.0, 3.0 })
    {
        const int B = orderBound (I);
        for (const auto a : nodes)
        {
            REQUIRE (ringable (a, I, I) == coincidenceByEnumeration (a.p, a.q, B, B));
            ++cells;
        }
    }
    REQUIRE (cells == 508);
}

TEST_CASE ("asymmetric corollary against enumeration, all index pairs", "[Horizon]")
{
    const auto nodes = verificationNodes();
    const double indices[] = { 0.9, 1.5, 2.0, 3.0 };

    for (double I1 : indices)
        for (double I2 : indices)
        {
            const int B1 = orderBound (I1);
            const int B2 = orderBound (I2);
            for (const auto a : nodes)
                REQUIRE (ringable (a, I1, I2)
                         == coincidenceByEnumeration (a.p, a.q, B1, B2));
        }
}

TEST_CASE ("the boundary is sharp, not asymptotic", "[Horizon]")
{
    // COINCIDENCE-HORIZON.md's own boundary exhibits, verbatim.
    REQUIRE (ringable ({ 7, 8 }, 0.9, 0.9));
    REQUIRE (ringable ({ 8, 7 }, 0.9, 0.9));
    REQUIRE (! ringable ({ 7, 9 }, 0.9, 0.9));
    REQUIRE (! ringable ({ 8, 9 }, 0.9, 0.9));
    REQUIRE (! ringable ({ 9, 8 }, 0.9, 0.9));
    REQUIRE (! ringable ({ 9, 7 }, 0.9, 0.9));

    REQUIRE (ringable ({ 11, 14 }, 3.0, 3.0));
    REQUIRE (ringable ({ 14, 11 }, 3.0, 3.0));
    REQUIRE (! ringable ({ 11, 15 }, 3.0, 3.0));
    REQUIRE (! ringable ({ 15, 14 }, 3.0, 3.0));
}

TEST_CASE ("swap non-invariance is real, not an implementation accident", "[Horizon]")
{
    // p ≤ 2B(I1) ∧ q ≤ 2B(I2): at alpha = 9/5 with 2B = 14 on one side and 8
    // on the other, the verdict depends on which side carries which index.
    const Alpha a { 9, 5 };
    REQUIRE (ringable (a, 3.0, 0.9));     // 9 ≤ 14 ∧ 5 ≤ 8
    REQUIRE (! ringable (a, 0.9, 3.0));   // 9 > 8
}

// ----------------------------------------------------------------------------
// The sufficiency witness
// ----------------------------------------------------------------------------

TEST_CASE ("witness is in-bounds and coincident wherever the predicate holds",
           "[Horizon]")
{
    // Mirrors the doc's exhaustive check. Its 1274 triples are B ∈ 2..11;
    // B = 1 is covered here too and the 2..11 population is pinned.
    int pinned = 0;
    for (int B = 1; B <= 11; ++B)
        for (int p = 1; p <= 2 * B; ++p)
            for (int q = 1; q <= 2 * B; ++q)
            {
                if (std::gcd (p, q) != 1) continue;
                if (B >= 2) ++pinned;

                const auto w = witness ({ p, q });
                REQUIRE (std::abs (w.n1)  <= B);
                REQUIRE (std::abs (w.n2)  <= B);
                REQUIRE (std::abs (w.n1p) <= B);
                REQUIRE (std::abs (w.n2p) <= B);
                // Distinct index pairs...
                REQUIRE ((w.n1 != w.n1p || w.n2 != w.n2p));
                // ...landing on the same frequency: (n1−n1')·q == (n2'−n2)·p.
                REQUIRE ((w.n1 - w.n1p) * q == (w.n2p - w.n2) * p);
            }
    REQUIRE (pinned == 1274);
}

// ----------------------------------------------------------------------------
// alphaOf and scope
// ----------------------------------------------------------------------------

TEST_CASE ("alphaOf reduces the cross ratio exactly", "[Horizon]")
{
    Alpha a;
    REQUIRE (alphaOf (3, 4, 9, 8, a));   // (9/8) / (3/4) = 36/24 = 3/2
    REQUIRE (a.p == 3);
    REQUIRE (a.q == 2);

    REQUIRE (alphaOf (1, 1, 1, 1, a));
    REQUIRE (a.p == 1);
    REQUIRE (a.q == 1);

    REQUIRE (! alphaOf (0, 1, 1, 1, a));
    REQUIRE (! alphaOf (1, 1, 1, -2, a));
}

TEST_CASE ("scope: dissolution is governed by effective operators", "[Horizon]")
{
    // order_bound = 0 removes an operator from the coincidence equation —
    // its lattice dimension is frozen at n = 0. Note the ε-support floor is
    // very low (J_1(I) ≈ I/2 > 1e-3 already at I ≈ 0.002), so at ε = 1e-3
    // nearly any enabled modulator counts as effective.
    REQUIRE (orderBound (0.001) == 0);
    REQUIRE (orderBound (0.05) == 1);
    REQUIRE (orderBound (0.1) == 2);

    REQUIRE (scopeFor (2, true)  == Scope::Exact);
    REQUIRE (scopeFor (2, false) == Scope::StackExtended);
    REQUIRE (scopeFor (1, true)  == Scope::Exact);
    REQUIRE (scopeFor (3, true)  == Scope::Degraded);
    REQUIRE (scopeFor (4, true)  == Scope::Dissolved);
    REQUIRE (scopeFor (16, false) == Scope::Dissolved);

    REQUIRE (Scope::Exact < Scope::StackExtended);
    REQUIRE (Scope::Degraded < Scope::Dissolved);
}
