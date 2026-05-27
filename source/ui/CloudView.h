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
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    /** Distinct colour per family index (stable hue wheel). */
    static juce::Colour familyColour (int index, int count);

private:
    void timerCallback() override;
    void rebuildImage();
    juce::Point<float> mapToScreen (float x, float y) const;
    juce::Point<float> screenToMap (juce::Point<float> px) const;
    int  nearestNodeToDot() const;
    void applyNode (int nodeIndex);

    BrocotAudioProcessor& proc;
    juce::Image cloud;              // baked static dots + labels
    int  currentNode { -1 };        // node nearest the dot (drives the patch)
    juce::Rectangle<float> plot;    // drawing area inside the margin

    // Direct-manipulation steering across the map. The dot is the cursor: it
    // follows the live patch until the user first interacts, then it DRIVES the
    // patch (the nearest node is applied as it moves). Click = seek toward that
    // point at the seek rate; double-click = drop the dot there instantly.
    juce::Point<float> dot;         // current cursor position, map space [-1,1]
    juce::Point<float> seekTarget;
    bool  seeking   { false };
    bool  dotDriving { false };     // false until first interaction
    juce::Slider seekRateKnob;      // map-units / second at full

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CloudView)
};

} // namespace brocot
