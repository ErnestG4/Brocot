#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"
#include "EnvelopePanel.h"
#include "FilterPanel.h"
#include "ModulatorPanel.h"

namespace brocot
{

/**
    The "Steer" tab — left column is the shape-target button list
    (13 rows: 12 families + 1 Textured-Smooth with op-count spinner);
    right column is the live patch surface (env + filter + the first
    two modulators stacked).

    Each row has a − and a + button.

    +  nudges the current patch toward the row's category by one
       NUDGE_STEP (0.15). Press repeatedly to traverse further.
    −  nudges the current patch back toward the captured "home"
       state by one NUDGE_STEP. All − buttons share this semantic —
       Will's mental model is "+ = more of this; − = less of what
       I just did", not a per-category anti-target.

    The Textured-Smooth row's op-count spinner (4..16) picks which
    Textured-Smooth · N ops category + maps to, so the dense-carpet
    dimension stays one collapsible slot in the list.

    Reset:  applies captured (back to home), then re-anchors home
            to that state so the next ± round starts fresh.

    Captured ("home") is set on tab construction (or on preset load
    via parameterChanged in the future). The user's morph trajectory
    is therefore predictable: − always undoes back to a known good
    state.
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

    // Header.
    juce::TextButton resetButton { "Reset" };
    juce::Label      hintLabel;

    // Shape-target rows. One per family; the texture row is a
    // dedicated set of widgets so it can host the op-count spinner.
    struct FamilyRow
    {
        juce::String     category;
        juce::TextButton minusBtn { juce::String::charToString (0x2212) };  // − (true minus, not hyphen)
        juce::TextButton plusBtn  { "+" };
        juce::Label      label;
    };
    std::vector<std::unique_ptr<FamilyRow>> familyRows;

    juce::TextButton textureMinusBtn { juce::String::charToString (0x2212) };
    juce::TextButton texturePlusBtn  { "+" };
    juce::Label      textureLabel    { {}, "Textured-Smooth" };
    juce::Label      textureOpLabel  { {}, "op" };
    juce::Slider     textureOpCount  { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };

    // The patch state the − buttons walk back toward.
    std::vector<PullIndex::OpState> captured;
    bool capturedValid { false };

    static constexpr float kNudgeStep = 0.15f;

    void buildRows();
    void onPlus  (const juce::String& category);
    void onMinus();
    void onReset();
    void anchorHome();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SteerTab)
};

} // namespace brocot
