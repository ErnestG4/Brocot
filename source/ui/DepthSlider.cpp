#include "DepthSlider.h"
#include "../audio/Bessel.h"
#include "../util/Colors.h"

namespace brocot
{

DepthSlider::DepthSlider()
{
    setSliderStyle (juce::Slider::LinearHorizontal);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setRange (0.0, (double) bessel::kDepthMax, 0.001);
}

DepthSlider::~DepthSlider() = default;

juce::Rectangle<float> DepthSlider::tickStripRect() const
{
    // Ticks live just above the slider track. The slider's track is in the
    // vertical middle (per BrocotLookAndFeel::drawLinearSlider), so we use
    // the top quarter of the local bounds for ticks.
    auto bounds = getLocalBounds().toFloat();
    return { bounds.getX(), bounds.getY() + 1.0f,
             bounds.getWidth(), bounds.getHeight() * 0.4f };
}

void DepthSlider::paint (juce::Graphics& g)
{
    auto strip = tickStripRect();
    const float stripBottom = strip.getBottom();

    for (const auto& lm : bessel::kLandmarks)
    {
        if (lm.value > bessel::kDepthMax) continue;
        const float t = lm.value / bessel::kDepthMax;
        const float x = strip.getX() + t * strip.getWidth();

        const bool isPeak = lm.kind == bessel::Landmark::Kind::Peak;
        const float tickHeight = lm.major ? strip.getHeight() * 0.8f
                                          : strip.getHeight() * 0.55f;

        juce::Colour c;
        if (isPeak)
            c = lm.major ? colors::accentBright : colors::accent.withAlpha (0.55f);
        else
            c = lm.major ? colors::besselZero.withAlpha (0.85f)
                         : colors::besselZero.withAlpha (0.45f);

        g.setColour (c);
        g.fillRect (juce::Rectangle<float> (x - 0.5f,
                                            stripBottom - tickHeight,
                                            1.0f, tickHeight));
    }

    // Delegate the actual track + thumb to the LookAndFeel.
    juce::Slider::paint (g);
}

juce::String DepthSlider::getTooltip()
{
    const auto* lm = bessel::nearest ((float) getValue());
    if (lm == nullptr)
        return juce::String::formatted ("I = %.3f", getValue());

    const char* kind = lm->kind == bessel::Landmark::Kind::Peak
                       ? "peak (max sideband)" : "zero (sideband null)";
    return juce::String::formatted ("J%d %s @ I = %.3f", lm->n, kind,
                                    (double) lm->value);
}

} // namespace brocot
