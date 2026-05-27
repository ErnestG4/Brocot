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
    seekRateKnob.setRange (0.01, 2.0, 0.001);
    seekRateKnob.setSkewFactorFromMidPoint (0.15);   // fine control at the slow end
    seekRateKnob.setValue (0.2);
    seekRateKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 48, 14);
    seekRateKnob.setTooltip ("Seek rate — how fast the dot travels to where you click.");
    addAndMakeVisible (seekRateKnob);

    // Colour the cloud by family or by a measured spectral characteristic, to
    // explore the real structure (axes, small clusters, tails, outliers).
    colourModeCombo.addItem ("family",     1);
    colourModeCombo.addItem ("brightness", 2);
    colourModeCombo.addItem ("density",    3);
    colourModeCombo.addItem ("feedback",   4);
    colourModeCombo.addItem ("hollowness", 5);
    colourModeCombo.addItem ("musicality", 6);
    colourModeCombo.setSelectedId (1, juce::dontSendNotification);
    colourModeCombo.onChange = [this]
    {
        colourMode = (ColourMode) (colourModeCombo.getSelectedId() - 1);
        rebuildImage();
        repaint();
    };
    addAndMakeVisible (colourModeCombo);

    startTimerHz (30);
}

CloudView::~CloudView() { stopTimer(); }

juce::Colour CloudView::familyColour (int index, int count)
{
    if (count <= 0) count = 1;
    const float h = (float) (index % count) / (float) count;
    return juce::Colour::fromHSV (h, 0.55f, 0.95f, 1.0f);
}

juce::Colour CloudView::featureColour (float v01)
{
    // low → high : blue → cyan → green → yellow → red.
    const float v = juce::jlimit (0.0f, 1.0f, v01);
    return juce::Colour::fromHSV ((1.0f - v) * 0.66f, 0.75f, 0.95f, 1.0f);
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

int CloudView::nearestNodeTo (juce::Point<float> p) const
{
    const auto& nodes = proc.getLandscape().getNodes();
    int best = -1; float bestD = 1.0e30f;
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        const float dx = nodes[i].x - p.x, dy = nodes[i].y - p.y;
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
    colourModeCombo.setBounds (10, 8, 120, 22);
    rebuildImage();
}

void CloudView::mouseMove (const juce::MouseEvent& e)
{
    hovering = true;
    const int node = nearestNodeTo (screenToMap (e.position));
    if (node != hoverNode) { hoverNode = node; repaint(); }
}

void CloudView::mouseDown (const juce::MouseEvent& e)
{
    // A plain click sets a destination; the dot auto-travels there at seek rate.
    seekTarget = screenToMap (e.position);
    seeking    = true;
    dotDriving = true;
    hovering   = false;
}

void CloudView::mouseDrag (const juce::MouseEvent& e)
{
    // Dragging grabs the dot directly (1:1) — scrub the landscape by hand. The
    // seek rate doesn't apply here; this is immediate, for hunting sounds.
    dot        = screenToMap (e.position);
    seeking    = false;
    dotDriving = true;
    hovering   = false;
    const int node = nearestNodeToDot();
    if (node != currentNode) { currentNode = node; applyNode (node); }
    repaint();
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

    // Dots, coloured by family or by a measured characteristic. Small +
    // translucent so density reads as shading and tails/outliers stay visible.
    auto dotColour = [this, fcount] (const Landscape::Node& n) -> juce::Colour
    {
        switch (colourMode)
        {
            case ColourMode::Brightness: return featureColour (n.bright);
            case ColourMode::Density:    return featureColour (n.dense);
            case ColourMode::Feedback:   return featureColour (n.feedback);
            case ColourMode::Hollow:     return featureColour (n.odd);
            case ColourMode::Musicality: return featureColour (n.tonal);
            case ColourMode::Family:
            default:                     return familyColour (n.family, fcount);
        }
    };
    for (const auto& n : nodes)
    {
        const auto pt = mapToScreen (n.x, n.y);
        g.setColour (dotColour (n).withAlpha (0.45f));
        g.fillEllipse (pt.x - 1.6f, pt.y - 1.6f, 3.2f, 3.2f);
    }

    // Family names float at each family's centroid — only in family mode (in
    // feature modes the colour gradient is the legend).
    if (colourMode == ColourMode::Family)
    {
        std::map<int, juce::Point<float>> sum;
        std::map<int, int> cnt;
        for (const auto& n : nodes) { sum[n.family] += mapToScreen (n.x, n.y); cnt[n.family] += 1; }
        const auto names = ls.getFamilyNames();
        g.setFont (fonts::serif (12.0f));
        for (const auto& [fi, s] : sum)
        {
            if (cnt[fi] < 30 || fi < 0 || fi >= names.size()) continue;
            const auto c = s / (float) cnt[fi];
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.drawText (names[fi], (int) c.x - 59, (int) c.y - 7, 120, 16, juce::Justification::centred);
            g.setColour (familyColour (fi, fcount).brighter (0.4f));
            g.drawText (names[fi], (int) c.x - 60, (int) c.y - 8, 120, 16, juce::Justification::centred);
        }
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
    const bool changed = (node != currentNode);
    if (changed) { currentNode = node; applyNode (node); }
    if (seeking || changed) repaint();   // idle (stationary dot) → no repaint
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

    // Hover inspect: profile of the node nearest the mouse, and — to reveal how
    // a sound type spreads — light up every node in that family (its scattered
    // pockets). Only while idle-hovering, so it never runs during drag/seek.
    if (hovering && hoverNode >= 0 && hoverNode < ls.getNodeCount())
    {
        const auto& nodes = ls.getNodes();
        const int fam = nodes[(size_t) hoverNode].family;
        const auto col = familyColour (fam, ls.getFamilyCount());
        for (const auto& nn : nodes)
            if (nn.family == fam)
            {
                const auto p = mapToScreen (nn.x, nn.y);
                g.setColour (col.withAlpha (0.9f));
                g.fillEllipse (p.x - 2.4f, p.y - 2.4f, 4.8f, 4.8f);
            }
    }

    // Hover inspect readout.
    if (hoverNode >= 0 && hoverNode < ls.getNodeCount())
    {
        const auto& n = ls.getNodes()[(size_t) hoverNode];
        const auto names = ls.getFamilyNames();
        const auto fam = (n.family >= 0 && n.family < names.size())
                             ? names[n.family] : juce::String ("?");
        const auto hp = mapToScreen (n.x, n.y);
        g.setColour (colors::primaryText.withAlpha (0.6f));
        g.drawEllipse (hp.x - 4.0f, hp.y - 4.0f, 8.0f, 8.0f, 1.0f);

        juce::Rectangle<int> box (10, getHeight() - 78, 290, 66);
        g.setColour (colors::surface.withAlpha (0.9f));
        g.fillRoundedRectangle (box.toFloat(), 4.0f);
        g.setColour (colors::borderAtAlpha (0.5f));
        g.drawRoundedRectangle (box.toFloat(), 4.0f, 1.0f);
        g.setColour (colors::primaryText);
        g.setFont (fonts::serif (13.0f));
        g.drawText (fam, box.reduced (8, 6).removeFromTop (18), juce::Justification::topLeft);
        g.setColour (colors::secondaryText);
        g.setFont (fonts::mono (11.0f));
        auto pct = [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)); };
        g.drawText ("bright " + pct (n.bright) + "  dense " + pct (n.dense),
                    box.reduced (8, 6).withTrimmedTop (20).removeFromTop (15),
                    juce::Justification::topLeft);
        g.drawText ("feedback " + pct (n.feedback) + "  hollow " + pct (n.odd)
                    + "  musical " + pct (n.tonal),
                    box.reduced (8, 6).withTrimmedTop (36).removeFromTop (15),
                    juce::Justification::topLeft);
    }

    // Hint.
    g.setColour (colors::secondaryText.withAlpha (0.7f));
    g.setFont (fonts::serifItalic (12.0f));
    g.drawText ("click to seek · double-click to drop · hover to inspect",
                getLocalBounds().reduced (10).removeFromBottom (18),
                juce::Justification::centredRight);
}

} // namespace brocot
