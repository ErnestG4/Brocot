#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

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
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    /** Distinct colour per family index (stable hue wheel). */
    static juce::Colour familyColour (int index, int count);

    // How the dots are coloured — by imposed family, or by a measured spectral
    // characteristic (to reveal the real axes / fine structure of the cloud).
    // Simplicity is the SCIENCE-GATED fallback (phase3/butterfly.md): a
    // config-derived depth-weighted mean denominator q̄ — no prominence /
    // butterfly overlay ships (that study came back NO-GO).
    enum class ColourMode { Family, Brightness, Density, Feedback, Hollow, Musicality, Criticality, Simplicity };

private:
    void timerCallback() override;
    void rebuildImage();
    juce::Point<float> mapToScreen (float x, float y) const;
    juce::Point<float> screenToMap (juce::Point<float> px) const;
    int  nearestNodeToDot() const;
    int  nearestNodeTo (juce::Point<float> mapPt) const;
    void applyNode (int nodeIndex);
    static juce::Colour featureColour (float v01);   // low→high gradient

    BrocotAudioProcessor& proc;
    juce::Image cloud;              // baked static dots + labels
    int  currentNode { -1 };        // node nearest the dot (drives the patch)
    juce::int64 locatedSig { 0 };   // ops signature at the last passive locate
                                    // (skips the full-cloud scan when unchanged)
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

    // Criticality shimmer: the baked image can't twinkle, so the top-crit
    // nodes live as cached dots in the dynamic layer (criticality/voids mode
    // only). Salts are deterministic (rank-hashed at bake time, no RNG); the
    // alpha math is pure (util/CloudMath.h) so it stays testable.
    struct ShimmerDot { juce::Point<float> pos; float crit; juce::uint32 salt; };
    std::vector<ShimmerDot> shimmerDots;   // rebuilt with the image
    float shimmerPhase { 0.0f };           // seconds, advanced by the timer
    bool  shimmerFlip  { false };          // repaint every OTHER tick (15 Hz)

    // Wormhole arcs: render-verified smooth cross-basin jumps (Landscape::
    // getJumps). Dormant until a map ships jump data — the toggle only shows
    // then. Arcs incident to the hover/current node light up regardless of
    // the toggle; the full smoothest-200 layer is opt-in.
    juce::ToggleButton wormholeToggle;
    void drawJumpArcs (juce::Graphics&);

    // Simplicity mode cache: per-node q̄ plus the map's p2/p98 scaling, built
    // at image-rebuild time (the Path::walk pass is a bake cost, not a paint
    // cost). Empty in every other mode.
    std::vector<float> simpMeanQ;          // −1 = no active ops → grey
    float simpQLo { 0.0f }, simpQHi { 1.0f };

    ColourMode colourMode { ColourMode::Family };
    juce::ComboBox colourModeCombo;
    juce::ComboBox mapCombo;        // pick the operator-depth map to navigate
    int hoverNode { -1 };           // node nearest the mouse (inspect readout)
    bool hovering { false };        // mouse moving without a button (idle inspect)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CloudView)
};

} // namespace brocot
