#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "util/FieldFrame.h"

using namespace brocot::fieldframe;
using brocot::field::Band;
using Catch::Approx;

TEST_CASE ("referenceIndex picks the simplest base rational", "[FieldFrame]")
{
    const std::vector<OpState> ops {
        { 4, 3, 0.0, 0.9 },
        { 2, 1, 0.0, 0.9 },   // q = 1 → the anchor
        { 3, 1, 0.0, 0.9 },   // q = 1 too, but larger p
        { 0, 0, 0.0, 0.9 },   // invalid, must be skipped
    };
    REQUIRE (referenceIndex (ops) == 1);
    REQUIRE (referenceIndex ({}) == -1);
}

TEST_CASE ("the same 10-cent detune is a waver on 1/1 and a buzz on 7/5",
           "[FieldFrame]")
{
    // MAP-FIELD.md item 2's own sentence, as numbers. Carrier 220 Hz,
    // reference at unison, both ops at I = 0.9.
    const OpState ref { 1, 1, 0.0, 0.9 };

    const auto onUnison = pairReadout (ref, { 1, 1, 10.0, 0.9 }, 220.0);
    REQUIRE (onUnison.valid);
    REQUIRE (onUnison.parent.p == 1);
    REQUIRE (onUnison.parent.q == 1);
    REQUIRE (onUnison.sepHz == Approx (1.27).margin (0.01));
    REQUIRE (onUnison.band == Band::SlowBeat);

    const auto onSeven = pairReadout (ref, { 7, 5, 10.0, 0.9 }, 220.0);
    REQUIRE (onSeven.valid);
    REQUIRE (onSeven.parent.p == 7);
    REQUIRE (onSeven.parent.q == 5);
    REQUIRE (onSeven.sepHz == Approx (8.92).margin (0.01));
    REQUIRE (onSeven.band == Band::Rough);
}

TEST_CASE ("an exact pair fuses; separation is zero", "[FieldFrame]")
{
    const auto r = pairReadout ({ 1, 1, 0.0, 0.9 }, { 4, 3, 0.0, 0.9 }, 220.0);
    REQUIRE (r.valid);
    REQUIRE (r.parent.p == 4);
    REQUIRE (r.parent.q == 3);
    REQUIRE (r.sepHz == 0.0);
    REQUIRE (r.band == Band::Fuses);
}

TEST_CASE ("solveBeatCents round-trips through pairReadout", "[FieldFrame]")
{
    const OpState ref   { 1, 1, 0.0, 0.9 };
    const OpState other { 4, 3, 0.0, 0.9 };

    const auto sol = solveBeatCents (ref, other, 220.0, 3.0);
    REQUIRE (sol.upOk);
    REQUIRE (sol.downOk);

    for (double cents : { sol.upCents, sol.downCents })
    {
        OpState detuned = other;
        detuned.detuneCents = cents;
        const auto back = pairReadout (ref, detuned, 220.0);
        REQUIRE (back.valid);
        REQUIRE (back.parent.p == 4);
        REQUIRE (back.parent.q == 3);
        REQUIRE (back.sepHz == Approx (3.0).margin (1e-9));
    }
}

TEST_CASE ("basin refusal carries into cents space", "[FieldFrame]")
{
    // The document's own refusal: at f_c·r1 = 110 Hz, +12 Hz against 8/7
    // lands under 7/6. The up side must refuse; the down side must solve.
    const OpState ref   { 1, 1, 0.0, 0.9 };
    const OpState other { 8, 7, 0.0, 0.9 };

    const auto sol = solveBeatCents (ref, other, 110.0, 12.0);
    REQUIRE (! sol.upOk);
    REQUIRE (sol.downOk);

    OpState detuned = other;
    detuned.detuneCents = sol.downCents;
    const auto back = pairReadout (ref, detuned, 110.0);
    REQUIRE (back.parent.p == 8);
    REQUIRE (back.parent.q == 7);
    REQUIRE (back.sepHz == Approx (12.0).margin (1e-9));
}

TEST_CASE ("degenerate frames refuse instead of guessing", "[FieldFrame]")
{
    // Zero-depth reference: no box at all.
    REQUIRE (! pairReadout ({ 1, 1, 0.0, 0.0 }, { 4, 3, 0.0, 0.9 }, 220.0).valid);
    // Invalid rational.
    REQUIRE (! pairReadout ({ 1, 1, 0.0, 0.9 }, { 0, 0, 0.0, 0.9 }, 220.0).valid);
    // No carrier.
    REQUIRE (! pairReadout ({ 1, 1, 0.0, 0.9 }, { 4, 3, 0.0, 0.9 }, 0.0).valid);
    // And the solver inherits the refusal.
    REQUIRE (! solveBeatCents ({ 1, 1, 0.0, 0.0 }, { 4, 3, 0.0, 0.9 }, 220.0, 3.0).upOk);
}
