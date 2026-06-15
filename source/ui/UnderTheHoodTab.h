#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"
#include "../params/ParameterTree.h"
#include "OperatorPanel.h"
#include "ModulatorPanel.h"

namespace brocot
{

/**
    Tab 2 of the v2 editor — the "raw knobs" view exposing the full
    operator + modulator banks.

    The banks are hosted inside a juce::Viewport: the inner Content lays the
    64 operator panels and 16 modulator panels out at fixed, usable cell
    sizes (columns chosen to fit the width) and is taller than the visible
    area, so the viewport scrolls. The previous layout crushed all 80 panels
    into whatever vertical space was left over, making each cell ~60 px and
    unusable.
*/
class UnderTheHoodTab : public juce::Component
{
public:
    explicit UnderTheHoodTab (BrocotAudioProcessor&);

    void resized() override;

private:
    struct Content : public juce::Component
    {
        explicit Content (BrocotAudioProcessor&);
        void paint (juce::Graphics&) override;
        void resized() override;

        // Lay panels out for the given width; returns the total content height.
        // apply=false just measures (no setBounds) so the host can size us.
        int layout (int width, bool apply);

        std::array<std::unique_ptr<OperatorPanel>,  params::kNumOperators>  operatorPanels;
        std::array<std::unique_ptr<ModulatorPanel>, params::kNumModulators> modulatorPanels;
        int opHeaderY = 0;
        int modHeaderY = 0;
    };

    juce::Viewport viewport;
    Content content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UnderTheHoodTab)
};

} // namespace brocot
