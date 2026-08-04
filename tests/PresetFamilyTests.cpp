#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

#include "presets/PresetManager.h"

using brocot::PresetManager;

// Only PresetManager's header-inline statics are exercised here — the class
// itself never links (PresetManager.cpp drags in the full processor).

namespace
{
    juce::var makeDict()
    {
        return juce::JSON::parse (R"({
            "01_pure.brocot":       { "family": "Prime 13", "dist": 0.2274,  "n_ops": 0 },
            "curated/foo.brocot":   { "family": "Sawtooth", "dist": 0.76523, "n_ops": 3 }
        })");
    }

    PresetManager::Entry makeEntry (juce::StringRef name, juce::StringRef family, bool factory)
    {
        PresetManager::Entry e;
        e.name = name;
        e.family = family;
        e.factory = factory;
        e.category = factory ? "factory" : "user";
        return e;
    }
}

// ----------------------------------------------------------------------------
// familyForRelativePath — pure lookup, no filesystem.
// ----------------------------------------------------------------------------

TEST_CASE ("familyForRelativePath finds top-level and subdir keys", "[PresetFamily]")
{
    const auto dict = makeDict();
    REQUIRE (PresetManager::familyForRelativePath (dict, "01_pure.brocot") == "Prime 13");
    REQUIRE (PresetManager::familyForRelativePath (dict, "curated/foo.brocot") == "Sawtooth");
}

TEST_CASE ("familyForRelativePath normalises Windows separators", "[PresetFamily]")
{
    REQUIRE (PresetManager::familyForRelativePath (makeDict(), "curated\\foo.brocot")
             == "Sawtooth");
}

TEST_CASE ("familyForRelativePath falls back to unclassified", "[PresetFamily]")
{
    const auto dict = makeDict();
    REQUIRE (PresetManager::familyForRelativePath (dict, "nope.brocot") == "unclassified");
    REQUIRE (PresetManager::familyForRelativePath (juce::var(), "01_pure.brocot")
             == "unclassified");   // missing/unparsed file → void var
}

// ----------------------------------------------------------------------------
// entryLess — the menu ordering PresetBar's heading pass depends on.
// ----------------------------------------------------------------------------

TEST_CASE ("entryLess groups factory entries by family, alphabetical within", "[PresetFamily]")
{
    std::vector<PresetManager::Entry> entries {
        makeEntry ("zeta",  "",         false),
        makeEntry ("bell2", "Sawtooth", true),
        makeEntry ("organ", "Organ 1/3/5", true),
        makeEntry ("alpha", "",         false),
        makeEntry ("bell1", "Sawtooth", true),
        makeEntry ("pure",  "Prime 13", true),
    };
    std::sort (entries.begin(), entries.end(), PresetManager::entryLess);

    // Factory first, families contiguous and alphabetical, names alphabetical
    // within a family; user entries follow sorted by name alone.
    REQUIRE (entries[0].name == "organ");
    REQUIRE (entries[1].name == "pure");
    REQUIRE (entries[2].name == "bell1");
    REQUIRE (entries[3].name == "bell2");
    REQUIRE (entries[4].name == "alpha");
    REQUIRE (entries[5].name == "zeta");

    for (size_t i = 0; i < 4; ++i)
        REQUIRE (entries[i].factory);
}

// ----------------------------------------------------------------------------
// Shipped preset_families.json — same anchor the production lookup uses
// (the presets dir containing factory/), reached via BROCOT_SOURCE_DIR.
// ----------------------------------------------------------------------------

TEST_CASE ("Shipped preset_families.json parses and resolves its own keys", "[PresetFamily]")
{
    const juce::File file (juce::String (BROCOT_SOURCE_DIR)
                               + "/resources/presets/preset_families.json");
    if (! file.existsAsFile())
        SKIP ("resources/presets/preset_families.json not present");

    const auto dict = juce::JSON::parse (file);
    auto* obj = dict.getDynamicObject();
    REQUIRE (obj != nullptr);
    REQUIRE (obj->getProperties().size() > 0);

    for (const auto& prop : obj->getProperties())
    {
        const auto family =
            PresetManager::familyForRelativePath (dict, prop.name.toString());
        REQUIRE (family.isNotEmpty());
        REQUIRE (family != "unclassified");   // every stored entry must carry a family
    }
}
