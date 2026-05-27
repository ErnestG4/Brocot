#include "CloudView.h"
#include "../PluginProcessor.h"
#include "../util/Colors.h"
#include "../util/Fonts.h"

#include <map>

namespace brocot
{

CloudView::CloudView (BrocotAudioProcessor& p) : proc (p)
{
    startTimerHz (15);   // recentre the "you are here" marker as the patch moves
}

CloudView::~CloudView() { stopTimer(); }

juce::Colour CloudView::familyColour (int index, int count)
{
    if (count <= 0) count = 1;
    const float h = (float) (index % count) / (float) count;
    return juce::Colour::fromHSV (h, 0.55f, 0.95f, 1.0f);
}

juce::Point<float> CloudView::mapToScreen (float x, float y) const
{
    // node space [-1,1] → plot rect; flip Y (screen y grows downward).
    return { plot.getX() + (x * 0.5f + 0.5f) * plot.getWidth(),
             plot.getY() + (1.0f - (y * 0.5f + 0.5f)) * plot.getHeight() };
}

void CloudView::resized()
{
    plot = getLocalBounds().toFloat().reduced (16.0f);
    rebuildImage();
}

void CloudView::rebuildImage()
{
    const auto w = juce::jmax (1, getWidth());
    const auto h = juce::jmax (1, getHeight());
    cloud = juce::Image (juce::Image::ARGB, w, h, true);
    juce::Graphics g (cloud);

    const auto& ls = proc.getLandscape();
    if (ls.isEmpty())
    {
        g.setColour (colors::secondaryText);
        g.setFont (fonts::serifItalic (15.0f));
        g.drawText ("landscape graph not loaded — run phase3.gpu_paths gradients",
                    getLocalBounds(), juce::Justification::centred);
        return;
    }

    const int fcount = ls.getFamilyCount();
    const auto& nodes = ls.getNodes();

    // Dots, coloured by family. Small + translucent so density reads as shading.
    for (const auto& n : nodes)
    {
        const auto pt = mapToScreen (n.x, n.y);
        g.setColour (familyColour (n.family, fcount).withAlpha (0.45f));
        g.fillEllipse (pt.x - 1.6f, pt.y - 1.6f, 3.2f, 3.2f);
    }

    // Family names floating at each family's screen-space centroid.
    std::map<int, juce::Point<float>> sum;
    std::map<int, int> cnt;
    for (const auto& n : nodes)
    {
        sum[n.family] += mapToScreen (n.x, n.y);
        cnt[n.family] += 1;
    }
    const auto names = ls.getFamilyNames();
    g.setFont (fonts::serif (12.0f));
    for (const auto& [fi, s] : sum)
    {
        if (cnt[fi] < 30 || fi < 0 || fi >= names.size()) continue;   // skip tiny
        const auto c = s / (float) cnt[fi];
        const auto col = familyColour (fi, fcount).brighter (0.4f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawText (names[fi], (int) c.x - 59, (int) c.y - 7, 120, 16,
                    juce::Justification::centred);   // shadow
        g.setColour (col);
        g.drawText (names[fi], (int) c.x - 60, (int) c.y - 8, 120, 16,
                    juce::Justification::centred);
    }
}

void CloudView::timerCallback()
{
    const auto& ls = proc.getLandscape();
    if (ls.isEmpty()) return;
    const int node = ls.nearestNode (proc.getCurrentOps());
    if (node != currentNode) { currentNode = node; repaint(); }
}

void CloudView::paint (juce::Graphics& g)
{
    g.fillAll (colors::background);
    if (cloud.isValid())
        g.drawImageAt (cloud, 0, 0);

    const auto& ls = proc.getLandscape();
    if (ls.isEmpty()) return;

    // "You are here": ring at the located node, pulsing accent.
    if (currentNode >= 0 && currentNode < ls.getNodeCount())
    {
        const auto& n = ls.getNodes()[(size_t) currentNode];
        const auto pt = mapToScreen (n.x, n.y);
        g.setColour (colors::accentBright);
        g.drawEllipse (pt.x - 7.0f, pt.y - 7.0f, 14.0f, 14.0f, 2.0f);
        g.setColour (colors::accentBright.withAlpha (0.25f));
        g.drawEllipse (pt.x - 11.0f, pt.y - 11.0f, 22.0f, 22.0f, 1.5f);
        g.setColour (colors::primaryText);
        g.fillEllipse (pt.x - 2.0f, pt.y - 2.0f, 4.0f, 4.0f);
    }
}

} // namespace brocot
