#include "FilterPanel.h"
#include "../PluginProcessor.h"
#include "../util/Colors.h"
#include "../util/Fonts.h"

namespace brocot
{

FilterPanel::FilterPanel (BrocotAudioProcessor& processor)
{
    addAndMakeVisible (title);
    title.setText ("filter", juce::dontSendNotification);
    title.setFont (fonts::serifItalic (14.0f));
    title.setColour (juce::Label::textColourId, colors::primaryText);

    addAndMakeVisible (typeCombo);
    typeCombo.addItem ("LP",    1);
    typeCombo.addItem ("BP",    2);
    typeCombo.addItem ("HP",    3);
    typeCombo.addItem ("Notch", 4);

    addAndMakeVisible (cutoffSlider);
    cutoffSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    cutoffSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);

    addAndMakeVisible (resonanceSlider);
    resonanceSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    resonanceSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);

    for (auto* l : { &cutoffLabel, &resonanceLabel })
    {
        addAndMakeVisible (*l);
        l->setFont (fonts::mono (10.0f));
        l->setColour (juce::Label::textColourId, colors::secondaryAtAlpha (0.6f));
    }
    cutoffLabel.setText    ("cutoff",    juce::dontSendNotification);
    resonanceLabel.setText ("resonance", juce::dontSendNotification);

    auto& apvts = processor.getAPVTS();
    typeAttach      = std::make_unique<ComboAttach>  (apvts, params::ids::filterType,      typeCombo);
    cutoffAttach    = std::make_unique<SliderAttach> (apvts, params::ids::filterCutoff,    cutoffSlider);
    resonanceAttach = std::make_unique<SliderAttach> (apvts, params::ids::filterResonance, resonanceSlider);
}

FilterPanel::~FilterPanel() = default;

void FilterPanel::paint (juce::Graphics& g)
{
    g.fillAll (colors::surface);
    g.setColour (colors::borderAtAlpha (0.4f));
    g.drawRect (getLocalBounds(), 1);
}

void FilterPanel::resized()
{
    auto r = getLocalBounds().reduced (10);
    title.setBounds (r.removeFromTop (18));
    r.removeFromTop (4);

    typeCombo.setBounds (r.removeFromTop (20));
    r.removeFromTop (8);

    cutoffLabel.setBounds   (r.removeFromTop (12));
    cutoffSlider.setBounds  (r.removeFromTop (18));
    r.removeFromTop (4);
    resonanceLabel.setBounds  (r.removeFromTop (12));
    resonanceSlider.setBounds (r.removeFromTop (18));
}

} // namespace brocot
