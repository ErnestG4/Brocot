#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <juce_core/juce_core.h>
#include <algorithm>

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

TEST_CASE ("J_m evaluates against known Bessel values", "[Bessel]")
{
    // Reference values (Abramowitz & Stegun tables).
    REQUIRE (lm::J (0, 0.0f) == Catch::Approx (1.0f));
    REQUIRE (lm::J (1, 0.0f) == Catch::Approx (0.0f));
    REQUIRE (lm::J (5, 0.0f) == Catch::Approx (0.0f));

    REQUIRE (lm::J (0, 1.0f) == Catch::Approx (0.7651977).margin (1e-5));
    REQUIRE (lm::J (1, 1.0f) == Catch::Approx (0.4400506).margin (1e-5));
    REQUIRE (lm::J (2, 2.0f) == Catch::Approx (0.3528340).margin (1e-5));
    REQUIRE (lm::J (0, 5.0f) == Catch::Approx (-0.1775968).margin (1e-5));
    REQUIRE (lm::J (3, 5.0f) == Catch::Approx (0.3648312).margin (1e-5));
    REQUIRE (lm::J (1, 8.0f) == Catch::Approx (0.2346364).margin (1e-5));
}

TEST_CASE ("J_m vanishes / peaks at the landmark values", "[Bessel]")
{
    // Each Zero landmark is a root of J_n; each Peak is an extremum (J' = 0).
    for (const auto& l : lm::kLandmarks)
    {
        const float v = lm::J (l.n, l.value);
        if (l.kind == lm::Landmark::Kind::Zero)
            REQUIRE (std::abs (v) < 1e-3f);
        else
        {
            // At a peak the neighbouring values are smaller in magnitude.
            const float a = std::abs (lm::J (l.n, l.value - 0.02f));
            const float b = std::abs (lm::J (l.n, l.value + 0.02f));
            REQUIRE (std::abs (v) >= a - 1e-4f);
            REQUIRE (std::abs (v) >= b - 1e-4f);
        }
    }
}

TEST_CASE ("J_{-m} = (-1)^m J_m", "[Bessel]")
{
    REQUIRE (lm::J (-1, 3.0f) == Catch::Approx (-lm::J (1, 3.0f)));
    REQUIRE (lm::J (-2, 3.0f) == Catch::Approx ( lm::J (2, 3.0f)));
    REQUIRE (lm::J (-3, 4.5f) == Catch::Approx (-lm::J (3, 4.5f)));
}

TEST_CASE ("kLandmarks matches the shipped bessel_landmarks.json", "[Bessel]")
{
    // The header names resources/tables/bessel_landmarks.json as the source
    // of truth; this is the sync test it promises. Every JSON peak/zero must
    // appear in kLandmarks with the same value/kind/n, and the counts must
    // match so neither side can grow without the other.
    const juce::File file (juce::String (BROCOT_SOURCE_DIR)
                               + "/resources/tables/bessel_landmarks.json");
    REQUIRE (file.existsAsFile());

    const auto json = juce::JSON::parse (file);
    auto* root = json.getDynamicObject();
    REQUIRE (root != nullptr);

    int checked = 0;
    for (const auto& [prop, kind] :
         { std::pair { "peaks", lm::Landmark::Kind::Peak },
           std::pair { "zeros", lm::Landmark::Kind::Zero } })
    {
        auto* arr = root->getProperty (prop).getArray();
        REQUIRE (arr != nullptr);
        for (const auto& entry : *arr)
        {
            auto* eo = entry.getDynamicObject();
            REQUIRE (eo != nullptr);
            const float value = (float) (double) eo->getProperty ("I");
            const int   n     = (int) eo->getProperty ("n");

            const bool found = std::any_of (
                lm::kLandmarks.begin(), lm::kLandmarks.end(),
                [&] (const lm::Landmark& lm)
                {
                    return lm.kind == kind && lm.n == n
                        && std::abs (lm.value - value) < 1e-4f;
                });
            INFO (prop << " entry I=" << value << " n=" << n);
            CHECK (found);
            ++checked;
        }
    }
    CHECK (checked == (int) lm::kLandmarks.size());
}
