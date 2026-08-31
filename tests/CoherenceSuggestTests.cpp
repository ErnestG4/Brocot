#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <vector>

#include "audio/CoherenceSuggest.h"

using namespace brocot::coherence;
using brocot::Path;

namespace
{
    OpSpec op (const char* path, double index)
    {
        return { Path (path).ratio(), index };
    }
}

TEST_CASE ("Suggestions all extend the base and are sorted by coherence", "[Coherence]")
{
    // A family rooted at R (2/1) with one member already present.
    const std::vector<OpSpec> current { op ("R", 3.0), op ("RLL", 3.0) };
    const auto s = suggestExtensions (current, Path ("R"), 3.0);

    REQUIRE (! s.empty());
    for (const auto& sug : s)
        REQUIRE (Path (sug.path).startsWith (Path ("R")));   // prefix-extension of base

    for (size_t i = 1; i < s.size(); ++i)
        REQUIRE (s[i - 1].coherence >= s[i].coherence);       // descending
}

TEST_CASE ("A prefix-extension beats an unrelated addition", "[Coherence]")
{
    // Current family: R (2/1). Extending its prefix should yield a config more
    // coherent than pairing it with a deep, incommensurate path.
    const std::vector<OpSpec> current { op ("R", 3.0) };
    const auto s = suggestExtensions (current, Path ("R"), 3.0);
    REQUIRE (! s.empty());
    const float bestExtension = s.front().coherence;

    // Unrelated: add 13/8 (a poorly-commensurate ratio) to the same R.
    std::vector<OpSpec> unrelated { op ("R", 3.0), { 13.0 / 8.0, 3.0 } };
    const float unrelatedCoh = score (unrelated).coherence;

    INFO ("best extension = " << bestExtension << ", unrelated = " << unrelatedCoh);
    REQUIRE (bestExtension > unrelatedCoh);
}

TEST_CASE ("Exact-duplicate ratios are not recommended", "[Coherence]")
{
    // RLL already present; the suggestion set must not re-propose RLL (4/3).
    const std::vector<OpSpec> current { op ("R", 3.0), op ("RLL", 3.0) };
    const auto s = suggestExtensions (current, Path ("R"), 3.0, 3, 32);
    for (const auto& sug : s)
        REQUIRE (std::abs (Path (sug.path).ratio() - Path ("RLL").ratio()) > 1e-9);
}

TEST_CASE ("gain is coherence minus the current baseline", "[Coherence]")
{
    const std::vector<OpSpec> current { op ("R", 3.0), op ("RLL", 3.0) };
    const float baseline = score (current).coherence;
    const auto s = suggestExtensions (current, Path ("R"), 3.0);
    REQUIRE (! s.empty());
    REQUIRE (s.front().gain == Catch::Approx (s.front().coherence - baseline).margin (1e-6));
}

// ----------------------------------------------------------------------------
// Horizon reachability filter (COINCIDENCE-HORIZON.md asymmetric corollary;
// census: brocot_suggest_census / brocot_suggest_score_census)
// ----------------------------------------------------------------------------

namespace
{
    // An OpSpec carrying its exact rational, as the shipping call sites now do.
    OpSpec opExact (int p, int q, double index)
    {
        const Path path = Path::fromRatio (p, q);
        int n = 0, d = 0;
        path.walk (n, d);
        return { path.ratio(), index, n, d };
    }
}

TEST_CASE ("near-unity extensions are filtered at musical depth", "[Coherence]")
{
    // The census's own regime: a near-unity op at I = 0.9 (2B = 8). Every
    // extension of 8/7 pushes p and q past the horizon, so the honest
    // suggestion set is EMPTY -- the old behaviour ranked these on noise.
    const std::vector<OpSpec> current { opExact (8, 7, 0.9) };
    const Path base = Path::fromRatio (8, 7);

    const auto s = suggestExtensions (current, base, 0.9, 3, 32);
    REQUIRE (s.empty());
    REQUIRE (! anyReachableExtension (current, base, 0.9, 3));
}

TEST_CASE ("reachable extensions survive the filter", "[Coherence]")
{
    // 2/1 at I = 3.0: RL = 3/2 gives alpha = 3/4, well inside the horizon.
    const std::vector<OpSpec> current { opExact (2, 1, 3.0) };
    const auto s = suggestExtensions (current, Path ("R"), 3.0);
    REQUIRE (! s.empty());
    REQUIRE (anyReachableExtension (current, Path ("R"), 3.0, 3));
}

TEST_CASE ("the filter consults the CURRENT op's index, not only the candidate's",
           "[Coherence]")
{
    // Same geometry, different current index: alpha = (5/2)/(2/1) = 5/4.
    // p = 5 needs 2*order_bound(I_current) >= 5, so the verdict flips between
    // a shallow and a deep current op while the candidate index stays fixed.
    // This is the index-blindness regression the census caught.
    const std::vector<OpSpec> shallow { opExact (2, 1, 0.05) };  // 2B = 2
    const std::vector<OpSpec> deep    { opExact (2, 1, 3.0)  };  // 2B = 14
    REQUIRE (! reachableFromAny (shallow, 5, 2, 0.9));
    REQUIRE (  reachableFromAny (deep,    5, 2, 0.9));
}

TEST_CASE ("legacy OpSpecs without exact rationals are not filtered", "[Coherence]")
{
    // p/q = 0 means unknown; the filter must refuse to guess from the double,
    // so the enumeration behaves exactly as before the filter existed.
    const std::vector<OpSpec> current { { Path ("R").ratio(), 0.05 } };
    const auto s = suggestExtensions (current, Path ("R"), 0.05, 2, 32);
    REQUIRE (! s.empty());
    REQUIRE (anyReachableExtension (current, Path ("R"), 0.05, 2));
}
