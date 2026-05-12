#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace brocot
{

class BrocotAudioProcessor;

/**
    Shared-path widget per BROCOT-SPEC.md §7. Displays the current shared
    path as a sequence of L/R cells; [+L] / [+R] extend, [-] retracts.
    OperatorPanel widgets show their position-along-this-path as a separate
    spinner/slider.

    Hidden when path mode is "free".
*/
class PathNavigator : public juce::Component,
                      private juce::Timer
{
public:
    explicit PathNavigator (BrocotAudioProcessor& processor);
    ~PathNavigator() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void refreshFromProcessor();
    void timerCallback() override;

    BrocotAudioProcessor& proc;
    juce::Label        ratioLabel;       // current resolved ratio
    juce::Label        pathLabel;        // L/R string
    juce::TextButton   btnAddL { "+L" };
    juce::TextButton   btnAddR { "+R" };
    juce::TextButton   btnDrop { "-" };

    juce::String cachedPath;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PathNavigator)
};

} // namespace brocot
