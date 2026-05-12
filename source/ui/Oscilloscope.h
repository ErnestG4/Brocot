#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace brocot
{

class BrocotAudioProcessor;

/**
    Live waveform display per BROCOT-SPEC.md §7. Amber waveform on dark
    background; faint grid; ~30 fps repaint via Timer. Pulls audio from the
    processor's lock-free scope ring.
*/
class Oscilloscope : public juce::Component,
                     private juce::Timer
{
public:
    explicit Oscilloscope (BrocotAudioProcessor& processor);
    ~Oscilloscope() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    BrocotAudioProcessor& proc;
    std::vector<float>    sampleBuffer;
    bool                  hasSamples { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Oscilloscope)
};

} // namespace brocot
