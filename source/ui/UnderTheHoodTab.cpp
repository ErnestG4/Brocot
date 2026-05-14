#include "UnderTheHoodTab.h"

namespace brocot
{

UnderTheHoodTab::UnderTheHoodTab (BrocotAudioProcessor& p)
    : proc (p)
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

void UnderTheHoodTab::resized()
{
    // Split the tab area in half — operator bank on top, modulator
    // bank on the bottom. Both use the same 8×2 grid + 4-px gap so the
    // two strips align vertically and feel like one continuous surface.
    auto bounds = getLocalBounds().reduced (16, 8);

    // Reserve the bottom half for the LFO bank.
    auto opArea  = bounds.removeFromTop (bounds.getHeight() / 2);
    bounds.removeFromTop (8);   // separator
    auto lfoArea = bounds;

    auto layoutGrid = [] (juce::Rectangle<int> area, int n, auto& panels)
    {
        const int cols = 8, rows = 2, gap = 4;
        const int cellW = (area.getWidth()  - gap * (cols - 1)) / cols;
        const int cellH = (area.getHeight() - gap * (rows - 1)) / rows;
        for (int i = 0; i < n; ++i)
        {
            const int col = i % cols;
            const int row = i / cols;
            const int x   = area.getX() + col * (cellW + gap);
            const int y   = area.getY() + row * (cellH + gap);
            if (auto& p = panels[(size_t) i])
                p->setBounds (x, y, cellW, cellH);
        }
    };

    layoutGrid (opArea,  params::kNumOperators,  operatorPanels);
    layoutGrid (lfoArea, params::kNumModulators, modulatorPanels);
}

} // namespace brocot
