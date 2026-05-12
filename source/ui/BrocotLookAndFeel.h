#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace brocot
{

/**
    Custom LookAndFeel covering Brocot's slider/button drawing per
    BROCOT-SPEC.md §7. Subclasses LookAndFeel_V4 so anything we don't
    override falls back to JUCE's modern defaults.

    Look-and-feel target: "vintage scientific instrument meets demoscene
    meets math notebook." Dark mode only in v1.
*/
class BrocotLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BrocotLookAndFeel();

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;
};

} // namespace brocot
