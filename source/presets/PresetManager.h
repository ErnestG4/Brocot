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
        juce::File   file;
        bool         factory { false };
    };

    explicit PresetManager (BrocotAudioProcessor& processor);

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
