#include "LFOPanel.h"
#include "../PluginProcessor.h"
#include "../util/Colors.h"
#include "../util/Fonts.h"

namespace brocot
{

LFOPanel::LFOPanel (BrocotAudioProcessor& processor)
{
    addAndMakeVisible (title);
    title.setText ("lfo → filter cutoff", juce::dontSendNotification);
    title.setFont (fonts::serifItalic (14.0f));
    title.setColour (juce::Label::textColourId, colors::primaryText);

    addAndMakeVisible (shapeCombo);
    shapeCombo.addItem ("Sine",     1);
    shapeCombo.addItem ("Triangle", 2);
    shapeCombo.addItem ("Square",   3);
    shapeCombo.addItem ("Saw",      4);

    addAndMakeVisible (rateSlider);
    rateSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    rateSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);

    addAndMakeVisible (depthSlider);
    depthSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    depthSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);

    addAndMakeVisible (keySyncToggle);
    keySyncToggle.setButtonText ("key sync");

    for (auto* l : { &rateLabel, &depthLabel })
    {
        addAndMakeVisible (*l);
        l->setFont (fonts::mono (10.0f));
        l->setColour (juce::Label::textColourId, colors::secondaryAtAlpha (0.6f));
    }
    rateLabel.setText  ("rate",  juce::dontSendNotification);
    depthLabel.setText ("depth", juce::dontSendNotification);

    auto& apvts = processor.getAPVTS();
    shapeAttach   = std::make_unique<ComboAttach>  (apvts, params::ids::lfoShape,   shapeCombo);
    rateAttach    = std::make_unique<SliderAttach> (apvts, params::ids::lfoRate,    rateSlider);
    depthAttach   = std::make_unique<SliderAttach> (apvts, params::ids::lfoDepth,   depthSlider);
    keySyncAttach = std::make_unique<ButtonAttach> (apvts, params::ids::lfoKeySync, keySyncToggle);
}

LFOPanel::~LFOPanel() = default;

void LFOPanel::paint (juce::Graphics& g)
{
    g.fillAll (colors::surface);
    g.setColour (colors::borderAtAlpha (0.4f));
    g.drawRect (getLocalBounds(), 1);
}

void LFOPanel::resized()
{
    auto r = getLocalBounds().reduced (10);
    title.setBounds (r.removeFromTop (18));
    r.removeFromTop (4);

    shapeCombo.setBounds (r.removeFromTop (20));
    r.removeFromTop (8);

    rateLabel.setBounds  (r.removeFromTop (12));
    rateSlider.setBounds (r.removeFromTop (18));
    r.removeFromTop (4);
    depthLabel.setBounds  (r.removeFromTop (12));
    depthSlider.setBounds (r.removeFromTop (18));
    r.removeFromTop (6);
    keySyncToggle.setBounds (r.removeFromTop (18));
}

} // namespace brocot
