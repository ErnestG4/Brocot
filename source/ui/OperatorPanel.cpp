#include "OperatorPanel.h"
#include "../PluginProcessor.h"
#include "../audio/Bessel.h"
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

    addAndMakeVisible (positionSlider);
    positionSlider.setSliderStyle (juce::Slider::IncDecButtons);
    positionSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 30, 18);
    positionSlider.setRange (0.0, 32.0, 1.0);

    addAndMakeVisible (positionLabel);
    positionLabel.setText ("position", juce::dontSendNotification);
    positionLabel.setJustificationType (juce::Justification::centredLeft);
    positionLabel.setFont (fonts::mono (10.0f));
    positionLabel.setColour (juce::Label::textColourId, colors::secondaryAtAlpha (0.5f));

    addAndMakeVisible (depthSlider);

    addAndMakeVisible (depthLabel);
    depthLabel.setJustificationType (juce::Justification::centredLeft);
    depthLabel.setFont (fonts::mono (10.0f));
    depthLabel.setColour (juce::Label::textColourId, colors::secondaryAtAlpha (0.6f));

    addAndMakeVisible (regimeLabel);
    regimeLabel.setJustificationType (juce::Justification::centredRight);
    regimeLabel.setFont (fonts::serifItalic (10.0f));
    regimeLabel.setColour (juce::Label::textColourId, colors::secondaryAtAlpha (0.4f));

    auto& apvts = proc.getAPVTS();
    enabledAttach  = std::make_unique<ButtonAttach> (
        apvts, params::opId (opIndex, "enabled"), enabledToggle);
    depthAttach    = std::make_unique<SliderAttach> (
        apvts, params::opId (opIndex, "depth"), depthSlider);
    positionAttach = std::make_unique<SliderAttach> (
        apvts, params::opId (opIndex, "position"), positionSlider);

    setFreeModeWidgetsVisible (! proc.isSharedMode());
    refreshFromProcessor();
    startTimerHz (15);   // path & depth display refresh — paths change rarely
}

void OperatorPanel::setFreeModeWidgetsVisible (bool freeMode)
{
    btnL.setVisible (freeMode);
    btnR.setVisible (freeMode);
    btnUp.setVisible (freeMode);
    btnRoot.setVisible (freeMode);

    positionSlider.setVisible (! freeMode);
    positionLabel.setVisible  (! freeMode);
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

    const float depth = (float) depthSlider.getValue();
    juce::String depthText = juce::String::formatted ("I = %.3f", (double) depth);
    if (const auto* lm = bessel::nearest (depth))
    {
        const char* sym = lm->kind == bessel::Landmark::Kind::Peak ? "+" : "0";
        depthText += juce::String::formatted ("  ·  J%d %s", lm->n, sym);
    }
    depthLabel.setText (depthText, juce::dontSendNotification);
    regimeLabel.setText (bessel::regimeLabel (depth), juce::dontSendNotification);

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
    setFreeModeWidgetsVisible (! proc.isSharedMode());
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

    // Both navigation widgets get laid out into the same region; only one
    // set is visible at any time (driven by path-mode toggle).
    auto navRow = r.removeFromTop (22);
    auto rowFreeL = navRow.removeFromLeft (navRow.getWidth() / 2);
    btnL.setBounds (rowFreeL);
    btnR.setBounds (navRow);
    r.removeFromTop (3);

    auto navRow2 = r.removeFromTop (18);
    btnUp.setBounds   (navRow2.removeFromLeft (navRow2.getWidth() / 2));
    btnRoot.setBounds (navRow2);
    r.removeFromTop (8);

    // Shared-mode widgets overlay the same vertical slot (only visible when
    // not in free mode).
    positionLabel.setBounds (rowFreeL.withY (rowFreeL.getY()).withTrimmedLeft (4));
    auto posSliderArea = navRow.withX (rowFreeL.getX()).withWidth (rowFreeL.getWidth() + navRow.getWidth())
                                  .withHeight (40)
                                  .withY (rowFreeL.getY() + 16);
    positionSlider.setBounds (posSliderArea);

    depthSlider.setBounds (r.removeFromTop (22));
    auto labelRow = r.removeFromTop (14);
    depthLabel.setBounds  (labelRow.removeFromLeft (labelRow.getWidth() * 2 / 3));
    regimeLabel.setBounds (labelRow);
}

} // namespace brocot
