#include "EnvelopePanel.h"
#include "../util/Colors.h"
#include "../util/Fonts.h"

namespace brocot
{

EnvelopePanel::EnvelopePanel (BrocotAudioProcessor& p)
    : proc (p)
{
    auto& apvts = proc.getAPVTS();

    for (auto* s : { &attack, &decay, &sustain, &release })
    {
        addAndMakeVisible (*s);
        s->setColour (juce::Slider::textBoxTextColourId,       colors::primaryText);
        s->setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
        s->setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        s->setNumDecimalPlacesToDisplay (2);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
    }

    attack .setTooltip ("Attack time (s)");
    decay  .setTooltip ("Decay time (s)");
    sustain.setTooltip ("Sustain level (0-1)");
    release.setTooltip ("Release time (s)");

    for (auto* l : { &aLabel, &dLabel, &sLabel, &rLabel })
    {
        addAndMakeVisible (*l);
        l->setJustificationType (juce::Justification::centred);
        l->setColour (juce::Label::textColourId, colors::primaryText);
    }

    attackAttach  = std::make_unique<SliderAttach> (apvts, params::ids::envAttack,  attack);
    decayAttach   = std::make_unique<SliderAttach> (apvts, params::ids::envDecay,   decay);
    sustainAttach = std::make_unique<SliderAttach> (apvts, params::ids::envSustain, sustain);
    releaseAttach = std::make_unique<SliderAttach> (apvts, params::ids::envRelease, release);
}

void EnvelopePanel::paint (juce::Graphics& g)
{
    g.setColour (colors::primaryText.withAlpha (0.10f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.5f), 6.0f, 1.0f);

    g.setColour (colors::primaryText);
    g.setFont (fonts::serifItalic (16.0f));
    g.drawText ("envelope",
                getLocalBounds().reduced (10, 6).removeFromTop (22),
                juce::Justification::centredLeft);
}

void EnvelopePanel::resized()
{
    auto bounds = getLocalBounds().reduced (10, 6);
    bounds.removeFromTop (24);   // header

    // Four stacked horizontal sliders (label · track · value), matching the
    // operator/modulator panels. Replaces the old four-knob strip, which fell
    // back to JUCE's stock rotary art (no custom drawRotarySlider) and read as
    // cramped at this panel's wide-and-short size.
    juce::Slider* sliders[] { &attack, &decay, &sustain, &release };
    juce::Label*  labels[]  { &aLabel, &dLabel, &sLabel, &rLabel };

    const int n      = 4;
    const int gap    = 2;
    const int rowH   = juce::jmax (16, (bounds.getHeight() - gap * (n - 1)) / n);
    const int labelW = 16;
    int y = bounds.getY();
    for (int i = 0; i < n; ++i)
    {
        juce::Rectangle<int> r { bounds.getX(), y, bounds.getWidth(), rowH };
        labels[i]->setBounds (r.removeFromLeft (labelW));
        r.removeFromLeft (4);
        sliders[i]->setBounds (r);
        y += rowH + gap;
    }
}

} // namespace brocot
