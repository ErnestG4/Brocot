#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace brocot
{

class BrocotAudioProcessor;

class EnvelopePanel : public juce::Component
{
public:
    explicit EnvelopePanel (BrocotAudioProcessor& processor);
    ~EnvelopePanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Label  title;
    juce::Slider attackSlider, decaySlider, sustainSlider, releaseSlider;
    juce::Label  attackLabel,  decayLabel,  sustainLabel,  releaseLabel;

    using SliderAttach = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAttach> attackAttach, decayAttach, sustainAttach, releaseAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EnvelopePanel)
};

} // namespace brocot
