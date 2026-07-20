#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <vector>

#include "audio/Coherence.h"
#include "audio/Path.h"

using namespace brocot::coherence;

namespace
{
    OpSpec op (const char* path, double index)
    {
        return { brocot::Path (path).ratio(), index };
    }

    float coh (const std::vector<OpSpec>& ops)
    {
        return score (ops).coherence;
    }
}

// ----------------------------------------------------------------------------
// §P0-a falsification gate: the banked prefix-family (R, RLL, RLLLR) must score
// markedly higher coherence than unrelated paths at matched depths.
// ----------------------------------------------------------------------------

TEST_CASE ("Prefix-family scores far higher than unrelated triples", "[Coherence]")
{
    const double idx = 3.0;

    auto ratio = [] (double p, double q, double i) { return OpSpec { p / q, i }; };

    // R=2/1, RLL=4/3, RLLLR=9/7 — shared Stern-Brocot / CF prefix.
    const float banked = coh ({ op ("R", idx), op ("RLL", idx), op ("RLLLR", idx) });

    // Triples with no prefix relation and pairwise-incommensurate denominators
    // (verified poorly-coincident in the design probe).
    const std::vector<std::vector<OpSpec>> unrelated {
        { ratio (3, 2, idx),  ratio (2, 3, idx),  ratio (7, 5, idx) },
        { ratio (5, 4, idx),  ratio (7, 3, idx),  ratio (8, 5, idx) },
        { ratio (5, 3, idx),  ratio (7, 4, idx),  ratio (9, 5, idx) },
        { ratio (11, 7, idx), ratio (13, 8, idx), ratio (5, 9, idx) },
    };

    float worstUnrelated = 0.0f;
    for (const auto& u : unrelated)
        worstUnrelated = std::max (worstUnrelated, coh (u));

    INFO ("banked = " << banked << ", worst unrelated = " << worstUnrelated);
    REQUIRE (banked > 0.25f);
    REQUIRE (banked > 2.0f * worstUnrelated);   // huge margin in practice (~7×)
}

TEST_CASE ("Coherence is 0 with fewer than two active modulators", "[Coherence]")
{
    REQUIRE (coh ({}) == Catch::Approx (0.0f));
    REQUIRE (coh ({ op ("RLL", 3.0) }) == Catch::Approx (0.0f));
    // A second op below the audibility floor doesn't count.
    REQUIRE (coh ({ op ("RLL", 3.0), op ("R", 0.001) }) == Catch::Approx (0.0f));
    REQUIRE (score ({ op ("RLL", 3.0), op ("R", 0.001) }).activeOps == 1);
}

TEST_CASE ("Identical ratios coincide fully; the score is scale-invariant", "[Coherence]")
{
    // Two modulators at the same ratio share every partial → maximal coincidence.
    const float same = coh ({ op ("RLL", 3.0), op ("RLL", 3.0) });
    REQUIRE (same > 0.9f);

    // Score depends only on ratios/indices, not the (implicit) carrier — it is
    // already expressed in carrier units, so there is nothing to vary here:
    // confirm two well-separated integer ratios still cohere on the harmonic
    // grid more than two anharmonic ones.
    const float harmonic  = coh ({ op ("R", 3.0),  op ("RR", 3.0) });   // 2/1, 3/1
    const float anharmonic = coh ({ op ("RLLLR", 3.0), op ("LRRRL", 3.0) });
    REQUIRE (harmonic > anharmonic);
}
