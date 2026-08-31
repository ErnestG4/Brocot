#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"
#include "../audio/GrooveField.h"
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

    - Drag right (positive) → drive a *velocity cursor* along the row's
                              route toward the category target. Holding
                              at +0.5 travels at half speed; +1.0 at
                              full speed.
    - Drag left  (negative) → drive the cursor backward: past the grab
                              point and on toward the Reset-anchored
                              "home" state (route waypoint 0).
    - Release the slider    → it snaps to 0, the travel stops, the
                              patch stays where you left it.

    The route is resolved once per grab:
    `[home, grab-point, ...landscape gradient toward the family]`,
    with the cursor starting at the grab-point waypoint. Slider
    position is a rate in waypoint-hops per second, accumulated by a
    30 Hz timer; fractional positions blend the two bracketing
    waypoints via `PullIndex::morph`, so slow scrubs still advance
    Stern-Brocot paths as the accumulated fraction crosses each
    per-step boundary.

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

    // Coherence suggest-mode (§P0-a): when on, recommend prefix-extension paths
    // for a new modulator that raise the config's path-prefix coherence.
    // Clicking a chip places that modulator on the next free op.
    juce::TextButton coherenceToggle  { "coherence" };
    juce::Label      suggestPrefix;
    static constexpr int kNumSuggestChips = 3;
    std::array<juce::TextButton, kNumSuggestChips> suggestChips;
    std::array<juce::String,     kNumSuggestChips> suggestPaths;
    double suggestIndex { 3.0 };

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
        bool          isDensityRow { false };   // S7-S8 orthogonal density:
                                                // slides sparser↔denser WITHIN
                                                // the current node's family
        juce::Label   label;
        juce::Slider  slider { juce::Slider::LinearHorizontal,
                                juce::Slider::NoTextBox };
        bool          dragging { false };

        // Route resolved once at drag-start: waypoint[0] = the Reset-anchored
        // home, waypoint[1] = where we grabbed (current patch), then the
        // smooth path across the landscape toward this row's family (or a
        // single best-exemplar waypoint as fallback). The slider drives `pos`
        // — a *velocity cursor* along this route, starting at the grab
        // waypoint — so the patch travels in a direction at a rate, never an
        // anchored 0→100 morph (Will), and leftward drag pulls back through
        // the grab point to home. pos is in WAYPOINT units (hops), so a
        // distant family is traversed at the same timbral pace as a near one.
        std::vector<Landscape::Waypoint> routeWaypoints;
        float pos { 0.0f };               // [0, routeWaypoints.size()-1]
    };

    // Sample a route at a continuous waypoint position `pos` (hops from the
    // start): integer part picks the segment, fractional part blends the two
    // bracketing waypoints (ops via PullIndex::morph, op-0 feedback lerped).
    static Landscape::Waypoint sampleRoute (
        const std::vector<Landscape::Waypoint>& wp, float pos);
    std::vector<std::unique_ptr<ScrubRow>> rows;

    // Texture row extras.
    juce::Label   textureOpLabel { {}, "op" };
    juce::Slider  textureOpCount { juce::Slider::IncDecButtons,
                                    juce::Slider::TextBoxLeft };
    juce::SortedSet<int> textureAvailableOps;  // op-counts present in pull index

    // Reset-anchored "home" (ops + op-0 feedback) — route waypoint 0, the
    // state a leftward drag ultimately pulls back to.
    Landscape::Waypoint homeWp;
    bool homeValid { false };

    // Timer: 30 Hz polling of slider deflections.
    static constexpr int kTimerHz = 30;

    // Travel speed at full slider deflection, in WAYPOINTS (timbral hops) per
    // second — so steering is a constant-pace glide along the route regardless
    // of how far the target family is. ~0.6 hop/s ≈ 1.7 s of smooth morph per
    // hop; each hop is itself a smooth SB-path-walk + depth + feedback morph.
    static constexpr float kHopsPerSecAtFull = 0.6f;

    // Farey-gravity groove field: between waypoints, each op's ratio glides
    // CONTINUOUSLY in log-ratio and is pulled toward consonant rationals (the
    // SB path-walk is bypassed during steering — Will). Gravity = pull
    // strength; restlessness lets shallow wells be escaped. Feel values come
    // from the steer.groove* params (S4), read per tick; their defaults equal
    // the pre-S4 hardcoded constants (groove::kDefault* in GrooveLock.h).
    GrooveField grooveField;

    // Groove-feel strip (S4): gravity + restlessness knobs and the S5 lock
    // toggle, in a slim row under the family rows.
    juce::Label   grooveStripLabel { {}, "groove" };
    juce::Label   grooveGravityLabel { {}, "g" };
    juce::Label   grooveRestlessLabel { {}, "r" };
    juce::Slider  grooveGravityKnob  { juce::Slider::RotaryHorizontalVerticalDrag,
                                       juce::Slider::NoTextBox };
    juce::Slider  grooveRestlessKnob { juce::Slider::RotaryHorizontalVerticalDrag,
                                       juce::Slider::NoTextBox };
    juce::ToggleButton grooveLockToggle { "lock" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> grooveGravityAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> grooveRestlessAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> grooveLockAttach;

    // S9: capture the current patch as a user preset under steered/.
    juce::TextButton keepButton { "keep" };

    // Continuous grooved ratio per op for the route position `pos`; 0 for
    // disabled ops. Pushed to the engine's steered-ratio override each tick.
    std::vector<float> groovedRatios (const ScrubRow& row) const;

    void buildRows();
    void wireSlider (ScrubRow& row);
    void updateSuggestions();       // refresh the coherence suggest-mode chips
    void setSuggestVisible (bool);  // show/hide the suggest prefix + chips
    void applySuggestion (int chipIndex);   // place the chip's path on a free op
    void timerCallback() override;
    void applyRowMorph (ScrubRow& row);
    // S5: on release, commit each steered ratio as nearest-groove path + exact
    // cents detune (zero audible discontinuity) instead of settling. Gated on
    // the grooveLock param and free path-mode; must run BEFORE
    // clearSteeredRatios so the grooved ratios are still resolvable.
    void lockToGrooves (const ScrubRow& row);
    void onKeep();      // S9: save current patch as a user preset under steered/
    juce::String resolveTextureCategory();
    void onReset();
    void anchorHome();
    void onGlobalDepth (float deltaPerSlot);
    void onRandomize();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SteerTab)
};

} // namespace brocot
