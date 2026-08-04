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

    // Mod-bank selector: applies just the modulator bank over the current tone.
    addAndMakeVisible (modBankCombo);
    modBankCombo.setTextWhenNothingSelected ("(mods)");
    modBankCombo.onChange = [this]
    {
        const int id = modBankCombo.getSelectedId();
        if (id <= 0) return;
        const auto& banks = manager.getModBanks();
        const int idx = id - 1;
        if (idx >= 0 && idx < (int) banks.size())
        {
            juce::String err;
            if (! manager.loadModBank (banks[(size_t) idx].file, &err))
                juce::AlertWindow::showMessageBoxAsync (
                    juce::AlertWindow::WarningIcon, "Mod bank load failed", err);
        }
    };

    addAndMakeVisible (saveModsBtn);
    saveModsBtn.onClick = [this] { showSaveModBankDialog(); };

    rebuildMenu();
    rebuildModBankMenu();
}

PresetBar::~PresetBar() = default;

void PresetBar::rebuildMenu()
{
    presetCombo.clear (juce::dontSendNotification);
    const auto& entries = manager.getEntries();

    // Item id must stay entries-index + 1 (onChange relies on it), so this
    // loop only inserts headings — grouping order comes from the manager's
    // (family, name) sort, never from reordering here.
    bool inFactorySection = false;
    bool inUserSection = false;
    juce::String lastFamily;
    int id = 1;
    for (const auto& e : entries)
    {
        if (e.factory && ! inFactorySection)
        {
            presetCombo.addSectionHeading ("factory");
            inFactorySection = true;
        }
        if (e.factory && e.family != lastFamily)
        {
            presetCombo.addSectionHeading (e.family);
            lastFamily = e.family;
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

void PresetBar::rebuildModBankMenu()
{
    modBankCombo.clear (juce::dontSendNotification);
    bool inFactory = false, inUser = false;
    int id = 1;
    for (const auto& e : manager.getModBanks())
    {
        if (e.factory && ! inFactory) { modBankCombo.addSectionHeading ("factory"); inFactory = true; }
        if (! e.factory && ! inUser)  { modBankCombo.addSectionHeading ("user");    inUser = true; }
        modBankCombo.addItem (e.name, id++);
    }
}

void PresetBar::showSaveModBankDialog()
{
    auto dir = manager.getModBankUserDirectory();
    auto chooser = std::make_shared<juce::FileChooser> (
        "Save modulator bank", dir.getChildFile ("mods.brocotmod"), "*.brocotmod");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                          | juce::FileBrowserComponent::canSelectFiles
                          | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, chooser] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File()) return;
            if (file.getFileExtension().isEmpty())
                file = file.withFileExtension (".brocotmod");

            juce::String err;
            if (manager.saveModBank (file, file.getFileNameWithoutExtension(), &err))
            {
                manager.rescan();
                rebuildModBankMenu();
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::AlertWindow::WarningIcon, "Save mods failed", err);
            }
        });
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
    // Left group: patch preset + save / save-as.
    presetCombo.setBounds (r.removeFromLeft (180).reduced (2, 4));
    saveBtn.setBounds     (r.removeFromLeft (52).reduced (2, 4));
    saveAsBtn.setBounds   (r.removeFromLeft (62).reduced (2, 4));
    // Right group: mod-bank chooser + save mods.
    saveModsBtn.setBounds (r.removeFromRight (74).reduced (2, 4));
    modBankCombo.setBounds (r.removeFromRight (160).reduced (2, 4));
}

} // namespace brocot
