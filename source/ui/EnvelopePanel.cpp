#include "EnvelopePanel.h"
#include "../PluginProcessor.h"
#include "../util/Colors.h"
#include "../util/Fonts.h"

namespace brocot
{

EnvelopePanel::EnvelopePanel (BrocotAudioProcessor& processor)
{
    addAndMakeVisible (title);
    title.setText ("amp envelope", juce::dontSendNotification);
    title.setFont (fonts::serifItalic (14.0f));
    title.setColour (juce::Label::textColourId, colors::primaryText);

    auto setupSlider = [this] (juce::Slider& s)
    {
        addAndMakeVisible (s);
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    };
    setupSlider (attackSlider);
    setupSlider (decaySlider);
    setupSlider (sustainSlider);
    setupSlider (releaseSlider);

    auto setupLabel = [this] (juce::Label& l, juce::StringRef text)
    {
        addAndMakeVisible (l);
        l.setText (text, juce::dontSendNotification);
        l.setFont (fonts::mono (10.0f));
        l.setColour (juce::Label::textColourId, colors::secondaryAtAlpha (0.6f));
    };
    setupLabel (attackLabel,  "A");
    setupLabel (decayLabel,   "D");
    setupLabel (sustainLabel, "S");
    setupLabel (releaseLabel, "R");

    auto& apvts = processor.getAPVTS();
    attackAttach  = std::make_unique<SliderAttach> (apvts, params::ids::envAttack,  attackSlider);
    decayAttach   = std::make_unique<SliderAttach> (apvts, params::ids::envDecay,   decaySlider);
    sustainAttach = std::make_unique<SliderAttach> (apvts, params::ids::envSustain, sustainSlider);
    releaseAttach = std::make_unique<SliderAttach> (apvts, params::ids::envRelease, releaseSlider);
}

EnvelopePanel::~EnvelopePanel() = default;

void EnvelopePanel::paint (juce::Graphics& g)
{
    g.fillAll (colors::surface);
    g.setColour (colors::borderAtAlpha (0.4f));
    g.drawRect (getLocalBounds(), 1);
}

void EnvelopePanel::resized()
{
    auto r = getLocalBounds().reduced (10);
    title.setBounds (r.removeFromTop (18));
    r.removeFromTop (4);

    auto row = [&] (juce::Label& l, juce::Slider& s)
    {
        auto strip = r.removeFromTop (16);
        l.setBounds (strip.removeFromLeft (16));
        s.setBounds (strip);
        r.removeFromTop (3);
    };
    row (attackLabel,  attackSlider);
    row (decayLabel,   decaySlider);
    row (sustainLabel, sustainSlider);
    row (releaseLabel, releaseSlider);
}

} // namespace brocot
