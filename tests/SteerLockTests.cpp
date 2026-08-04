#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>

#include "audio/GrooveField.h"
#include "audio/GrooveLock.h"
#include "audio/Path.h"

using brocot::GrooveField;
using brocot::LockResult;
using brocot::Path;
using brocot::lockRatioToGroove;
using Catch::Approx;

namespace
{
    double lockedRatio (const LockResult& r)
    {
        int p = 0, q = 0;
        r.path.walk (p, q);
        return ((double) p / (double) q) * std::exp2 ((double) r.detuneCents / 1200.0);
    }
}

// S4: the steer.groove* parameter defaults ARE these constants (ParameterTree.cpp
// passes groove::kDefault* straight into the layout, a compile-time tie), so
// pinning the constants here pins the params' defaults to the pre-S4 hardcoded
// SteerTab feel.
TEST_CASE ("Groove-feel defaults equal the pre-S4 hardcoded constants", "[GrooveLock]")
{
    REQUIRE (brocot::groove::kDefaultGravity      == 0.6f);
    REQUIRE (brocot::groove::kDefaultRestlessness == 0.3f);
    REQUIRE (brocot::groove::kDefaultLock         == true);
    // The lock's detune budget matches the opN.detune param range (±100c).
    REQUIRE (brocot::groove::kMaxDetuneCents == 100.0);
}

TEST_CASE ("pull() at the default feel (0.6, 0.3) — pinned regression", "[GrooveLock]")
{
    GrooveField gf;

    // Values captured from the pre-S4 code path (kGrooveGravity=0.6,
    // kGrooveRestlessness=0.3 hardcoded); any drift here means the param-fed
    // path no longer reproduces the shipped steering feel at defaults.
    REQUIRE (gf.pull (0.04f, 0.6f, 0.3f)
             == Approx (-0.0222832356).margin (1e-6));
    REQUIRE (gf.pull ((float) (std::log2 (1.5) + 0.02), 0.6f, 0.3f)
             == Approx (-0.00867539924).margin (1e-6));
    REQUIRE (gf.pull ((float) (std::log2 (5.0 / 4.0) + 0.02), 0.6f, 0.3f)
             == Approx (-0.00526913581).margin (1e-6));
    REQUIRE (gf.pull (-0.10f, 0.6f, 0.3f)
             == Approx (0.0466472767).margin (1e-6));
}

TEST_CASE ("lockRatioToGroove is exact: path ratio × 2^(detune/1200) == input", "[GrooveLock]")
{
    GrooveField gf;
    int locked = 0;

    for (int i = 0; i < 200; ++i)
    {
        const double r = 0.1 * std::pow (8.0 / 0.1, (double) i / 199.0);
        const auto res = lockRatioToGroove (gf, r, brocot::groove::kMaxDetuneCents);
        if (! res.ok)
            continue;
        ++locked;

        // The invariant that makes S5 seamless: releasing the steered
        // override lands on exactly the ratio that was sounding.
        REQUIRE (std::abs (std::log2 (lockedRatio (res)) - std::log2 (r)) < 1e-6);
        REQUIRE (std::abs (res.detuneCents) <= (float) brocot::groove::kMaxDetuneCents);
    }

    // Farey q≤16 wells are dense over [0.1, 8]: most of the grid must lock
    // (172/200 at the time of writing) — guards against a vacuous pass.
    REQUIRE (locked >= 150);
}

TEST_CASE ("lockRatioToGroove near a groove resolves its rational + small detune", "[GrooveLock]")
{
    GrooveField gf;

    // 10 cents sharp of 3/2 → the 3/2 well, detune ≈ +10c.
    const double r = 1.5 * std::exp2 (10.0 / 1200.0);
    const auto res = lockRatioToGroove (gf, r, brocot::groove::kMaxDetuneCents);
    REQUIRE (res.ok);
    int p = 0, q = 0;
    res.path.walk (p, q);
    REQUIRE (p == 3);
    REQUIRE (q == 2);
    REQUIRE (res.detuneCents == Approx (10.0f).margin (1e-3));

    // Exactly on a groove → detune ~0.
    const auto on = lockRatioToGroove (gf, 2.0, brocot::groove::kMaxDetuneCents);
    REQUIRE (on.ok);
    REQUIRE (std::abs (on.detuneCents) < 1e-3f);
}

TEST_CASE ("lockRatioToGroove refuses instead of clamping", "[GrooveLock]")
{
    GrooveField gf;

    // Non-positive ratios can't lock.
    REQUIRE_FALSE (lockRatioToGroove (gf, 0.0, 100.0).ok);
    REQUIRE_FALSE (lockRatioToGroove (gf, -1.5, 100.0).ok);

    // 120c below unison: the deep 1/1 well still dominates its shallow
    // neighbours there, but is beyond the ±100c detune budget — the result is
    // ok=false (op keeps the legacy settle), NEVER a clamped detune, which
    // would break the exactness invariant.
    REQUIRE_FALSE (lockRatioToGroove (gf, std::exp2 (-0.1), 100.0).ok);

    // A tight budget refuses what a full budget accepts.
    const double r = 1.5 * std::exp2 (10.0 / 1200.0);
    REQUIRE       (lockRatioToGroove (gf, r, 100.0).ok);
    REQUIRE_FALSE (lockRatioToGroove (gf, r, 5.0).ok);
}

TEST_CASE ("lockRatioToGroove is deterministic", "[GrooveLock]")
{
    GrooveField gf;
    for (int i = 0; i < 50; ++i)
    {
        const double r = 0.25 * std::pow (16.0, (double) i / 49.0);
        const auto a = lockRatioToGroove (gf, r, 100.0);
        const auto b = lockRatioToGroove (gf, r, 100.0);
        REQUIRE (a.ok == b.ok);
        if (a.ok)
        {
            REQUIRE (a.path.toString() == b.path.toString());
            REQUIRE (a.detuneCents == b.detuneCents);
        }
    }
}
