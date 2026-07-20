#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <numeric>
#include <vector>

#include "audio/BrocotLattice.h"
#include "audio/Path.h"

using namespace brocot::lattice;

namespace
{
    // Helper: resolve an L/R path to (p, q) so the CF-depth relation can be
    // checked against the actual Stern-Brocot walk.
    void walkPath (const char* s, int& p, int& q)
    {
        brocot::Path (s).walk (p, q);
    }

    std::vector<int> chernLabels (int p, int q, double lambda = 1.0)
    {
        std::vector<int> out;
        for (const auto& g : gaps (p, q, lambda))
            out.push_back (g.chern);
        return out;
    }
}

// ----------------------------------------------------------------------------
// simplicity / cfDepth — the two number-theoretic quantities (§2, §4.4)
// ----------------------------------------------------------------------------

TEST_CASE ("simplicity is the denominator q", "[Lattice]")
{
    REQUIRE (simplicity (7) == 7);
    REQUIRE (simplicity (1) == 1);
}

TEST_CASE ("cfTerms are the Euclidean continued-fraction quotients", "[Lattice]")
{
    REQUIRE (cfTerms (4, 3) == std::vector<int> { 1, 3 });
    REQUIRE (cfTerms (9, 7) == std::vector<int> { 1, 3, 2 });
    REQUIRE (cfTerms (1, 2) == std::vector<int> { 0, 2 });
    REQUIRE (cfTerms (2, 1) == std::vector<int> { 2 });
}

TEST_CASE ("cfDepth is convention-independent and equals path depth + 1", "[Lattice]")
{
    // The two CF forms [..., a] and [..., a-1, 1] have equal term sums.
    REQUIRE (cfDepth (4, 3) == 4);   // [1,3] and [1,2,1] both sum to 4
    REQUIRE (cfDepth (9, 7) == 6);   // [1,3,2] and [1,3,1,1] both sum to 6

    // cfDepth(p/q) == (L/R path depth) + 1 for the path that resolves to p/q.
    const char* paths[] = { "R", "L", "RL", "RLL", "RLLLR", "RLLLRLLLR" };
    for (const auto* s : paths)
    {
        int p, q;
        walkPath (s, p, q);
        REQUIRE (cfDepth (p, q) == brocot::Path (s).depth() + 1);
    }
}

// ----------------------------------------------------------------------------
// §6 regression fixtures — band edges (λ = 1)
// ----------------------------------------------------------------------------

TEST_CASE ("theta = 1/3 band edges", "[Lattice]")
{
    const auto b = almostMathieuBands (1, 3);
    REQUIRE (b.size() == 3);
    REQUIRE (b[0].lo == Catch::Approx (-2.7320508).margin (1e-4));
    REQUIRE (b[0].hi == Catch::Approx (-2.0)      .margin (1e-4));
    REQUIRE (b[1].lo == Catch::Approx (-0.7320508).margin (1e-4));
    REQUIRE (b[1].hi == Catch::Approx ( 0.7320508).margin (1e-4));
    REQUIRE (b[2].lo == Catch::Approx ( 2.0)      .margin (1e-4));
    REQUIRE (b[2].hi == Catch::Approx ( 2.7320508).margin (1e-4));
}

// ----------------------------------------------------------------------------
// §6 regression fixtures — gap Chern labels (λ = 1)
// ----------------------------------------------------------------------------

TEST_CASE ("theta = 1/3 gap labels are [+1, -1]", "[Lattice]")
{
    REQUIRE (chernLabels (1, 3) == std::vector<int> { +1, -1 });
}

TEST_CASE ("theta = 1/4 central gap closes; nonzero labels [+1, -1]", "[Lattice]")
{
    // The +2 central gap has zero width at lambda = 1, so it is dropped.
    const auto g = gaps (1, 4);
    REQUIRE (g.size() == 2);
    REQUIRE (chernLabels (1, 4) == std::vector<int> { +1, -1 });
}

TEST_CASE ("theta = 1/5 gap labels are [+1, +2, -2, -1]", "[Lattice]")
{
    REQUIRE (chernLabels (1, 5) == std::vector<int> { +1, +2, -2, -1 });
}

TEST_CASE ("theta = 2/5 gap labels are [-2, +1, -1, +2]", "[Lattice]")
{
    REQUIRE (chernLabels (2, 5) == std::vector<int> { -2, +1, -1, +2 });
}

TEST_CASE ("theta = 3/7 gap labels are [-2, +3, +1, -1, -3, +2]", "[Lattice]")
{
    REQUIRE (chernLabels (3, 7) == std::vector<int> { -2, +3, +1, -1, -3, +2 });
}

// ----------------------------------------------------------------------------
// §6 — E → −E spectral symmetry to machine precision, for all p/q
// ----------------------------------------------------------------------------

TEST_CASE ("spectrum is E -> -E symmetric", "[Lattice]")
{
    const std::array<std::pair<int, int>, 6> thetas {{
        { 1, 3 }, { 1, 4 }, { 1, 5 }, { 2, 5 }, { 3, 7 }, { 4, 9 },
    }};
    for (const auto& [p, q] : thetas)
    {
        const auto b = almostMathieuBands (p, q);
        for (int j = 0; j < q; ++j)
        {
            // band j reflects onto band (q-1-j): lo_j = -hi_{q-1-j}.
            const auto& lo = b[static_cast<size_t> (j)];
            const auto& hi = b[static_cast<size_t> (q - 1 - j)];
            REQUIRE (lo.lo == Catch::Approx (-hi.hi).margin (1e-9));
            REQUIRE (lo.hi == Catch::Approx (-hi.lo).margin (1e-9));
        }
    }
}

// ----------------------------------------------------------------------------
// prominence — widest gap width; a stable-plateau proxy (§2, structural only)
// ----------------------------------------------------------------------------

TEST_CASE ("prominence is the widest gap width and is positive for q >= 2", "[Lattice]")
{
    // 1/3: both gaps span [-2, -0.732] and [0.732, 2], width ~1.268.
    REQUIRE (prominence (1, 3) == Catch::Approx (1.2679492).margin (1e-4));

    // Simpler ratios (smaller q) have wider gaps than complex ones — the
    // "stable footing" ordering the navigator wants.
    REQUIRE (prominence (1, 2) > prominence (3, 7));

    // Integer flux (q = 1) has no gap structure.
    REQUIRE (prominence (2, 1) == Catch::Approx (0.0));
    REQUIRE (gaps (2, 1).empty());
}

TEST_CASE ("modinv and foldChern behave", "[Lattice]")
{
    REQUIRE (modinv (2, 5) == 3);
    REQUIRE (modinv (3, 7) == 5);
    REQUIRE (modinv (1, 3) == 1);
    REQUIRE (foldChern (2, 3) == -1);
    REQUIRE (foldChern (2, 4) == 2);   // q/2 stays positive (upper-inclusive)
    REQUIRE (foldChern (3, 4) == -1);
}
