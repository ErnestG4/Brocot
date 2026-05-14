#include "SteerTab.h"
#include "BrocotLookAndFeel.h"
#include "../util/Colors.h"
#include "../util/Fonts.h"

namespace brocot
{

// The shape categories that get a − / name / + row in the left column.
// Texture is handled separately because its row carries an op-count
// spinner; the rest are 1:1 with the pull-index's category names.
static const std::vector<juce::String> kShapeCategories = {
    "Sawtooth", "Square",   "Triangle",
    "Pulse-PW 4", "Organ 1/3/5", "Bell (stretched)",
    "Prime 2", "Prime 3", "Prime 5", "Prime 7", "Prime 11", "Prime 13",
};

SteerTab::SteerTab (BrocotAudioProcessor& p)
    : proc (p)
{
    addAndMakeVisible (envelopePanel);
    addAndMakeVisible (filterPanel);
    addAndMakeVisible (mod0Panel);
    addAndMakeVisible (mod1Panel);

    addAndMakeVisible (resetButton);
    resetButton.onClick = [this] { onReset(); };
    resetButton.setTooltip ("Restore the captured state and re-anchor home to it.");

    addAndMakeVisible (hintLabel);
    hintLabel.setJustificationType (juce::Justification::centredLeft);
    hintLabel.setText ("+ pushes the patch toward a shape  ·  − pulls it back toward home",
                       juce::dontSendNotification);

    buildRows();

    // Texture row controls.
    addAndMakeVisible (textureMinusBtn);
    addAndMakeVisible (texturePlusBtn);
    addAndMakeVisible (textureLabel);
    addAndMakeVisible (textureOpLabel);
    addAndMakeVisible (textureOpCount);

    textureLabel.setJustificationType (juce::Justification::centredLeft);
    textureOpLabel.setJustificationType (juce::Justification::centredRight);

    textureOpCount.setRange (4.0, 16.0, 1.0);
    textureOpCount.setValue (8.0);
    textureOpCount.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
    textureOpCount.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 28, 22);
    textureOpCount.setTooltip ("Op count for the Textured-Smooth target (4..16).");

    textureMinusBtn.onClick = [this] { onMinus(); };
    texturePlusBtn .onClick = [this]
    {
        const int n = (int) textureOpCount.getValue();
        onPlus (juce::String ("Textured-Smooth · ") + juce::String (n) + " ops");
    };
    textureMinusBtn.setTooltip ("Pull back toward home.");
    texturePlusBtn .setTooltip ("Nudge toward the Textured-Smooth · N ops shape.");

    anchorHome();

    // If the pull index isn't loaded, the whole tab gets disabled.
    // Buttons still render so the layout doesn't shift, but they're
    // inert and the hint tells the user how to fix it.
    if (proc.getPullIndex().isEmpty())
    {
        hintLabel.setText ("Pull index not loaded — run "
                            "`python -m phase3.build_pull_index`",
                            juce::dontSendNotification);
        resetButton.setEnabled (false);
        textureMinusBtn.setEnabled (false);
        texturePlusBtn .setEnabled (false);
        for (auto& r : familyRows)
        {
            r->minusBtn.setEnabled (false);
            r->plusBtn .setEnabled (false);
        }
    }
}

void SteerTab::buildRows()
{
    familyRows.clear();
    for (const auto& cat : kShapeCategories)
    {
        auto row = std::make_unique<FamilyRow>();
        row->category = cat;

        addAndMakeVisible (row->minusBtn);
        addAndMakeVisible (row->plusBtn);
        addAndMakeVisible (row->label);

        row->label.setText (cat, juce::dontSendNotification);
        row->label.setJustificationType (juce::Justification::centredLeft);

        // Bind each plus lambda to its own category string by value
        // (shadowing the member's `captured` ops vector — rename to
        // avoid the warning).
        const auto catName = cat;
        row->plusBtn.onClick  = [this, catName] { onPlus (catName); };
        row->minusBtn.onClick = [this] { onMinus(); };

        row->plusBtn .setTooltip ("Nudge toward " + cat + ".");
        row->minusBtn.setTooltip ("Pull back toward home.");

        familyRows.push_back (std::move (row));
    }
}

void SteerTab::anchorHome()
{
    captured = proc.getCurrentOps();
    capturedValid = ! captured.empty();
}

void SteerTab::onPlus (const juce::String& category)
{
    const auto* targetOps = proc.getPullIndex().getBestOps (category);
    if (targetOps == nullptr) return;

    auto cur = proc.getCurrentOps();
    auto nudged = PullIndex::morph (cur, *targetOps, kNudgeStep);
    proc.applyOps (nudged);
}

void SteerTab::onMinus()
{
    if (! capturedValid) anchorHome();
    if (! capturedValid) return;

    auto cur = proc.getCurrentOps();
    auto nudged = PullIndex::morph (cur, captured, kNudgeStep);
    proc.applyOps (nudged);
}

void SteerTab::onReset()
{
    if (! capturedValid) anchorHome();
    if (! capturedValid) return;

    proc.applyOps (captured);
    // Re-anchor — Reset is also "from now on this state is home".
    anchorHome();
}

void SteerTab::paint (juce::Graphics& g)
{
    g.fillAll (colors::background);

    g.setColour (colors::primaryText);
    g.setFont (fonts::serifItalic (24.0f));
    g.drawText ("steer",
                getLocalBounds().reduced (24, 14).removeFromTop (36),
                juce::Justification::topLeft);

    g.setColour (colors::accent.withAlpha (0.15f));
    g.drawHorizontalLine (54, 24.0f, (float) getWidth() - 24.0f);
}

void SteerTab::resized()
{
    auto bounds = getLocalBounds().reduced (16);
    bounds.removeFromTop (48);   // header band

    // Header row: hint label left + Reset right.
    auto headerRow = bounds.removeFromTop (32);
    resetButton.setBounds (headerRow.removeFromRight (80).reduced (0, 4));
    headerRow.removeFromRight (8);
    hintLabel.setBounds (headerRow);
    bounds.removeFromTop (8);

    // Two columns: shape buttons left, panels right.
    const int rightW = juce::jlimit (380, 540, bounds.getWidth() * 4 / 10);
    auto leftCol  = bounds.removeFromLeft (bounds.getWidth() - rightW - 12);
    bounds.removeFromLeft (12);
    auto rightCol = bounds;

    // Left column: 13 rows (12 families + 1 texture), evenly stacked.
    const int totalRows = (int) familyRows.size() + 1;
    const int rowGap = 2;
    const int rowH = juce::jlimit (24, 36,
                                   (leftCol.getHeight() - rowGap * (totalRows - 1)) / totalRows);

    auto layoutRow = [] (juce::Rectangle<int> r,
                         juce::TextButton& minus, juce::Label& label,
                         juce::TextButton& plus,
                         juce::Component* extraInline = nullptr,
                         juce::Label* extraLabel = nullptr)
    {
        const int btnW = 30;
        minus.setBounds (r.removeFromLeft (btnW).reduced (0, 2));
        r.removeFromLeft (6);
        plus .setBounds (r.removeFromRight (btnW).reduced (0, 2));
        r.removeFromRight (6);

        if (extraInline != nullptr)
        {
            const int sliderW = 80;
            extraInline->setBounds (r.removeFromRight (sliderW).reduced (0, 2));
            r.removeFromRight (4);
            if (extraLabel != nullptr)
                extraLabel->setBounds (r.removeFromRight (24));
        }
        label.setBounds (r);
    };

    int y = leftCol.getY();
    for (auto& row : familyRows)
    {
        layoutRow ({ leftCol.getX(), y, leftCol.getWidth(), rowH },
                   row->minusBtn, row->label, row->plusBtn);
        y += rowH + rowGap;
    }
    // Texture row at the bottom.
    layoutRow ({ leftCol.getX(), y, leftCol.getWidth(), rowH },
               textureMinusBtn, textureLabel, texturePlusBtn,
               &textureOpCount, &textureOpLabel);

    // Right column: 4 stacked panels.
    const int pGap = 8;
    const int pH   = (rightCol.getHeight() - pGap * 3) / 4;
    int py = rightCol.getY();
    juce::Component* panels[] { &envelopePanel, &filterPanel,
                                 &mod0Panel,     &mod1Panel };
    for (auto* p : panels)
    {
        p->setBounds (rightCol.getX(), py, rightCol.getWidth(), pH);
        py += pH + pGap;
    }
}

} // namespace brocot
