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
    ratioDisplay.setFont (fonts::serifItalic (26.0f));
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
    positionSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 40, 22);
    positionSlider.setRange (0.0, 32.0, 1.0);
    positionSlider.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);

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

    // Per-op detune + key-scaling — compact sliders, labelled by tooltip and
    // value suffix (the SB path gives the exact ratio; detune is the cents
    // off it where analog beating lives; key-scale tracks index to pitch).
    addAndMakeVisible (detuneSlider);
    detuneSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    detuneSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 42, 16);
    detuneSlider.setTextValueSuffix (" ct");
    detuneSlider.setTooltip ("Detune: cents off the exact Stern-Brocot ratio. "
                             "A few cents = analog beating / chorus / drift.");

    addAndMakeVisible (keyScaleSlider);
    keyScaleSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    keyScaleSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 42, 16);
    keyScaleSlider.setTooltip ("Key scale: how the modulation index tracks pitch. "
                               "+ = less FM up high (timbre stays coherent); - = more.");

    auto& apvts = proc.getAPVTS();
    enabledAttach  = std::make_unique<ButtonAttach> (
        apvts, params::opId (opIndex, "enabled"), enabledToggle);
    depthAttach    = std::make_unique<SliderAttach> (
        apvts, params::opId (opIndex, "depth"), depthSlider);
    positionAttach = std::make_unique<SliderAttach> (
        apvts, params::opId (opIndex, "position"), positionSlider);
    detuneAttach   = std::make_unique<SliderAttach> (
        apvts, params::opId (opIndex, "detune"), detuneSlider);
    keyScaleAttach = std::make_unique<SliderAttach> (
        apvts, params::opId (opIndex, "keyScale"), keyScaleSlider);

    // When the user enables this op in shared mode and the position is still 0
    // (i.e., this op has never been positioned), snap to the current shared
    // path tip so the new operator is audibly distinct from "1/1 root".
    enabledToggle.onStateChange = [this]
    {
        if (enabledToggle.getToggleState() && proc.isSharedMode())
        {
            if ((int) std::round (positionSlider.getValue()) == 0)
            {
                const int len = Path (proc.getSharedPath()).depth();
                if (len > 0)
                    positionSlider.setValue ((double) len, juce::sendNotificationSync);
            }
        }
    };

    setFreeModeWidgetsVisible (! proc.isSharedMode());
    refreshFromProcessor();
    startTimerHz (15);   // path & depth display refresh
}

OperatorPanel::~OperatorPanel() = default;

void OperatorPanel::setFreeModeWidgetsVisible (bool freeMode)
{
    btnL.setVisible (freeMode);
    btnR.setVisible (freeMode);
    btnUp.setVisible (freeMode);
    btnRoot.setVisible (freeMode);

    positionSlider.setVisible (! freeMode);
    positionLabel.setVisible  (! freeMode);
}

void OperatorPanel::refreshFromProcessor()
{
    const auto pathStr = proc.getOpPath (opIndex);
    const Path p (pathStr);
    int n = 0, d = 0;
    p.walk (n, d);

    ratioDisplay.setText (juce::String (n) + " / " + juce::String (d),
                          juce::dontSendNotification);
    pathLabel.setText (pathStr.isEmpty() ? juce::String (".") : pathStr,
                       juce::dontSendNotification);

    const float depth = (float) depthSlider.getValue();
    juce::String depthText = juce::String::formatted ("I = %.3f", (double) depth);
    if (const auto* lm = bessel::nearest (depth))
    {
        const char* sym = lm->kind == bessel::Landmark::Kind::Peak ? "+" : "0";
        depthText += juce::String::formatted ("  J%d %s", lm->n, sym);
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
    // Layout sketch:
    //   ┌──────────────────────────┐
    //   │ [x] mod N                │   enabledToggle  (18)
    //   │                          │   gap            (4)
    //   │      p / q (large)       │   ratioDisplay   (36)
    //   │      <path string>       │   pathLabel      (14)
    //   │                          │   gap            (6)
    //   │   [ free or shared       │   nav strip      (44)
    //   │     nav widgets ]        │
    //   │                          │   gap            (8)
    //   │ I = ...    regime label  │   depth row      (14)
    //   │ ────●────────────────    │   depthSlider    (22)
    //   │ det ──●──   key ──●──     │   tuning row     (16)
    //   └──────────────────────────┘
    auto r = getLocalBounds().reduced (8);

    // Reserve the detune / key-scale row from the *bottom* first, so it's
    // always laid out and visible even when the card is short (the elements
    // above squeeze instead of the tuning sliders vanishing).
    auto tuneRow = r.removeFromBottom (16);
    r.removeFromBottom (3);

    enabledToggle.setBounds (r.removeFromTop (18));
    r.removeFromTop (4);

    ratioDisplay.setBounds (r.removeFromTop (36));
    pathLabel.setBounds    (r.removeFromTop (14));
    r.removeFromTop (6);

    // 44-px nav strip is shared between free-mode (two button rows) and
    // shared-mode (centered position spinner). Each mode's widgets get the
    // full strip; only one set is visible at a time, so there's no overlap.
    auto navStrip = r.removeFromTop (44);

    // Free-mode layout: top row L/R, bottom row parent/root.
    {
        auto strip = navStrip;
        auto topRow = strip.removeFromTop (22);
        btnL.setBounds (topRow.removeFromLeft (topRow.getWidth() / 2).reduced (1));
        btnR.setBounds (topRow.reduced (1));

        auto bottomRow = strip.removeFromTop (20);
        btnUp.setBounds   (bottomRow.removeFromLeft (bottomRow.getWidth() / 2).reduced (1));
        btnRoot.setBounds (bottomRow.reduced (1));
    }

    // Shared-mode layout: label + spinner, vertically centered in the strip.
    {
        auto strip = navStrip;
        auto label = strip.removeFromTop (14);
        positionLabel.setBounds (label.reduced (2, 0));
        strip.removeFromTop (2);
        positionSlider.setBounds (strip.removeFromTop (24).reduced (2, 0));
    }

    r.removeFromTop (8);

    auto depthRow = r.removeFromTop (14);
    depthLabel.setBounds  (depthRow.removeFromLeft (depthRow.getWidth() * 3 / 5));
    regimeLabel.setBounds (depthRow);

    depthSlider.setBounds (r.removeFromTop (22));

    // Reserved bottom row: detune | key-scale, side by side.
    detuneSlider.setBounds   (tuneRow.removeFromLeft (tuneRow.getWidth() / 2).reduced (1, 0));
    keyScaleSlider.setBounds (tuneRow.reduced (1, 0));
}

} // namespace brocot
