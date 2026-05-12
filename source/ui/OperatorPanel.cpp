#include "OperatorPanel.h"
#include "../PluginProcessor.h"
#include "../audio/Path.h"
#include "../util/Colors.h"
#include "../util/Fonts.h"

namespace brocot
{

OperatorPanel::OperatorPanel (BrocotAudioProcessor& processor, int operatorIndex)
    : proc (processor), opIndex (operatorIndex)
{
    addAndMakeVisible (enabledToggle);
    enabledToggle.setButtonText ("mod " + juce::String (opIndex + 1));

    addAndMakeVisible (ratioDisplay);
    ratioDisplay.setJustificationType (juce::Justification::centred);
    ratioDisplay.setFont (fonts::serifItalic (28.0f));
    ratioDisplay.setColour (juce::Label::textColourId, colors::primaryText);

    addAndMakeVisible (pathLabel);
    pathLabel.setJustificationType (juce::Justification::centred);
    pathLabel.setFont (fonts::mono (10.0f));
    pathLabel.setColour (juce::Label::textColourId, colors::secondaryAtAlpha (0.5f));

    for (auto* btn : { &btnL, &btnR, &btnUp, &btnRoot })
        addAndMakeVisible (*btn);

    btnL.onClick    = [this] { mutatePath ([] (const juce::String& s) { return s + "L"; }); };
    btnR.onClick    = [this] { mutatePath ([] (const juce::String& s) { return s + "R"; }); };
    btnUp.onClick   = [this] { mutatePath ([] (const juce::String& s)
                                  { return s.isEmpty() ? s : s.dropLastCharacters (1); }); };
    btnRoot.onClick = [this] { mutatePath ([] (const juce::String&) { return juce::String(); }); };

    addAndMakeVisible (depthSlider);
    depthSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    depthSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    depthSlider.setRange (0.0, 8.0, 0.001);

    addAndMakeVisible (depthLabel);
    depthLabel.setJustificationType (juce::Justification::centredLeft);
    depthLabel.setFont (fonts::mono (10.0f));
    depthLabel.setColour (juce::Label::textColourId, colors::secondaryAtAlpha (0.6f));

    auto& apvts = proc.getAPVTS();
    enabledAttach = std::make_unique<ButtonAttach> (
        apvts, params::opId (opIndex, "enabled"), enabledToggle);
    depthAttach   = std::make_unique<SliderAttach> (
        apvts, params::opId (opIndex, "depth"), depthSlider);

    refreshFromProcessor();
    startTimerHz (15);   // path & depth display refresh — paths change rarely
}

OperatorPanel::~OperatorPanel() = default;

void OperatorPanel::refreshFromProcessor()
{
    const auto pathStr = proc.getOpPath (opIndex);
    const Path p (pathStr);
    int n = 0, d = 0;
    p.walk (n, d);

    ratioDisplay.setText (juce::String (n) + " / " + juce::String (d),
                          juce::dontSendNotification);
    pathLabel.setText (pathStr.isEmpty() ? juce::String (juce::CharPointer_UTF8 ("·"))
                                         : pathStr,
                       juce::dontSendNotification);
    depthLabel.setText (juce::String::formatted ("I = %.3f", (double) depthSlider.getValue()),
                        juce::dontSendNotification);

    btnUp.setEnabled (! pathStr.isEmpty());
}

void OperatorPanel::mutatePath (const std::function<juce::String (const juce::String&)>& mutator)
{
    const juce::String current = proc.getOpPath (opIndex);
    proc.setOpPath (opIndex, mutator (current));
    refreshFromProcessor();
}

void OperatorPanel::timerCallback()
{
    refreshFromProcessor();
}

void OperatorPanel::paint (juce::Graphics& g)
{
    g.fillAll (colors::surface);

    g.setColour (colors::borderAtAlpha (0.4f));
    g.drawRect (getLocalBounds(), 1);
}

void OperatorPanel::resized()
{
    auto r = getLocalBounds().reduced (8);

    enabledToggle.setBounds (r.removeFromTop (18));
    r.removeFromTop (4);

    ratioDisplay.setBounds (r.removeFromTop (38));
    pathLabel.setBounds (r.removeFromTop (14));
    r.removeFromTop (4);

    auto navRow = r.removeFromTop (22);
    btnL.setBounds    (navRow.removeFromLeft (navRow.getWidth() / 2));
    btnR.setBounds    (navRow);
    r.removeFromTop (3);

    auto navRow2 = r.removeFromTop (18);
    btnUp.setBounds   (navRow2.removeFromLeft (navRow2.getWidth() / 2));
    btnRoot.setBounds (navRow2);
    r.removeFromTop (8);

    depthLabel.setBounds  (r.removeFromTop (14));
    depthSlider.setBounds (r.removeFromTop (18));
}

} // namespace brocot
