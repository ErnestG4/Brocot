#include "PresetBar.h"
#include "../PluginProcessor.h"
#include "../util/Colors.h"
#include "../util/Fonts.h"

namespace brocot
{

PresetBar::PresetBar (BrocotAudioProcessor& processor, PresetManager& mgr)
    : proc (processor), manager (mgr)
{
    addAndMakeVisible (presetCombo);
    presetCombo.setTextWhenNothingSelected ("(init)");
    presetCombo.onChange = [this]
    {
        const int id = presetCombo.getSelectedId();
        if (id <= 0) return;
        const int idx = id - 1;
        const auto& entries = manager.getEntries();
        if (idx >= 0 && idx < (int) entries.size())
        {
            juce::String err;
            if (! manager.load (entries[(size_t) idx].file, &err))
                juce::AlertWindow::showMessageBoxAsync (
                    juce::AlertWindow::WarningIcon, "Preset load failed", err);
        }
    };

    addAndMakeVisible (saveBtn);
    saveBtn.onClick = [this] { showSaveAsDialog(); };

    addAndMakeVisible (saveAsBtn);
    saveAsBtn.onClick = [this] { showSaveAsDialog(); };

    rebuildMenu();
}

PresetBar::~PresetBar() = default;

void PresetBar::rebuildMenu()
{
    presetCombo.clear (juce::dontSendNotification);
    const auto& entries = manager.getEntries();

    bool inFactorySection = false;
    bool inUserSection = false;
    int id = 1;
    for (const auto& e : entries)
    {
        if (e.factory && ! inFactorySection)
        {
            presetCombo.addSectionHeading ("factory");
            inFactorySection = true;
        }
        if (! e.factory && ! inUserSection)
        {
            presetCombo.addSectionHeading ("user");
            inUserSection = true;
        }
        presetCombo.addItem (e.name, id++);
    }

    presetCombo.setText (manager.getCurrentName(), juce::dontSendNotification);
}

void PresetBar::showSaveAsDialog()
{
    auto dir = manager.getUserDirectory();
    auto chooser = std::make_shared<juce::FileChooser> (
        "Save preset", dir.getChildFile (manager.getCurrentName() + ".brocot"),
        "*.brocot");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                          | juce::FileBrowserComponent::canSelectFiles
                          | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, chooser] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File()) return;
            if (file.getFileExtension().isEmpty())
                file = file.withFileExtension (".brocot");

            const auto name = file.getFileNameWithoutExtension();
            juce::String err;
            if (manager.save (file, name, &err))
            {
                manager.rescan();
                rebuildMenu();
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::AlertWindow::WarningIcon, "Save failed", err);
            }
        });
}

void PresetBar::paint (juce::Graphics&) {}

void PresetBar::resized()
{
    auto r = getLocalBounds();
    saveAsBtn.setBounds   (r.removeFromRight (70).reduced (2, 4));
    saveBtn.setBounds     (r.removeFromRight (60).reduced (2, 4));
    r.removeFromRight (4);
    presetCombo.setBounds (r.removeFromRight (200).reduced (2, 4));
}

} // namespace brocot
