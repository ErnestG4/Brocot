#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace brocot
{

class BrocotAudioProcessor;

class LFOPanel : public juce::Component
{
public:
    explicit LFOPanel (BrocotAudioProcessor& processor);
    ~LFOPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Label        title;
    juce::ComboBox     shapeCombo;
    juce::Slider       rateSlider;
    juce::Slider       depthSlider;
    juce::ToggleButton keySyncToggle;
    juce::Label        rateLabel;
    juce::Label        depthLabel;

    using SliderAttach = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttach  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttach = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAttach> rateAttach;
    std::unique_ptr<SliderAttach> depthAttach;
    std::unique_ptr<ComboAttach>  shapeAttach;
    std::unique_ptr<ButtonAttach> keySyncAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LFOPanel)
};

} // namespace brocot
