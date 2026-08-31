#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "DepthSlider.h"

namespace brocot
{

class BrocotAudioProcessor;

/**
    One operator card per BROCOT-SPEC.md §7. Bound to one operator index —
    owns its widgets, reads/writes path strings through the processor's
    thread-safe accessor. In shared mode the L/R/parent buttons yield to a
    position-along-the-shared-path spinner (setFreeModeWidgetsVisible).
*/
class OperatorPanel : public juce::Component,
                      private juce::Timer
{
public:
    OperatorPanel (BrocotAudioProcessor& processor, int operatorIndex);
    ~OperatorPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;   // right-click on
                                                         // detune → beat menu

private:
    void refreshFromProcessor();
    void mutatePath (const std::function<juce::String (const juce::String&)>& mutator);

    void timerCallback() override;

    BrocotAudioProcessor& proc;
    const int opIndex;

    juce::ToggleButton enabledToggle;
    DepthSlider        depthSlider;
    juce::Label        depthLabel;
    juce::Label        regimeLabel;
    juce::Label        ratioDisplay;
    juce::Label        pathLabel;

    juce::TextButton btnL    { "L" };
    juce::TextButton btnR    { "R" };
    juce::TextButton btnUp   { "parent" };
    juce::TextButton btnRoot { "1/1" };

    // Shared-mode position spinner (visible only when path mode is shared).
    juce::Slider positionSlider;
    juce::Label  positionLabel;

    // Per-op fine detune (cents off the exact SB ratio) and key-scaling
    // (modulation index vs pitch). Compact sliders sharing one bottom row;
    // tooltips + value suffixes label them (no separate label widgets, to
    // keep the dense 64-card grid uncluttered).
    juce::Slider detuneSlider;
    juce::Slider keyScaleSlider;
    // Self-feedback — created only on op 1 (index 0); shares the bottom row.
    juce::Slider feedbackSlider;
    // Operator waveform — compact combo on the top row. Only the engine's
    // real shapes are added as items; the waveform param's reserved entries
    // (indices 5..7) are deliberately absent so they can't be selected here.
    juce::ComboBox waveformCombo;
    // Pulse duty cycle — tiny slider beside the combo, enabled only while
    // the waveform is pulse (it's inaudible otherwise).
    juce::Slider dutySlider;

    void updateDutyEnabled();

    /** Right-click on the detune slider: beat-rate targeting (MAP-FIELD.md
        item 5). Offers a menu of target beat rates against the patch's
        reference operator, each solved in closed form to a detune value;
        targets that would leave the basin (or the ±100c detune range) show
        as disabled with the reason — refuse, don't clamp. */
    void showBeatTargetMenu();

    using SliderAttach = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttach = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttach  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<SliderAttach> depthAttach;
    std::unique_ptr<SliderAttach> positionAttach;
    std::unique_ptr<SliderAttach> detuneAttach;
    std::unique_ptr<SliderAttach> keyScaleAttach;
    std::unique_ptr<SliderAttach> feedbackAttach;  // op 1 (index 0) only
    std::unique_ptr<SliderAttach> dutyAttach;
    std::unique_ptr<ButtonAttach> enabledAttach;
    std::unique_ptr<ComboAttach>  waveformAttach;

    void setFreeModeWidgetsVisible (bool freeMode);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OperatorPanel)
};

} // namespace brocot
