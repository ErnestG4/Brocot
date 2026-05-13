#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace brocot
{

class BrocotAudioProcessor;

/**
    One universal-modulator card in the V2 LFO bank. Bound to one mod
    index. APVTS attachments cover every host-automatable param; the live
    resolved-rate readout updates on a timer.

    Path navigation (Stern-Brocot ratio on the lock target) is deferred to
    V2.7 — for V2.5 the lock ratio is always 1/1 (raw target rate).
*/
class ModulatorPanel : public juce::Component,
                       private juce::Timer
{
public:
    ModulatorPanel (BrocotAudioProcessor& processor, int modIndex);
    ~ModulatorPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateVisibility();

    BrocotAudioProcessor& proc;
    const int modIndex;

    juce::Label header;
    juce::Label rateReadout;

    juce::Slider   rateSlider;
    juce::ComboBox lockKindCombo;
    juce::Slider   lockIndexSlider;

    juce::ComboBox shapeCombo;
    juce::Slider   kappaSlider;
    juce::Slider   depthSlider;

    juce::ComboBox targetKindCombo;
    juce::Slider   targetIndexSlider;

    using SliderAttach = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttach  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<SliderAttach> rateAttach;
    std::unique_ptr<ComboAttach>  lockKindAttach;
    std::unique_ptr<SliderAttach> lockIdxAttach;
    std::unique_ptr<ComboAttach>  shapeAttach;
    std::unique_ptr<SliderAttach> kappaAttach;
    std::unique_ptr<SliderAttach> depthAttach;
    std::unique_ptr<ComboAttach>  targetKindAttach;
    std::unique_ptr<SliderAttach> targetIdxAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModulatorPanel)
};

} // namespace brocot
