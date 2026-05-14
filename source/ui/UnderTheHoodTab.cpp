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
    // P3.12: ops are 64 now → 16×4 grid. LFO bank stays 8×2. Op area
    // gets 2/3 of the tab to keep panel cells from getting too short.
    auto bounds = getLocalBounds().reduced (16, 8);

    auto opArea  = bounds.removeFromTop ((bounds.getHeight() * 2) / 3);
    bounds.removeFromTop (8);   // separator
    auto lfoArea = bounds;

    auto layoutGrid = [] (juce::Rectangle<int> area, int n,
                          int cols, int rows, auto& panels)
    {
        const int gap = 4;
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

    layoutGrid (opArea,  params::kNumOperators,  /*cols*/ 16, /*rows*/ 4, operatorPanels);
    layoutGrid (lfoArea, params::kNumModulators, /*cols*/  8, /*rows*/ 2, modulatorPanels);
}

} // namespace brocot
