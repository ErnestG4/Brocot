#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <numeric>

#include "util/DensityGrid.h"

using brocot::viz::DensityGrid;
using Catch::Approx;

namespace
{
    constexpr int kN = DensityGrid::kN;

    float sum (const DensityGrid& g)
    {
        return std::accumulate (g.cells.begin(), g.cells.end(), 0.0f);
    }

    // Map-space coordinate of the centre of cell index i (either axis).
    float centreOf (int i)
    {
        return (((float) i + 0.5f) / (float) kN) * 2.0f - 1.0f;
    }
}

// ----------------------------------------------------------------------------
// accumulate / clamp
// ----------------------------------------------------------------------------

TEST_CASE ("accumulate lands in the addressed cell", "[DensityGrid]")
{
    DensityGrid g;
    g.accumulate (centreOf (10), centreOf (20));
    REQUIRE (g.cells[(size_t) (20 * kN + 10)] == Approx (1.0f));
    REQUIRE (sum (g) == Approx (1.0f));
}

TEST_CASE ("out-of-range points clamp into edge cells", "[DensityGrid]")
{
    DensityGrid g;
    g.accumulate (-7.0f, -7.0f);    // far bottom-left → cell (0, 0)
    g.accumulate ( 5.0f,  9.0f);    // far top-right   → cell (kN-1, kN-1)
    g.accumulate ( 1.0f,  1.0f);    // exact upper corner is in range too
    REQUIRE (g.cells[0] == Approx (1.0f));
    REQUIRE (g.cells[(size_t) ((kN - 1) * kN + (kN - 1))] == Approx (2.0f));
    REQUIRE (sum (g) == Approx (3.0f));   // no mass dropped
}

// ----------------------------------------------------------------------------
// blur — mass conservation + impulse response
// ----------------------------------------------------------------------------

TEST_CASE ("blur conserves total mass, including at the edges", "[DensityGrid]")
{
    DensityGrid g;
    // A spread of points including corners/edges, where naive clamping leaks.
    g.accumulate (-1.0f, -1.0f);
    g.accumulate ( 1.0f,  1.0f);
    g.accumulate ( 0.0f,  0.0f);
    for (int i = 0; i < 50; ++i)
        g.accumulate (-1.0f + 0.04f * (float) i, 0.7f - 0.03f * (float) i);

    const float before = sum (g);
    g.blur (3);
    REQUIRE (sum (g) == Approx (before).epsilon (1.0e-3));
}

TEST_CASE ("single impulse peaks at its cell and spreads symmetrically", "[DensityGrid]")
{
    DensityGrid g;
    const int cx = 48, cy = 40;
    g.accumulate (centreOf (cx), centreOf (cy));
    g.blur();   // default 2 passes → per-axis kernel [1,2,3,2,1]/9, strict peak

    auto at = [&g] (int i, int j) { return g.cells[(size_t) (j * kN + i)]; };

    const float peak = at (cx, cy);
    REQUIRE (peak == Approx (9.0f / 81.0f));
    for (int dj = -2; dj <= 2; ++dj)
        for (int di = -2; di <= 2; ++di)
            if (di != 0 || dj != 0)
                REQUIRE (at (cx + di, cy + dj) < peak);

    // Symmetric spread: mirror cells equal in all four directions.
    REQUIRE (at (cx - 1, cy) == Approx (at (cx + 1, cy)));
    REQUIRE (at (cx, cy - 1) == Approx (at (cx, cy + 1)));
    REQUIRE (at (cx - 2, cy) == Approx (at (cx + 2, cy)));
    REQUIRE (at (cx - 1, cy - 1) == Approx (at (cx + 1, cy + 1)));
}

// ----------------------------------------------------------------------------
// sample01 — bilinearity, clamping, p98 normalisation
// ----------------------------------------------------------------------------

TEST_CASE ("sample01 is bilinear across cell centres", "[DensityGrid]")
{
    DensityGrid g;
    // Two adjacent cells with distinct masses; everything else zero. p98 over
    // the mostly-zero grid falls back to the max cell (4), so values scale /4.
    for (int i = 0; i < 4; ++i) g.accumulate (centreOf (30), centreOf (50));
    for (int i = 0; i < 2; ++i) g.accumulate (centreOf (31), centreOf (50));

    // Exactly at the two centres.
    REQUIRE (g.sample01 (centreOf (30), centreOf (50)) == Approx (1.0f));
    REQUIRE (g.sample01 (centreOf (31), centreOf (50)) == Approx (0.5f));

    // Midway between the centres = the average.
    const float mid = 0.5f * (centreOf (30) + centreOf (31));
    REQUIRE (g.sample01 (mid, centreOf (50)) == Approx (0.75f));

    // Quarter of the way: 0.75·1 + 0.25·0.5.
    const float quarter = 0.75f * centreOf (30) + 0.25f * centreOf (31);
    REQUIRE (g.sample01 (quarter, centreOf (50)) == Approx (0.875f));
}

TEST_CASE ("sample01 clamps out-of-range coordinates to the edge", "[DensityGrid]")
{
    DensityGrid g;
    g.accumulate (-1.0f, -1.0f);   // mass in corner cell (0,0)
    REQUIRE (g.sample01 (-9.0f, -9.0f) == Approx (g.sample01 (centreOf (0), centreOf (0))));
    REQUIRE (g.sample01 (-9.0f, -9.0f) == Approx (1.0f));   // the only mass = the max
    REQUIRE (g.sample01 ( 9.0f,  9.0f) == Approx (0.0f));   // far corner is empty
}

TEST_CASE ("p98 normalisation caps outliers at 1", "[DensityGrid]")
{
    DensityGrid g;
    // >2% of cells at a moderate level (so p98 = moderate), one spiked cell.
    for (int i = 0; i < kN; ++i)          // one full row + another = ~2.1% of cells
        for (int rep = 0; rep < 5; ++rep)
        {
            g.accumulate (centreOf (i), centreOf (10));
            g.accumulate (centreOf (i), centreOf (11));
        }
    for (int rep = 0; rep < 500; ++rep)   // the outlier
        g.accumulate (centreOf (5), centreOf (60));

    // The spiked cell samples at exactly 1 (clamped), not 100.
    REQUIRE (g.sample01 (centreOf (5), centreOf (60)) == Approx (1.0f));
    // And the moderate cells normalise against p98 (=5), not the spike.
    REQUIRE (g.sample01 (centreOf (40), centreOf (10)) == Approx (1.0f));
}

TEST_CASE ("empty grid samples to zero", "[DensityGrid]")
{
    const DensityGrid g;
    REQUIRE (g.sample01 (0.0f, 0.0f) == 0.0f);
}
