#include "SteerTab.h"
#include "BrocotLookAndFeel.h"
#include "../util/Colors.h"
#include "../util/Fonts.h"

namespace brocot
{

// The shape categories that get a scrub-slider row in the left column.
// Texture is handled separately because its target is parameterised by
// an op-count spinner. The other 12 are 1:1 with the pull-index keys.
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
    resetButton.setTooltip ("Re-anchor home to the current state — the next "
                            "leftward drag will pull back here.");

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
    hintLabel.setText ("Drag a row right to push toward that shape  ·  drag left to pull home",
                       juce::dontSendNotification);

    // Inventory which N-op Textured-Smooth categories are actually
    // present. P3.13 fix: never construct the category name from a `·`
    // literal — parse loaded names by ASCII prefix instead.
    {
        const auto& idx = proc.getPullIndex();
        for (const auto& n : idx.getCategoryNamesStartingWith ("Textured-Smooth"))
        {
            // Expect "Textured-Smooth <something> N ops". Pull the
            // trailing integer before " ops".
            if (! n.endsWith (" ops")) continue;
            const auto head = n.dropLastCharacters (4);   // strip " ops"
            int digitsEnd = head.length();
            int digitsStart = digitsEnd;
            while (digitsStart > 0
                   && head[digitsStart - 1] >= '0'
                   && head[digitsStart - 1] <= '9')
                --digitsStart;
            if (digitsStart == digitsEnd) continue;
            const int n_ops = head.substring (digitsStart, digitsEnd).getIntValue();
            if (n_ops > 0) textureAvailableOps.add (n_ops);
        }
    }

    buildRows();

    // Texture-row extras (the op-count spinner). The slider lives on
    // the row itself like every other category.
    addAndMakeVisible (textureOpLabel);
    addAndMakeVisible (textureOpCount);
    textureOpLabel.setJustificationType (juce::Justification::centredRight);
    textureOpCount.setRange (4.0, 64.0, 1.0);
    textureOpCount.setValue (textureAvailableOps.isEmpty() ? 8 : textureAvailableOps[0]);
    textureOpCount.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
    textureOpCount.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 28, 22);
    textureOpCount.setTooltip ("Op count for the Textured-Smooth target. "
                                "If the chosen count has no exemplar, the "
                                "nearest available op-count wins.");

    anchorHome();

    if (proc.getPullIndex().isEmpty())
    {
        hintLabel.setText ("Pull index not loaded — run "
                            "`python -m phase3.build_pull_index`",
                            juce::dontSendNotification);
        resetButton.setEnabled (false);
        for (auto& r : rows)
            r->slider.setEnabled (false);
    }
}

void SteerTab::buildRows()
{
    rows.clear();

    auto addRow = [this] (const juce::String& cat, bool isTexture, const juce::String& display)
    {
        auto r = std::make_unique<ScrubRow>();
        r->category     = cat;
        r->isTextureRow = isTexture;

        addAndMakeVisible (r->label);
        r->label.setText (display, juce::dontSendNotification);
        r->label.setJustificationType (juce::Justification::centredLeft);

        addAndMakeVisible (r->slider);
        r->slider.setRange (-1.0, 1.0, 0.0);
        r->slider.setDoubleClickReturnValue (true, 0.0);
        r->slider.setValue (0.0, juce::dontSendNotification);
        r->slider.setTooltip (isTexture
            ? "Drag right to push toward Textured-Smooth · N ops; left to pull home. "
              "Releases back to neutral; the patch stays where you let go."
            : "Drag right to push toward " + cat + "; left to pull home. "
              "Releases back to neutral; the patch stays where you let go.");

        rows.push_back (std::move (r));
        wireSlider (*rows.back());
    };

    for (const auto& cat : kShapeCategories)
        addRow (cat, false, cat);
    addRow ({}, true, "Textured-Smooth");
}

void SteerTab::wireSlider (ScrubRow& row)
{
    auto* rowPtr = &row;
    row.slider.onDragStart = [this, rowPtr]
    {
        rowPtr->dragging  = true;
        rowPtr->lastValue = rowPtr->slider.getValue();
    };
    row.slider.onValueChange = [this, rowPtr]
    {
        const double v = rowPtr->slider.getValue();
        const double delta = v - rowPtr->lastValue;
        rowPtr->lastValue = v;
        if (! rowPtr->dragging) return;       // ignore programmatic spring-back
        if (std::abs (delta) < 1e-6) return;
        onScrubDelta (*rowPtr, (float) delta);
    };
    row.slider.onDragEnd = [this, rowPtr]
    {
        rowPtr->dragging = false;
        // Snap visual back to neutral. dontSendNotification avoids
        // an inverse morph on release.
        rowPtr->slider.setValue (0.0, juce::dontSendNotification);
        rowPtr->lastValue = 0.0;
    };
}

void SteerTab::onScrubDelta (ScrubRow& row, float delta)
{
    const float step = std::abs (delta) * kSliderCoefficient;
    if (delta > 0.0f)
    {
        const auto category = row.isTextureRow ? resolveTextureCategory()
                                                : row.category;
        if (category.isEmpty())
        {
            hintLabel.setText ("Textured-Smooth: no exemplars loaded",
                               juce::dontSendNotification);
            return;
        }
        applyMorphToward (category, step);
    }
    else
    {
        applyMorphToHome (step);
    }
}

juce::String SteerTab::resolveTextureCategory()
{
    if (textureAvailableOps.isEmpty()) return {};

    const int requested = (int) textureOpCount.getValue();

    // Find the nearest available op-count by absolute distance.
    int bestN     = textureAvailableOps[0];
    int bestDist  = std::abs (bestN - requested);
    for (int i = 1; i < textureAvailableOps.size(); ++i)
    {
        const int n = textureAvailableOps[i];
        const int d = std::abs (n - requested);
        if (d < bestDist) { bestN = n; bestDist = d; }
    }

    // Look the category up by ASCII prefix + parse, never by literal `·`.
    const auto& idx = proc.getPullIndex();
    const auto suffix = juce::String (" ") + juce::String (bestN) + " ops";
    for (const auto& n : idx.getCategoryNamesStartingWith ("Textured-Smooth"))
        if (n.endsWith (suffix))
            return n;
    return {};
}

void SteerTab::applyMorphToward (const juce::String& category, float step)
{
    const auto* targetOps = proc.getPullIndex().getBestOps (category);
    if (targetOps == nullptr)
    {
        hintLabel.setText ("no best-ops for '" + category + "'",
                           juce::dontSendNotification);
        return;
    }
    auto cur = proc.getCurrentOps();
    auto nudged = PullIndex::morph (cur, *targetOps, step);
    proc.applyOps (nudged);
    hintLabel.setText (juce::String ("→ ") + category, juce::dontSendNotification);
}

void SteerTab::applyMorphToHome (float step)
{
    if (! capturedValid) anchorHome();
    if (! capturedValid) return;
    auto cur = proc.getCurrentOps();
    auto nudged = PullIndex::morph (cur, captured, step);
    proc.applyOps (nudged);
    hintLabel.setText ("← back toward home", juce::dontSendNotification);
}

void SteerTab::anchorHome()
{
    captured = proc.getCurrentOps();
    capturedValid = ! captured.empty();
}

void SteerTab::onReset()
{
    anchorHome();
    hintLabel.setText ("home re-anchored to current state",
                       juce::dontSendNotification);
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
    juce::Random& rng = juce::Random::getSystemRandom();
    std::vector<PullIndex::OpState> ops;
    ops.reserve (64);
    for (int i = 0; i < 64; ++i)
    {
        PullIndex::OpState s;
        s.enabled = rng.nextFloat() < 0.5f;
        if (s.enabled)
        {
            const int pathLen = 1 + rng.nextInt (8);
            juce::String path;
            for (int j = 0; j < pathLen; ++j)
                path += (rng.nextBool() ? "L" : "R");
            s.path = path;
            s.depth = 0.04f + rng.nextFloat() * 0.56f;
        }
        else
        {
            s.path = {};
            s.depth = 0.0f;
        }
        ops.push_back (s);
    }
    proc.applyOps (ops);
    anchorHome();
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
    bounds.removeFromTop (48);

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

    const int rightW = juce::jlimit (380, 540, bounds.getWidth() * 4 / 10);
    auto leftCol  = bounds.removeFromLeft (bounds.getWidth() - rightW - 12);
    bounds.removeFromLeft (12);
    auto rightCol = bounds;

    const int totalRows = (int) rows.size();
    const int rowGap = 2;
    const int rowH = juce::jlimit (24, 36,
                                   (leftCol.getHeight() - rowGap * (totalRows - 1)) / juce::jmax (1, totalRows));

    // Label width: roughly half the row so the slider has room.
    const int labelW = juce::jlimit (110, 180, leftCol.getWidth() * 4 / 10);

    int y = leftCol.getY();
    for (auto& row : rows)
    {
        juce::Rectangle<int> r { leftCol.getX(), y, leftCol.getWidth(), rowH };
        row->label.setBounds (r.removeFromLeft (labelW));
        r.removeFromLeft (6);
        if (row->isTextureRow)
        {
            // op-count spinner + small "op" label between label and slider.
            const int spinnerW = 84;
            textureOpCount.setBounds (r.removeFromLeft (spinnerW).reduced (0, 2));
            r.removeFromLeft (4);
            textureOpLabel.setBounds (r.removeFromLeft (22));
            r.removeFromLeft (4);
        }
        row->slider.setBounds (r.reduced (0, 4));
        y += rowH + rowGap;
    }

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
