#pragma once

#include <juce_graphics/juce_graphics.h>

namespace brocot::fonts
{

/**
    BROCOT-SPEC.md §7 calls for EB Garamond (display/ratios) and JetBrains
    Mono (numbers/params), shipped under resources/fonts/. Those .ttf files
    aren't checked in yet — once they land, swap the FontOptions("Georgia",
    ...) calls below for Typeface::createSystemTypefaceFor() with the binary
    data. The component-level callers all reach for serif()/mono() helpers
    so the swap is a one-file change.
*/

inline juce::Font serif (float pointSize, int styleFlags = juce::Font::plain)
{
    return juce::Font (juce::FontOptions ("Georgia", pointSize,
                                          styleFlags));
}

inline juce::Font serifItalic (float pointSize)
{
    return serif (pointSize, juce::Font::italic);
}

inline juce::Font mono (float pointSize, int styleFlags = juce::Font::plain)
{
    return juce::Font (juce::FontOptions ("monospace", pointSize, styleFlags));
}

} // namespace brocot::fonts
