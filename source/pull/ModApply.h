#pragma once

#include "Landscape.h"

#include <utility>
#include <vector>

// The plan logic below is pure juce_core so the test binary (which links
// neither juce_audio_processors nor the GUI modules) can pin it. Plugin
// builds — where JUCE defines JUCE_MODULE_AVAILABLE_juce_audio_processors —
// additionally get the APVTS executor and compile-time checks that the
// local mirrors match the real params:: facts.
#if JUCE_MODULE_AVAILABLE_juce_audio_processors
 #include "../params/ParameterTree.h"
#endif

namespace brocot
{

/**
    Mod-map → universal-modulator-bank mapping (M3b, phase3/modmap.md).

    A mod-map node ships ModSpecs — offline modulator assignments of the form
    "an LFO of `shape` at `rate` Hz scaling op-`op` depth (kind 0) or op-0
    feedback (kind 1) by 1 + depth·wave(2π·rate·t)". These helpers realize a
    spec in the runtime 16-slot universal modulator bank, mirroring the
    reset-then-overlay convention of PresetSchema::applyModBank (the
    .brocotmod leak fix): first zero what could sound, then overlay the plan.

    ── HONESTY NOTE (Will QAs by ear) ─────────────────────────────────────
    The offline renderer modeled modulation MULTIPLICATIVELY — the target
    (op depth or feedback) scaled by 1 + mdepth·sin(2π·rate·t). The runtime
    bank differs in both units and route:
      · Op-depth targets are ADDITIVE at runtime (±bankDepth is summed onto
        the op's effective depth, in 0..8 op-depth units), so we scale
        spec.depth by kDepthSwing to approximate the offline swing at
        typical map op depths — not the exact multiplicative curve.
      · Kind-1 (op-0 feedback) specs route to the REAL Feedback target
        (params::TargetKind::Feedback, added 2026-08-31 — before that they
        were approximated as op-0 depth wobble). The runtime route is
        additive on the 0..1 feedback amount while the offline model was
        multiplicative (fb0·(1 + d·sin)), so the plan scales spec.depth by
        kFeedbackSwing ≈ the swing at mid feedback; magnitude is approximate,
        speed/shape/route are exact.
    The mapping therefore targets AUDIBLE EQUIVALENCE (a wobble of the same
    speed, shape and rough magnitude on the same operator), not bit-parity
    with the offline trajectory fingerprints.
*/
namespace modapply
{
    // ── Mirrors of the params:: facts the pure logic needs ─────────────
    // (static_assert-checked against the real ones in plugin builds; the
    // tests pin the literal values/strings independently).

    /** = params::kNumModulators. */
    inline constexpr int kNumSlots = 16;

    /** = params::modId — the "mod<slot>.<key>" APVTS id contract. */
    inline juce::String slotParamId (int slot, const char* key)
    {
        return juce::String ("mod") + juce::String (slot)
             + juce::String (".") + juce::String (key);
    }

    /** = (int) params::TargetKind::Feedback in the targetKind choice list. */
    inline constexpr int kTargetKindFeedback = 7;

    /** Additive swing on the 0..1 feedback amount per unit spec depth —
        matches the offline multiplicative model at fb0 = 0.5. */
    inline constexpr float kFeedbackSwing = 0.5f;

    /** = (int) params::TargetKind::OpDepth in the targetKind choice list
        (Filter cutoff, Carrier AM, Carrier FM, Op depth, ...). */
    inline constexpr int kTargetKindOpDepth = 3;

    /** Bank depth per unit spec.depth for both target kinds. The bank's
        bipolar LFO output (±depth, 0..1 param) is added onto the op's
        effective depth (0..8 units); typical map op depths sit around
        0.5–1, so ±0.5·spec.depth lands in the offline ±spec.depth·opDepth
        swing regime without ever overdriving a low-depth op into a
        rectified square (negative effective depth clamps at 0). */
    inline constexpr float kDepthSwing = 0.5f;

    // The bank's rate param range (ParameterTree: logRange (0.1, 800, 5),
    // plan values are denormalized Hz so only the endpoints matter here).
    // Specs are clamped into it; the shipped map mines 0.3–7 Hz, so the
    // clamp only ever bites on hand-edited or future artifacts.
    inline constexpr float kRateMinHz = 0.1f;
    inline constexpr float kRateMaxHz = 800.0f;

    // Bank shape-choice indices (ParameterTree shapeChoices — the
    // envelope::Shape order): spec sine → "Sine" (0), spec tri →
    // "Triangle" (6, NOT 1 — Von Mises sits at 1).
    inline constexpr int kShapeSine     = 0;
    inline constexpr int kShapeTriangle = 6;

    /** The (paramID, DENORMALIZED value) pairs that realize `spec` in bank
        slot `slot`. Covers ALL 11 per-slot params (kModParams' field set) so
        a leftover lock / PSK / key-sync from the previous patch can never
        deform the planned LFO; non-planned fields get their ParameterTree
        defaults. Out-of-range slots yield an empty plan. */
    inline std::vector<std::pair<juce::String, float>>
        modParamPlan (const Landscape::ModSpec& spec, int slot)
    {
        std::vector<std::pair<juce::String, float>> plan;
        if (slot < 0 || slot >= kNumSlots)
            return plan;

        const float rate  = juce::jlimit (kRateMinHz, kRateMaxHz, spec.rate);
        const bool  isFeedback = spec.kind == 1;
        const float depth = juce::jlimit (0.0f, 1.0f, spec.depth)
                          * (isFeedback ? kFeedbackSwing : kDepthSwing);
        const int   shape = spec.shape == 1 ? kShapeTriangle : kShapeSine;
        // kind 0 → that op's depth; kind 1 → the real op-0 Feedback route
        // (see honesty note above for the units). targetIndex is an IntParam
        // 0..15 (and the processor clamps to the same range), so that is the
        // hard ceiling — fine for mod maps, which are ≤ 16-op (the shipped
        // one is 6-op).
        const int targetKind = isFeedback ? kTargetKindFeedback : kTargetKindOpDepth;
        const int targetOp   = isFeedback ? 0 : juce::jlimit (0, 15, spec.op);

        auto add = [&plan, slot] (const char* key, float v)
        { plan.emplace_back (slotParamId (slot, key), v); };

        add ("rate",        rate);
        add ("shape",       (float) shape);
        add ("kappa",       8.0f);                 // default (unused by sine/tri)
        add ("depth",       depth);
        add ("lockKind",    0.0f);                 // Free — rate is the spec's
        add ("lockIndex",   0.0f);
        add ("targetKind",  (float) targetKind);
        add ("targetIndex", (float) targetOp);
        add ("psk",         0.0f);                 // no phase quantization
        add ("pskSmooth",   0.0f);
        add ("keySync",     0.0f);
        return plan;
    }

    /** The reset half of the reset-then-overlay convention: zero all 16
        slots' depths. Depth 0 makes a slot inert whatever its other params
        hold, so this alone guarantees no previous modulation survives a
        node apply. */
    inline std::vector<std::pair<juce::String, float>> resetPlan()
    {
        std::vector<std::pair<juce::String, float>> plan;
        plan.reserve ((size_t) kNumSlots);
        for (int i = 0; i < kNumSlots; ++i)
            plan.emplace_back (slotParamId (i, "depth"), 0.0f);
        return plan;
    }

   #if JUCE_MODULE_AVAILABLE_juce_audio_processors
    static_assert (kNumSlots == params::kNumModulators,
                   "modapply mirror drifted from params::kNumModulators");
    static_assert (kTargetKindOpDepth == (int) params::TargetKind::OpDepth,
                   "modapply mirror drifted from params::TargetKind");
    static_assert (kTargetKindFeedback == (int) params::TargetKind::Feedback,
                   "modapply mirror drifted from params::TargetKind::Feedback");

    /** Execute reset + one plan per spec (spec i → bank slot i; specs past
        the bank size are ignored) via setValueNotifyingHost. Values are
        denormalized; each param's own range does the 0..1 conversion. */
    inline void applyModsToAPVTS (juce::AudioProcessorValueTreeState& apvts,
                                  const std::vector<Landscape::ModSpec>& mods)
    {
        // The one mirror a static_assert can't reach: the id format.
        jassert (slotParamId (0, "rate") == params::modId (0, "rate"));

        auto setAll = [&apvts] (const std::vector<std::pair<juce::String, float>>& plan)
        {
            for (const auto& [id, v] : plan)
                if (auto* p = apvts.getParameter (id))
                    p->setValueNotifyingHost (p->convertTo0to1 (v));
        };
        setAll (resetPlan());
        for (int i = 0; i < (int) mods.size() && i < kNumSlots; ++i)
            setAll (modParamPlan (mods[(size_t) i], i));
    }
   #endif
}

} // namespace brocot
