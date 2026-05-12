#include "BrocotLookAndFeel.h"
#include "../util/Colors.h"
#include "../util/Fonts.h"

namespace brocot
{

BrocotLookAndFeel::BrocotLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, colors::background);
    setColour (juce::Slider::backgroundColourId,         colors::surface);
    setColour (juce::Slider::trackColourId,              colors::accent.withAlpha (0.5f));
    setColour (juce::Slider::thumbColourId,              colors::accentBright);
    setColour (juce::Slider::textBoxTextColourId,        colors::primaryText);
    setColour (juce::Slider::textBoxBackgroundColourId,  colors::surface);
    setColour (juce::Slider::textBoxOutlineColourId,     colors::borderAtAlpha (0.2f));
    setColour (juce::TextButton::buttonColourId,         colors::surface);
    setColour (juce::TextButton::buttonOnColourId,       colors::accent);
    setColour (juce::TextButton::textColourOnId,         colors::background);
    setColour (juce::TextButton::textColourOffId,        colors::secondaryText);
    setColour (juce::ToggleButton::textColourId,         colors::secondaryText);
    setColour (juce::ToggleButton::tickColourId,         colors::accentBright);
    setColour (juce::ToggleButton::tickDisabledColourId, colors::disabled);
    setColour (juce::ComboBox::backgroundColourId,       colors::surface);
    setColour (juce::ComboBox::textColourId,             colors::primaryText);
    setColour (juce::ComboBox::outlineColourId,          colors::borderAtAlpha (0.4f));
    setColour (juce::PopupMenu::backgroundColourId,      colors::surface);
    setColour (juce::PopupMenu::textColourId,            colors::primaryText);
    setColour (juce::PopupMenu::highlightedBackgroundColourId,
               colors::accent.withAlpha (0.3f));
    setColour (juce::Label::textColourId,                colors::primaryText);
}

void BrocotLookAndFeel::drawLinearSlider (juce::Graphics& g,
                                          int x, int y, int width, int height,
                                          float sliderPos,
                                          float /*minSliderPos*/, float /*maxSliderPos*/,
                                          juce::Slider::SliderStyle style,
                                          juce::Slider& /*slider*/)
{
    // Horizontal-bar style: thin amber track, small bright thumb.
    const float trackHeight = 4.0f;
    const float trackY = (float) y + (float) height * 0.5f - trackHeight * 0.5f;

    g.setColour (colors::surface);
    g.fillRoundedRectangle ((float) x, trackY, (float) width, trackHeight, 1.5f);

    if (style == juce::Slider::LinearHorizontal || style == juce::Slider::LinearBar)
    {
        g.setColour (colors::accent.withAlpha (0.45f));
        g.fillRoundedRectangle ((float) x, trackY,
                                sliderPos - (float) x, trackHeight, 1.5f);

        const float thumbR = 6.0f;
        g.setColour (colors::accentBright);
        g.fillEllipse (sliderPos - thumbR, trackY + trackHeight * 0.5f - thumbR,
                       thumbR * 2.0f, thumbR * 2.0f);
    }
}

void BrocotLookAndFeel::drawButtonBackground (juce::Graphics& g,
                                              juce::Button& button,
                                              const juce::Colour& /*colour*/,
                                              bool isHighlighted,
                                              bool isDown)
{
    auto bounds = button.getLocalBounds().toFloat();
    const bool active = button.getToggleState();

    juce::Colour fill = active ? colors::accent
                       : isDown ? colors::accent.withAlpha (0.35f)
                       : isHighlighted ? colors::surface.brighter (0.10f)
                                       : colors::surface;
    g.setColour (fill);
    g.fillRect (bounds);

    g.setColour (active ? colors::accentBright
                        : colors::borderAtAlpha (0.4f));
    g.drawRect (bounds, 1.0f);
}

void BrocotLookAndFeel::drawToggleButton (juce::Graphics& g,
                                          juce::ToggleButton& button,
                                          bool isHighlighted, bool isDown)
{
    auto bounds = button.getLocalBounds().toFloat();
    const bool on = button.getToggleState();
    const float boxSize = juce::jmin (16.0f, bounds.getHeight() - 4.0f);
    auto box = juce::Rectangle<float> (bounds.getX() + 2.0f,
                                       bounds.getCentreY() - boxSize * 0.5f,
                                       boxSize, boxSize);

    g.setColour (on ? colors::accent : colors::surface);
    g.fillRect (box);

    g.setColour (on ? colors::accentBright : colors::borderAtAlpha (0.5f));
    g.drawRect (box, 1.0f);

    g.setColour (button.findColour (juce::ToggleButton::textColourId));
    g.setFont (fonts::mono (11.0f));
    auto textBounds = bounds.withTrimmedLeft (boxSize + 8.0f);
    g.drawFittedText (button.getButtonText(),
                      textBounds.toNearestInt(),
                      juce::Justification::centredLeft, 1);

    juce::ignoreUnused (isHighlighted, isDown);
}

} // namespace brocot
