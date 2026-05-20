#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"
#include "EnvelopePanel.h"
#include "FilterPanel.h"
#include "ModulatorPanel.h"

namespace brocot
{

/**
    The "Steer" tab — left column is the shape-target list (12 family
    rows + 1 Textured-Smooth row with an op-count display); right column
    is the live patch surface (env + filter + the first two modulators
    stacked).

    Each row carries a **rate-of-change scrub slider** centred at 0.

    - Drag right (positive) → set a positive *rate* of morph toward
                              this row's category target. Holding at
                              +0.5 morphs at half speed for as long as
                              you hold; holding at +1.0 morphs at full
                              speed.
    - Drag left  (negative) → set a negative rate; the patch morphs
                              back toward the captured "home" state at
                              that speed.
    - Release the slider    → it snaps to 0, the morph stops, the patch
                              stays where you left it.

    Slider position is rate (`α / second`), not a one-shot step. A
    30 Hz timer accumulates progress per row; each row's morph runs
    from its session-start snapshot toward either its category target
    (positive progress) or home (negative progress). New `mouseDown`
    captures the current patch as a new session-start so a fresh drag
    starts a fresh morph from wherever you currently are.

    Slow scrubs (small slider deflection) still advance Stern-Brocot
    paths because progress is *accumulated* across timer ticks before
    being passed to `morph()` — each tick's tiny α adds to a stored
    fraction, and `morph()` walks the path whenever the fraction
    crosses a per-step boundary.

    The Textured-Smooth row's category is resolved dynamically from
    the op-count spinner: nearest available `Textured-Smooth · N ops`
    in the pull index. The lookup parses N from loaded names by
    ASCII prefix; never construct a `·` literal in C++ (the JUCE
    `String(const char*)` ctor treats source bytes as Latin-1, see
    P3.13 worked example).

    Reset re-anchors home to the current state.
    Header globals: `−0.1` / `+0.1` step every enabled op's depth by
    0.1; `rand` overwrites the patch with a fresh low-depth random
    carpet.
*/
class SteerTab : public juce::Component,
                 private juce::Timer
{
public:
    explicit SteerTab (BrocotAudioProcessor&);
    ~SteerTab() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    BrocotAudioProcessor& proc;

    // Right column panels.
    EnvelopePanel  envelopePanel { proc };
    FilterPanel    filterPanel   { proc };
    ModulatorPanel mod0Panel     { proc, 0 };
    ModulatorPanel mod1Panel     { proc, 1 };

    // Header buttons.
    juce::TextButton resetButton      { "Reset" };
    juce::TextButton randomizeButton  { "rand" };
    juce::TextButton depthMinusButton { juce::String::charToString (0x2212)
                                         + juce::String ("0.1") };
    juce::TextButton depthPlusButton  { "+0.1" };
    juce::Label      hintLabel;

    // One scrub row per shape category. Sliders are LinearHorizontal,
    // range -1..+1, default 0, no text box.
    //
    // Per-row state lives here so the timer can accumulate fractional
    // morph progress across ticks (needed for the path walk — single
    // ticks of tiny α would otherwise round to zero path-steps and
    // the path would never advance no matter how long the user held).
    struct ScrubRow
    {
        juce::String  category;         // empty for the texture row;
                                        // resolved dynamically from
                                        // `textureOpCount`.
        bool          isTextureRow { false };
        juce::Label   label;
        juce::Slider  slider { juce::Slider::LinearHorizontal,
                                juce::Slider::NoTextBox };
        bool          dragging { false };

        // Snapshot of the patch the moment the user grabbed the
        // slider — `morph()` always runs from here, with a signed
        // progress driving it toward either `category` (>0) or
        // `home` (<0).
        std::vector<PullIndex::OpState> sessionStart;
        float sessionProgress { 0.0f };   // in [-1, +1]
    };
    std::vector<std::unique_ptr<ScrubRow>> rows;

    // Texture row extras.
    juce::Label   textureOpLabel { {}, "op" };
    juce::Slider  textureOpCount { juce::Slider::IncDecButtons,
                                    juce::Slider::TextBoxLeft };
    juce::SortedSet<int> textureAvailableOps;  // op-counts present in pull index

    // The patch state the negative-direction slider walks toward.
    std::vector<PullIndex::OpState> captured;
    bool capturedValid { false };

    // Timer: 30 Hz polling of slider deflections.
    static constexpr int kTimerHz = 30;

    // How fast a fully-deflected slider sweeps progress. 0.05 α/sec
    // = a full morph at full deflection takes ~20 sec (Will's call:
    // "cut the slider impact down by a factor of 30"). The scrub is
    // for *fine* shaping; depth interpolates continuously through it,
    // and SB paths advance after enough ticks accumulate past the
    // round-to-nearest step threshold.
    static constexpr float kRatePerSecAtFull = 0.05f;

    void buildRows();
    void wireSlider (ScrubRow& row);
    void timerCallback() override;
    void applyRowMorph (ScrubRow& row);
    juce::String resolveTextureCategory();
    void onReset();
    void anchorHome();
    void onGlobalDepth (float deltaPerSlot);
    void onRandomize();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SteerTab)
};

} // namespace brocot
