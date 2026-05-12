#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "ui/BrocotLookAndFeel.h"
#include "ui/OperatorPanel.h"
#include "ui/PathNavigator.h"
#include "ui/FilterPanel.h"
#include "ui/EnvelopePanel.h"
#include "ui/LFOPanel.h"
#include "ui/RatioReadout.h"

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

    juce::ComboBox pathModeCombo;
    PathNavigator  pathNavigator { processorRef };
    RatioReadout   ratioReadout  { processorRef };
    std::array<std::unique_ptr<OperatorPanel>, params::kNumOperators> operatorPanels;
    FilterPanel    filterPanel   { processorRef };
    EnvelopePanel  envelopePanel { processorRef };
    LFOPanel       lfoPanel      { processorRef };

    using ComboAttach = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<ComboAttach> pathModeAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrocotAudioProcessorEditor)
};

} // namespace brocot
