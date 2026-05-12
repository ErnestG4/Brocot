#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace brocot
{

/**
    Port of farey-fm.html's findDepthRatio: finds the simplest small-integer
    ratio that approximates a set of depth values. Returns an empty string if
    no operators have audible depth.

    The integer / scale search treats values < epsilon (0.02 by default) as
    silent (rendered as ".") and normalises the remainder by the minimum.
    A residue note is appended when the best fit isn't within epsilon.
*/
juce::String findDepthRatio (const std::vector<float>& depths,
                             int maxDenom = 12,
                             float epsilon = 0.02f);

} // namespace brocot
