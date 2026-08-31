#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <numeric>
#include <random>

#include "audio/MapField.h"

using namespace brocot::field;
using Catch::Approx;

// ----------------------------------------------------------------------------
// parentOf — verified against brute-force enumeration over the box
// ----------------------------------------------------------------------------

namespace
{
    // Independent ground truth: minimise |a1 + a2*alpha| over nonzero
    // |a1|,|a2| <= A directly, tie-break smallest |a2| then smallest |a1|,
    // reporting |a1|/|a2|. Never consults parentOf's rounding shortcut.
    Parent parentByEnumeration (double alpha, int A)
    {
        Parent best;
        double bestErr = -1.0;
        for (int a2 = 1; a2 <= A; ++a2)
            for (int a1 = -A; a1 <= A; ++a1)
            {
                if (a1 == 0) continue;
                const double err = std::abs ((double) a1 + (double) a2 * alpha);
                if (bestErr < 0.0 || err < bestErr)
                {
                    best = { std::abs (a1), a2, err };
                    bestErr = err;
                }
            }
        return best;
    }
}

TEST_CASE ("parentOf matches brute-force enumeration over the box", "[MapField]")
{
    std::mt19937 rng (20260831);
    for (int A : { 8, 10, 14 })
    {
        std::uniform_real_distribution<double> dist (1.0 / A + 1e-6, (double) A - 1e-6);
        for (int i = 0; i < 400; ++i)
        {
            const double alpha = dist (rng);
            const auto got = parentOf (alpha, A);
            const auto truth = parentByEnumeration (alpha, A);
            INFO ("alpha = " << alpha << ", A = " << A);
            REQUIRE (got.p == truth.p);
            REQUIRE (got.q == truth.q);
            REQUIRE (got.gap == Approx (truth.gap).margin (1e-12));
            REQUIRE (std::gcd (got.p, got.q) == 1);   // convergents are reduced
        }
    }
}

TEST_CASE ("a below-horizon node is its own parent with gap zero", "[MapField]")
{
    const auto p34 = parentOf (0.75, 8);
    REQUIRE (p34.p == 3);
    REQUIRE (p34.q == 4);
    REQUIRE (p34.gap == 0.0);

    const auto unison = parentOf (1.0, 8);
    REQUIRE (unison.p == 1);
    REQUIRE (unison.q == 1);
    REQUIRE (unison.gap == 0.0);
}

TEST_CASE ("the parent is denominator-weighted, not nearest-in-ratio", "[MapField]")
{
    // MAP-FIELD.md's own exhibit: alpha = 19/18 at A = 8. The deepest in-box
    // ancestor 8/7 sits closer on the ratio axis; the parent is 1/1.
    const auto p = parentOf (19.0 / 18.0, 8);
    REQUIRE (p.p == 1);
    REQUIRE (p.q == 1);
    REQUIRE (p.gap == Approx (1.0 / 18.0).margin (1e-12));

    // And the separations the document prints, at f_c·r1 = 220 Hz.
    REQUIRE (separationHz (p, 220.0) == Approx (12.2).margin (0.05));
}

TEST_CASE ("basin boundaries sit on mediants; ties take the smaller q", "[MapField]")
{
    // 9/8 is the mediant between 1/1 and 8/7 (A = 8).
    const double mediant = 9.0 / 8.0;
    REQUIRE (parentOf (mediant - 1e-6, 8).q == 1);   // 1/1 side
    REQUIRE (parentOf (mediant + 1e-6, 8).q == 7);   // 8/7 side

    const auto tie = parentOf (mediant, 8);           // dead centre
    REQUIRE (tie.p == 1);
    REQUIRE (tie.q == 1);
}

TEST_CASE ("outside (1/A, A) the parent degenerates instead of guessing", "[MapField]")
{
    REQUIRE (! parentOf (0.10, 8).valid());     // below 1/8
    REQUIRE (! parentOf (9.00, 8).valid());     // above 8
    REQUIRE (! parentOf (1.00, 0).valid());     // no box at all
    REQUIRE (parentOf (0.75, 8).valid());
}

// ----------------------------------------------------------------------------
// Beat-rate targeting
// ----------------------------------------------------------------------------

TEST_CASE ("solveBeat round-trips through parentOf to 1e-9", "[MapField]")
{
    // Mirrors brocot_beat_target's round-trip: for every accepted side, the
    // solved alpha's parent — recomputed independently — is the requested
    // parent, and its separation is the requested delta.
    const double fcr1 = 220.0, I = 0.9;
    const int A = 2 * brocot::horizon::orderBound (I);

    const Parent parents[] = { { 1, 1, 0 }, { 3, 4, 0 }, { 4, 3, 0 },
                               { 8, 7, 0 }, { 7, 6, 0 } };
    int accepted = 0;
    for (const auto& par : parents)
        for (double delta : { 0.5, 3.0, 12.0, 20.0 })
        {
            const auto sol = solveBeat (par, delta, fcr1, I);
            for (const auto& [ok, alpha] : { std::pair { sol.upOk, sol.up },
                                             std::pair { sol.downOk, sol.down } })
            {
                if (! ok) continue;
                ++accepted;
                const auto back = parentOf (alpha, A);
                REQUIRE (back.p == par.p);
                REQUIRE (back.q == par.q);
                REQUIRE (separationHz (back, fcr1) == Approx (delta).margin (1e-9));
            }
        }
    REQUIRE (accepted > 20);   // the grid is mostly solvable at 220 Hz
}

TEST_CASE ("basin refusal fires where the document says it does", "[MapField]")
{
    // At f_c·r1 = 110 Hz, +12 Hz against 8/7 lands under 7/6 — refused; the
    // −12 Hz side stays in the basin. Refuse, don't clamp.
    const auto sol = solveBeat ({ 8, 7, 0.0 }, 12.0, 110.0, 0.9);
    REQUIRE (! sol.upOk);
    REQUIRE (sol.downOk);
    REQUIRE (parentOf (sol.up, 8).p == 7);    // where the + side actually lands
    REQUIRE (parentOf (sol.up, 8).q == 6);
    REQUIRE (separationHz (parentOf (sol.down, 8), 110.0) == Approx (12.0).margin (1e-9));
}

TEST_CASE ("an over-large target refuses on both sides", "[MapField]")
{
    // 200 Hz against 1/1 at 220 Hz: up lands under 2/1, down leaves (1/A, A).
    const auto sol = solveBeat ({ 1, 1, 0.0 }, 200.0, 220.0, 0.9);
    REQUIRE (! sol.upOk);
    REQUIRE (! sol.downOk);
}

TEST_CASE ("solveBeat refuses nonsense inputs", "[MapField]")
{
    REQUIRE (! solveBeat ({ 0, 0, 0.0 }, 3.0, 220.0, 0.9).upOk);     // degenerate parent
    REQUIRE (! solveBeat ({ 1, 1, 0.0 }, -1.0, 220.0, 0.9).upOk);    // negative delta
    REQUIRE (! solveBeat ({ 1, 1, 0.0 }, 3.0, 0.0, 0.9).upOk);       // no reference freq
}

// ----------------------------------------------------------------------------
// Regime bands
// ----------------------------------------------------------------------------

TEST_CASE ("regime bands follow the published table", "[MapField]")
{
    REQUIRE (bandOf (0.0)  == Band::Fuses);
    REQUIRE (bandOf (3.0)  == Band::SlowBeat);
    REQUIRE (bandOf (8.0)  == Band::Rough);
    REQUIRE (bandOf (12.0) == Band::Rough);
    REQUIRE (bandOf (20.0) == Band::Separate);
    REQUIRE (bandOf (30.0) == Band::Separate);
}
