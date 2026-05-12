#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace brocot
{

class BrocotAudioProcessor;

class FilterPanel : public juce::Component
{
public:
    explicit FilterPanel (BrocotAudioProcessor& processor);
    ~FilterPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Label    title;
    juce::ComboBox typeCombo;
    juce::Slider   cutoffSlider;
    juce::Slider   resonanceSlider;
    juce::Label    cutoffLabel;
    juce::Label    resonanceLabel;

    using SliderAttach = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttach  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<SliderAttach> cutoffAttach;
    std::unique_ptr<SliderAttach> resonanceAttach;
    std::unique_ptr<ComboAttach>  typeAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilterPanel)
};

} // namespace brocot
