#pragma once

#include <juce_graphics/juce_graphics.h>

namespace brocot::colors
{

// BROCOT-SPEC.md §7 palette. Keep these centralised — every panel, slider,
// and custom-drawn control should reach for these names, not literal values.

inline const juce::Colour background       { 12,  10,  9   };  // very dark stone
inline const juce::Colour surface          { 20,  18,  16  };  // card background
inline const juce::Colour border           { 120, 80,  30  };  // dim amber (low alpha)
inline const juce::Colour primaryText      { 254, 243, 199 };  // warm cream
inline const juce::Colour secondaryText    { 253, 230, 138 };  // amber-200
inline const juce::Colour accent           { 245, 158, 11  };  // amber-500
inline const juce::Colour accentBright     { 252, 211, 77  };  // amber-300
inline const juce::Colour besselZero       { 103, 232, 249 };  // cyan-300
inline const juce::Colour disabled         { 87,  83,  78  };  // stone-600

inline juce::Colour borderAtAlpha (float alpha = 0.4f) { return border.withAlpha (alpha); }
inline juce::Colour secondaryAtAlpha (float alpha = 0.6f) { return secondaryText.withAlpha (alpha); }

} // namespace brocot::colors
