#include "PluginEditor.h"
#include "util/Colors.h"
#include "util/Fonts.h"

namespace brocot
{

BrocotAudioProcessorEditor::BrocotAudioProcessorEditor (BrocotAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processorRef (p)
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (aboutBtn);
    aboutBtn.setTooltip ("About brocot");
    aboutBtn.onClick = [this] { AboutDialog::launch (this); };

    addAndMakeVisible (presetBar);

    addAndMakeVisible (pathModeCombo);
    pathModeCombo.addItem ("shared", 1);
    pathModeCombo.addItem ("free",   2);
    pathModeAttach = std::make_unique<ComboAttach> (
        processorRef.getAPVTS(), params::ids::pathMode, pathModeCombo);
    pathModeCombo.setTooltip ("Shared: operators sit at positions along one trunk path. "
                              "Free: each operator has its own path through the tree.");

    addAndMakeVisible (polyphonyCombo);
    polyphonyCombo.addItem ("mono", 1);
    polyphonyCombo.addItem ("duo",  2);
    polyphonyAttach = std::make_unique<ComboAttach> (
        processorRef.getAPVTS(), params::ids::polyphony, polyphonyCombo);
    polyphonyCombo.setTooltip ("Mono: latest-note priority. Duo: two simultaneous notes with oldest-voice stealing.");

    addAndMakeVisible (pathNavigator);
    addAndMakeVisible (oscilloscope);
    addAndMakeVisible (ratioReadout);
    addAndMakeVisible (filterPanel);
    addAndMakeVisible (envelopePanel);
    addAndMakeVisible (lfoPanel);

    // Tooltips on the panel containers themselves require SettableTooltipClient
    // inheritance, which my custom Components don't have. Individual controls
    // inside each panel (sliders, combos, toggles) get tooltips through their
    // own setTooltip() calls in v1.x; the combos above are tooltip-equipped.

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

    g.setColour (colors::accent.withAlpha (0.15f));
    g.drawHorizontalLine (40, 24.0f, (float) getWidth() - 24.0f);
}

void BrocotAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced (24);

    // Title row: "brocot" on the left (drawn by paint), preset bar +
    // about button on the right.
    auto titleRow = bounds.removeFromTop (40);
    aboutBtn.setBounds (titleRow.removeFromRight (32).reduced (4, 8));
    titleRow.removeFromRight (4);
    presetBar.setBounds (titleRow.removeFromRight (360));

    // Path-mode toggle + shared-path navigator strip.
    auto pathRow = bounds.removeFromTop (52);
    pathModeCombo.setBounds  (pathRow.removeFromLeft (110).reduced (0, 12));
    pathRow.removeFromLeft (6);
    polyphonyCombo.setBounds (pathRow.removeFromLeft (80).reduced (0, 12));
    pathRow.removeFromLeft (8);
    pathNavigator.setBounds (pathRow);
    bounds.removeFromTop (8);

    // Oscilloscope strip — full-width live waveform.
    oscilloscope.setBounds (bounds.removeFromTop (100));
    bounds.removeFromTop (8);

    // Ratio readout strip: live frequency-ratio list + depth-ratio
    // approximation. Display-only; matches farey-fm's findDepthRatio output.
    ratioReadout.setBounds (bounds.removeFromTop (54));
    bounds.removeFromTop (10);

    // Filter / Env / LFO panels along the bottom strip (spec §7 layout).
    auto bottomStrip = bounds.removeFromBottom (130);
    bounds.removeFromBottom (8);
    const int panelW = bottomStrip.getWidth() / 3;
    filterPanel.setBounds   (bottomStrip.removeFromLeft (panelW).reduced (2));
    envelopePanel.setBounds (bottomStrip.removeFromLeft (panelW).reduced (2));
    lfoPanel.setBounds      (bottomStrip.reduced (2));

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
