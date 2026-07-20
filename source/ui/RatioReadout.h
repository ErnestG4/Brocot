#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace brocot
{

class BrocotAudioProcessor;

/**
    Three-panel readout per BROCOT-SPEC.md §7:
      - Left: frequency ratios for all enabled operators ("2/1 · 4/3 · 9/7")
      - Middle: depth ratios with simplest-integer approximation and residue
                ("72 : 12 : 13  raw 5.13 : 0.86 : 0.90")
      - Right: path-prefix coherence (§P0-a) — the fraction of sideband energy
               the enabled modulators land on shared partials. Colour-coded so
               a coherent, prefix-related config reads bright. Display only.

    Display only — does not lock or modify operator state. Matches the
    farey-fm prototype's findDepthRatio behaviour.
*/
class RatioReadout : public juce::Component,
                     private juce::Timer
{
public:
    explicit RatioReadout (BrocotAudioProcessor& processor);
    ~RatioReadout() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshFromProcessor();

    BrocotAudioProcessor& proc;

    juce::Label freqHeader;
    juce::Label depthHeader;
    juce::Label coherenceHeader;
    juce::Label freqValues;
    juce::Label depthValues;
    juce::Label coherenceValues;

    float coherence01 { 0.0f };            // latest score, for the bar
    juce::Rectangle<int> coherenceBar {};  // bar bounds, set in resized()

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RatioReadout)
};

} // namespace brocot
