#include "UnderTheHoodTab.h"
#include "../util/Colors.h"
#include "../util/Fonts.h"

namespace brocot
{

UnderTheHoodTab::Content::Content (BrocotAudioProcessor& proc)
{
    for (int i = 0; i < params::kNumOperators; ++i)
    {
        operatorPanels[(size_t) i] = std::make_unique<OperatorPanel> (proc, i);
        addAndMakeVisible (*operatorPanels[(size_t) i]);
    }
    for (int i = 0; i < params::kNumModulators; ++i)
    {
        modulatorPanels[(size_t) i] = std::make_unique<ModulatorPanel> (proc, i);
        addAndMakeVisible (*modulatorPanels[(size_t) i]);
    }
}

int UnderTheHoodTab::Content::layout (int width, bool apply)
{
    // Fixed cell sizes large enough for each panel's controls to be usable
    // (OperatorPanel needs ~200 px tall; ModulatorPanel ~165). Column count
    // is whatever fits the width at the minimum cell width.
    constexpr int margin     = 12;
    constexpr int gap        = 6;
    constexpr int sectionGap = 18;
    constexpr int headerH    = 18;
    constexpr int opMinW     = 200, opCellH  = 200;
    constexpr int modMinW    = 224, modCellH = 165;

    const int innerW = juce::jmax (opMinW, width - margin * 2);
    int y = margin;

    auto grid = [&] (int n, int minW, int cellH, auto& panels) -> void
    {
        const int cols  = juce::jmax (1, (innerW + gap) / (minW + gap));
        const int cellW = (innerW - gap * (cols - 1)) / cols;
        const int rows  = (n + cols - 1) / cols;
        for (int i = 0; i < n; ++i)
        {
            const int c = i % cols, r = i / cols;
            if (apply && panels[(size_t) i])
                panels[(size_t) i]->setBounds (margin + c * (cellW + gap),
                                               y + r * (cellH + gap),
                                               cellW, cellH);
        }
        y += rows * cellH + gap * juce::jmax (0, rows - 1);
    };

    opHeaderY = y;
    y += headerH + 4;
    grid (params::kNumOperators, opMinW, opCellH, operatorPanels);

    y += sectionGap;
    modHeaderY = y;
    y += headerH + 4;
    grid (params::kNumModulators, modMinW, modCellH, modulatorPanels);

    return y + margin;
}

void UnderTheHoodTab::Content::paint (juce::Graphics& g)
{
    g.fillAll (colors::background);
    g.setColour (colors::primaryText);
    g.setFont (fonts::serifItalic (16.0f));
    g.drawText ("operators",   12, opHeaderY,  300, 18, juce::Justification::centredLeft);
    g.drawText ("modulators",  12, modHeaderY, 300, 18, juce::Justification::centredLeft);
}

void UnderTheHoodTab::Content::resized()
{
    layout (getWidth(), true);
}

// ---------------------------------------------------------------------------

UnderTheHoodTab::UnderTheHoodTab (BrocotAudioProcessor& p)
    : content (p)
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);   // vertical only
    addAndMakeVisible (viewport);
}

void UnderTheHoodTab::resized()
{
    viewport.setBounds (getLocalBounds());

    // Reserve room for the vertical scrollbar so cells don't sit under it.
    const int sbW = viewport.getScrollBarThickness();
    const int w   = juce::jmax (1, getWidth() - sbW);
    const int h   = content.layout (w, false);
    content.setSize (w, juce::jmax (h, getHeight()));
}

} // namespace brocot
