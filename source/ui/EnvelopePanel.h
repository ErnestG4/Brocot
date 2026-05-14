#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"

namespace brocot
{

/**
    A/D/S/R sliders bound to the global env.* APVTS params. Drives the
    voice's amp envelope (P3.10e re-wired the audio side; V2.8 had
    pinned it to a fixed gate). Compact four-slider strip sized for
    the steer tab.
*/
class EnvelopePanel : public juce::Component
{
public:
    explicit EnvelopePanel (BrocotAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttach = juce::AudioProcessorValueTreeState::SliderAttachment;

    BrocotAudioProcessor& proc;

    juce::Slider attack    { juce::Slider::LinearBarVertical, juce::Slider::NoTextBox };
    juce::Slider decay     { juce::Slider::LinearBarVertical, juce::Slider::NoTextBox };
    juce::Slider sustain   { juce::Slider::LinearBarVertical, juce::Slider::NoTextBox };
    juce::Slider release   { juce::Slider::LinearBarVertical, juce::Slider::NoTextBox };

    juce::Label aLabel { {}, "A" };
    juce::Label dLabel { {}, "D" };
    juce::Label sLabel { {}, "S" };
    juce::Label rLabel { {}, "R" };

    std::unique_ptr<SliderAttach> attackAttach;
    std::unique_ptr<SliderAttach> decayAttach;
    std::unique_ptr<SliderAttach> sustainAttach;
    std::unique_ptr<SliderAttach> releaseAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EnvelopePanel)
};

} // namespace brocot
