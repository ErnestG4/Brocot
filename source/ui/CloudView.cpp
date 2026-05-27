#include "CloudView.h"
#include "../PluginProcessor.h"
#include "../util/Colors.h"
#include "../util/Fonts.h"

#include <map>

namespace brocot
{

CloudView::CloudView (BrocotAudioProcessor& p) : proc (p)
{
    // Seek-rate knob: how fast the dot travels across the map (map-units/sec;
    // the map spans 2 units corner-to-corner per axis).
    seekRateKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    seekRateKnob.setRange (0.1, 3.0, 0.01);
    seekRateKnob.setValue (0.6);
    seekRateKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 48, 14);
    seekRateKnob.setTooltip ("Seek rate — how fast the dot travels to where you click.");
    addAndMakeVisible (seekRateKnob);

    startTimerHz (30);
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

juce::Point<float> CloudView::screenToMap (juce::Point<float> px) const
{
    if (plot.getWidth() <= 0.0f || plot.getHeight() <= 0.0f) return {};
    const float mx = ((px.x - plot.getX()) / plot.getWidth()  - 0.5f) * 2.0f;
    const float my = ((1.0f - (px.y - plot.getY()) / plot.getHeight()) - 0.5f) * 2.0f;
    return { juce::jlimit (-1.0f, 1.0f, mx), juce::jlimit (-1.0f, 1.0f, my) };
}

int CloudView::nearestNodeToDot() const
{
    const auto& ls = proc.getLandscape();
    const auto& nodes = ls.getNodes();
    int best = -1; float bestD = 1.0e30f;
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        const float dx = nodes[i].x - dot.x, dy = nodes[i].y - dot.y;
        const float d = dx * dx + dy * dy;
        if (d < bestD) { bestD = d; best = (int) i; }
    }
    return best;
}

void CloudView::applyNode (int nodeIndex)
{
    const auto& ls = proc.getLandscape();
    if (nodeIndex < 0 || nodeIndex >= ls.getNodeCount()) return;
    const auto& n = ls.getNodes()[(size_t) nodeIndex];
    proc.applyOps (n.ops);             // smoothers glide ratios/depths
    proc.applyOp0Feedback (n.feedback);
}

void CloudView::resized()
{
    plot = getLocalBounds().toFloat().reduced (16.0f);
    seekRateKnob.setBounds (getWidth() - 70, 8, 60, 60);
    rebuildImage();
}

void CloudView::mouseDown (const juce::MouseEvent& e)
{
    seekTarget = screenToMap (e.position);
    seeking    = true;
    dotDriving = true;
}

void CloudView::mouseDrag (const juce::MouseEvent& e)
{
    seekTarget = screenToMap (e.position);   // redirect mid-seek by dragging
    seeking    = true;
}

void CloudView::mouseDoubleClick (const juce::MouseEvent& e)
{
    // Drop the dot here instantly, then snap the patch to this region.
    dot        = screenToMap (e.position);
    seeking    = false;
    dotDriving = true;
    currentNode = nearestNodeToDot();
    applyNode (currentNode);
    repaint();
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

    if (! dotDriving)
    {
        // Passive: the dot tracks the node nearest the live patch (located by
        // params) until the user takes over.
        const int node = ls.nearestNode (proc.getCurrentOps());
        if (node != currentNode)
        {
            currentNode = node;
            if (auto* n = ls.node (node)) dot = { n->x, n->y };
            repaint();
        }
        return;
    }

    // Driving: glide the dot toward the seek target at the seek rate, and apply
    // the node nearest the dot as we move (smoothers handle the audio glide).
    if (seeking)
    {
        const float dt   = 1.0f / 30.0f;
        const float step = (float) seekRateKnob.getValue() * dt;
        const auto  delta = seekTarget - dot;
        const float dist  = delta.getDistanceFromOrigin();
        if (dist <= step || dist < 1.0e-4f) { dot = seekTarget; seeking = false; }
        else                                { dot += delta * (step / dist); }
    }

    const int node = nearestNodeToDot();
    if (node != currentNode)
    {
        currentNode = node;
        applyNode (node);
    }
    repaint();
}

void CloudView::paint (juce::Graphics& g)
{
    g.fillAll (colors::background);
    if (cloud.isValid())
        g.drawImageAt (cloud, 0, 0);

    const auto& ls = proc.getLandscape();
    if (ls.isEmpty()) return;

    // Seek target + travel line while gliding.
    if (dotDriving && seeking)
    {
        const auto from = mapToScreen (dot.x, dot.y);
        const auto to   = mapToScreen (seekTarget.x, seekTarget.y);
        g.setColour (colors::accent.withAlpha (0.5f));
        g.drawLine ({ from, to }, 1.5f);
        g.setColour (colors::accent.withAlpha (0.8f));
        g.drawEllipse (to.x - 4.0f, to.y - 4.0f, 8.0f, 8.0f, 1.5f);
    }

    // The dot — "you are here" / the steering cursor.
    const auto pt = mapToScreen (dot.x, dot.y);
    g.setColour (colors::accentBright);
    g.drawEllipse (pt.x - 7.0f, pt.y - 7.0f, 14.0f, 14.0f, 2.0f);
    g.setColour (colors::accentBright.withAlpha (0.25f));
    g.drawEllipse (pt.x - 11.0f, pt.y - 11.0f, 22.0f, 22.0f, 1.5f);
    g.setColour (colors::primaryText);
    g.fillEllipse (pt.x - 2.0f, pt.y - 2.0f, 4.0f, 4.0f);

    // Hint.
    g.setColour (colors::secondaryText.withAlpha (0.7f));
    g.setFont (fonts::serifItalic (12.0f));
    g.drawText ("click to seek · double-click to drop the dot",
                getLocalBounds().reduced (10).removeFromBottom (18),
                juce::Justification::centredLeft);
}

} // namespace brocot
