#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "ui/BrocotLookAndFeel.h"
#include "ui/PathNavigator.h"
#include "ui/RatioReadout.h"
#include "ui/Oscilloscope.h"
#include "ui/PresetBar.h"
#include "ui/AboutDialog.h"
#include "ui/SteerTab.h"
#include "ui/UnderTheHoodTab.h"

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
    juce::TooltipWindow tooltipWindow { this, 700 };

    juce::TextButton aboutBtn { "?" };
    PresetBar      presetBar { processorRef, processorRef.getPresetManager() };
    juce::MidiKeyboardComponent keyboard {
        processorRef.getKeyboardState(),
        juce::MidiKeyboardComponent::horizontalKeyboard
    };
    using ComboAttach  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAttach = juce::AudioProcessorValueTreeState::SliderAttachment;

    juce::ComboBox pathModeCombo;
    juce::ComboBox polyphonyCombo;
    juce::Slider   maxVoicesSlider;
    std::unique_ptr<ComboAttach>  pathModeAttach;
    std::unique_ptr<ComboAttach>  polyphonyAttach;
    std::unique_ptr<SliderAttach> maxVoicesAttach;

    PathNavigator  pathNavigator { processorRef };
    Oscilloscope   oscilloscope  { processorRef };
    RatioReadout   ratioReadout  { processorRef };

    // P3.10c: tabs. Persistent header (title row, path mode, oscilloscope,
    // ratio readout) sits above; tabs occupy the middle; keyboard stays
    // pinned to the bottom. Tab content swaps in the tabbed area.
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    SteerTab        steerTab        { processorRef };
    UnderTheHoodTab underTheHoodTab { processorRef };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrocotAudioProcessorEditor)
};

} // namespace brocot
