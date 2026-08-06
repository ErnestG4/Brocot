#include "FareyView.h"
#include "../PluginProcessor.h"
#include "../audio/BrocotLattice.h"
#include "../util/Colors.h"
#include "../util/Fonts.h"

#include <cmath>
#include <numeric>

namespace brocot
{

namespace
{
    // Longest prefix of `p` whose resolved denominator q stays ≤ qMax, so the
    // navigation cursor sits on the drawn wing and its lattice math stays cheap.
    Path cappedPrefix (const Path& p, int qMax, int hardDepth = 40)
    {
        Path best;
        const int maxLen = juce::jmin (p.depth(), hardDepth);
        for (int len = 1; len <= maxLen; ++len)
        {
            const Path cand = p.prefix (len);
            int pp, qq;
            cand.walk (pp, qq);
            if (qq > qMax) break;
            best = cand;
        }
        return best;
    }

    double fracTheta (int p, int q)
    {
        const double r = (double) p / (double) q;
        return r - std::floor (r);
    }
}

FareyView::FareyView (BrocotAudioProcessor& processor) : proc (processor)
{
    bakeButterfly();

    // Resolve the 64 enabled-param pointers once: the 10 Hz flux signature
    // (and the flux-column paint loop) must not pay 64 String builds + APVTS
    // map lookups per tick.
    for (int i = 0; i < params::kNumOperators; ++i)
        opEnabledRaw[(size_t) i] =
            proc.getAPVTS().getRawParameterValue (params::opId (i, "enabled"));

    lastSharedPath = proc.getSharedPath();
    cursor = cappedPrefix (Path (lastSharedPath), kQMax);
    rebuildNodes();
    startTimerHz (10);
}

FareyView::~FareyView() { stopTimer(); }

// ----------------------------------------------------------------------------
// Baking the butterfly (depends only on the lattice math, so done once)
// ----------------------------------------------------------------------------

juce::Colour FareyView::chernColour (int chern)
{
    // Diverging hue by signed Chern integer; magnitude deepens saturation.
    const float t = 0.5f + (float) chern / (float) (2 * kQMax);   // ~[0.25,0.75]
    const float sat = juce::jlimit (0.35f, 0.9f, 0.35f + 0.06f * (float) std::abs (chern));
    return juce::Colour::fromHSV (juce::jlimit (0.0f, 1.0f, t), sat, 0.85f, 1.0f);
}

void FareyView::bakeButterfly()
{
    // The bake depends only on kQMax/kBakeW/kBakeH (λ = 1), never on the
    // patch, so it is PROCESS-static: FareyView is a direct editor member, and
    // without this every editor open re-ran 323 coprime-pair eigensolves
    // (~230 ms on the message thread). First open pays once; juce::Image is
    // ref-counted so the member is a shallow handle onto the shared bake.
    static const juce::Image baked = []
    {
        juce::Image img (juce::Image::ARGB, kBakeW, kBakeH, true);
        juce::Graphics g (img);
        g.fillAll (colors::background);

        auto xOf = [] (double theta) { return (float) (theta * (kBakeW - 1)); };
        auto yOf = [] (double e)
        {
            return (float) ((1.0 - (e + kEMax) / (2.0 * kEMax)) * (kBakeH - 1));
        };

        const float w = 2.0f;   // column width in px

        for (int q = 2; q <= kQMax; ++q)
        {
            for (int p = 1; p < q; ++p)
            {
                if (std::gcd (p, q) != 1) continue;
                const float x = xOf ((double) p / q);

                // ONE almostMathieuBands per pair. The gaps are derived from
                // the same band vector (complements between the sorted bands,
                // chern = folded Wannier index — mirrors lattice::gaps
                // exactly); calling lattice::gaps here would re-run the
                // eigensolves and double the bake cost.
                const auto bands = lattice::almostMathieuBands (p, q);
                const int  inv   = lattice::modinv (p, q);

                // Gaps first (tinted by Chern), then bands on top (cream) so
                // the allowed spectrum reads as the solid wing.
                for (int j = 1; j < q; ++j)
                {
                    const double lo = bands[(size_t) (j - 1)].hi;
                    const double hi = bands[(size_t) j].lo;
                    if (hi - lo <= 1.0e-6) continue;   // closed gap
                    const int chern = lattice::foldChern ((j * inv) % q, q);
                    g.setColour (chernColour (chern).withAlpha (0.55f));
                    const float y0 = yOf (hi), y1 = yOf (lo);
                    g.fillRect (x - w * 0.5f, y0, w, y1 - y0);
                }
                for (const auto& band : bands)
                {
                    g.setColour (colors::primaryText.withAlpha (0.85f));
                    const float y0 = yOf (band.hi), y1 = yOf (band.lo);
                    g.fillRect (x - w * 0.5f, y0, w, juce::jmax (1.0f, y1 - y0));
                }
            }
        }
        return img;
    }();

    butterfly = baked;
}

// ----------------------------------------------------------------------------
// Coordinate transforms
// ----------------------------------------------------------------------------

float FareyView::thetaToX (double theta) const
{
    return plot.getX() + (float) theta * plot.getWidth();
}

float FareyView::energyToY (double e) const
{
    return plot.getY() + (float) ((1.0 - (e + kEMax) / (2.0 * kEMax))) * plot.getHeight();
}

// ----------------------------------------------------------------------------
// Timer / cursor sync
// ----------------------------------------------------------------------------

void FareyView::syncCursorFromProcessor()
{
    const auto shared = proc.getSharedPath();
    if (shared != lastSharedPath)
    {
        lastSharedPath = shared;
        cursor = cappedPrefix (Path (shared), kQMax);
        rebuildNodes();
    }
}

void FareyView::rebuildNodes()
{
    nodeVis.clear();

    // Current cursor node — a ring; clicking steps back to the parent. The
    // widest-gap readout runs the eigensolve HERE (cursor changes only) and
    // is cached as a string for paint.
    {
        int p, q;
        cursor.walk (p, q);
        cursorGapInfo.clear();
        double bestWidth = 0.0; int bestChern = 0;
        for (const auto& gp : lattice::gaps (p, q))
            if (gp.width > bestWidth) { bestWidth = gp.width; bestChern = gp.chern; }
        if (bestWidth > 0.0)
            cursorGapInfo = juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 widest gap Chern "))
                          + juce::String (bestChern) + ", width "
                          + juce::String (bestWidth, 2);
        nodeVis.push_back ({ cursor, cursor.parent(), fracTheta (p, q), 7.0f,
                             colors::accent,
                             "cursor " + juce::String (p) + "/" + juce::String (q)
                               + cursorGapInfo
                               + (cursor.isEmpty() ? juce::String() : "   (click ↑ parent)"),
                             true });
    }

    // Reachable L/R children — the gaps you can land on. Size ∝ prominence,
    // hue = Chern of the widest gap.
    for (auto step : { 'L', 'R' })
    {
        const Path child = cursor.extended ((juce::juce_wchar) step);
        int p, q;
        child.walk (p, q);
        float radius = 5.0f;
        juce::Colour col = colors::secondaryText;
        // ONE gaps() per child: the vector carries both prominence (= max
        // width) and the widest gap's Chern — a separate prominence() call
        // would re-run the same eigensolves. Guard at kChildQMax: this runs
        // per cursor move on the message thread, and q ≥ 233 solves cost
        // hundreds of ms (children past the guard keep default size/colour).
        if (q <= kChildQMax)
        {
            int widest = 0; double best = 0.0;
            for (const auto& gp : lattice::gaps (p, q))
                if (gp.width > best) { best = gp.width; widest = gp.chern; }
            radius = juce::jlimit (4.0f, 16.0f, 4.0f + 9.0f * (float) best);
            col = chernColour (widest);
        }
        nodeVis.push_back ({ child, child, fracTheta (p, q), radius, col,
                             juce::String (p) + "/" + juce::String (q), false });
    }

    rebuildTargets();
}

void FareyView::rebuildTargets()
{
    // Hit-rects derive purely from nodeVis + plot, so they are rebuilt on
    // cursor change and resize — never in paint(), which may not have run yet
    // when the first click arrives.
    targets.clear();
    const float cy = energyToY (0.0);
    for (const auto& nv : nodeVis)
    {
        if (nv.isCursor && nv.path.isEmpty()) continue;   // root has no parent
        const float cx = thetaToX (nv.theta);
        const juce::Rectangle<float> r (cx - nv.radius, cy - nv.radius,
                                        nv.radius * 2, nv.radius * 2);
        targets.push_back ({ nv.onClick, r.expanded (3.0f) });
    }
}

void FareyView::timerCallback()
{
    // Repaint only when the navigation state actually changed: an idle farey
    // tab must cost nothing (a large resizable window makes unconditional
    // 10 Hz full-view blits read as memory pressure on soft renderers).
    if (! isShowing())
        return;

    // Debounced butterfly rescale: resized() only marks dirty (live-resize
    // fires per drag event; a full software resample of the bake per event
    // stalls the drag at large window sizes). The real rescale runs once the
    // size has held still for ≥ 1 full tick; paint() stretch-blits the raw
    // bake meanwhile.
    if (rescalePending && ++rescaleStableTicks >= 2)
    {
        rescaleButterfly();
        rescalePending = false;
        repaint();
    }

    syncCursorFromProcessor();
    const auto sig = computeFluxSig();
    if (sig != fluxSig)
    {
        fluxSig = sig;
        repaint();
    }
}

juce::String FareyView::computeFluxSig() const
{
    // Enabled flags come from the pointers cached in the ctor (atomic loads,
    // no per-tick String/map work); only enabled ops pay the getOpPath
    // lock + copy.
    juce::String sig (lastSharedPath);
    for (int i = 0; i < params::kNumOperators; ++i)
    {
        const auto* pr = opEnabledRaw[(size_t) i];
        if (pr != nullptr && pr->load() > 0.5f)
            sig << '|' << i << ':' << proc.getOpPath (i);
    }
    return sig;
}

void FareyView::rescaleButterfly()
{
    const int w = juce::roundToInt (plot.getWidth());
    const int h = juce::roundToInt (plot.getHeight());
    scaledButterfly = (w > 0 && h > 0) ? butterfly.rescaled (w, h)
                                       : juce::Image();
}

// ----------------------------------------------------------------------------
// Paint
// ----------------------------------------------------------------------------

void FareyView::paint (juce::Graphics& g)
{
    g.fillAll (colors::background);

    // Title.
    g.setColour (colors::primaryText);
    g.setFont (fonts::serifItalic (24.0f));
    g.drawText ("farey", getLocalBounds().reduced (24, 14).removeFromTop (36),
                juce::Justification::topLeft);

    // The baked butterfly, pre-scaled once the size settles — a per-frame
    // stretch blit of the bake would dominate paint cost at large window
    // sizes. While a live resize is in flight (rescalePending) a stretch blit
    // of the raw bake is the fallback: one frame of soft edges beats a full
    // software resample per drag event.
    if (! rescalePending && scaledButterfly.isValid())
        g.drawImageAt (scaledButterfly, (int) plot.getX(), (int) plot.getY());
    else if (butterfly.isValid())
        g.drawImage (butterfly, plot);
    g.setColour (colors::borderAtAlpha (0.4f));
    g.drawRect (plot, 1.0f);

    // θ axis ticks at a few Farey fractions.
    g.setFont (fonts::mono (10.0f));
    g.setColour (colors::secondaryAtAlpha (0.6f));
    const std::array<std::pair<const char*, double>, 5> ticks {{
        { "0", 0.0 }, { "1/3", 1.0 / 3 }, { "1/2", 0.5 }, { "2/3", 2.0 / 3 }, { "1", 1.0 } }};
    for (const auto& [label, theta] : ticks)
    {
        const float x = thetaToX (theta);
        g.drawVerticalLine ((int) x, plot.getBottom(), plot.getBottom() + 4.0f);
        g.drawText (label, (int) x - 14, (int) plot.getBottom() + 4, 28, 12,
                    juce::Justification::centred);
    }
    g.drawText (juce::CharPointer_UTF8 ("θ = p/q  (flux = frac of the ratio)"),
                plot.toNearestInt().removeFromBottom (16).translated (0, 20),
                juce::Justification::centredRight);

    // Chern legend — the gap tint decoded. Pure HSV math per swatch, no
    // eigensolves, so it may live in paint. NB: chernColour spreads hue over
    // ±kQMax (=32), which compresses small |c| into neighbouring hues at this
    // qMax; widening the spread is a one-line change in chernColour — flagged
    // for Will's visual pass.
    {
        const float sw = 12.0f;
        const float ly = plot.getBottom() + 26.0f;
        float lx = plot.getX();
        g.setFont (fonts::mono (10.0f));
        for (int c = -4; c <= 4; ++c)
        {
            g.setColour (chernColour (c));
            g.fillRect (lx, ly, sw, sw);
            g.setColour (colors::secondaryAtAlpha (0.8f));
            g.drawText ((c > 0 ? "+" : "") + juce::String (c),
                        (int) (lx + sw + 3), (int) ly, 20, 12,
                        juce::Justification::centredLeft);
            lx += sw + 27.0f;
        }
        g.setColour (colors::secondaryAtAlpha (0.6f));
        g.drawText ("gap tint = Chern integer",
                    (int) lx + 8, (int) ly, (int) (plot.getRight() - lx) - 8, 12,
                    juce::Justification::centredLeft);
    }

    // Flux columns for every enabled operator: where the live sound sits.
    // Same ctor-cached enabled pointers as computeFluxSig.
    for (int i = 0; i < params::kNumOperators; ++i)
    {
        const auto* pr = opEnabledRaw[(size_t) i];
        if (pr == nullptr || pr->load() <= 0.5f) continue;

        int p, q;
        Path (proc.getOpPath (i)).walk (p, q);
        const float x = thetaToX (fracTheta (p, q));
        g.setColour (colors::accentBright.withAlpha (0.5f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
    }

    // Navigation cursor + reachable L/R children, from the cached viz.
    const float cy = energyToY (0.0);
    for (const auto& nv : nodeVis)
    {
        const float cx = thetaToX (nv.theta);
        const juce::Rectangle<float> r (cx - nv.radius, cy - nv.radius, nv.radius * 2, nv.radius * 2);

        if (nv.isCursor)
        {
            g.setColour (nv.colour);
            g.drawEllipse (r, 2.0f);
            g.setFont (fonts::mono (11.0f));
            g.drawText (juce::CharPointer_UTF8 (nv.label.toRawUTF8()),
                        (int) plot.getX(), (int) plot.getY() - 18, (int) plot.getWidth(), 16,
                        juce::Justification::centredLeft);
        }
        else
        {
            g.setColour (nv.colour.withAlpha (0.9f));
            g.fillEllipse (r);
            g.setColour (colors::background);
            g.drawEllipse (r, 1.0f);
            g.setColour (colors::secondaryAtAlpha (0.8f));
            g.setFont (fonts::mono (10.0f));
            g.drawText (nv.label, (int) cx - 20, (int) (r.getBottom()) + 1, 40, 12,
                        juce::Justification::centred);
        }
    }
}

void FareyView::resized()
{
    auto b = getLocalBounds().toFloat().reduced (24.0f);
    b.removeFromTop (48.0f);       // title + cursor label
    b.removeFromBottom (44.0f);    // θ axis labels + Chern legend strip
    plot = b.withTrimmedLeft (8.0f).withTrimmedRight (8.0f);
    // Rescale is DEFERRED (live-resize fires this per drag event): mark dirty
    // and let the 10 Hz timer resample once the size holds still.
    rescalePending = true;
    rescaleStableTicks = 0;
    rebuildTargets();
}

void FareyView::mouseDown (const juce::MouseEvent& e)
{
    const auto pos = e.position;
    for (const auto& t : targets)
    {
        if (t.hit.contains (pos))
        {
            cursor = t.path;
            proc.setSharedPath (cursor.toString());
            lastSharedPath = cursor.toString();
            rebuildNodes();
            repaint();
            return;
        }
    }
}

} // namespace brocot
