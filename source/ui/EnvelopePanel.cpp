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
        s->setColour (juce::Slider::rotarySliderFillColourId,    colors::accent);
        s->setColour (juce::Slider::rotarySliderOutlineColourId,
                      colors::primaryText.withAlpha (0.20f));
        s->setColour (juce::Slider::thumbColourId,               colors::accent);
        s->setColour (juce::Slider::textBoxTextColourId,         colors::primaryText);
        s->setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
        s->setNumDecimalPlacesToDisplay (2);
        s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 50, 14);
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

    auto labelRow = bounds.removeFromTop (16);
    bounds.removeFromTop (2);

    juce::Slider* sliders[] { &attack, &decay, &sustain, &release };
    juce::Label*  labels[]  { &aLabel, &dLabel, &sLabel, &rLabel };

    const int n   = 4;
    const int gap = 4;
    const int w   = (bounds.getWidth() - gap * (n - 1)) / n;
    int x = bounds.getX();
    for (int i = 0; i < n; ++i)
    {
        labels[i]->setBounds (x, labelRow.getY(), w, labelRow.getHeight());
        sliders[i]->setBounds (x, bounds.getY(), w, bounds.getHeight());
        x += w + gap;
    }
}

} // namespace brocot
