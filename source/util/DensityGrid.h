#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace brocot::viz
{

/**
    Fixed-resolution 2D density accumulator over the landscape's map space
    [-1,1]², for the "criticality/voids" underlay (CloudView): dense regions
    of the shipped embedding get shaded so the VOIDS — the light regions the
    critical points speckle and line — read immediately. Nothing is inferred;
    this is node density of the embedding as shipped.

    Pure + header-only + JUCE-free so it is trivially unit-testable, matching
    the BrocotLattice.h pattern. Built once at image-rebuild time; sample01()
    is then read per cell/pixel.
*/
struct DensityGrid
{
    static constexpr int kN = 96;

    std::vector<float> cells = std::vector<float> ((size_t) (kN * kN), 0.0f);   // row-major

    /** Deposit one unit of mass at map-space (x, y) ∈ [-1,1]²; out-of-range
        points clamp into the edge cells (mass is never dropped). */
    void accumulate (float x, float y)
    {
        cells[(size_t) (cellIndex (y) * kN + cellIndex (x))] += 1.0f;
        normCache = -1.0f;
    }

    /** Separable 3-tap box blur, `passes` times. Mass-conserving: at the grid
        edges the outflow that would leave the grid stays in the edge cell
        (nothing is clamped away), so the total is invariant. */
    void blur (int passes = 2)
    {
        std::vector<float> tmp ((size_t) kN, 0.0f);
        for (int pass = 0; pass < passes; ++pass)
        {
            for (int row = 0; row < kN; ++row)   // horizontal
                blurLine (&cells[(size_t) (row * kN)], 1, tmp);
            for (int col = 0; col < kN; ++col)   // vertical
                blurLine (&cells[(size_t) col], kN, tmp);
        }
        normCache = -1.0f;
    }

    /** Bilinear density at map-space (x, y), normalised by the p98 cell value
        (robust to a few spiked cells) and clamped to [0, 1]. Coordinates
        outside [-1,1] clamp to the edge. Empty grid → 0. */
    float sample01 (float x, float y) const
    {
        const float norm = p98();
        if (norm <= 0.0f) return 0.0f;

        // Cell-centre coordinates: centre of cell i sits at gx == i.
        const float gx = centreCoord (x), gy = centreCoord (y);
        const int   i0 = std::min ((int) gx, kN - 2);
        const int   j0 = std::min ((int) gy, kN - 2);
        const float tx = gx - (float) i0, ty = gy - (float) j0;

        auto at = [this] (int i, int j) { return cells[(size_t) (j * kN + i)]; };
        const float v = (1.0f - ty) * ((1.0f - tx) * at (i0, j0)     + tx * at (i0 + 1, j0))
                      +         ty  * ((1.0f - tx) * at (i0, j0 + 1) + tx * at (i0 + 1, j0 + 1));
        return std::clamp (v / norm, 0.0f, 1.0f);
    }

private:
    mutable float normCache { -1.0f };   // p98, computed lazily after edits

    static int cellIndex (float coord)
    {
        const int i = (int) std::floor ((coord + 1.0f) * 0.5f * (float) kN);
        return std::clamp (i, 0, kN - 1);
    }

    static float centreCoord (float coord)
    {
        const float g = (coord + 1.0f) * 0.5f * (float) kN - 0.5f;
        return std::clamp (g, 0.0f, (float) (kN - 1));
    }

    /** One 1D pass along a strided line. Each cell spreads 1/3 to each side;
        at the ends the blocked third stays put (mass conservation). */
    static void blurLine (float* line, int stride, std::vector<float>& tmp)
    {
        for (int i = 0; i < kN; ++i) tmp[(size_t) i] = line[(size_t) (i * stride)];
        constexpr float k = 1.0f / 3.0f;
        for (int i = 0; i < kN; ++i)
        {
            const float left  = (i > 0)      ? tmp[(size_t) (i - 1)] : tmp[0];
            const float right = (i < kN - 1) ? tmp[(size_t) (i + 1)] : tmp[(size_t) (kN - 1)];
            line[(size_t) (i * stride)] = k * (left + tmp[(size_t) i] + right);
        }
    }

    float p98() const
    {
        if (normCache >= 0.0f) return normCache;
        std::vector<float> sorted (cells);
        const size_t idx = (size_t) ((double) (sorted.size() - 1) * 0.98);
        std::nth_element (sorted.begin(), sorted.begin() + (long) idx, sorted.end());
        float v = sorted[idx];
        if (v <= 0.0f) v = *std::max_element (cells.begin(), cells.end());
        normCache = std::max (v, 0.0f);
        return normCache;
    }
};

} // namespace brocot::viz
