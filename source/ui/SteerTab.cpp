#include "SteerTab.h"
#include "BrocotLookAndFeel.h"
#include "../audio/CoherenceSuggest.h"
#include "../audio/GrooveLock.h"
#include "../audio/Path.h"
#include "../util/Colors.h"
#include "../util/Fonts.h"

#include <cmath>

namespace brocot
{

static const std::vector<juce::String> kShapeCategories = {
    "Sawtooth", "Square",   "Triangle",
    "Pulse-PW 4", "Organ 1/3/5", "Bell (stretched)",
    "Prime 2", "Prime 3", "Prime 5", "Prime 7", "Prime 11", "Prime 13",
};

SteerTab::SteerTab (BrocotAudioProcessor& p)
    : proc (p)
{
    addAndMakeVisible (envelopePanel);
    addAndMakeVisible (filterPanel);
    addAndMakeVisible (mod0Panel);
    addAndMakeVisible (mod1Panel);

    addAndMakeVisible (resetButton);
    resetButton.onClick = [this] { onReset(); };
    resetButton.setTooltip (juce::CharPointer_UTF8 ("Re-anchor home to the current state — the next "
                            "leftward drag will pull back here."));

    addAndMakeVisible (depthMinusButton);
    depthMinusButton.onClick = [this] { onGlobalDepth (-0.1f); };
    depthMinusButton.setTooltip ("Lower every enabled operator's depth by 0.1 (clamped at 0.04).");

    addAndMakeVisible (depthPlusButton);
    depthPlusButton.onClick = [this] { onGlobalDepth (+0.1f); };
    depthPlusButton.setTooltip ("Raise every enabled operator's depth by 0.1 (clamped at 8.0).");

    addAndMakeVisible (randomizeButton);
    randomizeButton.onClick = [this] { onRandomize(); };
    randomizeButton.setTooltip ("Replace the patch with a fresh low-depth random carpet (32 ops on, depth 0.04..0.6).");

    addAndMakeVisible (keepButton);
    keepButton.onClick = [this] { onKeep(); };
    keepButton.setTooltip ("Save the current patch as a user preset (steered/keep_<timestamp>).");

    // Groove-feel strip (S4). Attachments bind to the steer.groove* params;
    // groovedRatios reads the same params per tick, so knob moves are heard
    // live during a scrub.
    {
        auto& apvts = proc.getAPVTS();
        using SliderAttach = juce::AudioProcessorValueTreeState::SliderAttachment;
        using ButtonAttach = juce::AudioProcessorValueTreeState::ButtonAttachment;

        addAndMakeVisible (grooveStripLabel);
        grooveStripLabel.setJustificationType (juce::Justification::centredLeft);

        for (auto* l : { &grooveGravityLabel, &grooveRestlessLabel })
        {
            addAndMakeVisible (*l);
            l->setJustificationType (juce::Justification::centredRight);
        }

        addAndMakeVisible (grooveGravityKnob);
        grooveGravityKnob.setTooltip (juce::CharPointer_UTF8 (
            "Gravity — how hard steering is pulled into consonant Farey grooves."));
        addAndMakeVisible (grooveRestlessKnob);
        grooveRestlessKnob.setTooltip (juce::CharPointer_UTF8 (
            "Restlessness — lets steering escape shallow grooves (deep wells still hold)."));

        addAndMakeVisible (grooveLockToggle);
        grooveLockToggle.setTooltip (juce::CharPointer_UTF8 (
            "On release, lock each steered op to its nearest groove's rational + exact "
            "detune (no settle). Off = legacy settle back to the path rational."));

        grooveGravityAttach  = std::make_unique<SliderAttach> (apvts, params::ids::grooveGravity,  grooveGravityKnob);
        grooveRestlessAttach = std::make_unique<SliderAttach> (apvts, params::ids::grooveRestless, grooveRestlessKnob);
        grooveLockAttach     = std::make_unique<ButtonAttach> (apvts, params::ids::grooveLock,     grooveLockToggle);
    }

    addAndMakeVisible (hintLabel);
    hintLabel.setJustificationType (juce::Justification::centredLeft);
    hintLabel.setText (juce::CharPointer_UTF8 ("Hold a row right to push toward that shape  ·  hold left to pull home"),
                       juce::dontSendNotification);

    addAndMakeVisible (coherenceToggle);
    coherenceToggle.setClickingTogglesState (true);
    coherenceToggle.setTooltip (juce::CharPointer_UTF8 (
        "Suggest prefix-extension paths that raise path-prefix coherence — "
        "modulators sharing a Stern-Brocot prefix share sideband partials (§P0-a). "
        "Click a chip to place that modulator on a free op."));
    coherenceToggle.onClick = [this]
    {
        setSuggestVisible (coherenceToggle.getToggleState());
        updateSuggestions();
    };

    addAndMakeVisible (suggestPrefix);
    suggestPrefix.setJustificationType (juce::Justification::centredLeft);
    suggestPrefix.setFont (fonts::mono (11.0f));
    suggestPrefix.setColour (juce::Label::textColourId, colors::secondaryAtAlpha (0.85f));

    for (int i = 0; i < kNumSuggestChips; ++i)
    {
        auto& chip = suggestChips[(size_t) i];
        addAndMakeVisible (chip);
        chip.setColour (juce::TextButton::buttonColourId, colors::surface);
        chip.onClick = [this, i] { applySuggestion (i); };
        chip.setTooltip ("Place this coherent modulator on the next free operator.");
    }
    setSuggestVisible (false);

    // Inventory available Textured-Smooth op-counts by ASCII prefix
    // scan + integer parse — never construct the `·`-containing
    // literal in C++ source.
    {
        const auto& idx = proc.getPullIndex();
        for (const auto& n : idx.getCategoryNamesStartingWith ("Textured-Smooth"))
        {
            if (! n.endsWith (" ops")) continue;
            const auto head = n.dropLastCharacters (4);
            int digitsEnd = head.length();
            int digitsStart = digitsEnd;
            while (digitsStart > 0
                   && head[digitsStart - 1] >= '0'
                   && head[digitsStart - 1] <= '9')
                --digitsStart;
            if (digitsStart == digitsEnd) continue;
            const int n_ops = head.substring (digitsStart, digitsEnd).getIntValue();
            if (n_ops > 0) textureAvailableOps.add (n_ops);
        }
    }

    buildRows();

    addAndMakeVisible (textureOpLabel);
    addAndMakeVisible (textureOpCount);
    textureOpLabel.setJustificationType (juce::Justification::centredRight);
    textureOpCount.setRange (4.0, 64.0, 1.0);
    textureOpCount.setValue (textureAvailableOps.isEmpty() ? 8 : textureAvailableOps[0]);
    textureOpCount.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
    textureOpCount.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 28, 22);
    textureOpCount.setTooltip ("Op count for the Textured-Smooth target. "
                                "If the chosen count has no exemplar, the "
                                "nearest available op-count wins.");

    anchorHome();

    if (proc.getPullIndex().isEmpty())
    {
        hintLabel.setText (juce::CharPointer_UTF8 ("Pull index not loaded — run "
                            "`python -m phase3.build_pull_index`"),
                            juce::dontSendNotification);
        resetButton.setEnabled (false);
        for (auto& r : rows)
            r->slider.setEnabled (false);
    }
    else
    {
        startTimerHz (kTimerHz);
    }
}

SteerTab::~SteerTab() { stopTimer(); }

void SteerTab::buildRows()
{
    rows.clear();

    auto addRow = [this] (const juce::String& cat, bool isTexture, const juce::String& display)
    {
        auto r = std::make_unique<ScrubRow>();
        r->category     = cat;
        r->isTextureRow = isTexture;

        addAndMakeVisible (r->label);
        r->label.setText (display, juce::dontSendNotification);
        r->label.setJustificationType (juce::Justification::centredLeft);

        addAndMakeVisible (r->slider);
        r->slider.setRange (-1.0, 1.0, 0.0);
        r->slider.setDoubleClickReturnValue (true, 0.0);
        r->slider.setValue (0.0, juce::dontSendNotification);
        // Click-on-track must NOT snap the value to the click position —
        // otherwise a quick click pumps a tick or two of morph before
        // the slider springs back. The user has to deliberately grab
        // the thumb (parked at 0) and drag from there. P3.13(c).
        r->slider.setSliderSnapsToMousePosition (false);
        r->slider.setTooltip (isTexture
            ? "Hold right to morph toward Textured-Smooth · N ops; left to pull home. "
              "Slider position is rate — further from neutral = faster. Releases to 0."
            : "Hold right to morph toward " + cat + "; left to pull home. "
              "Slider position is rate — further from neutral = faster. Releases to 0.");

        rows.push_back (std::move (r));
        wireSlider (*rows.back());
    };

    for (const auto& cat : kShapeCategories)
        addRow (cat, false, cat);
    addRow ({}, true, "Textured-Smooth");
}

void SteerTab::wireSlider (ScrubRow& row)
{
    auto* rowPtr = &row;
    row.slider.onDragStart = [this, rowPtr]
    {
        rowPtr->dragging = true;
        rowPtr->pos      = 0.0f;     // start the cursor at where we are now

        // Resolve the route once per grab: [home, grab-point, ...gradient
        // toward this row's family] (or a single best-exemplar destination
        // waypoint when no gradient field covers the family). The cursor
        // starts at the grab-point waypoint, so positive rates travel toward
        // the family and negative rates pull back through the grab point to
        // the Reset-anchored home. The slider then drives `pos` along this
        // route as a velocity cursor.
        rowPtr->routeWaypoints.clear();
        const auto cat = rowPtr->isTextureRow ? resolveTextureCategory()
                                              : rowPtr->category;
        if (cat.isEmpty())
            return;

        const Landscape::Waypoint startWp { proc.getCurrentOps(),
                                            proc.getOp0Feedback() };
        int sliceTo = 0;             // 0 = full waypoints (fallback route)
        const auto& ls = proc.getLandscape();
        if (! ls.isEmpty() && ls.hasFamily (cat))
        {
            auto wp = ls.waypoints (ls.nearestNode (startWp.ops), cat);
            if (! wp.empty())
            {
                sliceTo = ls.getNOps();
                rowPtr->routeWaypoints.reserve (wp.size() + 2);
                rowPtr->routeWaypoints.push_back (startWp);
                for (auto& w : wp)
                    rowPtr->routeWaypoints.push_back (std::move (w));
            }
        }
        if (rowPtr->routeWaypoints.empty())
            if (const auto* best = proc.getPullIndex().getBestOps (cat))
            {
                rowPtr->routeWaypoints.push_back (startWp);
                rowPtr->routeWaypoints.push_back ({ *best, 0.0f });
            }

        if (! rowPtr->routeWaypoints.empty() && homeValid)
        {
            rowPtr->routeWaypoints.insert (rowPtr->routeWaypoints.begin(), homeWp);
            rowPtr->pos = 1.0f;      // cursor at the grab point, home behind it
        }

        // Map contract: a depth-N map edits ops 0..N-1 (nested prefix) and
        // preserves the rest of the patch — CloudView::applyNode honours this.
        // Slice every waypoint of a landscape route to the map depth so
        // morph's implicit-off padding never fades out (and path-trims) the
        // ops the map promises not to touch. The exemplar fallback keeps full
        // waypoints: there the target defines the WHOLE patch and the padding
        // is what fades the extra ops out (the Textured-Smooth fix in morph).
        if (sliceTo > 0)
            for (auto& w : rowPtr->routeWaypoints)
                if ((int) w.ops.size() > sliceTo)
                    w.ops.resize ((size_t) sliceTo);
    };
    // No onValueChange morph — slider position is a *rate*, processed by the
    // timer. The cursor position (and the patch) persist on release.
    row.slider.onDragEnd = [this, rowPtr]
    {
        rowPtr->dragging = false;
        rowPtr->slider.setValue (0.0, juce::dontSendNotification);
        // S5: while the steered ratios are still resolvable, commit each op
        // to its nearest groove's rational + exact detune, so releasing the
        // override below is audibly seamless (grooveLock gates this; off, or
        // an unlockable op, falls back to the legacy settle).
        lockToGrooves (*rowPtr);
        // Release the continuous override: the patch reverts to the discrete
        // (path) state — after a lock the path+detune equal the grooved ratio
        // exactly; otherwise the 20ms ratio smoother absorbs the tiny settle.
        proc.clearSteeredRatios();
    };
}

void SteerTab::timerCallback()
{
    constexpr float dt = 1.0f / (float) kTimerHz;

    for (auto& row : rows)
    {
        if (! row->dragging || row->routeWaypoints.empty()) continue;
        const float v = (float) row->slider.getValue();
        if (std::abs (v) < 1e-3f) continue;

        // Velocity cursor: advance/retreat the position along the route at a
        // rate set by the slider, in waypoint (hop) units. No anchored 0→100,
        // no snap — release leaves the patch wherever the cursor stopped.
        const float maxPos = (float) row->routeWaypoints.size() - 1.0f;
        row->pos = juce::jlimit (0.0f, maxPos,
                                 row->pos + v * kHopsPerSecAtFull * dt);
        applyRowMorph (*row);
    }

    if (coherenceToggle.getToggleState())
        updateSuggestions();
}

void SteerTab::updateSuggestions()
{
    auto& apvts = proc.getAPVTS();

    std::vector<coherence::OpSpec> current;
    Path  base;
    int   baseDepth = 1 << 20;
    int   baseQ     = 1 << 20;
    bool  haveBase  = false;
    double depthSum = 0.0;

    for (int i = 0; i < params::kNumOperators; ++i)
    {
        bool enabled = false;
        if (auto* p = apvts.getRawParameterValue (params::opId (i, "enabled")))
            enabled = (p->load() > 0.5f);
        if (! enabled) continue;

        float depth = 0.0f;
        if (auto* p = apvts.getRawParameterValue (params::opId (i, "depth")))
            depth = p->load();

        const Path path (proc.getOpPath (i));
        int n, d;
        path.walk (n, d);
        current.push_back ({ path.ratio(), (double) depth });
        depthSum += depth;

        // Base = the shortest enabled path (the family's common ancestor),
        // breaking ties toward the simplest ratio (smallest q).
        if (path.depth() < baseDepth || (path.depth() == baseDepth && d < baseQ))
        {
            base = path;
            baseDepth = path.depth();
            baseQ = d;
            haveBase = true;
        }
    }

    auto hideChips = [this] (const juce::String& msg)
    {
        suggestPrefix.setText (msg, juce::dontSendNotification);
        for (int i = 0; i < kNumSuggestChips; ++i)
        {
            suggestChips[(size_t) i].setVisible (false);
            suggestPaths[(size_t) i] = {};
        }
    };

    if (! haveBase)
    {
        hideChips ("enable a modulator to get coherent suggestions");
        return;
    }

    suggestIndex = current.empty() ? 3.0 : depthSum / (double) current.size();
    const auto sugg = coherence::suggestExtensions (current, base, suggestIndex, 3, kNumSuggestChips);

    if (sugg.empty())
    {
        hideChips ("no distinct prefix-extension improves coherence");
        return;
    }

    suggestPrefix.setText (juce::CharPointer_UTF8 ("coherent next →"), juce::dontSendNotification);
    for (int i = 0; i < kNumSuggestChips; ++i)
    {
        auto& chip = suggestChips[(size_t) i];
        const bool have = i < (int) sugg.size();
        chip.setVisible (coherenceToggle.getToggleState() && have);
        if (have)
        {
            suggestPaths[(size_t) i] = sugg[(size_t) i].path;
            chip.setButtonText (sugg[(size_t) i].path + "  "
                                + juce::String (juce::roundToInt (sugg[(size_t) i].coherence * 100.0f)) + "%");
        }
        else
        {
            suggestPaths[(size_t) i] = {};
        }
    }
}

void SteerTab::setSuggestVisible (bool on)
{
    suggestPrefix.setVisible (on);
    for (int i = 0; i < kNumSuggestChips; ++i)
        // Individual chips also gate on whether a suggestion exists (set in
        // updateSuggestions); hide-all here when the mode is off.
        if (! on) suggestChips[(size_t) i].setVisible (false);
}

void SteerTab::applySuggestion (int chipIndex)
{
    if (chipIndex < 0 || chipIndex >= kNumSuggestChips) return;
    const juce::String path = suggestPaths[(size_t) chipIndex];
    if (path.isEmpty()) return;

    // The suggestion is an independent modulator ratio, so it only sounds in
    // FREE mode (shared mode derives every op from the one shared path). Switch
    // if needed — sound-preserving, since opRatios already hold the derived
    // ratios — so the placement sticks instead of being reverted on the next
    // shared refresh.
    const bool switchedToFree = proc.isSharedMode();
    if (switchedToFree)
        if (auto* pm = proc.getAPVTS().getParameter (params::ids::pathMode))
            pm->setValueNotifyingHost (1.0f);   // index 1 = free

    // Place it on the first disabled operator (fallback: the lowest-depth one).
    auto ops = proc.getCurrentOps();
    int slot = -1;
    for (int i = 0; i < (int) ops.size(); ++i)
        if (! ops[(size_t) i].enabled) { slot = i; break; }
    if (slot < 0)
    {
        float lowest = 1.0e9f;
        for (int i = 0; i < (int) ops.size(); ++i)
            if (ops[(size_t) i].depth < lowest) { lowest = ops[(size_t) i].depth; slot = i; }
    }
    if (slot < 0) return;

    ops[(size_t) slot].path    = path;
    ops[(size_t) slot].enabled = true;
    ops[(size_t) slot].depth   = (float) juce::jlimit (0.04, 8.0, suggestIndex);
    proc.applyOps (ops);

    hintLabel.setText ("placed " + path + " on op " + juce::String (slot + 1)
                       + juce::String (juce::CharPointer_UTF8 ("  ·  coherence rising"))
                       + (switchedToFree ? juce::String (juce::CharPointer_UTF8 ("  ·  free mode"))
                                         : juce::String()),
                       juce::dontSendNotification);
    updateSuggestions();
}

void SteerTab::applyRowMorph (ScrubRow& row)
{
    if (row.routeWaypoints.empty()) return;

    const auto sampled = sampleRoute (row.routeWaypoints, row.pos);
    proc.applyOps (sampled.ops);
    proc.applyOp0Feedback (sampled.feedback);
    // Groove steering: override each op's ratio with a continuous, Farey-pulled
    // glide (the SB path-walk in applyOps is bypassed for the audible ratio).
    proc.applySteeredRatios (groovedRatios (row));

    const auto cat = row.isTextureRow ? resolveTextureCategory() : row.category;
    const int total = (int) row.routeWaypoints.size() - 1;
    hintLabel.setText (juce::String (juce::CharPointer_UTF8 ("→ ")) + cat + "   hop "
                       + juce::String (row.pos, 1) + " / " + juce::String (total),
                       juce::dontSendNotification);
}

Landscape::Waypoint SteerTab::sampleRoute (
    const std::vector<Landscape::Waypoint>& wp, float pos)
{
    if (wp.empty())
        return {};
    if (wp.size() == 1)
        return wp.front();
    const float clamped = juce::jlimit (0.0f, (float) wp.size() - 1.0f, pos);
    // Sample the route end as morph(A, B, 1.0) of the FINAL segment rather
    // than returning wp.back() verbatim: morph's implicit-off padding then
    // emits explicit disabled slots for the ops a shorter target doesn't
    // cover, so a padded route (the exemplar fallback) arrives with its
    // faded-out ops cleanly off instead of stuck at the last tick's residual
    // depth.
    const int   i    = juce::jmin ((int) std::floor (clamped), (int) wp.size() - 2);
    const float frac = clamped - (float) i;
    const auto& A = wp[(size_t) i];
    const auto& B = wp[(size_t) i + 1];
    return { PullIndex::morph (A.ops, B.ops, frac),
             A.feedback + (B.feedback - A.feedback) * frac };
}

std::vector<float> SteerTab::groovedRatios (const ScrubRow& row) const
{
    const auto& wp = row.routeWaypoints;
    std::vector<float> out;
    if (wp.empty()) return out;

    // Groove feel from the S4 params (message thread, per tick). Defaults
    // equal the pre-S4 constants; missing params (never in practice) too.
    const auto& apvts = proc.getAPVTS();
    float gravity      = groove::kDefaultGravity;
    float restlessness = groove::kDefaultRestlessness;
    if (auto* p = apvts.getRawParameterValue (params::ids::grooveGravity))
        gravity = p->load();
    if (auto* p = apvts.getRawParameterValue (params::ids::grooveRestless))
        restlessness = p->load();

    const float clamped = juce::jlimit (0.0f, (float) wp.size() - 1.0f, row.pos);
    int i = (int) std::floor (clamped);
    int j = i + 1;
    float frac = clamped - (float) i;
    if (i >= (int) wp.size() - 1) { i = (int) wp.size() - 1; j = i; frac = 0.0f; }

    const auto& A = wp[(size_t) i].ops;
    const auto& B = wp[(size_t) j].ops;
    const int n = juce::jmin ((int) A.size(), (int) B.size(),
                              params::kNumOperators);
    out.assign ((size_t) n, 0.0f);

    for (int op = 0; op < n; ++op)
    {
        // Only ops audible across this segment get a continuous ratio.
        if (! (A[(size_t) op].enabled || B[(size_t) op].enabled))
            continue;
        double rA = brocot::Path (A[(size_t) op].path).ratio();
        double rB = brocot::Path (B[(size_t) op].path).ratio();
        if (! (rA > 0.0)) rA = 1.0;
        if (! (rB > 0.0)) rB = 1.0;
        // Continuous log-ratio glide between the two waypoints' rationals,
        // then Farey-gravity pull toward the nearest consonant groove.
        const float logL = (float) ((1.0 - frac) * std::log2 (rA)
                                     + frac * std::log2 (rB));
        const float logG = logL + grooveField.pull (logL, gravity, restlessness);
        out[(size_t) op] = std::exp2 (logG);
    }
    return out;
}

void SteerTab::lockToGrooves (const ScrubRow& row)
{
    auto& apvts = proc.getAPVTS();
    bool lockOn = groove::kDefaultLock;
    if (auto* p = apvts.getRawParameterValue (params::ids::grooveLock))
        lockOn = (p->load() > 0.5f);
    // Shared mode derives every op ratio from the one shared path, so a
    // per-op path+detune lock would be inert (the applySuggestion caveat) —
    // release falls through to the legacy settle there.
    if (! lockOn || proc.isSharedMode())
        return;

    const auto ratios = groovedRatios (row);
    int locked = 0;
    for (int op = 0; op < (int) ratios.size() && op < params::kNumOperators; ++op)
    {
        const double r = ratios[(size_t) op];
        if (! (r > 0.0))
            continue;

        const auto lock = lockRatioToGroove (grooveField, r, groove::kMaxDetuneCents);
        if (! lock.ok)
            continue;   // beyond capture / failed descent → legacy settle for this op

        // path × 2^(detune/1200) == the grooved ratio exactly, so dropping
        // the steered override after this is audibly seamless.
        proc.setOpPath (op, lock.path.toString());
        if (auto* dp = apvts.getParameter (params::opId (op, "detune")))
            dp->setValueNotifyingHost (dp->convertTo0to1 (lock.detuneCents));
        ++locked;
    }

    if (locked > 0)
        hintLabel.setText ("locked " + juce::String (locked)
                           + (locked == 1 ? " op" : " ops") + " to grooves",
                           juce::dontSendNotification);
}

void SteerTab::onKeep()
{
    auto& pm  = proc.getPresetManager();
    auto  dir = pm.getUserDirectory().getChildFile ("steered");

    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H%M%S");
    juce::String name = "keep_" + stamp;
    auto file = dir.getChildFile (name + ".brocot");
    for (int c = 2; file.existsAsFile(); ++c)     // same-second keeps
    {
        name = "keep_" + stamp + "_" + juce::String (c);
        file = dir.getChildFile (name + ".brocot");
    }

    juce::String err;
    if (pm.save (file, name, &err))
    {
        pm.rescan();    // user-dir scan is recursive, so steered/ shows up
        hintLabel.setText ("kept as steered/" + name, juce::dontSendNotification);
    }
    else
    {
        hintLabel.setText ("keep failed: " + err, juce::dontSendNotification);
    }
}

juce::String SteerTab::resolveTextureCategory()
{
    if (textureAvailableOps.isEmpty()) return {};

    const int requested = (int) textureOpCount.getValue();
    int bestN     = textureAvailableOps[0];
    int bestDist  = std::abs (bestN - requested);
    for (int i = 1; i < textureAvailableOps.size(); ++i)
    {
        const int n = textureAvailableOps[i];
        const int d = std::abs (n - requested);
        if (d < bestDist) { bestN = n; bestDist = d; }
    }

    const auto& idx = proc.getPullIndex();
    const auto suffix = juce::String (" ") + juce::String (bestN) + " ops";
    for (const auto& n : idx.getCategoryNamesStartingWith ("Textured-Smooth"))
        if (n.endsWith (suffix))
            return n;
    return {};
}

void SteerTab::anchorHome()
{
    homeWp    = { proc.getCurrentOps(), proc.getOp0Feedback() };
    homeValid = ! homeWp.ops.empty();
}

void SteerTab::onReset()
{
    anchorHome();
    // Drop any cached route + cursor so the next grab re-plots from the
    // current (re-anchored) patch.
    for (auto& r : rows)
    {
        r->routeWaypoints.clear();
        r->pos = 0.0f;
    }
    hintLabel.setText ("home re-anchored to current state",
                       juce::dontSendNotification);
}

void SteerTab::onGlobalDepth (float deltaPerSlot)
{
    auto ops = proc.getCurrentOps();
    int touched = 0;
    for (auto& s : ops)
    {
        if (! s.enabled) continue;
        const float oldDepth = s.depth;
        s.depth = juce::jlimit (0.04f, 8.0f, s.depth + deltaPerSlot);
        if (std::abs (s.depth - oldDepth) > 1e-6f) ++touched;
    }
    proc.applyOps (ops);
    hintLabel.setText (juce::String ("depth ") + (deltaPerSlot >= 0 ? "+" : "")
                       + juce::String (deltaPerSlot, 2)
                       + "  (touched " + juce::String (touched) + " ops)",
                       juce::dontSendNotification);
}

void SteerTab::onRandomize()
{
    juce::Random& rng = juce::Random::getSystemRandom();
    std::vector<PullIndex::OpState> ops;
    ops.reserve (64);
    for (int i = 0; i < 64; ++i)
    {
        PullIndex::OpState s;
        s.enabled = rng.nextFloat() < 0.5f;
        if (s.enabled)
        {
            const int pathLen = 1 + rng.nextInt (8);
            juce::String path;
            for (int j = 0; j < pathLen; ++j)
                path += (rng.nextBool() ? "L" : "R");
            s.path = path;
            s.depth = 0.04f + rng.nextFloat() * 0.56f;
        }
        else
        {
            s.path = {};
            s.depth = 0.0f;
        }
        ops.push_back (s);
    }
    proc.applyOps (ops);
    anchorHome();
    for (auto& r : rows)
    {
        r->routeWaypoints.clear();
        r->pos = 0.0f;
    }
    const int enabledCount = (int) std::count_if (ops.begin(), ops.end(),
        [] (const PullIndex::OpState& s) { return s.enabled; });
    hintLabel.setText (juce::String (juce::CharPointer_UTF8 ("randomized — ")) + juce::String (enabledCount)
                       + " ops enabled, depths 0.04..0.6",
                       juce::dontSendNotification);
}

void SteerTab::paint (juce::Graphics& g)
{
    g.fillAll (colors::background);

    g.setColour (colors::primaryText);
    g.setFont (fonts::serifItalic (24.0f));
    g.drawText ("steer",
                getLocalBounds().reduced (24, 14).removeFromTop (36),
                juce::Justification::topLeft);

    g.setColour (colors::accent.withAlpha (0.15f));
    g.drawHorizontalLine (54, 24.0f, (float) getWidth() - 24.0f);
}

void SteerTab::resized()
{
    auto bounds = getLocalBounds().reduced (16);
    bounds.removeFromTop (48);

    auto headerRow = bounds.removeFromTop (32);
    resetButton    .setBounds (headerRow.removeFromRight (60).reduced (0, 4));
    headerRow.removeFromRight (4);
    randomizeButton.setBounds (headerRow.removeFromRight (60).reduced (0, 4));
    headerRow.removeFromRight (4);
    keepButton     .setBounds (headerRow.removeFromRight (48).reduced (0, 4));
    headerRow.removeFromRight (4);
    depthPlusButton .setBounds (headerRow.removeFromRight (54).reduced (0, 4));
    headerRow.removeFromRight (4);
    depthMinusButton.setBounds (headerRow.removeFromRight (54).reduced (0, 4));
    headerRow.removeFromRight (8);
    coherenceToggle.setBounds (headerRow.removeFromRight (84).reduced (0, 4));
    headerRow.removeFromRight (12);
    hintLabel.setBounds (headerRow);
    bounds.removeFromTop (4);

    // A slim row for the coherence suggest-mode chips (visible only when the
    // toggle is on; the reserved strip keeps the layout stable either way).
    {
        auto row = bounds.removeFromTop (18);
        suggestPrefix.setBounds (row.removeFromLeft (110));
        for (int i = 0; i < kNumSuggestChips; ++i)
        {
            row.removeFromLeft (4);
            suggestChips[(size_t) i].setBounds (row.removeFromLeft (96).reduced (0, 1));
        }
    }
    bounds.removeFromTop (4);

    const int rightW = juce::jlimit (380, 540, bounds.getWidth() * 4 / 10);
    auto leftCol  = bounds.removeFromLeft (bounds.getWidth() - rightW - 12);
    bounds.removeFromLeft (12);
    auto rightCol = bounds;

    // Groove-feel strip (S4): slim row pinned under the family rows.
    auto grooveStrip = leftCol.removeFromBottom (34);
    leftCol.removeFromBottom (4);
    {
        auto s = grooveStrip;
        grooveStripLabel  .setBounds (s.removeFromLeft (56));
        grooveGravityLabel.setBounds (s.removeFromLeft (14));
        s.removeFromLeft (2);
        grooveGravityKnob .setBounds (s.removeFromLeft (32));
        s.removeFromLeft (8);
        grooveRestlessLabel.setBounds (s.removeFromLeft (14));
        s.removeFromLeft (2);
        grooveRestlessKnob.setBounds (s.removeFromLeft (32));
        s.removeFromLeft (10);
        grooveLockToggle  .setBounds (s.removeFromLeft (64));
    }

    const int totalRows = (int) rows.size();
    const int rowGap = 2;
    const int rowH = juce::jlimit (24, 36,
                                   (leftCol.getHeight() - rowGap * (totalRows - 1)) / juce::jmax (1, totalRows));

    const int labelW = juce::jlimit (110, 180, leftCol.getWidth() * 4 / 10);

    int y = leftCol.getY();
    for (auto& row : rows)
    {
        juce::Rectangle<int> r { leftCol.getX(), y, leftCol.getWidth(), rowH };
        row->label.setBounds (r.removeFromLeft (labelW));
        r.removeFromLeft (6);
        if (row->isTextureRow)
        {
            const int spinnerW = 84;
            textureOpCount.setBounds (r.removeFromLeft (spinnerW).reduced (0, 2));
            r.removeFromLeft (4);
            textureOpLabel.setBounds (r.removeFromLeft (22));
            r.removeFromLeft (4);
        }
        row->slider.setBounds (r.reduced (0, 4));
        y += rowH + rowGap;
    }

    const int pGap = 8;
    const int pH   = (rightCol.getHeight() - pGap * 3) / 4;
    int py = rightCol.getY();
    juce::Component* panels[] { &envelopePanel, &filterPanel,
                                 &mod0Panel,     &mod1Panel };
    for (auto* p : panels)
    {
        p->setBounds (rightCol.getX(), py, rightCol.getWidth(), pH);
        py += pH + pGap;
    }
}

} // namespace brocot
