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
    addAndMakeVisible (envelopePanel);
    addAndMakeVisible (filterPanel);
    addAndMakeVisible (mod0Panel);
    addAndMakeVisible (mod1Panel);

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

    // Re-anchor whenever the user picks a different shape target: the
    // slider's α=0 should mean "where I was when I picked this
    // category", α=1 should mean "the category's best exemplar". If
    // we didn't re-anchor here, switching categories would morph from
    // the *previous* category's morph result.
    categoryCombo.onChange = [this]
    {
        captured = proc.getCurrentOps();
        capturedValid = ! captured.empty();
        alphaSlider.setValue (0.0, juce::dontSendNotification);
    };

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
    // First-touch fallback: if we never captured (e.g., user dragged
    // the slider before changing categories), snapshot the current
    // state now. After the first capture, we leave it alone — the
    // category combo's onChange is the canonical re-anchor point so
    // dragging the slider back to α=0 restores the exact pre-shape
    // state for *this* category, no matter how many times the user
    // drags within the session.
    if (! capturedValid)
    {
        captured = proc.getCurrentOps();
        capturedValid = ! captured.empty();
    }
}

void SteerTab::onSliderChange()
{
    if (! capturedValid) return;
    applyMorph ((float) alphaSlider.getValue());
}

void SteerTab::onSliderEnd()
{
    // Intentionally a no-op. We don't update the captured state on
    // release — it stays anchored at "what was loaded / what the
    // category-change just snapshotted". Dragging the slider back to
    // 0 should always undo the shape cleanly. Picking a new category
    // is the explicit re-anchor moment.
}

void SteerTab::onReset()
{
    if (! capturedValid) return;
    proc.applyOps (captured);
    alphaSlider.setValue (0.0, juce::dontSendNotification);
    // Reset is also a re-anchor: from now on α=0 means *this* state.
    captured = proc.getCurrentOps();
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

    // Row 1: category combo + alpha slider on the same line — saves
    // vertical real estate so the four panels below have room.
    auto controlRow = bounds.removeFromTop (40);
    categoryLabel.setBounds (controlRow.removeFromLeft (130));
    controlRow.removeFromLeft (8);
    categoryCombo.setBounds (controlRow.removeFromLeft (240).reduced (0, 6));
    controlRow.removeFromLeft (16);

    resetButton.setBounds (controlRow.removeFromRight (80).reduced (0, 6));
    controlRow.removeFromRight (8);
    alphaLabel.setBounds (controlRow.removeFromLeft (70));
    alphaSlider.setBounds (controlRow.reduced (0, 6));

    bounds.removeFromTop (8);
    hintLabel.setBounds (bounds.removeFromTop (32));
    bounds.removeFromTop (8);

    // Four-panel row: envelope | filter | mod 0 | mod 1
    auto panelRow = bounds;
    const int cols = 4, gap = 8;
    const int w    = (panelRow.getWidth() - gap * (cols - 1)) / cols;
    int x = panelRow.getX();
    juce::Component* panels[] { &envelopePanel, &filterPanel, &mod0Panel, &mod1Panel };
    for (auto* p : panels)
    {
        p->setBounds (x, panelRow.getY(), w, panelRow.getHeight());
        x += w + gap;
    }
}

} // namespace brocot
