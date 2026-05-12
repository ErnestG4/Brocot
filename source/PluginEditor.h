#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "ui/BrocotLookAndFeel.h"
#include "ui/OperatorPanel.h"

namespace brocot
{

class BrocotAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit BrocotAudioProcessorEditor (BrocotAudioProcessor&);
    ~BrocotAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    BrocotAudioProcessor& processorRef;

    BrocotLookAndFeel lookAndFeel;

    std::array<std::unique_ptr<OperatorPanel>, params::kNumOperators> operatorPanels;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrocotAudioProcessorEditor)
};

} // namespace brocot
