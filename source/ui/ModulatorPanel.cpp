#include "ModulatorPanel.h"
#include "../PluginProcessor.h"
#include "../audio/Path.h"
#include "../util/Colors.h"
#include "../util/Fonts.h"

namespace brocot
{

ModulatorPanel::ModulatorPanel (BrocotAudioProcessor& processor, int modIndexIn)
    : proc (processor), modIndex (modIndexIn)
{
    addAndMakeVisible (header);
    header.setFont (fonts::serifItalic (14.0f));
    header.setColour (juce::Label::textColourId, colors::primaryText);
    header.setText ("mod " + juce::String (modIndex + 1), juce::dontSendNotification);

    addAndMakeVisible (rateReadout);
    rateReadout.setFont (fonts::mono (9.0f));
    rateReadout.setJustificationType (juce::Justification::centredRight);
    rateReadout.setColour (juce::Label::textColourId, colors::secondaryAtAlpha (0.6f));

    auto& apvts = proc.getAPVTS();
    const auto setupSlider = [] (juce::Slider& s, juce::Slider::SliderStyle style)
    {
        s.setSliderStyle (style);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    };

    addAndMakeVisible (rateSlider);
    setupSlider (rateSlider, juce::Slider::LinearHorizontal);
    rateAttach = std::make_unique<SliderAttach> (
        apvts, params::modId (modIndex, "rate"), rateSlider);

    addAndMakeVisible (lockKindCombo);
    lockKindCombo.addItem ("free",    1);
    lockKindCombo.addItem ("carrier", 2);
    lockKindCombo.addItem ("op",      3);
    lockKindCombo.addItem ("mod",     4);
    lockKindAttach = std::make_unique<ComboAttach> (
        apvts, params::modId (modIndex, "lockKind"), lockKindCombo);

    addAndMakeVisible (lockIndexSlider);
    lockIndexSlider.setSliderStyle (juce::Slider::IncDecButtons);
    lockIndexSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 28, 16);
    lockIndexSlider.setRange (0, 15, 1);
    lockIdxAttach = std::make_unique<SliderAttach> (
        apvts, params::modId (modIndex, "lockIndex"), lockIndexSlider);

    // Stern-Brocot path nav for the lock ratio.
    addAndMakeVisible (pqDisplay);
    pqDisplay.setFont (fonts::serifItalic (12.0f));
    pqDisplay.setJustificationType (juce::Justification::centredLeft);
    pqDisplay.setColour (juce::Label::textColourId, colors::accent);

    for (auto* btn : { &btnL, &btnR, &btnUp, &btnHome })
        addAndMakeVisible (*btn);

    auto mutatePath = [this] (auto mutator)
    {
        const juce::String current = proc.getModPath (modIndex);
        proc.setModPath (modIndex, mutator (current));
    };
    btnL.onClick    = [this, mutatePath] { mutatePath ([] (const juce::String& s) { return s + "L"; }); };
    btnR.onClick    = [this, mutatePath] { mutatePath ([] (const juce::String& s) { return s + "R"; }); };
    btnUp.onClick   = [this, mutatePath] { mutatePath ([] (const juce::String& s)
                                  { return s.isEmpty() ? s : s.dropLastCharacters (1); }); };
    btnHome.onClick = [this, mutatePath] { mutatePath ([] (const juce::String&) { return juce::String(); }); };

    addAndMakeVisible (shapeCombo);
    shapeCombo.addItem ("sine",     1);
    shapeCombo.addItem ("vonmises", 2);
    shapeCombo.addItem ("gauss",    3);
    shapeCombo.addItem ("square",   4);
    shapeCombo.addItem ("saw rise", 5);
    shapeCombo.addItem ("saw fall", 6);
    shapeCombo.addItem ("triangle", 7);
    shapeCombo.addItem ("expdecay", 8);
    shapeAttach = std::make_unique<ComboAttach> (
        apvts, params::modId (modIndex, "shape"), shapeCombo);

    addAndMakeVisible (kappaSlider);
    setupSlider (kappaSlider, juce::Slider::LinearHorizontal);
    kappaAttach = std::make_unique<SliderAttach> (
        apvts, params::modId (modIndex, "kappa"), kappaSlider);

    addAndMakeVisible (depthSlider);
    setupSlider (depthSlider, juce::Slider::LinearHorizontal);
    depthAttach = std::make_unique<SliderAttach> (
        apvts, params::modId (modIndex, "depth"), depthSlider);

    addAndMakeVisible (targetKindCombo);
    targetKindCombo.addItem ("filter cutoff", 1);
    targetKindCombo.addItem ("carrier AM",    2);
    targetKindCombo.addItem ("carrier FM",    3);
    targetKindCombo.addItem ("op depth",      4);
    targetKindCombo.addItem ("op position",   5);
    targetKindCombo.addItem ("mod rate",      6);
    targetKindCombo.addItem ("mod depth",     7);
    targetKindCombo.addItem ("feedback (op1)", 8);
    targetKindCombo.addItem ("env retrig",     9);
    targetKindAttach = std::make_unique<ComboAttach> (
        apvts, params::modId (modIndex, "targetKind"), targetKindCombo);

    addAndMakeVisible (targetIndexSlider);
    targetIndexSlider.setSliderStyle (juce::Slider::IncDecButtons);
    targetIndexSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 28, 16);
    targetIndexSlider.setRange (0, 15, 1);
    targetIdxAttach = std::make_unique<SliderAttach> (
        apvts, params::modId (modIndex, "targetIndex"), targetIndexSlider);

    addAndMakeVisible (keySyncToggle);
    keySyncToggle.setButtonText ("key-sync");
    keySyncAttach = std::make_unique<ButtonAttach> (
        apvts, params::modId (modIndex, "keySync"), keySyncToggle);

    // Reflect lockKind / targetKind in widget visibility immediately + on
    // every 10Hz refresh.
    lockKindCombo.onChange = [this] { updateVisibility(); };
    targetKindCombo.onChange = [this] { updateVisibility(); };
    updateVisibility();

    startTimerHz (10);
}

ModulatorPanel::~ModulatorPanel() = default;

void ModulatorPanel::timerCallback()
{
    const float hz = proc.getResolvedModRate (modIndex);
    const char* zone = hz >= 20.0f ? "audio" : "sub";
    rateReadout.setText (juce::String::formatted ("%.2f Hz | %s", (double) hz, zone),
                         juce::dontSendNotification);

    // Refresh path readout in case path or carrier changed externally.
    const auto pathStr = proc.getModPath (modIndex);
    const Path p (pathStr);
    int n = 0, d = 0;
    p.walk (n, d);
    pqDisplay.setText (juce::String (n) + "/" + juce::String (d),
                       juce::dontSendNotification);
    btnUp.setEnabled (! pathStr.isEmpty());
}

void ModulatorPanel::updateVisibility()
{
    const int lockKind   = lockKindCombo.getSelectedItemIndex();
    const int targetKind = targetKindCombo.getSelectedItemIndex();

    // Lock index only meaningful for Op (=2) or Mod (=3).
    const bool showLockIdx = (lockKind == 2 || lockKind == 3);
    lockIndexSlider.setVisible (showLockIdx);

    // Rate slider hidden when locked — the path nav row + readout take its
    // place; lock kind × SB-ratio = the resolved rate (shown in the header).
    const bool isLocked = (lockKind != 0);
    rateSlider.setVisible (! isLocked);

    pqDisplay.setVisible (isLocked);
    btnL.setVisible  (isLocked);
    btnR.setVisible  (isLocked);
    btnUp.setVisible (isLocked);
    btnHome.setVisible (isLocked);

    // Target index meaningful for OpDepth(3) / OpPos(4) / ModRate(5) /
    // ModDepth(6).
    const bool showTgtIdx = targetKind >= 3 && targetKind <= 6;
    targetIndexSlider.setVisible (showTgtIdx);
}

void ModulatorPanel::paint (juce::Graphics& g)
{
    g.fillAll (colors::surface);
    g.setColour (colors::borderAtAlpha (0.4f));
    g.drawRect (getLocalBounds(), 1);

    // Left-edge stripe matches the resolved-rate zone (amber = audio,
    // green-amber = sub-audio) for quick visual scanning.
    const float hz = proc.getResolvedModRate (modIndex);
    g.setColour (hz >= 20.0f ? colors::accent : colors::secondaryAtAlpha (0.5f));
    g.fillRect (juce::Rectangle<int> (0, 0, 2, getHeight()));
}

void ModulatorPanel::resized()
{
    auto r = getLocalBounds().reduced (6).withTrimmedLeft (4);

    // Header row: title + live rate readout.
    auto headerRow = r.removeFromTop (16);
    header.setBounds (headerRow.removeFromLeft (60));
    rateReadout.setBounds (headerRow);
    r.removeFromTop (2);

    // Lock kind + (optional) index.
    auto lockRow = r.removeFromTop (18);
    lockKindCombo.setBounds (lockRow.removeFromLeft (lockRow.getWidth() * 5 / 8).reduced (1));
    lockIndexSlider.setBounds (lockRow.reduced (1));
    r.removeFromTop (2);

    // Rate slider OR path-nav row (mutually exclusive via visibility).
    auto rateRow = r.removeFromTop (16);
    rateSlider.setBounds (rateRow);

    // Layout the path-nav widgets in the same slot:
    //   [<] [1] [L] [R]    p/q
    auto navArea = rateRow;
    pqDisplay.setBounds (navArea.removeFromRight (navArea.getWidth() / 3));
    const int btnW = navArea.getWidth() / 4;
    btnUp.setBounds   (navArea.removeFromLeft (btnW).reduced (1, 0));
    btnHome.setBounds (navArea.removeFromLeft (btnW).reduced (1, 0));
    btnL.setBounds    (navArea.removeFromLeft (btnW).reduced (1, 0));
    btnR.setBounds    (navArea.reduced (1, 0));

    r.removeFromTop (4);

    // Shape.
    shapeCombo.setBounds (r.removeFromTop (18));
    r.removeFromTop (2);

    // Kappa + depth.
    kappaSlider.setBounds (r.removeFromTop (14));
    r.removeFromTop (2);
    depthSlider.setBounds (r.removeFromTop (14));
    r.removeFromTop (4);

    // Target kind + (optional) index.
    auto tgtRow = r.removeFromTop (18);
    targetKindCombo.setBounds (tgtRow.removeFromLeft (tgtRow.getWidth() * 5 / 8).reduced (1));
    targetIndexSlider.setBounds (tgtRow.reduced (1));

    // Key-sync toggle on its own short row.
    r.removeFromTop (2);
    keySyncToggle.setBounds (r.removeFromTop (16).reduced (1, 0));
}

} // namespace brocot
