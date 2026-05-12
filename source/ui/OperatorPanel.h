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

    using SliderAttach = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttach = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAttach> depthAttach;
    std::unique_ptr<SliderAttach> positionAttach;
    std::unique_ptr<ButtonAttach> enabledAttach;

    void setFreeModeWidgetsVisible (bool freeMode);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OperatorPanel)
};

} // namespace brocot
