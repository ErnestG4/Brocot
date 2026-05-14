#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"

namespace brocot
{

/**
    The "Steer" tab of the v2 editor — Will's "a synth you can steer"
    surface. Picks a category from the precomputed pull index, captures
    the operator state when the user grabs the morph slider, and
    continuously interpolates the live state toward the category's
    lowest-objective exemplar as α goes 0 → 1.

    α is a one-shot ramp from "where I was when I grabbed the slider"
    to "the target". Releasing the slider locks the morphed state in;
    the next slider grab captures THAT as the new starting point. A
    Reset button restores the most-recent capture so users can undo a
    half-finished morph cleanly.

    Hidden completely if `processor.getPullIndex().isEmpty()` — the
    runtime is then missing the table the buttons would call into. The
    onscreen message tells the user how to (re)generate it.
*/
class SteerTab : public juce::Component
{
public:
    explicit SteerTab (BrocotAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    BrocotAudioProcessor& proc;

    juce::Label   categoryLabel { {}, "Shape toward" };
    juce::ComboBox categoryCombo;

    juce::Label  alphaLabel { {}, "amount" };
    juce::Slider alphaSlider { juce::Slider::LinearHorizontal,
                                juce::Slider::TextBoxRight };

    juce::TextButton resetButton { "Reset" };
    juce::Label hintLabel;

    // The patch state captured the moment the user grabs the alpha
    // slider — morph() interpolates between this and the target.
    std::vector<PullIndex::OpState> captured;
    bool capturedValid { false };

    void onSliderStart();
    void onSliderChange();
    void onSliderEnd();
    void onReset();
    void rebuildCategoryList();
    void applyMorph (float alpha);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SteerTab)
};

} // namespace brocot
