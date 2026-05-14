#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"

namespace brocot
{

/**
    Filter UI for the steer tab. Wraps the existing FilterBank's APVTS
    params (type / cutoff / resonance). The audio side is already
    wired in PluginProcessor; this is the missing visual surface for
    the existing capability.

    "Multi-filter" in Will's punch list = the type combo gives access
    to LP / BP / HP / Notch from the same Cytomic SVF. If we add
    parallel filter instances later, the panel grows; for now it's
    one filter with a mode selector.
*/
class FilterPanel : public juce::Component
{
public:
    explicit FilterPanel (BrocotAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttach = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttach  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    BrocotAudioProcessor& proc;

    juce::ComboBox typeCombo;
    juce::Slider   cutoff      { juce::Slider::LinearBarVertical, juce::Slider::NoTextBox };
    juce::Slider   resonance   { juce::Slider::LinearBarVertical, juce::Slider::NoTextBox };

    juce::Label typeLabel  { {}, "type" };
    juce::Label cutoffLabel { {}, "cutoff" };
    juce::Label resonanceLabel { {}, "res" };

    std::unique_ptr<ComboAttach>  typeAttach;
    std::unique_ptr<SliderAttach> cutoffAttach;
    std::unique_ptr<SliderAttach> resonanceAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilterPanel)
};

} // namespace brocot
