#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace brocot
{

class BrocotAudioProcessor;

/**
    Discovers, loads, and saves `.brocot` JSON preset files. Factory presets
    ship under resources/presets/factory/ (Slice 14 populates these); user
    presets live in the OS application-data directory per spec §9.
*/
class PresetManager
{
public:
    struct Entry
    {
        juce::String name;
        juce::String category;
        juce::String family;      // measured family label; "unclassified" if unknown
        juce::File   file;
        bool         factory { false };
    };

    explicit PresetManager (BrocotAudioProcessor& processor);

    // Looks up the measured family for a preset path relative to the factory
    // dir in the parsed preset_families.json dict. Backslashes are normalised
    // to forward slashes (the JSON is generated with '/' separators); a
    // non-object dict, missing key, or empty family → "unclassified".
    // Header-inline so BrocotTests can exercise it without linking
    // PresetManager.cpp (which pulls in the full processor).
    static juce::String familyForRelativePath (const juce::var& dict, juce::String relPath)
    {
        relPath = relPath.replaceCharacter ('\\', '/');
        if (auto* obj = dict.getDynamicObject())
            if (auto* e = obj->getProperty (relPath).getDynamicObject())
                if (auto fam = e->getProperty ("family").toString(); fam.isNotEmpty())
                    return fam;
        return "unclassified";
    }

    // Menu order: factory before user; factory grouped by family then name,
    // user by name alone. PresetBar relies on this ordering — combo item ids
    // map 1:1 onto entry indices, so grouping must happen here, not there.
    static bool entryLess (const Entry& a, const Entry& b)
    {
        if (a.factory != b.factory) return a.factory;   // factory first
        if (a.factory)
            if (const int c = a.family.compareIgnoreCase (b.family); c != 0)
                return c < 0;
        return a.name.compareIgnoreCase (b.name) < 0;
    }

    void rescan();
    const std::vector<Entry>& getEntries() const noexcept { return entries; }

    bool load (const juce::File& file, juce::String* outError = nullptr);
    bool save (const juce::File& file, const juce::String& name,
               juce::String* outError = nullptr) const;

    // Mix-and-match modulator banks (.brocotmod) — just the mod bank, applied
    // over the current tone. Discovered alongside presets; listed separately.
    const std::vector<Entry>& getModBanks() const noexcept { return modBanks; }
    bool loadModBank (const juce::File& file, juce::String* outError = nullptr);
    bool saveModBank (const juce::File& file, const juce::String& name,
                      juce::String* outError = nullptr) const;
    juce::File getModBankUserDirectory() const;

    juce::File getUserDirectory() const;
    juce::File getFactoryDirectory() const;

    const juce::String& getCurrentName() const noexcept { return currentName; }

private:
    BrocotAudioProcessor& proc;
    juce::String          currentName { "init" };
    std::vector<Entry>    entries;
    std::vector<Entry>    modBanks;
};

} // namespace brocot
