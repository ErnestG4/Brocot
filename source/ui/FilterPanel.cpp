#include "FilterPanel.h"
#include "../util/Colors.h"
#include "../util/Fonts.h"

namespace brocot
{

FilterPanel::FilterPanel (BrocotAudioProcessor& p)
    : proc (p)
{
    auto& apvts = proc.getAPVTS();

    addAndMakeVisible (typeCombo);
    typeCombo.addItem ("LP",    1);
    typeCombo.addItem ("BP",    2);
    typeCombo.addItem ("HP",    3);
    typeCombo.addItem ("Notch", 4);
    typeCombo.setTooltip ("Filter type — Cytomic SVF output mode.");

    for (auto* s : { &cutoff, &resonance })
    {
        addAndMakeVisible (*s);
        s->setColour (juce::Slider::rotarySliderFillColourId,    colors::accent);
        s->setColour (juce::Slider::rotarySliderOutlineColourId,
                      colors::primaryText.withAlpha (0.20f));
        s->setColour (juce::Slider::thumbColourId,               colors::accent);
        s->setColour (juce::Slider::textBoxTextColourId,         colors::primaryText);
        s->setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
        s->setNumDecimalPlacesToDisplay (2);
        s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 50, 14);
    }
    cutoff   .setTooltip ("Cutoff frequency (0 = 20 Hz, 1 = ~Nyquist).");
    resonance.setTooltip ("Resonance / Q (0..1).");

    for (auto* l : { &typeLabel, &cutoffLabel, &resonanceLabel })
    {
        addAndMakeVisible (*l);
        l->setJustificationType (juce::Justification::centred);
        l->setColour (juce::Label::textColourId, colors::primaryText);
    }

    typeAttach      = std::make_unique<ComboAttach>  (apvts, params::ids::filterType,      typeCombo);
    cutoffAttach    = std::make_unique<SliderAttach> (apvts, params::ids::filterCutoff,    cutoff);
    resonanceAttach = std::make_unique<SliderAttach> (apvts, params::ids::filterResonance, resonance);
}

void FilterPanel::paint (juce::Graphics& g)
{
    g.setColour (colors::primaryText.withAlpha (0.10f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.5f), 6.0f, 1.0f);

    g.setColour (colors::primaryText);
    g.setFont (fonts::serifItalic (16.0f));
    g.drawText ("filter",
                getLocalBounds().reduced (10, 6).removeFromTop (22),
                juce::Justification::centredLeft);
}

void FilterPanel::resized()
{
    auto bounds = getLocalBounds().reduced (10, 6);
    bounds.removeFromTop (24);   // header

    // Top row: type combo (full width)
    typeCombo.setBounds (bounds.removeFromTop (28));
    bounds.removeFromTop (8);

    auto labelRow = bounds.removeFromTop (16);
    bounds.removeFromTop (2);

    juce::Slider* sliders[] { &cutoff, &resonance };
    juce::Label*  labels[]  { &cutoffLabel, &resonanceLabel };

    const int n   = 2;
    const int gap = 8;
    const int w   = (bounds.getWidth() - gap * (n - 1)) / n;
    int x = bounds.getX();
    for (int i = 0; i < n; ++i)
    {
        labels[i] ->setBounds (x, labelRow.getY(), w, labelRow.getHeight());
        sliders[i]->setBounds (x, bounds.getY(), w, bounds.getHeight());
        x += w + gap;
    }

    typeLabel.setBounds (0, 0, 0, 0);   // unused; combo's own text suffices
}

} // namespace brocot
