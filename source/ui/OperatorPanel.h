#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "DepthSlider.h"

namespace brocot
{

class BrocotAudioProcessor;

/**
    One operator card in free mode per BROCOT-SPEC.md §7. Bound to one
    operator index — owns its widgets, reads/writes path strings through
    the processor's thread-safe accessor.

    Shared-mode UI variant lands in Slice 10 (path navigator + position).
*/
class OperatorPanel : public juce::Component,
                      private juce::Timer
{
public:
    OperatorPanel (BrocotAudioProcessor& processor, int operatorIndex);
    ~OperatorPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

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

    using SliderAttach = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttach = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAttach> depthAttach;
    std::unique_ptr<SliderAttach> positionAttach;
    std::unique_ptr<SliderAttach> detuneAttach;
    std::unique_ptr<SliderAttach> keyScaleAttach;
    std::unique_ptr<SliderAttach> feedbackAttach;  // op 1 (index 0) only
    std::unique_ptr<ButtonAttach> enabledAttach;

    void setFreeModeWidgetsVisible (bool freeMode);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OperatorPanel)
};

} // namespace brocot
