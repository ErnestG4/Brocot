#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace brocot
{

class BrocotAudioProcessor;

/**
    Neighbour-cloud visualiser — a pedagogical 2D map of the FM-soundspace
    landscape. Every graph node is a dot positioned by the offline 2D embedding
    (screen distance ≈ timbral distance) and coloured by its timbre family, with
    family names floating at their cluster centres. A moving highlight marks
    "where we are" (the node nearest the current patch, located by parameters),
    so you can watch steering traverse the landscape and eyeball whether the
    families are sensibly clustered / aligned.

    The static cloud + labels are baked into an image once; only the highlight
    repaints per tick.
*/
class CloudView : public juce::Component,
                  private juce::Timer
{
public:
    explicit CloudView (BrocotAudioProcessor&);
    ~CloudView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Distinct colour per family index (stable hue wheel). */
    static juce::Colour familyColour (int index, int count);

private:
    void timerCallback() override;
    void rebuildImage();
    juce::Point<float> mapToScreen (float x, float y) const;

    BrocotAudioProcessor& proc;
    juce::Image cloud;              // baked static dots + labels
    int  currentNode { -1 };        // located node (moves as the patch changes)
    juce::Rectangle<float> plot;    // drawing area inside the margin

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CloudView)
};

} // namespace brocot
