#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <numeric>

#include "audio/Path.h"

using brocot::Path;

namespace
{
    struct PQ { int p, q; };

    PQ walk (const Path& path)
    {
        PQ r{};
        path.walk (r.p, r.q);
        return r;
    }
}

// ----------------------------------------------------------------------------
// Spec §5 — the required test cases.
// ----------------------------------------------------------------------------

TEST_CASE ("Empty path is 1/1", "[Path]")
{
    Path p;
    auto r = walk (p);
    REQUIRE (r.p == 1);
    REQUIRE (r.q == 1);
    REQUIRE (p.depth() == 0);
    REQUIRE (p.isEmpty());
}

TEST_CASE ("R is 2/1", "[Path]")
{
    Path p ("R");
    auto r = walk (p);
    REQUIRE (r.p == 2);
    REQUIRE (r.q == 1);
}

TEST_CASE ("L is 1/2", "[Path]")
{
    Path p ("L");
    auto r = walk (p);
    REQUIRE (r.p == 1);
    REQUIRE (r.q == 2);
}

TEST_CASE ("RLL is 4/3", "[Path]")
{
    Path p ("RLL");
    auto r = walk (p);
    REQUIRE (r.p == 4);
    REQUIRE (r.q == 3);
}

TEST_CASE ("RLLLR is 9/7", "[Path]")
{
    Path p ("RLLLR");
    auto r = walk (p);
    REQUIRE (r.p == 9);
    REQUIRE (r.q == 7);
}

TEST_CASE ("RLLLRLLLR is 43/34", "[Path]")
{
    Path p ("RLLLRLLLR");
    auto r = walk (p);
    REQUIRE (r.p == 43);
    REQUIRE (r.q == 34);
}

TEST_CASE ("Parent of RLL is RL → 3/2", "[Path]")
{
    Path p ("RLL");
    Path parent = p.parent();
    REQUIRE (parent.toString() == "RL");
    auto r = walk (parent);
    REQUIRE (r.p == 3);
    REQUIRE (r.q == 2);
}

TEST_CASE ("Common prefix of RLL and RLLLR is RLL", "[Path]")
{
    Path a ("RLL");
    Path b ("RLLLR");
    auto common = a.commonPrefix (b);
    REQUIRE (common.toString() == "RLL");
    REQUIRE (b.commonPrefix (a).toString() == "RLL");
}

TEST_CASE ("Children of root are 1/2 and 2/1", "[Path]")
{
    Path root;
    int p, q, lp, lq, rp, rq;
    root.walkWithChildren (p, q, lp, lq, rp, rq);
    REQUIRE (p == 1);  REQUIRE (q == 1);
    REQUIRE (lp == 1); REQUIRE (lq == 2);
    REQUIRE (rp == 2); REQUIRE (rq == 1);
}

// ----------------------------------------------------------------------------
// Additional coverage — behaviours the spec implies but doesn't enumerate.
// ----------------------------------------------------------------------------

TEST_CASE ("extended() returns a fresh Path; original is unchanged", "[Path]")
{
    Path original ("RL");
    Path child = original.extended ('R');
    REQUIRE (original.toString() == "RL");
    REQUIRE (child.toString() == "RLR");
}

TEST_CASE ("parent() of root is still root (no-op, no underflow)", "[Path]")
{
    Path root;
    Path parent = root.parent();
    REQUIRE (parent.isEmpty());
    REQUIRE (parent == root);
}

TEST_CASE ("prefix clamps to valid range", "[Path]")
{
    Path p ("RLLLR");
    REQUIRE (p.prefix (-3).toString() == "");
    REQUIRE (p.prefix (0).toString()  == "");
    REQUIRE (p.prefix (3).toString()  == "RLL");
    REQUIRE (p.prefix (5).toString()  == "RLLLR");
    REQUIRE (p.prefix (99).toString() == "RLLLR");
}

TEST_CASE ("startsWith respects prefix structure", "[Path]")
{
    Path a ("RLLLR");
    REQUIRE (a.startsWith (Path ("")));
    REQUIRE (a.startsWith (Path ("R")));
    REQUIRE (a.startsWith (Path ("RL")));
    REQUIRE (a.startsWith (Path ("RLLLR")));
    REQUIRE_FALSE (a.startsWith (Path ("L")));
    REQUIRE_FALSE (a.startsWith (Path ("RLLLL")));
    REQUIRE_FALSE (a.startsWith (Path ("RLLLRR")));  // longer than a
}

TEST_CASE ("Common prefix at branch point is correct", "[Path]")
{
    REQUIRE (Path ("RLR").commonPrefix (Path ("RLL")).toString() == "RL");
    REQUIRE (Path ("R").commonPrefix   (Path ("L")).toString()   == "");
    REQUIRE (Path ("RL").commonPrefix  (Path ("RL")).toString()  == "RL");
    REQUIRE (Path ("").commonPrefix    (Path ("RLR")).toString() == "");
}

TEST_CASE ("ratio() agrees with walk()", "[Path]")
{
    // 9/7 ≈ 1.2857142857...
    REQUIRE (Path ("RLLLR").ratio() == Catch::Approx (9.0 / 7.0));
    REQUIRE (Path ("R").ratio()     == Catch::Approx (2.0));
    REQUIRE (Path ("L").ratio()     == Catch::Approx (0.5));
    REQUIRE (Path ().ratio()        == Catch::Approx (1.0));
}

TEST_CASE ("walk() results are always in lowest terms", "[Path]")
{
    // Mediant walks on adjacent Stern-Brocot pairs preserve coprimality —
    // we don't reduce by GCD. Spot-check a few deep paths to confirm.
    const char* paths[] = { "RLR", "RLLLR", "RLLLRLLLR", "RRLLRRLL", "LRLRLRLR" };
    for (const auto* s : paths)
    {
        Path pth (s);
        int p, q;
        pth.walk (p, q);
        REQUIRE (std::gcd (p, q) == 1);
    }
}

TEST_CASE ("walkWithChildren matches walk on the extended paths", "[Path]")
{
    Path p ("RLLLR");
    int wp, wq, lp, lq, rp, rq;
    p.walkWithChildren (wp, wq, lp, lq, rp, rq);

    int lwp, lwq, rwp, rwq;
    p.extended ('L').walk (lwp, lwq);
    p.extended ('R').walk (rwp, rwq);

    REQUIRE (lp == lwp); REQUIRE (lq == lwq);
    REQUIRE (rp == rwp); REQUIRE (rq == rwq);
}

TEST_CASE ("Equality and string round-trip", "[Path]")
{
    Path a ("RLLR");
    Path b ("RLLR");
    Path c ("RLLL");
    REQUIRE (a == b);
    REQUIRE_FALSE (a == c);
    REQUIRE (a != c);
    REQUIRE (Path (a.toString()) == a);
}

TEST_CASE ("Path navigator example: extend root by R, then L, then L → 4/3", "[Path]")
{
    Path p;
    p = p.extended ('R').extended ('L').extended ('L');
    REQUIRE (p.toString() == "RLL");
    auto r = walk (p);
    REQUIRE (r.p == 4);
    REQUIRE (r.q == 3);
}

// ----------------------------------------------------------------------------
// fromRatio — inverse of walk() via Stern-Brocot descent.
// ----------------------------------------------------------------------------

TEST_CASE ("fromRatio round-trips every coprime p/q up to 16", "[Path]")
{
    // Both orientations cover all reduced ratios in [1/16, 16]. Depth is
    // bounded by the continued-fraction sum (≤ 16 here), well under the
    // default cap.
    for (int p = 1; p <= 16; ++p)
        for (int q = 1; q <= 16; ++q)
        {
            if (std::gcd (p, q) != 1)
                continue;

            Path path;
            REQUIRE (Path::fromRatio (p, q, path));
            auto r = walk (path);
            REQUIRE (r.p == p);
            REQUIRE (r.q == q);
        }
}

TEST_CASE ("fromRatio of 1/1 succeeds with the empty (root) path", "[Path]")
{
    Path path ("RLL");   // pre-set to prove `out` is written
    REQUIRE (Path::fromRatio (1, 1, path));
    REQUIRE (path.isEmpty());
}

TEST_CASE ("fromRatio reduces non-lowest-terms input: 6/4 == 3/2", "[Path]")
{
    REQUIRE (Path::fromRatio (6, 4) == Path::fromRatio (3, 2));
    REQUIRE (Path::fromRatio (6, 4).toString() == "RL");
}

TEST_CASE ("fromRatio fails past maxDepth, leaving out untouched", "[Path]")
{
    // 1000/999 = [1; 999] → depth 1000, far beyond a cap of 8.
    Path path ("RLL");
    REQUIRE_FALSE (Path::fromRatio (1000, 999, path, 8));
    REQUIRE (path.toString() == "RLL");
    REQUIRE (Path::fromRatio (1000, 999, 8).isEmpty());
}

TEST_CASE ("fromRatio rejects non-positive p or q", "[Path]")
{
    Path path;
    REQUIRE_FALSE (Path::fromRatio (0, 5, path));
    REQUIRE_FALSE (Path::fromRatio (5, 0, path));
    REQUIRE_FALSE (Path::fromRatio (-3, 2, path));
    REQUIRE (Path::fromRatio (0, 5).isEmpty());
}
