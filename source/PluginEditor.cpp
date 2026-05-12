#include "PluginEditor.h"

namespace brocot
{

namespace colors
{
    // BROCOT-SPEC.md §7 — palette constants. Centralized later in util/Colors.h
    // when Slice 8 lands; defined inline here so Slice 1 has the right look.
    static const juce::Colour background       { 12,  10,  9   };
    static const juce::Colour primaryText      { 254, 243, 199 };
    static const juce::Colour amber500         { 245, 158, 11  };
    static const juce::Colour amber200_60      { (juce::uint8) 253, (juce::uint8) 230, (juce::uint8) 138, 0.6f };
}

BrocotAudioProcessorEditor::BrocotAudioProcessorEditor (BrocotAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processorRef (p)
{
    setSize (1100, 720);
    setResizable (true, true);
    setResizeLimits (880, 580, 1600, 1050);
}

BrocotAudioProcessorEditor::~BrocotAudioProcessorEditor() = default;

void BrocotAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (colors::background);

    g.setColour (colors::primaryText);
    g.setFont (juce::Font (juce::FontOptions ("Georgia", 24.0f, juce::Font::italic)));
    g.drawText ("brocot",
                getLocalBounds().removeFromTop (60).withTrimmedLeft (24),
                juce::Justification::centredLeft);

    g.setColour (colors::amber200_60);
    g.setFont (juce::Font (juce::FontOptions ("monospace", 10.0f, juce::Font::plain)));
    g.drawText ("v0.1 · slice 1 scaffold",
                getLocalBounds().removeFromTop (60).withTrimmedRight (24),
                juce::Justification::centredRight);

    g.setColour (colors::amber500.withAlpha (0.15f));
    g.drawHorizontalLine (60, 24.0f, (float) getWidth() - 24.0f);
}

void BrocotAudioProcessorEditor::resized() {}

} // namespace brocot
