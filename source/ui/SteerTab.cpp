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

    addAndMakeVisible (depthMinusButton);
    depthMinusButton.onClick = [this] { onGlobalDepth (-0.1f); };
    depthMinusButton.setTooltip ("Lower every enabled operator's depth by 0.1 (clamped at 0.04).");

    addAndMakeVisible (depthPlusButton);
    depthPlusButton.onClick = [this] { onGlobalDepth (+0.1f); };
    depthPlusButton.setTooltip ("Raise every enabled operator's depth by 0.1 (clamped at 8.0).");

    addAndMakeVisible (randomizeButton);
    randomizeButton.onClick = [this] { onRandomize(); };
    randomizeButton.setTooltip ("Replace the patch with a fresh low-depth random carpet (32 ops on, depth 0.04..0.6).");

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

    // P3.12 added op-counts 24/32/48/64 to the textured-smooth bank;
    // the spinner range was 4..16 before and silently ignored the
    // new buckets — pressing + on the texture row at op-count > 16
    // would have looked up a non-existent category and done nothing.
    textureOpCount.setRange (4.0, 64.0, 1.0);
    textureOpCount.setValue (8.0);
    textureOpCount.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
    textureOpCount.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 28, 22);
    textureOpCount.setTooltip ("Op count for the Textured-Smooth target. "
                                "If the chosen count has no exemplar, the "
                                "nearest available op-count wins.");

    textureMinusBtn.onClick = [this] { onMinus(); };
    texturePlusBtn .onClick = [this]
    {
        const int requested = (int) textureOpCount.getValue();

        // Find the nearest existing Textured-Smooth · N ops category.
        // The bank doesn't have a preset at every integer (e.g., 17..23
        // and 25..31 are sparse), so we walk outward from `requested`.
        const auto& idx = proc.getPullIndex();
        for (int radius = 0; radius <= 64; ++radius)
        {
            for (int sign : { 0, +1, -1 })
            {
                if (radius == 0 && sign != 0) continue;
                const int candidate = requested + sign * radius;
                if (candidate < 1) continue;
                const auto name = juce::String ("Textured-Smooth · ")
                                + juce::String (candidate) + " ops";
                if (idx.getCategory (name) != nullptr)
                {
                    hintLabel.setText (juce::String ("→ ") + name,
                                       juce::dontSendNotification);
                    onPlus (name);
                    return;
                }
            }
        }
        // No category matched (the lookup walked the entire 1..64 range
        // without finding any Textured-Smooth target). Surface that —
        // silent failure is the bug we're chasing.
        hintLabel.setText ("Textured-Smooth: no category found near "
                            + juce::String (requested) + " ops",
                            juce::dontSendNotification);
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
    if (targetOps == nullptr)
    {
        hintLabel.setText ("no best-ops for '" + category + "'",
                           juce::dontSendNotification);
        return;
    }

    auto cur = proc.getCurrentOps();
    auto nudged = PullIndex::morph (cur, *targetOps, kNudgeStep);
    proc.applyOps (nudged);
    hintLabel.setText ("→ " + category + "  (target slots: "
                        + juce::String ((int) targetOps->size()) + ")",
                        juce::dontSendNotification);
}

void SteerTab::onMinus()
{
    if (! capturedValid) anchorHome();
    if (! capturedValid) return;

    auto cur = proc.getCurrentOps();
    auto nudged = PullIndex::morph (cur, captured, kNudgeStep);
    proc.applyOps (nudged);
    hintLabel.setText ("← back toward home", juce::dontSendNotification);
}

void SteerTab::onReset()
{
    if (! capturedValid) anchorHome();
    if (! capturedValid) return;

    proc.applyOps (captured);
    // Re-anchor — Reset is also "from now on this state is home".
    anchorHome();
    hintLabel.setText ("reset to home", juce::dontSendNotification);
}

void SteerTab::onGlobalDepth (float deltaPerSlot)
{
    auto ops = proc.getCurrentOps();
    int touched = 0;
    for (auto& s : ops)
    {
        if (! s.enabled) continue;
        const float oldDepth = s.depth;
        s.depth = juce::jlimit (0.04f, 8.0f, s.depth + deltaPerSlot);
        if (std::abs (s.depth - oldDepth) > 1e-6f) ++touched;
    }
    proc.applyOps (ops);
    hintLabel.setText (juce::String ("depth ") + (deltaPerSlot >= 0 ? "+" : "")
                       + juce::String (deltaPerSlot, 2)
                       + "  (touched " + juce::String (touched) + " ops)",
                       juce::dontSendNotification);
}

void SteerTab::onRandomize()
{
    // Freeform low-depth random carpet — same depth band as the
    // experimental textured-smooth bank (≤ 0.6), random L/R paths
    // 1..8 chars deep, ~half the slots enabled. Drops you somewhere
    // unfamiliar in the parameter space; the − button + Reset are
    // the way back out (Reset now treats the random as home).
    juce::Random& rng = juce::Random::getSystemRandom();
    std::vector<PullIndex::OpState> ops;
    ops.reserve (64);
    for (int i = 0; i < 64; ++i)
    {
        PullIndex::OpState s;
        s.enabled = rng.nextFloat() < 0.5f;   // ~32 enabled on average
        if (s.enabled)
        {
            const int pathLen = 1 + rng.nextInt (8);
            juce::String path;
            for (int j = 0; j < pathLen; ++j)
                path += (rng.nextBool() ? "L" : "R");
            s.path = path;
            s.depth = 0.04f + rng.nextFloat() * 0.56f;   // [0.04, 0.60]
        }
        else
        {
            s.path = {};
            s.depth = 0.0f;
        }
        ops.push_back (s);
    }
    proc.applyOps (ops);
    anchorHome();    // fresh random becomes the new home
    const int enabledCount = (int) std::count_if (ops.begin(), ops.end(),
        [] (const PullIndex::OpState& s) { return s.enabled; });
    hintLabel.setText ("randomized — " + juce::String (enabledCount)
                       + " ops enabled, depths 0.04..0.6",
                       juce::dontSendNotification);
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

    // Header row: hint label left + global controls right.
    // Layout right-to-left: [Reset][rand][+0.1][−0.1] [hint .... ]
    auto headerRow = bounds.removeFromTop (32);
    resetButton    .setBounds (headerRow.removeFromRight (60).reduced (0, 4));
    headerRow.removeFromRight (4);
    randomizeButton.setBounds (headerRow.removeFromRight (60).reduced (0, 4));
    headerRow.removeFromRight (4);
    depthPlusButton .setBounds (headerRow.removeFromRight (54).reduced (0, 4));
    headerRow.removeFromRight (4);
    depthMinusButton.setBounds (headerRow.removeFromRight (54).reduced (0, 4));
    headerRow.removeFromRight (12);
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
