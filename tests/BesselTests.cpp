#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "audio/Bessel.h"

namespace lm = brocot::bessel;

TEST_CASE ("Landmark table has the expected spec values", "[Bessel]")
{
    REQUIRE (lm::kLandmarks.size() == 10);

    // The spec §10 / bessel_landmarks.json constants — drift here means the
    // C++ table is out of sync with the JSON source of truth.
    REQUIRE (lm::kLandmarks[0].value == Catch::Approx (1.8412f));
    REQUIRE (lm::kLandmarks[1].value == Catch::Approx (2.4048f));
    REQUIRE (lm::kLandmarks[2].value == Catch::Approx (3.0542f));
    REQUIRE (lm::kLandmarks[3].value == Catch::Approx (3.8317f));
    REQUIRE (lm::kLandmarks[8].value == Catch::Approx (6.4156f));
}

TEST_CASE ("nearest returns the closest landmark within tolerance", "[Bessel]")
{
    {
        // Exactly on the J₁ peak.
        auto* l = lm::nearest (1.8412f);
        REQUIRE (l != nullptr);
        REQUIRE (l->n == 1);
        REQUIRE (l->kind == lm::Landmark::Kind::Peak);
    }
    {
        // Just inside tolerance of the J₀ null.
        auto* l = lm::nearest (2.40f);
        REQUIRE (l != nullptr);
        REQUIRE (l->n == 0);
        REQUIRE (l->kind == lm::Landmark::Kind::Zero);
    }
}

TEST_CASE ("nearest returns nullptr outside tolerance", "[Bessel]")
{
    // 0.5 is well clear of every landmark (closest is J₁ peak at 1.84).
    REQUIRE (lm::nearest (0.5f) == nullptr);
    REQUIRE (lm::nearest (0.5f, 0.05f) == nullptr);
    // Tight tolerance around a value between two landmarks.
    REQUIRE (lm::nearest (2.7f, 0.05f) == nullptr);
}

TEST_CASE ("Regime labels match the spec's four bands", "[Bessel]")
{
    REQUIRE (lm::regimeLabel (0.1f)  == "tongue edge");
    REQUIRE (lm::regimeLabel (0.45f) == "tongue edge");
    REQUIRE (lm::regimeLabel (1.0f)  == "near-locked");
    REQUIRE (lm::regimeLabel (2.5f)  == "near-locked");
    REQUIRE (lm::regimeLabel (3.5f)  == "rich sidebands");
    REQUIRE (lm::regimeLabel (4.8f)  == "rich sidebands");
    REQUIRE (lm::regimeLabel (5.5f)  == "clangy");
    REQUIRE (lm::regimeLabel (7.0f)  == "clangy");
}

TEST_CASE ("Major flag aligns with n ≤ 2", "[Bessel]")
{
    for (const auto& l : lm::kLandmarks)
        REQUIRE (l.major == (l.n <= 2));
}
