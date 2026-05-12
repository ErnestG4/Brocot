#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace brocot
{

/**
    Horizontal depth slider with Bessel landmark ticks per
    BROCOT-SPEC.md §7. Range 0..8 (the depth-max set by spec). Drawing:
      - Peaks: amber ticks (accent / accentBright depending on major/minor)
      - Zeros: cyan ticks (besselZero)
      - Major ticks (n ≤ 2) drawn taller than minor.

    The slider exposes a juce::Label for the nearest-landmark text;
    OperatorPanel owns the Label and updates it from the slider's value
    via a listener.
*/
class DepthSlider : public juce::Slider
{
public:
    DepthSlider();
    ~DepthSlider() override;

    void paint (juce::Graphics&) override;
    juce::String getTooltip() override;

private:
    juce::Rectangle<float> tickStripRect() const;
};

} // namespace brocot
