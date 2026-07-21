#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

#include "../audio/Path.h"

namespace brocot
{

class BrocotAudioProcessor;

/**
    The "farey" view (BROCOT-SPEC.md §P1-a) — the map and the spectrum as one
    object. It draws the Hofstadter / almost-Mathieu butterfly directly from the
    validated `brocot::lattice` math: for every flux θ = p/q (q ≤ qMax) the
    allowed bands and the gaps between them, with each gap tinted by its Chern
    (Wannier) integer. Over the current patch it overlays:

      - a bright FLUX COLUMN at θ = frac(ratio) for every enabled operator —
        where the live sound sits on the wing;
      - the Stern-Brocot navigation CURSOR (the shared path) and its reachable
        L/R children as landable nodes, sized by gap-width prominence. Clicking
        a child extends the shared path (setSharedPath) — the nested-prefix ops
        move together, so the coherent family navigates as one.

    STRUCTURAL, not literal (§4.1): the butterfly is an organizing / annotation
    layer over ratio space, not a claim that a played note "is a band or gap".

    The butterfly itself is baked once into an image (it depends only on the
    lattice math, not the patch); only the overlay repaints as the patch or the
    cursor moves.
*/
class FareyView : public juce::Component,
                  private juce::Timer
{
public:
    explicit FareyView (BrocotAudioProcessor& processor);
    ~FareyView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void bakeButterfly();
    void syncCursorFromProcessor();
    void rebuildNodes();                   // recompute cursor + child viz (on cursor change)

    // θ ∈ [0,1] → x, E ∈ [-kEMax,kEMax] → y within the plot rectangle.
    float thetaToX (double theta) const;
    float energyToY (double e) const;

    static juce::Colour chernColour (int chern);

    BrocotAudioProcessor& proc;

    juce::Image butterfly;                 // baked band/gap image
    juce::Rectangle<float> plot;           // drawing rect (set in resized)

    Path cursor;                           // navigation position = shared path
    juce::String lastSharedPath;           // to detect external path changes

    // Cached per-node visuals — recomputed only when the cursor moves (the
    // prominence/Chern eigensolves must not run on every 10 Hz repaint).
    struct NodeVis
    {
        Path         path;
        Path         onClick;    // where a click on this node navigates
        double       theta;
        float        radius;
        juce::Colour colour;
        juce::String label;
        bool         isCursor;   // ring (current) vs filled dot (child)
    };
    std::vector<NodeVis> nodeVis;

    struct Target { Path path; juce::Rectangle<float> hit; };
    std::vector<Target> targets;           // clickable hit-rects, rebuilt in paint

    // q ceiling for the drawn wing and for keeping the cursor legible/cheap.
    static constexpr int    kQMax        = 32;
    static constexpr double kEMax        = 4.0;    // |E| ≤ 2λ+2 at λ=1
    static constexpr int    kBakeW       = 1000;
    static constexpr int    kBakeH       = 640;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FareyView)
};

} // namespace brocot
