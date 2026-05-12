#include "PluginEditor.h"
#include "util/Colors.h"
#include "util/Fonts.h"

namespace brocot
{

BrocotAudioProcessorEditor::BrocotAudioProcessorEditor (BrocotAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processorRef (p)
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (pathModeCombo);
    pathModeCombo.addItem ("shared", 1);
    pathModeCombo.addItem ("free",   2);
    pathModeAttach = std::make_unique<ComboAttach> (
        processorRef.getAPVTS(), params::ids::pathMode, pathModeCombo);

    addAndMakeVisible (pathNavigator);

    for (int i = 0; i < params::kNumOperators; ++i)
    {
        operatorPanels[(size_t) i] = std::make_unique<OperatorPanel> (processorRef, i);
        addAndMakeVisible (*operatorPanels[(size_t) i]);
    }

    setSize (1100, 720);
    setResizable (true, true);
    setResizeLimits (880, 580, 1600, 1050);
}

BrocotAudioProcessorEditor::~BrocotAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void BrocotAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (colors::background);

    // Title row
    g.setColour (colors::primaryText);
    g.setFont (fonts::serifItalic (24.0f));
    g.drawText ("brocot",
                getLocalBounds().removeFromTop (40).withTrimmedLeft (24),
                juce::Justification::centredLeft);

    g.setColour (colors::secondaryAtAlpha (0.6f));
    g.setFont (fonts::mono (10.0f));
    g.drawText ("v0.1 · free mode",
                getLocalBounds().removeFromTop (40).withTrimmedRight (24),
                juce::Justification::centredRight);

    g.setColour (colors::accent.withAlpha (0.15f));
    g.drawHorizontalLine (40, 24.0f, (float) getWidth() - 24.0f);
}

void BrocotAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced (24);
    bounds.removeFromTop (40);     // title row reserved by paint()

    // Path-mode toggle + shared-path navigator strip.
    auto pathRow = bounds.removeFromTop (52);
    pathModeCombo.setBounds (pathRow.removeFromLeft (120).reduced (0, 12));
    pathRow.removeFromLeft (8);
    pathNavigator.setBounds (pathRow);
    bounds.removeFromTop (8);

    // Reserve the bottom strip for filter/env/LFO panels (Slice 12). For now
    // it stays empty; the operator grid fills the rest.
    bounds.removeFromBottom (130);

    // 4-wide × 2-tall grid of operator panels (spec §7 layout mock).
    const int cols = 4, rows = 2;
    const int gap = 8;
    const int cellW = (bounds.getWidth() - gap * (cols - 1)) / cols;
    const int cellH = (bounds.getHeight() - gap * (rows - 1)) / rows;

    for (int i = 0; i < params::kNumOperators; ++i)
    {
        const int col = i % cols;
        const int row = i / cols;
        const int x = bounds.getX() + col * (cellW + gap);
        const int y = bounds.getY() + row * (cellH + gap);
        if (auto& panel = operatorPanels[(size_t) i])
            panel->setBounds (x, y, cellW, cellH);
    }
}

} // namespace brocot
