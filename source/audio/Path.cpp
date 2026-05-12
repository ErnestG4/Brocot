#include "Path.h"

#include <cassert>

namespace brocot
{

namespace
{
    // Reject any non-{L,R} input rather than silently coercing — paths come
    // from user navigation, presets, and the path navigator widget, so the
    // origin is always under our control; a stray character is a bug.
    [[maybe_unused]] bool isValidStep (juce::juce_wchar c) noexcept
    {
        return c == 'L' || c == 'R';
    }
}

Path::Path (const juce::String& stepsIn)
    : steps (stepsIn)
{
    for ([[maybe_unused]] auto c : stepsIn)
        jassert (isValidStep (c));
}

Path Path::extended (juce::juce_wchar step) const
{
    jassert (isValidStep (step));
    return Path (steps + juce::String::charToString (step));
}

Path Path::parent() const
{
    if (steps.isEmpty())
        return *this;
    return Path (steps.dropLastCharacters (1));
}

void Path::walk (int& p, int& q) const
{
    int leftP = 0,  leftQ  = 1;
    int rightP = 1, rightQ = 0;
    int curP = 1,   curQ   = 1;

    for (auto step : steps)
    {
        if (step == 'L')
        {
            rightP = curP;
            rightQ = curQ;
        }
        else // 'R'
        {
            leftP = curP;
            leftQ = curQ;
        }
        curP = leftP + rightP;
        curQ = leftQ + rightQ;
    }

    p = curP;
    q = curQ;
}

void Path::walkWithChildren (int& p,  int& q,
                             int& lp, int& lq,
                             int& rp, int& rq) const
{
    int leftP = 0,  leftQ  = 1;
    int rightP = 1, rightQ = 0;
    int curP = 1,   curQ   = 1;

    for (auto step : steps)
    {
        if (step == 'L')
        {
            rightP = curP;
            rightQ = curQ;
        }
        else
        {
            leftP = curP;
            leftQ = curQ;
        }
        curP = leftP + rightP;
        curQ = leftQ + rightQ;
    }

    p = curP;
    q = curQ;
    lp = curP + leftP;
    lq = curQ + leftQ;
    rp = curP + rightP;
    rq = curQ + rightQ;
}

double Path::ratio() const
{
    int p, q;
    walk (p, q);
    return (double) p / (double) q;
}

Path Path::prefix (int length) const
{
    length = juce::jlimit (0, steps.length(), length);
    return Path (steps.substring (0, length));
}

bool Path::startsWith (const Path& other) const
{
    return steps.startsWith (other.steps);
}

Path Path::commonPrefix (const Path& other) const
{
    const int maxLen = juce::jmin (steps.length(), other.steps.length());
    int i = 0;
    while (i < maxLen && steps[i] == other.steps[i])
        ++i;
    return Path (steps.substring (0, i));
}

} // namespace brocot
