#include "SteerTab.h"
#include "BrocotLookAndFeel.h"
#include "../util/Colors.h"
#include "../util/Fonts.h"

namespace brocot
{

SteerTab::SteerTab (BrocotAudioProcessor& p)
    : proc (p)
{
    addAndMakeVisible (categoryLabel);
    addAndMakeVisible (categoryCombo);
    addAndMakeVisible (alphaLabel);
    addAndMakeVisible (alphaSlider);
    addAndMakeVisible (resetButton);
    addAndMakeVisible (hintLabel);

    categoryLabel.setJustificationType (juce::Justification::centredLeft);
    alphaLabel   .setJustificationType (juce::Justification::centredLeft);
    hintLabel    .setJustificationType (juce::Justification::centred);

    alphaSlider.setRange (0.0, 1.0, 0.001);
    alphaSlider.setValue (0.0);
    alphaSlider.setNumDecimalPlacesToDisplay (2);

    alphaSlider.onDragStart  = [this] { onSliderStart(); };
    alphaSlider.onValueChange = [this] { onSliderChange(); };
    alphaSlider.onDragEnd    = [this] { onSliderEnd(); };

    resetButton.onClick = [this] { onReset(); };

    rebuildCategoryList();
}

void SteerTab::rebuildCategoryList()
{
    const auto& idx = proc.getPullIndex();
    categoryCombo.clear (juce::dontSendNotification);

    if (idx.isEmpty())
    {
        // Hide the steering controls when the table isn't loaded — the
        // hint label takes over so the user knows what to do.
        categoryCombo.setEnabled (false);
        alphaSlider.setEnabled (false);
        resetButton.setEnabled (false);
        hintLabel.setText (
            "Pull index not loaded.  Run:\n"
            "    python -m phase3.build_pull_index\n"
            "from the project root.",
            juce::dontSendNotification);
        return;
    }

    int id = 1;
    for (const auto& name : idx.getCategoryNames())
        categoryCombo.addItem (name, id++);
    categoryCombo.setSelectedId (1, juce::dontSendNotification);

    hintLabel.setText (
        "Pick a category, then drag the slider.  "
        "α = 0 → your patch.  α = 1 → the category's best exemplar.",
        juce::dontSendNotification);
}

void SteerTab::onSliderStart()
{
    captured = proc.getCurrentOps();
    capturedValid = ! captured.empty();
}

void SteerTab::onSliderChange()
{
    if (! capturedValid) return;
    applyMorph ((float) alphaSlider.getValue());
}

void SteerTab::onSliderEnd()
{
    // Slider released — lock the current morphed state in. The *next*
    // grab will capture this state as the new starting point. We don't
    // need to do anything explicit here; the state already lives in
    // APVTS via applyOps().
}

void SteerTab::onReset()
{
    if (! capturedValid) return;
    proc.applyOps (captured);
    alphaSlider.setValue (0.0, juce::dontSendNotification);
}

void SteerTab::applyMorph (float alpha)
{
    const auto& idx = proc.getPullIndex();
    const auto categoryName = categoryCombo.getText();
    const auto* targetOps   = idx.getBestOps (categoryName);
    if (targetOps == nullptr) return;

    auto morphed = PullIndex::morph (captured, *targetOps, alpha);
    proc.applyOps (morphed);
}

void SteerTab::paint (juce::Graphics& g)
{
    g.fillAll (colors::background);

    // Header text — pulled into a paint pass to use the lookAndFeel's
    // serif italic, same vibe as the main editor title.
    g.setColour (colors::primaryText);
    g.setFont (fonts::serifItalic (28.0f));
    g.drawText ("steer",
                getLocalBounds().reduced (24, 16).removeFromTop (40),
                juce::Justification::topLeft);

    g.setColour (colors::accent.withAlpha (0.15f));
    g.drawHorizontalLine (62, 24.0f, (float) getWidth() - 24.0f);
}

void SteerTab::resized()
{
    auto bounds = getLocalBounds().reduced (24);
    bounds.removeFromTop (54);   // header
    bounds.removeFromTop (12);

    // Row 1: category combo
    auto catRow = bounds.removeFromTop (40);
    categoryLabel.setBounds (catRow.removeFromLeft (130));
    catRow.removeFromLeft (8);
    categoryCombo.setBounds (catRow.removeFromLeft (320).reduced (0, 6));

    bounds.removeFromTop (12);

    // Row 2: alpha slider + reset
    auto sliderRow = bounds.removeFromTop (44);
    alphaLabel.setBounds (sliderRow.removeFromLeft (130));
    sliderRow.removeFromLeft (8);
    resetButton.setBounds (sliderRow.removeFromRight (80).reduced (0, 6));
    sliderRow.removeFromRight (8);
    alphaSlider.setBounds (sliderRow.reduced (0, 6));

    bounds.removeFromTop (16);

    hintLabel.setBounds (bounds.removeFromTop (60));
}

} // namespace brocot
