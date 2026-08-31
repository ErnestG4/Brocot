#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <atomic>
#include <vector>

#include "../audio/Path.h"
#include "../params/ParameterTree.h"
#include "../util/FieldFrame.h"

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
        move together, so the coherent family navigates as one;
      - a PER-OPERATOR layer: every enabled op as a numbered, clickable marker
        at its θ, with pair-relation ARCS between them from the coincidence
        horizon's own pair frame (fuses / beat band / no arc = no reachable
        coincidence). Clicking a marker SELECTS that op: the arrow keys and
        node clicks then move that one partial (free mode edits its path,
        shared mode its position) instead of the whole stack; esc or an empty
        click releases. This is how the tree shows relationships between the
        partials and lets you change one without moving the rest.

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
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void bakeButterfly();
    void syncCursorFromProcessor();
    void rebuildNodes();                   // recompute cursor + child viz (on cursor change)
    void rebuildTargets();                 // recompute hit-rects (on cursor change / resize)
    void rescaleButterfly();               // re-derive scaledButterfly (resize only)
    juce::String computeFluxSig() const;   // cheap state hash for the repaint gate

    // θ ∈ [0,1] → x, E ∈ [-kEMax,kEMax] → y within the plot rectangle.
    float thetaToX (double theta) const;
    float energyToY (double e) const;

    static juce::Colour chernColour (int chern);

    BrocotAudioProcessor& proc;

    juce::Image butterfly;                 // baked band/gap image (fixed size;
                                           // ref to the process-wide bake)
    juce::Image scaledButterfly;           // pre-scaled to plot — never
                                           // stretch-blit per steady-state frame
    bool rescalePending { true };          // live-resize debounce: paint
                                           // stretch-blits the raw bake until
                                           // the size holds still for a tick
    int  rescaleStableTicks { 0 };
    juce::Rectangle<float> plot;           // drawing rect (set in resized)
    juce::String fluxSig;                  // repaint gate: shared path + per-op
                                           // enabled/path signature

    // Raw enabled-param pointers, resolved once — the 10 Hz signature must not
    // build 64 param-ID strings + map lookups per tick. Positions are cached
    // alongside for the partial-plan gestures.
    std::array<std::atomic<float>*, params::kNumOperators> opEnabledRaw {};
    std::array<std::atomic<float>*, params::kNumOperators> opPositionRaw {};
    std::array<std::atomic<float>*, params::kNumOperators> opDepthRaw {};
    std::array<std::atomic<float>*, params::kNumOperators> opWaveRaw {};

    /** Coincidence-horizon overlay state (COINCIDENCE-HORIZON.md §4 proposed
        feature, gated by the validity boundary). Recomputed each rebuild:
        drawn ONLY when the patch is in the theorem's Exact scope (two
        effective sine modulators). BLo/BHi are the two ops' order bounds; a
        node p/q is judged by the asymmetric corollary under both possible
        assignments of the indices to the two sides — rings either way, rings
        for exactly one assignment, or cannot ring at all. Equal depths
        collapse this to the exact matched horizon. */
    void computeHorizon();
    bool horizonActive { false };
    int  horizonBLo { 0 };
    int  horizonBHi { 0 };

    // ------------------------------------------------------------------
    // Per-operator layer: every enabled op as a labelled, selectable marker
    // on the θ axis, with PAIR-RELATION arcs between them — fuses / beat
    // band / (no arc = no reachable coincidence), computed from the
    // horizon's own pair frame (FieldFrame) with each op's exact rational,
    // detune, and its OWN index. Selecting a marker retargets the tree
    // gestures at THAT operator alone: keyboard steps and node clicks move
    // one partial instead of the whole stack (free mode edits its path;
    // shared mode steps its position along the shared path).
    // ------------------------------------------------------------------
    struct OpMarker
    {
        int op { 0 };                    // APVTS operator index
        int p { 1 }, q { 1 };
        double theta { 0.0 };
        juce::Rectangle<float> hit;
    };
    struct PairRel
    {
        int a { 0 }, b { 0 };            // indices into opMarkers (a = ref)
        fieldframe::PairReadout r;
    };
    void rebuildOpMarkers();             // markers + relations (sig-gated)
    void stepSelectedOp (int direction); // ±1: free = path L/R, shared = pos
    void ascendSelectedOp();             // free mode: parent
    void moveSelectedOpTo (const Path& node);
    static juce::Colour bandColour (const fieldframe::PairReadout& r);

    std::vector<OpMarker> opMarkers;
    std::vector<PairRel>  pairRels;
    int selectedOp { -1 };               // -1 = none; gestures drive the stack
    std::array<std::atomic<float>*, params::kNumOperators> opDetuneRaw {};

    // ------------------------------------------------------------------
    // Shared-path tool row (workstream F4). All gestures act through the
    // params / setSharedPath, so undo-less but host-visible; every action
    // confirms (or explains a refusal) via the transient hint line.
    // ------------------------------------------------------------------
    void ascend();                           // cursor.parent()
    void descend (juce::juce_wchar step);    // cursor + L/R, q ≤ kChildQMax
    void dive();                             // descend the more prominent child
    void addPartial();                       // viz::addPartialPlan → params
    void removePartial();                    // viz::removePartialPlan → params
    void stepLandmark (int direction);       // outermost partial's depth →
                                             // prev/next Bessel landmark
    void commitCursor();                     // setSharedPath + local rebuild
    void setHint (const juce::String& text);
    void updateToolEnablement (bool force);  // enabled only in shared mode

    juce::TextButton ascendBtn, leftBtn, rightBtn, diveBtn,
                     addPartBtn, removePartBtn, jPrevBtn, jNextBtn;
    juce::Rectangle<int> hintArea;           // strip remainder, right of buttons
    bool sharedToolsEnabled { true };        // last-seen mode (change-gated)
    juce::String flashHint;                  // transient action confirmation
    juce::uint32 flashHintUntilMs { 0 };     // 0 = inactive

    Path cursor;                           // navigation position = shared path
    juce::String lastSharedPath;           // to detect external path changes
    juce::String cursorGapInfo;            // widest-gap Chern/width readout —
                                           // computed in rebuildNodes, never
                                           // in paint (the eigensolve rule)

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
    std::vector<Target> targets;           // clickable hit-rects (cursor change / resize)

    // q ceiling for the drawn wing and for keeping the cursor legible/cheap.
    static constexpr int    kQMax        = 32;
    // Eigensolve ceiling for CHILD nodes: children of a q ≤ 32 cursor solve in
    // ~10 ms; q ≥ 233 solves cost hundreds of ms and stalled the message
    // thread per cursor move. Beyond this, children keep default size/colour.
    static constexpr int    kChildQMax   = 64;
    static constexpr double kEMax        = 4.0;    // |E| ≤ 2λ+2 at λ=1
    static constexpr int    kBakeW       = 1000;
    static constexpr int    kBakeH       = 640;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FareyView)
};

} // namespace brocot
