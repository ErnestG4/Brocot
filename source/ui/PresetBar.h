#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../presets/PresetManager.h"

namespace brocot
{

class BrocotAudioProcessor;

/**
    Top-bar preset selector per BROCOT-SPEC.md §7. Dropdown menu listing
    factory + user presets, save / save-as buttons, current preset name.
*/
class PresetBar : public juce::Component
{
public:
    PresetBar (BrocotAudioProcessor& processor, PresetManager& manager);
    ~PresetBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void rebuildMenu();
    void rebuildModBankMenu();
    void showSaveAsDialog();
    void showSaveModBankDialog();

    BrocotAudioProcessor& proc;
    PresetManager&        manager;

    juce::ComboBox    presetCombo;
    juce::TextButton  saveBtn   { "save" };
    juce::TextButton  saveAsBtn { "save as" };

    // Mix-and-match modulator-bank selector (applies just the mod bank over the
    // current tone) + save.
    juce::ComboBox    modBankCombo;
    juce::TextButton  saveModsBtn { "save mods" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBar)
};

} // namespace brocot
