#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"
#include "../params/ParameterTree.h"
#include "OperatorPanel.h"
#include "ModulatorPanel.h"

namespace brocot
{

/**
    Tab 2 of the v2 editor — the "raw knobs" view that exposes the full
    operator + modulator banks. Just lays out the existing 8×2 ops grid
    on top and the 8×2 modulator-bank grid on the bottom. No new
    behaviour vs. the pre-P3.10c layout; this is the box the rest of
    the editor's controls migrate into.
*/
class UnderTheHoodTab : public juce::Component
{
public:
    explicit UnderTheHoodTab (BrocotAudioProcessor&);

    void resized() override;

private:
    BrocotAudioProcessor& proc;
    std::array<std::unique_ptr<OperatorPanel>, params::kNumOperators>   operatorPanels;
    std::array<std::unique_ptr<ModulatorPanel>, params::kNumModulators> modulatorPanels;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UnderTheHoodTab)
};

} // namespace brocot
