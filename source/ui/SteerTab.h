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

    Each row has a **spring-back slider** centred at 0.

    - Drag right (positive)  → nudge the patch toward this row's
                                category target, in steps proportional
                                to how far the slider moves.
    - Drag left  (negative)  → nudge back toward the captured "home"
                                state, again proportional to slider
                                travel.
    - Release the slider     → it snaps to 0; the patch stays where
                                you let go. The slider is a *scrub
                                controller*, not a position.

    The Textured-Smooth row carries an op-count spinner (4..64) that
    picks which `Textured-Smooth · N ops` category the row morphs
    toward. The lookup parses N from category names loaded from the
    pull index, so we never compare against a `·` literal in the
    source (the JUCE `String(const char*)` constructor treats source
    bytes as Latin-1, mangling `·` to `Â·` — see P3.13).

    Reset re-anchors home to the current state.
    The header also exposes global controls: −0.1 / +0.1 step every
    enabled op's depth by 0.1; `rand` overwrites the patch with a
    fresh low-depth random carpet.
*/
class SteerTab : public juce::Component
{
public:
    explicit SteerTab (BrocotAudioProcessor&);

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
    // range -1..+1, default 0, no text box. We track `lastValue` so
    // each `onValueChange` callback can convert the change to a
    // delta-driven morph step and `dragging` so the snap-back at
    // release doesn't trigger another morph in the opposite direction.
    struct ScrubRow
    {
        juce::String  category;         // empty for the texture row;
                                        // resolved dynamically from
                                        // `textureOpCount`.
        bool          isTextureRow { false };
        juce::Label   label;
        juce::Slider  slider { juce::Slider::LinearHorizontal,
                                juce::Slider::NoTextBox };
        double        lastValue { 0.0 };
        bool          dragging  { false };
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

    // How much morph one full slider sweep (0 → ±1) is worth.
    // Pre-P3.13 the buttons used 0.15 per click; one full drag should
    // feel like a few clicks' worth of motion. Tunable.
    static constexpr float kSliderCoefficient = 0.35f;

    void buildRows();
    void wireSlider (ScrubRow& row);
    void onScrubDelta (ScrubRow& row, float delta);
    void applyMorphToward (const juce::String& category, float step);
    void applyMorphToHome (float step);
    juce::String resolveTextureCategory();    // ← parsed, no `·` literal
    void onReset();
    void anchorHome();
    void onGlobalDepth (float deltaPerSlot);
    void onRandomize();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SteerTab)
};

} // namespace brocot
