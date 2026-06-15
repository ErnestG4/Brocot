#include "FilterPanel.h"
#include "../util/Colors.h"
#include "../util/Fonts.h"

#include <cmath>

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
    typeCombo.setTooltip (juce::CharPointer_UTF8 ("Filter type — Cytomic SVF output mode."));

    for (auto* s : { &cutoff, &resonance })
    {
        addAndMakeVisible (*s);
        s->setColour (juce::Slider::textBoxTextColourId,       colors::primaryText);
        s->setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
        s->setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        s->setNumDecimalPlacesToDisplay (2);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 18);
    }

    // The cutoff param is normalised 0..1; PluginProcessor maps it to a log
    // 20 Hz..20 kHz sweep (cutoffHz = 20 * 1000^x). Show that Hz value in the
    // readout so the control reads like a filter, not an abstract 0..1.
    cutoff.textFromValueFunction = [] (double v)
    {
        const double hz = 20.0 * std::pow (1000.0, v);
        return hz >= 1000.0 ? juce::String (hz / 1000.0, 2) + " kHz"
                            : juce::String (juce::roundToInt (hz)) + " Hz";
    };
    cutoff.valueFromTextFunction = [] (const juce::String& t)
    {
        auto s = t.trim().toLowerCase();
        double hz = s.getDoubleValue() * (s.contains ("k") ? 1000.0 : 1.0);
        hz = juce::jlimit (20.0, 20000.0, hz);
        return std::log (hz / 20.0) / std::log (1000.0);
    };

    cutoff   .setTooltip ("Cutoff frequency (20 Hz .. 20 kHz, logarithmic).");
    resonance.setTooltip ("Resonance / Q (0..1).");

    for (auto* l : { &typeLabel, &cutoffLabel, &resonanceLabel })
    {
        addAndMakeVisible (*l);
        l->setJustificationType (juce::Justification::centredLeft);
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

    // Type combo (full width), then cutoff / resonance as stacked horizontal
    // sliders (label · track · value) to match the envelope + operator panels.
    typeCombo.setBounds (bounds.removeFromTop (24));
    bounds.removeFromTop (6);

    juce::Slider* sliders[] { &cutoff, &resonance };
    juce::Label*  labels[]  { &cutoffLabel, &resonanceLabel };

    const int n      = 2;
    const int gap    = 4;
    const int rowH   = juce::jmax (18, (bounds.getHeight() - gap * (n - 1)) / n);
    const int labelW = 52;
    int y = bounds.getY();
    for (int i = 0; i < n; ++i)
    {
        juce::Rectangle<int> r { bounds.getX(), y, bounds.getWidth(), rowH };
        labels[i]->setBounds (r.removeFromLeft (labelW));
        r.removeFromLeft (4);
        sliders[i]->setBounds (r);
        y += rowH + gap;
    }

    typeLabel.setBounds (0, 0, 0, 0);   // unused; combo's own text suffices
}

} // namespace brocot
