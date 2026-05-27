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

    addAndMakeVisible (keyboard);
    keyboard.setLowestVisibleKey (36);   // C2 — three octaves below middle C
    keyboard.setOctaveForMiddleC (4);
    setWantsKeyboardFocus (true);        // let computer-keyboard input reach
    keyboard.setWantsKeyboardFocus (true);

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
    polyphonyCombo.addItem ("poly", 3);
    polyphonyAttach = std::make_unique<ComboAttach> (
        processorRef.getAPVTS(), params::ids::polyphony, polyphonyCombo);
    polyphonyCombo.setTooltip ("Mono: latest-note priority. Duo: two notes. "
                               "Poly: up to 'voices' simultaneous notes with oldest-voice stealing.");

    // Voice count for poly mode (inert in mono/duo).
    addAndMakeVisible (maxVoicesSlider);
    maxVoicesSlider.setSliderStyle (juce::Slider::IncDecButtons);
    maxVoicesSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 32, 20);
    maxVoicesSlider.setTooltip ("Poly voice count (number of simultaneous notes).");
    maxVoicesAttach = std::make_unique<SliderAttach> (
        processorRef.getAPVTS(), params::ids::maxVoices, maxVoicesSlider);

    addAndMakeVisible (pathNavigator);
    addAndMakeVisible (oscilloscope);
    addAndMakeVisible (ratioReadout);

    // P3.10c: TabbedComponent for the steer / under-the-hood split.
    // Tab content lives in the dedicated SteerTab and UnderTheHoodTab
    // classes; this editor just hosts the chrome.
    addAndMakeVisible (tabs);
    tabs.setTabBarDepth (28);
    // The old slider-based "steer" tab is retired — steering now lives directly
    // on the landscape map (click-to-seek). SteerTab.{h,cpp} are kept in the
    // build but no longer surfaced; reinstate the tab here to bring it back.
    tabs.addTab ("landscape",      colors::background, &cloudView,       false);
    tabs.addTab ("under the hood", colors::background, &underTheHoodTab, false);

    // Bumped default height to 940 to fit the V2 LFO bank (16 panels in an
    // 8×2 grid) below the operator strip. Width unchanged.
    setSize (1100, 940);
    setResizable (true, true);
    setResizeLimits (880, 720, 1600, 1200);
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
    // Wider now that the bar also carries the mod-bank selector + save.
    presetBar.setBounds (titleRow.removeFromRight (560));

    // Path-mode toggle + shared-path navigator strip.
    auto pathRow = bounds.removeFromTop (52);
    pathModeCombo.setBounds  (pathRow.removeFromLeft (110).reduced (0, 12));
    pathRow.removeFromLeft (6);
    polyphonyCombo.setBounds (pathRow.removeFromLeft (80).reduced (0, 12));
    pathRow.removeFromLeft (6);
    maxVoicesSlider.setBounds (pathRow.removeFromLeft (78).reduced (0, 14));
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

    keyboard.setBounds (bounds.removeFromBottom (64));
    bounds.removeFromBottom (8);

    // Everything left over goes to the tab bar + its current tab content.
    tabs.setBounds (bounds);
}

} // namespace brocot
