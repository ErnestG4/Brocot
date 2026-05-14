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
        s->setColour (juce::Slider::trackColourId, colors::accent);
        s->setColour (juce::Slider::backgroundColourId,
                      colors::primaryText.withAlpha (0.10f));
        s->setSliderSnapsToMousePosition (false);
    }

    attack .setTooltip ("Attack time");
    decay  .setTooltip ("Decay time");
    sustain.setTooltip ("Sustain level (0-1)");
    release.setTooltip ("Release time");

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

    auto labelRow = bounds.removeFromBottom (18);
    bounds.removeFromBottom (4);

    juce::Slider* sliders[] { &attack, &decay, &sustain, &release };
    juce::Label*  labels[]  { &aLabel, &dLabel, &sLabel, &rLabel };

    const int n   = 4;
    const int gap = 4;
    const int w   = (bounds.getWidth() - gap * (n - 1)) / n;
    int x = bounds.getX();
    for (int i = 0; i < n; ++i)
    {
        sliders[i]->setBounds (x, bounds.getY(), w, bounds.getHeight());
        labels[i] ->setBounds (x, labelRow.getY(), w, labelRow.getHeight());
        x += w + gap;
    }
}

} // namespace brocot
