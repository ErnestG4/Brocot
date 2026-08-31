#include "OperatorPanel.h"
#include "../PluginProcessor.h"
#include "../audio/Bessel.h"
#include "../audio/Path.h"
#include "../util/Colors.h"
#include "../util/FieldFrame.h"
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
    detuneSlider.addMouseListener (this, false);   // right-click → beat menu
    detuneSlider.setTooltip ("Detune: cents off the exact Stern-Brocot ratio. "
                             "A few cents = analog beating / chorus / drift. "
                             "Right-click: target a beat rate in Hz.");

    addAndMakeVisible (keyScaleSlider);
    keyScaleSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    keyScaleSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 42, 16);
    keyScaleSlider.setTooltip ("Key scale: how the modulation index tracks pitch. "
                               "+ = less FM up high (timbre stays coherent); - = more.");

    // Self-feedback exists on op 1 only — the one topology parallel FM can't
    // reach. Shares the bottom row (split three ways) when present.
    if (opIndex == 0)
    {
        addAndMakeVisible (feedbackSlider);
        feedbackSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        feedbackSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 36, 16);
        feedbackSlider.setTooltip ("Feedback: op 1 modulates itself. "
                                   "Adds bright, sawtooth-like harmonics.");
    }

    auto& apvts = proc.getAPVTS();
    // Operator waveform — compact combo (short labels for the dense grid).
    // Item index == choice index (the ComboBoxAttachment contract). The v2
    // param's reserved entries (5..7) are NOT added: they can't be picked
    // from the UI, and if the host parks the param on one the combo simply
    // shows no selection (the engine plays them as sine).
    addAndMakeVisible (waveformCombo);
    waveformCombo.addItem ("sin", 1);
    waveformCombo.addItem ("tri", 2);
    waveformCombo.addItem ("saw", 3);
    waveformCombo.addItem ("sqr", 4);
    waveformCombo.addItem ("pls", 5);
    waveformCombo.setTooltip (juce::CharPointer_UTF8 ("Operator waveform — non-sine modulators reshape "
                              "the FM sidebands (more timbre per op)."));
    waveformAttach = std::make_unique<ComboAttach> (
        apvts, params::opId (opIndex, "waveform"), waveformCombo);

    // Pulse duty — audible only for the pulse shape, so it's enabled only
    // while the combo sits on pulse (updated on change + by the timer, which
    // also catches host-driven parameter flips while the editor is open).
    addAndMakeVisible (dutySlider);
    dutySlider.setSliderStyle (juce::Slider::LinearHorizontal);
    dutySlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    dutySlider.setTooltip (juce::CharPointer_UTF8 ("Pulse duty cycle: 0.5 = square; narrower combs the "
                           "sidebands — harmonic n scales as |sin(\xcf\x80\xc2\xb7n\xc2\xb7"
                           "duty)|."));
    dutyAttach = std::make_unique<SliderAttach> (
        apvts, params::opId (opIndex, "duty"), dutySlider);

    // Even-harmonic suppression (double-pulse β) — pulse-only, like duty.
    addAndMakeVisible (evenSupSlider);
    evenSupSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    evenSupSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    evenSupSlider.setTooltip (juce::CharPointer_UTF8 ("Even-harmonic suppression (double-pulse \xce\xb2): "
                              "0 = plain pulse, 1 = odd harmonics only. Sweeps ~20 dB of "
                              "even-harmonic level and moves no ratio in or out of the "
                              "ringing set \xe2\x80\x94 a pure parity/timbre axis."));
    evenSupAttach = std::make_unique<SliderAttach> (
        apvts, params::opId (opIndex, "evensup"), evenSupSlider);
    waveformCombo.onChange = [this] { updateDutyEnabled(); };
    updateDutyEnabled();

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
    if (opIndex == 0)
        feedbackAttach = std::make_unique<SliderAttach> (
            apvts, params::opId (0, "feedback"), feedbackSlider);

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

void OperatorPanel::updateDutyEnabled()
{
    // Index 4 = "pulse" in the waveform choice (see ParameterTree.cpp).
    const bool isPulse = waveformCombo.getSelectedItemIndex() == 4;
    dutySlider.setEnabled (isPulse);
    evenSupSlider.setEnabled (isPulse);
}

void OperatorPanel::timerCallback()
{
    setFreeModeWidgetsVisible (! proc.isSharedMode());
    refreshFromProcessor();
    updateDutyEnabled();
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

    {
        auto topRow = r.removeFromTop (18);
        waveformCombo.setBounds (topRow.removeFromRight (46));   // right of the toggle
        dutySlider.setBounds (topRow.removeFromRight (34).reduced (2, 1));
        evenSupSlider.setBounds (topRow.removeFromRight (34).reduced (2, 1));
        enabledToggle.setBounds (topRow);
    }
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

    // Reserved bottom row: detune | key-scale (| feedback on op 1), side by
    // side. Op 1 splits the row three ways to fit its self-feedback slider.
    if (opIndex == 0)
    {
        const int third = tuneRow.getWidth() / 3;
        detuneSlider.setBounds   (tuneRow.removeFromLeft (third).reduced (1, 0));
        keyScaleSlider.setBounds (tuneRow.removeFromLeft (third).reduced (1, 0));
        feedbackSlider.setBounds (tuneRow.reduced (1, 0));
    }
    else
    {
        detuneSlider.setBounds   (tuneRow.removeFromLeft (tuneRow.getWidth() / 2).reduced (1, 0));
        keyScaleSlider.setBounds (tuneRow.reduced (1, 0));
    }
}

void OperatorPanel::mouseDown (const juce::MouseEvent& e)
{
    if (e.eventComponent == &detuneSlider && e.mods.isPopupMenu())
        showBeatTargetMenu();
}

void OperatorPanel::showBeatTargetMenu()
{
    auto& apvts = proc.getAPVTS();

    // Gather the enabled ops in field-frame form, remembering where this op
    // landed in the vector.
    std::vector<fieldframe::OpState> ops;
    int myPos = -1;
    for (int i = 0; i < params::kNumOperators; ++i)
    {
        bool enabled = false;
        if (auto* p = apvts.getRawParameterValue (params::opId (i, "enabled")))
            enabled = (p->load() > 0.5f);
        if (! enabled) continue;

        float depth = 0.0f, detune = 0.0f;
        if (auto* p = apvts.getRawParameterValue (params::opId (i, "depth")))  depth = p->load();
        if (auto* p = apvts.getRawParameterValue (params::opId (i, "detune"))) detune = p->load();

        const Path path (proc.getOpPath (i));
        int n = 0, d = 0;
        path.walk (n, d);
        if (i == opIndex) myPos = (int) ops.size();
        ops.push_back ({ n, d, (double) detune, (double) depth });
    }

    juce::PopupMenu menu;
    if (myPos < 0 || ops.size() < 2)
    {
        menu.addItem (1, "beat targeting needs two enabled operators", false);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (detuneSlider));
        return;
    }

    // Reference = simplest OTHER op (this op detunes against it).
    auto others = ops;
    others.erase (others.begin() + myPos);
    const int refInOthers = fieldframe::referenceIndex (others);
    const auto ref  = others[(size_t) refInOthers];
    const auto mine = ops[(size_t) myPos];
    const double carrierHz = proc.getLastNoteHz();

    const auto frame = fieldframe::pairReadout (ref, mine, carrierHz);
    if (! frame.valid)
    {
        menu.addItem (1, "no reachable coincidence at these depths", false);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (detuneSlider));
        return;
    }

    menu.addSectionHeader ("beat vs " + juce::String (frame.parent.p) + "/"
                           + juce::String (frame.parent.q) + " of "
                           + juce::String (ref.p) + "/" + juce::String (ref.q)
                           + "  @" + juce::String (carrierHz, 0) + "Hz");

    const double targets[] = { 0.0, 1.0, 2.0, 3.0, 5.0, 8.0, 12.0, 20.0 };
    for (double delta : targets)
    {
        const auto sol = fieldframe::solveBeatCents (ref, mine, carrierHz, delta);

        // Prefer the in-basin side closest to the current detune; a side is
        // usable only if it also fits the ±100c parameter range.
        auto usable = [] (bool ok, double cents)
            { return ok && std::abs (cents) <= 100.0; };
        const bool upU = usable (sol.upOk, sol.upCents), dnU = usable (sol.downOk, sol.downCents);

        juce::String label = delta == 0.0 ? "fuse (0 Hz)"
                                          : juce::String (delta, 0) + " Hz";
        if (! upU && ! dnU)
        {
            const char* why = (sol.upOk || sol.downOk) ? "out of detune range"
                                                       : "leaves the basin";
            menu.addItem (1000, label + "  -  " + why, false);
            continue;
        }

        const double cents = (upU && dnU)
            ? (std::abs (sol.upCents - mine.detuneCents)
                   <= std::abs (sol.downCents - mine.detuneCents) ? sol.upCents : sol.downCents)
            : (upU ? sol.upCents : sol.downCents);

        menu.addItem (label + "  (" + juce::String (cents, 1) + " ct)",
                      [this, cents] { detuneSlider.setValue (cents, juce::sendNotificationSync); });
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (detuneSlider));
}

} // namespace brocot
