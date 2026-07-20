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
