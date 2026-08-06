#include "CloudView.h"
#include "../PluginProcessor.h"
#include "../audio/Path.h"
#include "../util/CloudMath.h"
#include "../util/Colors.h"
#include "../util/DensityGrid.h"
#include "../util/Fonts.h"

#include <algorithm>
#include <map>

namespace brocot
{

namespace
{
    // Depth-weighted mean denominator q̄ of a node's active ops — the
    // simplicity descriptor. The Path::walk happens here (bake time / single
    // hover node); the weighting math is pure (util/CloudMath.h).
    float meanQOfNode (const Landscape::Node& n)
    {
        std::vector<viz::OpQ> ops;
        ops.reserve (n.ops.size());
        for (const auto& o : n.ops)
        {
            int p = 1, q = 1;
            Path (o.path).walk (p, q);
            ops.push_back ({ q, o.depth, o.enabled });
        }
        return viz::meanQOfOps (ops);
    }
}

CloudView::CloudView (BrocotAudioProcessor& p) : proc (p)
{
    // Seek-rate knob: how fast the dot travels across the map (map-units/sec;
    // the map spans 2 units corner-to-corner per axis).
    seekRateKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    seekRateKnob.setRange (0.01, 2.0, 0.001);
    seekRateKnob.setSkewFactorFromMidPoint (0.15);   // fine control at the slow end
    seekRateKnob.setValue (0.2);
    seekRateKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 48, 14);
    seekRateKnob.setTooltip (juce::CharPointer_UTF8 ("Seek rate — how fast the dot travels to where you click."));
    addAndMakeVisible (seekRateKnob);

    // Colour the cloud by family or by a measured spectral characteristic, to
    // explore the real structure (axes, small clusters, tails, outliers).
    colourModeCombo.addItem ("family",     1);
    colourModeCombo.addItem ("brightness", 2);
    colourModeCombo.addItem ("density",    3);
    colourModeCombo.addItem ("feedback",   4);
    colourModeCombo.addItem ("hollowness", 5);
    colourModeCombo.addItem ("musicality", 6);
    colourModeCombo.addItem ("criticality/voids", 7);
    colourModeCombo.addItem ("simplicity", 8);
    colourModeCombo.setSelectedId (1, juce::dontSendNotification);
    colourModeCombo.onChange = [this]
    {
        colourMode = (ColourMode) (colourModeCombo.getSelectedId() - 1);
        rebuildImage();
        repaint();
    };
    addAndMakeVisible (colourModeCombo);

    // Operator-depth map selector: each map navigates ops 0..depth-1 (nested
    // prefix), preserving the rest of the patch's timbre. Switching re-locates
    // the dot to the current patch on the new map.
    for (int i = 0; i < proc.getMapCount(); ++i)
        mapCombo.addItem (proc.getMapName (i), i + 1);
    mapCombo.setSelectedId (proc.getActiveMap() + 1, juce::dontSendNotification);
    mapCombo.onChange = [this]
    {
        proc.setActiveMap (mapCombo.getSelectedId() - 1);
        dotDriving = false;        // re-sync the dot to the patch on the new map
        currentNode = -1;
        rebuildImage();
        repaint();
    };
    addAndMakeVisible (mapCombo);

    // Wormhole layer toggle — a child only made visible when the active map
    // actually ships verified jumps (rebuildImage keeps that in sync).
    wormholeToggle.setButtonText ("wormholes");
    wormholeToggle.setTooltip (juce::CharPointer_UTF8 ("Show the smoothest render-verified cross-basin jumps."));
    wormholeToggle.onClick = [this] { repaint(); };
    addChildComponent (wormholeToggle);

    // Criticality-seek gestures (opt-in; no default behaviour changes).
    critButton.setButtonText ("crit");
    critButton.setTooltip (juce::CharPointer_UTF8 ("Seek the most critical node near the dot (key: c)."));
    critButton.onClick = [this] { seekToCritical(); };
    addAndMakeVisible (critButton);

    rimSnapToggle.setButtonText ("rim snap");
    rimSnapToggle.setTooltip (juce::CharPointer_UTF8 ("Snap clicks to a nearby critical node (tight radius)."));
    addAndMakeVisible (rimSnapToggle);

    // Family-wide hover highlight is opt-in (default OFF): lighting every
    // node of the hovered family repaints half the cloud per mouse move and
    // buries the local structure; off, hover lights just the hovered node.
    familyToggle.setButtonText ("family");
    familyToggle.setTooltip (juce::CharPointer_UTF8 ("hover lights the whole family"));
    familyToggle.onClick = [this] { repaint(); };
    addAndMakeVisible (familyToggle);

    setWantsKeyboardFocus (true);          // for the 'c' shortcut

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
    mapCombo.setBounds (10, 8, 124, 22);   // wide enough for "16-op mix" etc.
    colourModeCombo.setBounds (140, 8, 130, 22);
    wormholeToggle.setBounds (276, 8, 100, 22);
    critButton.setBounds (380, 8, 44, 22);
    rimSnapToggle.setBounds (430, 8, 80, 22);
    familyToggle.setBounds (514, 8, 72, 22);
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

    // Rim snap (opt-in): retarget the click to the best critical node within
    // a tight radius; misses leave the click exactly as it was.
    if (rimSnapToggle.getToggleState())
    {
        const auto& ls = proc.getLandscape();
        if (const auto* n = ls.node (ls.bestCritNode (seekTarget.x, seekTarget.y, 0.12f)))
            seekTarget = { n->x, n->y };
    }

    seeking    = true;
    dotDriving = true;
    hovering   = false;
}

void CloudView::seekToCritical()
{
    // Bounded search around the dot, widening once; a miss reports via the
    // hint line and does nothing — this gesture NEVER teleports across the
    // map. A hit goes through the exact user-click pathway (seekTarget +
    // seeking), so the patch glides through the same smoothers.
    const auto& ls = proc.getLandscape();
    if (ls.isEmpty()) return;

    int node = ls.bestCritNode (dot.x, dot.y, 0.25f);
    if (node < 0) node = ls.bestCritNode (dot.x, dot.y, 0.5f);
    if (const auto* n = ls.node (node))
    {
        seekTarget = { n->x, n->y };
        seeking    = true;
        dotDriving = true;
        hovering   = false;
        return;
    }
    flashHint = "no critical node nearby";
    flashHintUntilMs = juce::Time::getMillisecondCounter() + 1800;
    repaint();
}

bool CloudView::keyPressed (const juce::KeyPress& key)
{
    if (key.getTextCharacter() == 'c') { seekToCritical(); return true; }
    return false;
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
    wormholeToggle.setVisible (! ls.getJumps().empty());
    if (ls.isEmpty())
    {
        g.setColour (colors::secondaryText);
        g.setFont (fonts::serifItalic (15.0f));
        g.drawText (juce::CharPointer_UTF8 ("landscape graph not loaded — run phase3.gpu_paths gradients"),
                    getLocalBounds(), juce::Justification::centred);
        return;
    }

    const int fcount = ls.getFamilyCount();
    const auto& nodes = ls.getNodes();

    // Criticality/voids only: underlay = node density of the shipped
    // embedding; voids are the light regions; nothing is inferred. Dense
    // regions get a dark slate wash so the speckle in the voids (the critical
    // points that line them) reads immediately. One-time bake cost only.
    if (colourMode == ColourMode::Criticality)
    {
        viz::DensityGrid grid;
        for (const auto& n : nodes)
            grid.accumulate (n.x, n.y);
        grid.blur();

        constexpr int kN = viz::DensityGrid::kN;
        const juce::Colour slate (38, 46, 56);
        for (int iy = 0; iy < kN; ++iy)
        {
            for (int ix = 0; ix < kN; ++ix)
            {
                const float x0 = -1.0f + 2.0f * (float) ix / kN;
                const float x1 = -1.0f + 2.0f * (float) (ix + 1) / kN;
                const float y0 = -1.0f + 2.0f * (float) iy / kN;
                const float y1 = -1.0f + 2.0f * (float) (iy + 1) / kN;
                const float cx = 0.5f * (x0 + x1), cyy = 0.5f * (y0 + y1);
                const auto tl = mapToScreen (x0, y1);   // y flips on screen
                const auto br = mapToScreen (x1, y0);
                g.setColour (slate.withAlpha (0.10f + 0.25f * grid.sample01 (cx, cyy)));
                g.fillRect (juce::Rectangle<float> (tl, br));
            }
        }
    }

    // Criticality/voids only: cache the top-crit nodes for the dynamic-layer
    // shimmer. Deterministic salts (rank-hashed, no RNG) keep the twinkle
    // stable across rebakes.
    shimmerDots.clear();
    if (colourMode == ColourMode::Criticality)
    {
        std::vector<float> crits (nodes.size());
        for (size_t i = 0; i < nodes.size(); ++i) crits[i] = nodes[i].crit;
        const auto top = viz::topCritIndices (crits, 400);
        shimmerDots.reserve (top.size());
        for (size_t rank = 0; rank < top.size(); ++rank)
        {
            const auto& n = nodes[(size_t) top[rank]];
            shimmerDots.push_back ({ mapToScreen (n.x, n.y), n.crit,
                                     (juce::uint32) rank * 2654435761u });
        }
    }

    // Simplicity only: cache per-node q̄ + the map's p2..p98 scaling. The
    // shipped feature fields arrive pre-scaled to 0..1; q̄ is derived here
    // from the op paths, so it gets the same percentile treatment at bake
    // time. Config-derived descriptor only — no butterfly claim.
    simpMeanQ.clear();
    if (colourMode == ColourMode::Simplicity)
    {
        simpMeanQ.resize (nodes.size(), -1.0f);
        std::vector<float> valid;
        valid.reserve (nodes.size());
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            simpMeanQ[i] = meanQOfNode (nodes[i]);
            if (simpMeanQ[i] >= 0.0f) valid.push_back (simpMeanQ[i]);
        }
        simpQLo = 0.0f; simpQHi = 1.0f;
        if (! valid.empty())
        {
            std::sort (valid.begin(), valid.end());
            simpQLo = valid[(size_t) (0.02 * (double) (valid.size() - 1))];
            simpQHi = valid[(size_t) (0.98 * (double) (valid.size() - 1))];
            if (simpQHi <= simpQLo) simpQHi = simpQLo + 1.0f;   // flat map guard
        }
    }

    // Dots, coloured by family or by a measured characteristic. Small +
    // translucent so density reads as shading and tails/outliers stay visible.
    // Simplicity keeps the ramp convention (blue→red = low→high): blue =
    // simple (low q̄), red = complex; inactive nodes go grey.
    auto dotColour = [this, fcount] (const Landscape::Node& n, size_t i) -> juce::Colour
    {
        switch (colourMode)
        {
            case ColourMode::Brightness: return featureColour (n.bright);
            case ColourMode::Density:    return featureColour (n.dense);
            case ColourMode::Feedback:   return featureColour (n.feedback);
            case ColourMode::Hollow:     return featureColour (n.odd);
            case ColourMode::Musicality: return featureColour (n.tonal);
            case ColourMode::Criticality:return featureColour (n.crit);
            case ColourMode::Simplicity:
                return simpMeanQ[i] < 0.0f
                           ? colors::disabled
                           : featureColour ((simpMeanQ[i] - simpQLo) / (simpQHi - simpQLo));
            case ColourMode::Family:
            default:                     return familyColour (n.family, fcount);
        }
    };
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        const auto& n = nodes[i];
        const auto pt = mapToScreen (n.x, n.y);
        g.setColour (dotColour (n, i).withAlpha (0.45f));
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
    // A hidden tab must cost nothing: the passive branch's getCurrentOps +
    // 64-op signature is real work at 30 Hz. On the hidden→showing transition
    // force one passive re-locate (locatedSig reset) so the dot resyncs to
    // whatever the patch became while this tab was away.
    if (! isShowing())
    {
        wasShowing = false;
        return;
    }
    if (! wasShowing)
    {
        wasShowing = true;
        locatedSig = 0;
        repaint();
    }

    const auto& ls = proc.getLandscape();
    if (ls.isEmpty()) return;

    // Expire the transient hint (one repaint to restore the standard line).
    if (flashHintUntilMs != 0
        && juce::Time::getMillisecondCounter() >= flashHintUntilMs)
    {
        flashHintUntilMs = 0;
        repaint();
    }

    // Criticality/voids only: the shimmer animates, so this mode repaints
    // every OTHER tick (15 Hz). Every other mode keeps the idle-no-repaint
    // invariant exactly — a stationary dot costs nothing.
    if (colourMode == ColourMode::Criticality && ! shimmerDots.empty())
    {
        shimmerPhase += 1.0f / 30.0f;
        shimmerFlip = ! shimmerFlip;
        if (shimmerFlip) repaint();
    }

    if (! dotDriving)
    {
        // Passive: the dot tracks the node nearest the live patch (located by
        // params) until the user takes over. The locate is a full-cloud
        // histogram scan (~50k nodes × 40 bins), so only run it when the
        // patch has actually changed since the last locate — a cheap
        // signature over (path, depth, enabled) gates it. currentNode < 0
        // (startup / map switch) always relocates.
        const auto ops = proc.getCurrentOps();
        auto sig = (juce::int64) 0xCBF29CE484222325ULL;  // FNV-1a-ish fold
        for (const auto& o : ops)
        {
            sig = sig * 1099511628211LL + o.path.hashCode64();
            sig = sig * 1099511628211LL + (juce::int64) juce::roundToInt (o.depth * 1.0e4f);
            sig = sig * 1099511628211LL + (o.enabled ? 1 : 0);
        }
        if (sig == locatedSig && currentNode >= 0)
            return;
        locatedSig = sig;
        const int node = ls.nearestNode (ops);
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

void CloudView::drawJumpArcs (juce::Graphics& g)
{
    // Wormholes = render-verified smooth cross-basin jumps (phase3/jumps.md).
    // Dynamic layer: the incident-arc highlight follows hover/current, so the
    // arcs can't live in the baked image. ≤ 2048 jumps as shipped; the linear
    // incidence scans stay trivially cheap at repaint rates.
    const auto& ls = proc.getLandscape();
    const auto& jumps = ls.getJumps();
    if (jumps.empty()) return;

    const auto& nodes = ls.getNodes();
    const float tau = juce::jmax (ls.getJumpTau(), 1.0e-6f);
    const auto  centre = plot.getCentre();

    auto smoothOf = [tau] (const Landscape::Jump& j)
    {
        return juce::jlimit (0.0f, 1.0f, 1.0f - j.rough / tau);
    };
    auto arcOf = [this, &nodes, centre] (const Landscape::Jump& j,
                                         juce::Point<float>& p1, juce::Point<float>& p2)
    {
        const auto& na = nodes[(size_t) j.a];
        const auto& nb = nodes[(size_t) j.b];
        p1 = mapToScreen (na.x, na.y);
        p2 = mapToScreen (nb.x, nb.y);
        const auto c = viz::arcControl ({ p1.x, p1.y }, { p2.x, p2.y },
                                        { centre.x, centre.y });
        juce::Path arc;
        arc.startNewSubPath (p1);
        arc.quadraticTo (c.x, c.y, p2.x, p2.y);
        return arc;
    };

    // Opt-in layer: the smoothest min(200, all) jumps, faint, alpha graded by
    // smoothness. getJumps() is sorted smoothest-first as shipped.
    if (wormholeToggle.getToggleState())
    {
        const int nDraw = juce::jmin (200, (int) jumps.size());
        for (int i = 0; i < nDraw; ++i)
        {
            juce::Point<float> p1, p2;
            const auto arc = arcOf (jumps[(size_t) i], p1, p2);
            g.setColour (colors::secondaryText.withAlpha (
                juce::jlimit (0.0f, 1.0f, 0.08f + 0.30f * smoothOf (jumps[(size_t) i]))));
            g.strokePath (arc, juce::PathStrokeType (1.0f));
        }
    }

    // Regardless of the toggle: jumps incident to the hover/current node draw
    // bright — "where can this sound tunnel to" is always answerable.
    std::vector<int> incident;
    for (int node : { hoverNode, currentNode })
        if (node >= 0)
            for (int ji : ls.jumpsFrom (node))
                incident.push_back (ji);
    std::sort (incident.begin(), incident.end());
    incident.erase (std::unique (incident.begin(), incident.end()), incident.end());

    for (int ji : incident)
    {
        const auto& j = jumps[(size_t) ji];
        juce::Point<float> p1, p2;
        const auto arc = arcOf (j, p1, p2);
        g.setColour (colors::accent.withAlpha (0.4f + 0.5f * smoothOf (j)));
        g.strokePath (arc, juce::PathStrokeType (1.5f));
        for (const auto& e : { p1, p2 })
            g.drawEllipse (e.x - 3.0f, e.y - 3.0f, 6.0f, 6.0f, 1.0f);
    }
}

void CloudView::paint (juce::Graphics& g)
{
    g.fillAll (colors::background);
    if (cloud.isValid())
        g.drawImageAt (cloud, 0, 0);

    const auto& ls = proc.getLandscape();
    if (ls.isEmpty()) return;

    // Criticality shimmer over the baked image — the twinkle that makes the
    // void-edge speckle findable by eye. Alpha math is pure (CloudMath.h).
    for (const auto& s : shimmerDots)
    {
        g.setColour (featureColour (s.crit)
                         .withAlpha (viz::shimmerAlpha (s.crit, shimmerPhase, s.salt)));
        g.fillEllipse (s.pos.x - 1.2f, s.pos.y - 1.2f, 2.4f, 2.4f);
    }

    drawJumpArcs (g);

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

    // Hover inspect: light the hovered node — and, only when the "family"
    // toggle is on, every node of its family (revealing a sound type's
    // scattered pockets). Only while idle-hovering, so it never runs during
    // drag/seek.
    if (hovering && hoverNode >= 0 && hoverNode < ls.getNodeCount())
    {
        const auto& nodes = ls.getNodes();
        const int fam = nodes[(size_t) hoverNode].family;
        const auto col = familyColour (fam, ls.getFamilyCount());
        auto light = [&g, this, &col] (const Landscape::Node& nn)
        {
            const auto p = mapToScreen (nn.x, nn.y);
            g.setColour (col.withAlpha (0.9f));
            g.fillEllipse (p.x - 2.4f, p.y - 2.4f, 4.8f, 4.8f);
        };
        if (familyToggle.getToggleState())
        {
            for (const auto& nn : nodes)
                if (nn.family == fam)
                    light (nn);
        }
        else
        {
            light (nodes[(size_t) hoverNode]);
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
        g.drawText ("bright " + pct (n.bright) + "  dense " + pct (n.dense)
                    + "  critical " + pct (n.crit),
                    box.reduced (8, 6).withTrimmedTop (20).removeFromTop (15),
                    juce::Justification::topLeft);
        // q̄ = the simplicity descriptor, recomputed for just this node (a
        // handful of Path::walks — nothing like the bake-time full pass).
        const float mq = meanQOfNode (n);
        g.drawText ("feedback " + pct (n.feedback) + "  hollow " + pct (n.odd)
                    + "  musical " + pct (n.tonal)
                    + juce::String (juce::CharPointer_UTF8 ("  q\xcc\x84 "))
                    + (mq >= 0.0f ? juce::String (juce::roundToInt (mq))
                                  : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94"))),
                    box.reduced (8, 6).withTrimmedTop (36).removeFromTop (15),
                    juce::Justification::topLeft);
    }

    // Hint — briefly overridden by a transient message (e.g. a crit-seek
    // miss); the timer restores the standard line when it expires.
    const bool flashing = flashHintUntilMs != 0
                       && juce::Time::getMillisecondCounter() < flashHintUntilMs;
    g.setColour (colors::secondaryText.withAlpha (0.7f));
    g.setFont (fonts::serifItalic (12.0f));
    g.drawText (flashing ? flashHint
                         : juce::String (juce::CharPointer_UTF8 ("click to seek · double-click to drop · hover to inspect")),
                getLocalBounds().reduced (10).removeFromBottom (18),
                juce::Justification::centredRight);
}

} // namespace brocot
