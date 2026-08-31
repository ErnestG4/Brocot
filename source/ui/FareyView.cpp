#include "FareyView.h"
#include "../PluginProcessor.h"
#include "../audio/BrocotLattice.h"
#include "../audio/Horizon.h"
#include "../util/Colors.h"
#include "../util/FareyMath.h"
#include "../util/Fonts.h"

#include <cmath>
#include <functional>
#include <numeric>

namespace brocot
{

using viz::cappedPrefix;
using viz::fracTheta;

FareyView::FareyView (BrocotAudioProcessor& processor) : proc (processor)
{
    bakeButterfly();

    // Resolve the 64 enabled/position-param pointers once: the 10 Hz flux
    // signature (and the flux-column paint loop) must not pay 64 String
    // builds + APVTS map lookups per tick.
    for (int i = 0; i < params::kNumOperators; ++i)
    {
        opEnabledRaw[(size_t) i] =
            proc.getAPVTS().getRawParameterValue (params::opId (i, "enabled"));
        opPositionRaw[(size_t) i] =
            proc.getAPVTS().getRawParameterValue (params::opId (i, "position"));
        opDepthRaw[(size_t) i] =
            proc.getAPVTS().getRawParameterValue (params::opId (i, "depth"));
        opWaveRaw[(size_t) i] =
            proc.getAPVTS().getRawParameterValue (params::opId (i, "waveform"));
        opDetuneRaw[(size_t) i] =
            proc.getAPVTS().getRawParameterValue (params::opId (i, "detune"));
    }

    // Shared-path tool row. LnF styles TextButtons; the editor owns the
    // TooltipWindow. Every gesture is also on the keyboard (keyPressed).
    auto initButton = [this] (juce::TextButton& b, const char* text,
                              const char* tip, std::function<void()> fn)
    {
        b.setButtonText (juce::CharPointer_UTF8 (text));
        b.setTooltip (juce::CharPointer_UTF8 (tip));
        b.onClick = std::move (fn);
        addAndMakeVisible (b);
    };
    initButton (ascendBtn, "\xe2\x86\x91",
                "Ascend: move the cursor to its parent node (key: Up).",
                [this] { ascend(); });
    initButton (leftBtn, "\xe2\x97\x80L",
                "Descend to the LEFT child (cursor path + L; skipped past q = 64) (key: Left).",
                [this] { descend ('L'); });
    initButton (rightBtn, "R\xe2\x96\xb6",
                "Descend to the RIGHT child (cursor path + R; skipped past q = 64) (key: Right).",
                [this] { descend ('R'); });
    initButton (diveBtn, "dive",
                "Descend into the more PROMINENT child — the one with the widest spectral gap (key: d).",
                [this] { dive(); });
    initButton (addPartBtn, "+part",
                "Add a partial: enable the first free operator, one position past the outermost partial (key: +).",
                [this] { addPartial(); });
    initButton (removePartBtn, "\xe2\x88\x92part",
                "Remove the OUTERMOST partial — the enabled op highest on the shared path (key: -).",
                [this] { removePartial(); });
    initButton (jPrevBtn, "J\xe2\x88\x92",
                "Step the outermost partial's depth DOWN to the previous Bessel landmark (acts on the same op \xe2\x88\x92part removes) (key: [).",
                [this] { stepLandmark (-1); });
    initButton (jNextBtn, "J+",
                "Step the outermost partial's depth UP to the next Bessel landmark (acts on the same op \xe2\x88\x92part removes) (key: ]).",
                [this] { stepLandmark (+1); });

    setWantsKeyboardFocus (true);
    updateToolEnablement (true);

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
    // Diverging hue by signed Chern integer (viz::chernHue, spread = kQMax);
    // magnitude deepens saturation.
    const float sat = juce::jlimit (0.35f, 0.9f, 0.35f + 0.06f * (float) std::abs (chern));
    return juce::Colour::fromHSV (viz::chernHue (chern, kQMax), sat, 0.85f, 1.0f);
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

void FareyView::computeHorizon()
{
    // Scope gate first (COINCIDENCE-HORIZON.md validity boundary): the
    // annotation exists only where the theorem does — two effective sine
    // modulators. An op is effective when it can emit any sideband at all
    // (order_bound ≥ 1); at N = 3 the horizon degrades (17.3%) and at N ≥ 4
    // it predicts nothing, so the overlay is simply absent there rather than
    // discovered-to-be-wrong on a big patch.
    horizonActive = false;
    horizonBLo = horizonBHi = 0;

    int effective = 0;
    bool allSine = true;
    double minDepth = 0.0, maxDepth = 0.0;
    for (int i = 0; i < params::kNumOperators; ++i)
    {
        const auto* en = opEnabledRaw[(size_t) i];
        if (en == nullptr || en->load() <= 0.5f) continue;
        const auto* dp = opDepthRaw[(size_t) i];
        const double depth = dp != nullptr ? (double) dp->load() : 0.0;
        if (horizon::orderBound (depth) < 1) continue;

        if (effective == 0) { minDepth = maxDepth = depth; }
        else { minDepth = std::min (minDepth, depth); maxDepth = std::max (maxDepth, depth); }
        ++effective;
        const auto* wv = opWaveRaw[(size_t) i];
        if (wv != nullptr && (int) wv->load() != 0) allSine = false;
    }

    if (horizon::scopeFor (effective, allSine) != horizon::Scope::Exact)
        return;

    horizonActive = true;
    horizonBLo = horizon::orderBound (minDepth);
    horizonBHi = horizon::orderBound (maxDepth);
}

void FareyView::rebuildOpMarkers()
{
    opMarkers.clear();
    pairRels.clear();

    // If the selected op got disabled out from under us, release it.
    {
        const auto* en = selectedOp >= 0 ? opEnabledRaw[(size_t) selectedOp] : nullptr;
        if (selectedOp >= 0 && (en == nullptr || en->load() <= 0.5f))
            selectedOp = -1;
    }

    std::vector<fieldframe::OpState> states;
    for (int i = 0; i < params::kNumOperators; ++i)
    {
        const auto* en = opEnabledRaw[(size_t) i];
        if (en == nullptr || en->load() <= 0.5f) continue;

        int p = 1, q = 1;
        Path (proc.getOpPath (i)).walk (p, q);
        const auto* dt = opDetuneRaw[(size_t) i];
        const auto* dp = opDepthRaw[(size_t) i];

        OpMarker m;
        m.op = i;
        m.p = p; m.q = q;
        m.theta = fracTheta (p, q);
        // Hit rect near the plot top, staggered over three rows so ops at
        // the same θ stay individually clickable.
        const float x = thetaToX (m.theta);
        const float y = plot.getY() + 4.0f + 14.0f * (float) ((int) opMarkers.size() % 3);
        m.hit = { x - 9.0f, y, 18.0f, 12.0f };
        opMarkers.push_back (m);
        states.push_back ({ p, q,
                            dt != nullptr ? (double) dt->load() : 0.0,
                            dp != nullptr ? (double) dp->load() : 0.0 });
    }

    // Pair relations. Reference side of each pair = the simpler rational
    // (smaller q, tie smaller p) — the deterministic convention the field
    // readout uses; the predicate is NOT swap-invariant, so the choice is
    // stated rather than accidental. All pairs while legible (≤ 8 ops);
    // past that, only the selected op's relations.
    const double carrierHz = proc.getLastNoteHz();
    auto simpler = [this] (int ia, int ib)
    {
        const auto& A = opMarkers[(size_t) ia];
        const auto& B = opMarkers[(size_t) ib];
        return (A.q < B.q || (A.q == B.q && A.p <= B.p)) ? std::pair { ia, ib }
                                                         : std::pair { ib, ia };
    };
    auto addRel = [&] (int ia, int ib)
    {
        const auto [ra, rb] = simpler (ia, ib);
        pairRels.push_back ({ ra, rb,
                              fieldframe::pairReadout (states[(size_t) ra],
                                                       states[(size_t) rb],
                                                       carrierHz) });
    };
    const int n = (int) opMarkers.size();
    if (n <= 8)
    {
        for (int a = 0; a < n; ++a)
            for (int b = a + 1; b < n; ++b)
                addRel (a, b);
    }
    else if (selectedOp >= 0)
    {
        int selIdx = -1;
        for (int i = 0; i < n; ++i)
            if (opMarkers[(size_t) i].op == selectedOp) selIdx = i;
        if (selIdx >= 0)
            for (int i = 0; i < n; ++i)
                if (i != selIdx)
                    addRel (selIdx, i);
    }
}

void FareyView::rebuildNodes()
{
    computeHorizon();
    rebuildOpMarkers();
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

        // Horizon annotation, Exact scope only. Wording is the gated claim —
        // COINCIDENCE STRUCTURE — never "dead" or "not worth visiting":
        // above-horizon ratios are perfectly distinct, they simply cannot
        // fuse. Raising depth extends the horizon and brings nodes back.
        juce::String label = juce::String (p) + "/" + juce::String (q);
        if (horizonActive)
        {
            // Asymmetric corollary under both index assignments: p ≤ 2B(I₁)
            // ∧ q ≤ 2B(I₂), with (I₁,I₂) either way round.
            const bool wayA = p <= 2 * horizonBHi && q <= 2 * horizonBLo;
            const bool wayB = p <= 2 * horizonBLo && q <= 2 * horizonBHi;
            if (! wayA && ! wayB)
            {
                col = col.withMultipliedAlpha (0.35f);
                label += juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7 no coincidence structure at this depth"));
            }
            else if (! (wayA && wayB))
                label += juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7 rings for one index assignment only"));
        }
        nodeVis.push_back ({ child, child, fracTheta (p, q), radius, col,
                             label, false });
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

    // Tool row follows the path mode (change-gated: setEnabled + the
    // persistent free-mode hint repaint only when the mode actually flips).
    updateToolEnablement (false);

    // Expire the transient action hint (one repaint to clear it).
    if (flashHintUntilMs != 0
        && juce::Time::getMillisecondCounter() >= flashHintUntilMs)
    {
        flashHintUntilMs = 0;
        repaint();
    }

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
        rebuildOpMarkers();
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

    // Horizon overlay inputs: the depth slider is also the field's zoom level
    // (MAP-FIELD.md item 3), so crossing an order-bound step must redraw the
    // node annotations. Order bounds quantise the depths, so this adds at
    // most a couple of sig changes per octave of depth motion.
    {
        int effective = 0; bool allSine = true; int bLo = 0, bHi = 0;
        for (int i = 0; i < params::kNumOperators; ++i)
        {
            const auto* en = opEnabledRaw[(size_t) i];
            if (en == nullptr || en->load() <= 0.5f) continue;
            const auto* dp = opDepthRaw[(size_t) i];
            const int b = horizon::orderBound (dp != nullptr ? (double) dp->load() : 0.0);
            if (b < 1) continue;
            if (effective == 0) { bLo = bHi = b; }
            else { bLo = std::min (bLo, b); bHi = std::max (bHi, b); }
            ++effective;
            const auto* wv = opWaveRaw[(size_t) i];
            if (wv != nullptr && (int) wv->load() != 0) allSine = false;
        }
        sig << "|H" << (allSine ? 1 : 0) << ':' << effective << ':' << bLo << ':' << bHi;
    }

    // Pair-relation inputs: detunes (0.1c), per-op order bound, carrier (1 Hz)
    // and the selection — the marker/arc layer redraws when any of these move.
    {
        sig << "|R" << selectedOp << ':'
            << (int) std::lround (proc.getLastNoteHz());
        for (int i = 0; i < params::kNumOperators; ++i)
        {
            const auto* en = opEnabledRaw[(size_t) i];
            if (en == nullptr || en->load() <= 0.5f) continue;
            const auto* dt = opDetuneRaw[(size_t) i];
            const auto* dp = opDepthRaw[(size_t) i];
            sig << ',' << (int) std::lround ((dt != nullptr ? dt->load() : 0.0f) * 10.0f)
                << ':' << horizon::orderBound (dp != nullptr ? (double) dp->load() : 0.0);
        }
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

    // Tool-row hint line: the persistent free-mode notice wins, else the
    // transient action confirmation (expired by the timer).
    {
        const bool flashing = flashHintUntilMs != 0
                           && juce::Time::getMillisecondCounter() < flashHintUntilMs;
        juce::String hintLine;
        if (selectedOp >= 0 && ! flashing)
        {
            // Persistent selection notice: which op, where it sits, and its
            // relations to the rest of the stack (structural wording only).
            for (const auto& m : opMarkers)
                if (m.op == selectedOp)
                    hintLine = "op " + juce::String (m.op + 1) + " = "
                             + juce::String (m.p) + "/" + juce::String (m.q);
            int listed = 0;
            for (const auto& rel : pairRels)
            {
                const auto& A = opMarkers[(size_t) rel.a];
                const auto& B = opMarkers[(size_t) rel.b];
                if (A.op != selectedOp && B.op != selectedOp) continue;
                const int other = A.op == selectedOp ? B.op : A.op;
                hintLine += juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7 vs op "))
                          + juce::String (other + 1) + ": ";
                if (! rel.r.valid)
                    hintLine += "silent";
                else if (rel.r.band == field::Band::Fuses)
                    hintLine += juce::String (rel.r.parent.p) + "/"
                              + juce::String (rel.r.parent.q) + " fuses";
                else
                    hintLine += juce::String (rel.r.parent.p) + "/"
                              + juce::String (rel.r.parent.q) + " "
                              + juce::String (rel.r.sepHz, 1) + "Hz";
                if (++listed == 3) break;
            }
            hintLine += juce::String (juce::CharPointer_UTF8 ("   (\xe2\x86\x90\xe2\x86\x92 step \xc2\xb7 click node \xc2\xb7 esc)"));
        }
        else if (! sharedToolsEnabled)
            hintLine = juce::CharPointer_UTF8 ("shared-path tools \xe2\x80\x94 switch path mode to shared");
        else if (flashing)
            hintLine = flashHint;
        if (hintLine.isNotEmpty())
        {
            g.setColour (colors::secondaryText.withAlpha (0.8f));
            g.setFont (fonts::serifItalic (12.0f));
            g.drawText (hintLine, hintArea.reduced (8, 0),
                        juce::Justification::centredLeft);
        }
    }

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
    // The selected op's column brightens — the tree gestures act on it alone.
    for (const auto& m : opMarkers)
    {
        const float x = thetaToX (m.theta);
        const bool sel = m.op == selectedOp;
        g.setColour (colors::accentBright.withAlpha (sel ? 0.95f : 0.5f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        if (sel)
            g.drawVerticalLine ((int) x + 1, plot.getY(), plot.getBottom());
    }

    // Pair-relation arcs between operators: the coincidence structure of the
    // live stack, from the horizon's pair frame. Colour = regime band on the
    // separation (the honest home for perceptual wording); NO ARC means the
    // pair has no reachable coincidence at these depths. Labels only while
    // they stay legible.
    {
        const float yBase = plot.getY() + 46.0f;
        const bool labelAll = (int) pairRels.size() <= 4;
        for (const auto& rel : pairRels)
        {
            if (! rel.r.valid) continue;
            const auto& A = opMarkers[(size_t) rel.a];
            const auto& B = opMarkers[(size_t) rel.b];
            const bool touchesSel = A.op == selectedOp || B.op == selectedOp;

            const float xa = thetaToX (A.theta), xb = thetaToX (B.theta);
            const float dip = juce::jlimit (14.0f, 64.0f, std::abs (xb - xa) * 0.25f + 10.0f);
            juce::Path arc;
            arc.startNewSubPath (xa, yBase);
            arc.quadraticTo ((xa + xb) * 0.5f, yBase + dip, xb, yBase);

            auto col = bandColour (rel.r);
            const bool fuses = rel.r.band == field::Band::Fuses;
            g.setColour (col.withAlpha (touchesSel ? 0.95f : (fuses ? 0.8f : 0.55f)));
            g.strokePath (arc, juce::PathStrokeType (fuses ? 2.2f : 1.2f));

            if (labelAll || touchesSel)
            {
                juce::String lbl = juce::String (rel.r.parent.p) + "/"
                                 + juce::String (rel.r.parent.q);
                lbl += fuses ? juce::String (" fuses")
                             : " " + juce::String (rel.r.sepHz, 1) + "Hz";
                g.setColour (col.withAlpha (0.9f));
                g.setFont (fonts::mono (9.0f));
                g.drawText (lbl, (int) ((xa + xb) * 0.5f) - 40,
                            (int) (yBase + dip * 0.5f) - 5, 80, 10,
                            juce::Justification::centred);
            }
        }
    }

    // Operator markers — numbered, clickable, selection-aware.
    for (const auto& m : opMarkers)
    {
        const bool sel = m.op == selectedOp;
        g.setColour (sel ? colors::accentBright : colors::surface);
        g.fillRoundedRectangle (m.hit, 3.0f);
        g.setColour (sel ? colors::background : colors::accent);
        g.setFont (fonts::mono (9.0f));
        g.drawText (juce::String (m.op + 1), m.hit.toNearestInt(),
                    juce::Justification::centred);
        if (! sel)
        {
            g.setColour (colors::accent.withAlpha (0.6f));
            g.drawRoundedRectangle (m.hit, 3.0f, 1.0f);
        }
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

    // Tool row: a 34 px strip between the title block and the plot (the plot
    // shrinks; the title/legend rows keep their existing budgets).
    auto strip = b.removeFromTop (34.0f).toNearestInt();
    auto row = strip.withTrimmedBottom (8);
    auto place = [&row] (juce::TextButton& btn, int w)
    {
        btn.setBounds (row.removeFromLeft (w));
        row.removeFromLeft (6);
    };
    place (ascendBtn,     46);
    place (leftBtn,       46);
    place (rightBtn,      46);
    place (diveBtn,       50);
    place (addPartBtn,    56);
    place (removePartBtn, 56);
    place (jPrevBtn,      46);
    place (jNextBtn,      46);
    hintArea = row;                // whatever is left carries the hint line

    b.removeFromBottom (44.0f);    // θ axis labels + Chern legend strip
    plot = b.withTrimmedLeft (8.0f).withTrimmedRight (8.0f);
    // Rescale is DEFERRED (live-resize fires this per drag event): mark dirty
    // and let the 10 Hz timer resample once the size holds still.
    rescalePending = true;
    rescaleStableTicks = 0;
    rebuildTargets();
}

juce::Colour FareyView::bandColour (const fieldframe::PairReadout& r)
{
    switch (r.band)
    {
        case field::Band::Fuses:    return colors::accentBright;
        case field::Band::SlowBeat: return colors::besselZero;
        case field::Band::Rough:    return colors::accent;
        case field::Band::Separate: return colors::secondaryText.withAlpha (0.7f);
    }
    return colors::disabled;
}

void FareyView::stepSelectedOp (int direction)
{
    if (selectedOp < 0) return;
    if (proc.isSharedMode())
    {
        // Position along the shared path, ±1 — the per-op move shared mode
        // actually has. Clamped to the path length by the engine.
        if (auto* p = proc.getAPVTS().getParameter (params::opId (selectedOp, "position")))
        {
            const int cur = (int) std::round (p->convertFrom0to1 (p->getValue()));
            const int next = juce::jmax (0, cur + direction);
            p->setValueNotifyingHost (p->convertTo0to1 ((float) next));
            setHint ("op " + juce::String (selectedOp + 1) + " position "
                     + juce::String (next));
        }
        return;
    }
    // Free mode: extend this op's own path with L/R (q-capped like descend).
    const Path cur (proc.getOpPath (selectedOp));
    const Path next = cur.extended (direction < 0 ? 'L' : 'R');
    int p = 0, q = 0;
    next.walk (p, q);
    if (q > kChildQMax)
    {
        setHint ("op " + juce::String (selectedOp + 1) + ": q > "
                 + juce::String (kChildQMax) + " refused");
        return;
    }
    proc.setOpPath (selectedOp, next.toString());
    setHint ("op " + juce::String (selectedOp + 1) + " \xe2\x86\x92 "
             + juce::String (p) + "/" + juce::String (q));
}

void FareyView::ascendSelectedOp()
{
    if (selectedOp < 0) return;
    if (proc.isSharedMode())
    {
        stepSelectedOp (-1);       // ↑ = step back along the shared path
        return;
    }
    const Path cur (proc.getOpPath (selectedOp));
    if (cur.isEmpty()) return;
    const Path next = cur.parent();
    int p = 0, q = 0;
    next.walk (p, q);
    proc.setOpPath (selectedOp, next.toString());
    setHint ("op " + juce::String (selectedOp + 1) + " \xe2\x86\x91 "
             + juce::String (p) + "/" + juce::String (q));
}

void FareyView::moveSelectedOpTo (const Path& node)
{
    if (selectedOp < 0) return;
    int p = 0, q = 0;
    node.walk (p, q);
    if (proc.isSharedMode())
    {
        // Shared mode: the clicked node is the cursor or one of its
        // children, i.e. on (or one step past) the shared path. Extend the
        // shared path if the click extends it, then park this op's position
        // at the node's depth — other ops' positions are untouched by a
        // pure extension.
        if (! Path (proc.getSharedPath()).startsWith (node))
        {
            cursor = node;
            commitCursor();
        }
        if (auto* pp = proc.getAPVTS().getParameter (params::opId (selectedOp, "position")))
            pp->setValueNotifyingHost (pp->convertTo0to1 ((float) node.depth()));
    }
    else
    {
        proc.setOpPath (selectedOp, node.toString());
    }
    setHint ("op " + juce::String (selectedOp + 1) + " \xe2\x86\x92 "
             + juce::String (p) + "/" + juce::String (q));
}

void FareyView::mouseDown (const juce::MouseEvent& e)
{
    const auto pos = e.position;

    // Operator markers first: click selects (click again releases), and the
    // tree gestures retarget to that op alone.
    for (const auto& m : opMarkers)
    {
        if (m.hit.contains (pos))
        {
            selectedOp = (selectedOp == m.op ? -1 : m.op);
            rebuildOpMarkers();
            repaint();
            return;
        }
    }

    for (const auto& t : targets)
    {
        if (t.hit.contains (pos))
        {
            if (selectedOp >= 0)
            {
                moveSelectedOpTo (t.path);   // move ONE partial, not the stack
                return;
            }
            cursor = t.path;
            commitCursor();
            return;
        }
    }

    // Empty click releases the selection.
    if (selectedOp >= 0)
    {
        selectedOp = -1;
        rebuildOpMarkers();
        repaint();
    }
}

// ----------------------------------------------------------------------------
// Shared-path tool row + keyboard
// ----------------------------------------------------------------------------

namespace
{
    // Snapshot of the 64 ops' enabled/position state for the partial plans —
    // reads the ctor-cached raw pointers, so a click costs 128 atomic loads.
    std::vector<viz::OpPartial> collectPartials (
        const std::array<std::atomic<float>*, params::kNumOperators>& enabledRaw,
        const std::array<std::atomic<float>*, params::kNumOperators>& positionRaw)
    {
        std::vector<viz::OpPartial> ops ((size_t) params::kNumOperators);
        for (size_t i = 0; i < (size_t) params::kNumOperators; ++i)
        {
            const auto* en  = enabledRaw[i];
            const auto* pos = positionRaw[i];
            ops[i].enabled  = en != nullptr && en->load() > 0.5f;
            ops[i].position = pos != nullptr ? (int) std::round (pos->load()) : 0;
        }
        return ops;
    }
}

void FareyView::commitCursor()
{
    proc.setSharedPath (cursor.toString());
    lastSharedPath = cursor.toString();
    rebuildNodes();
    repaint();
}

void FareyView::setHint (const juce::String& text)
{
    flashHint = text;
    flashHintUntilMs = juce::Time::getMillisecondCounter() + 2200;
    repaint();
}

void FareyView::updateToolEnablement (bool force)
{
    const bool shared = proc.isSharedMode();
    if (! force && shared == sharedToolsEnabled)
        return;
    sharedToolsEnabled = shared;
    for (auto* b : { &ascendBtn, &leftBtn, &rightBtn, &diveBtn,
                     &addPartBtn, &removePartBtn, &jPrevBtn, &jNextBtn })
        b->setEnabled (shared);
    repaint();   // the persistent free-mode hint line
}

void FareyView::ascend()
{
    if (cursor.isEmpty())
    {
        setHint ("already at the root 1/1");
        return;
    }
    cursor = cursor.parent();
    commitCursor();
}

void FareyView::descend (juce::juce_wchar step)
{
    const Path child = cursor.extended (step);
    int p, q;
    child.walk (p, q);
    if (q > kChildQMax)
    {
        setHint (juce::CharPointer_UTF8 ("beyond the drawn wing (q > 64)"));
        return;
    }
    cursor = child;
    commitCursor();
}

void FareyView::dive()
{
    const auto pick = viz::pickProminentChild (cursor, kChildQMax);
    if (! pick.valid)
    {
        setHint ("both children beyond the drawn wing");
        return;
    }
    setHint (juce::String::charToString ((juce::juce_wchar) pick.side)
             + " (prom " + juce::String (pick.prominence, 2) + ")");
    descend ((juce::juce_wchar) pick.side);
}

void FareyView::addPartial()
{
    const auto plan = viz::addPartialPlan (collectPartials (opEnabledRaw, opPositionRaw));
    if (! plan.valid)
    {
        setHint ("all 64 partials in use");
        return;
    }
    // applyOps does NOT write position — set the params directly. Position
    // first so the op enables already in place; the position listener then
    // refreshes the derived ratios (handleAsyncUpdate).
    auto& apvts = proc.getAPVTS();
    if (auto* posParam = apvts.getParameter (params::opId (plan.opIndex, "position")))
        posParam->setValueNotifyingHost (posParam->convertTo0to1 ((float) plan.position));
    if (auto* enParam = apvts.getParameter (params::opId (plan.opIndex, "enabled")))
        enParam->setValueNotifyingHost (1.0f);
    setHint ("partial " + juce::String (plan.opIndex + 1)
             + " @ position " + juce::String (plan.position));
}

void FareyView::removePartial()
{
    const auto plan = viz::removePartialPlan (collectPartials (opEnabledRaw, opPositionRaw));
    if (! plan.valid)
    {
        setHint ("no partials to remove");
        return;
    }
    auto& apvts = proc.getAPVTS();
    if (auto* enParam = apvts.getParameter (params::opId (plan.opIndex, "enabled")))
        enParam->setValueNotifyingHost (0.0f);
    setHint ("removed partial " + juce::String (plan.opIndex + 1));
}

void FareyView::stepLandmark (int direction)
{
    // Acts on the OUTERMOST partial — the same op −part would remove (the two
    // gestures deliberately share a target; the tooltips say so).
    const auto plan = viz::removePartialPlan (collectPartials (opEnabledRaw, opPositionRaw));
    if (! plan.valid)
    {
        setHint ("no enabled partial");
        return;
    }

    auto& apvts = proc.getAPVTS();
    const auto depthId = params::opId (plan.opIndex, "depth");
    float depth = 0.0f;
    if (auto* raw = apvts.getRawParameterValue (depthId))
        depth = raw->load();

    const auto* lm = direction > 0 ? viz::nextLandmark (depth)
                                   : viz::prevLandmark (depth);
    if (lm == nullptr)
    {
        setHint (direction > 0 ? "past the last landmark"
                               : "below the first landmark");
        return;
    }
    if (auto* param = apvts.getParameter (depthId))
        param->setValueNotifyingHost (param->convertTo0to1 (lm->value));
    setHint (juce::String (lm->label)
             + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "))
             + bessel::regimeLabel (lm->value)
             + "  (partial " + juce::String (plan.opIndex + 1) + ")");
}

bool FareyView::keyPressed (const juce::KeyPress& key)
{
    // With an operator selected, the arrow keys drive THAT op (both modes);
    // escape releases it and the keys fall back to the whole-stack gestures.
    if (key.getKeyCode() == juce::KeyPress::escapeKey)
    {
        if (selectedOp < 0) return false;
        selectedOp = -1;
        rebuildOpMarkers();
        repaint();
        return true;
    }
    if (selectedOp >= 0)
    {
        if (key.getKeyCode() == juce::KeyPress::upKey)    { ascendSelectedOp();   return true; }
        if (key.getKeyCode() == juce::KeyPress::leftKey)  { stepSelectedOp (-1);  return true; }
        if (key.getKeyCode() == juce::KeyPress::rightKey) { stepSelectedOp (+1);  return true; }
    }

    // Keyboard mirrors the tool row — shared mode only (free mode ignores,
    // so keys fall through to whatever else wants them).
    if (! proc.isSharedMode())
        return false;

    if (key.getKeyCode() == juce::KeyPress::upKey)    { ascend();        return true; }
    if (key.getKeyCode() == juce::KeyPress::leftKey)  { descend ('L');   return true; }
    if (key.getKeyCode() == juce::KeyPress::rightKey) { descend ('R');   return true; }

    switch (key.getTextCharacter())
    {
        case 'd':           dive();              return true;
        case '+': case '=': addPartial();        return true;
        case '-':           removePartial();     return true;
        case '[':           stepLandmark (-1);   return true;
        case ']':           stepLandmark (+1);   return true;
        default: break;
    }
    return false;
}

} // namespace brocot
