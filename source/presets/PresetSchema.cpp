#include "PresetSchema.h"
#include "../PluginProcessor.h"

namespace brocot::preset
{

namespace
{
    juce::DynamicObject* asObject (const juce::var& v)
    {
        return v.isObject() ? v.getDynamicObject() : nullptr;
    }

    void setParam (juce::AudioProcessorValueTreeState& apvts,
                   juce::StringRef id, float normalisedOrRaw, bool isNormalised)
    {
        if (auto* p = apvts.getParameter (id.text))
        {
            const float v01 = isNormalised ? juce::jlimit (0.0f, 1.0f, normalisedOrRaw)
                                           : p->convertTo0to1 (normalisedOrRaw);
            p->setValueNotifyingHost (v01);
        }
    }

    int filterTypeIndex (const juce::String& s)
    {
        const auto lower = s.toLowerCase();
        if (lower == "lp")    return 0;
        if (lower == "bp")    return 1;
        if (lower == "hp")    return 2;
        if (lower == "notch") return 3;
        return 0;
    }
    const char* filterTypeString (int idx)
    {
        switch (idx) { case 0: return "lp"; case 1: return "bp";
                       case 2: return "hp"; case 3: return "notch"; }
        return "lp";
    }

    int lfoShapeIndex (const juce::String& s)
    {
        const auto lower = s.toLowerCase();
        if (lower == "sine")     return 0;
        if (lower == "triangle") return 1;
        if (lower == "square")   return 2;
        if (lower == "saw")      return 3;
        return 0;
    }
    const char* lfoShapeString (int idx)
    {
        switch (idx) { case 0: return "sine";   case 1: return "triangle";
                       case 2: return "square"; case 3: return "saw"; }
        return "sine";
    }

    float paramValue (const juce::AudioProcessorValueTreeState& apvts,
                      juce::StringRef id)
    {
        if (auto* p = apvts.getRawParameterValue (id.text))
            return p->load();
        return 0.0f;
    }
}

juce::var serialise (const BrocotAudioProcessor& proc)
{
    const auto& apvts = proc.getAPVTS();

    // Schema v2: required top-level fields are `name`, `version`,
    // `provenance`, and the synth-state blocks. Metadata (author,
    // description, category) lives inside `provenance` rather than at top
    // level; that makes "where did this preset come from?" explicit and
    // gives ARS-generated presets a home for search params + objective.
    auto* root = new juce::DynamicObject();
    root->setProperty ("name",    "untitled");
    root->setProperty ("version", kFormatVersion);

    auto* provenance = new juce::DynamicObject();
    provenance->setProperty ("kind",        "manual");  // manual edits / saves
    provenance->setProperty ("author",      "");
    provenance->setProperty ("description", "");
    provenance->setProperty ("category",    "user");
    root->setProperty ("provenance", juce::var (provenance));

    auto* engine = new juce::DynamicObject();
    {
        const int polyIdx = (int) std::lround (paramValue (apvts, params::ids::polyphony));
        engine->setProperty ("polyphony",
                             polyIdx >= 2 ? "poly" : polyIdx == 1 ? "duo" : "mono");
        engine->setProperty ("maxVoices",
                             (int) std::lround (paramValue (apvts, params::ids::maxVoices)));
    }
    engine->setProperty ("octaveShift",
                         (int) std::round (paramValue (apvts, params::ids::octaveShift)));

    const bool sharedMode = proc.isSharedMode();
    engine->setProperty ("pathMode", sharedMode ? "shared" : "free");
    if (sharedMode)
        engine->setProperty ("sharedPath", proc.getSharedPath());

    juce::Array<juce::var> ops;
    for (int i = 0; i < params::kNumOperators; ++i)
    {
        auto* op = new juce::DynamicObject();
        op->setProperty ("enabled",
                         paramValue (apvts, params::opId (i, "enabled")) > 0.5f);
        op->setProperty ("depth",
                         (double) paramValue (apvts, params::opId (i, "depth")));
        // Per-op detune (cents) and key-scaling — written only when non-zero
        // so existing/handwritten presets stay byte-identical at defaults.
        if (const float dt = paramValue (apvts, params::opId (i, "detune")); dt != 0.0f)
            op->setProperty ("detune", (double) dt);
        if (const float ks = paramValue (apvts, params::opId (i, "keyScale")); ks != 0.0f)
            op->setProperty ("keyScale", (double) ks);
        // Self-feedback exists on op 1 (index 0) only; written when non-zero.
        if (i == 0)
            if (const float fb = paramValue (apvts, params::opId (0, "feedback")); fb != 0.0f)
                op->setProperty ("feedback", (double) fb);
        if (sharedMode)
            op->setProperty ("position",
                             (int) std::round (paramValue (apvts, params::opId (i, "position"))));
        else
            op->setProperty ("path", proc.getOpPath (i));
        ops.add (juce::var (op));
    }
    engine->setProperty ("operators", ops);
    root->setProperty ("engine", juce::var (engine));

    auto* filter = new juce::DynamicObject();
    filter->setProperty ("type",
                         juce::String (filterTypeString (
                             (int) std::round (paramValue (apvts, params::ids::filterType)))));
    filter->setProperty ("cutoff",    (double) paramValue (apvts, params::ids::filterCutoff));
    filter->setProperty ("resonance", (double) paramValue (apvts, params::ids::filterResonance));
    root->setProperty ("filter", juce::var (filter));

    auto* envelope = new juce::DynamicObject();
    envelope->setProperty ("attack",  (double) paramValue (apvts, params::ids::envAttack));
    envelope->setProperty ("decay",   (double) paramValue (apvts, params::ids::envDecay));
    envelope->setProperty ("sustain", (double) paramValue (apvts, params::ids::envSustain));
    envelope->setProperty ("release", (double) paramValue (apvts, params::ids::envRelease));
    root->setProperty ("envelope", juce::var (envelope));

    auto* lfo = new juce::DynamicObject();
    lfo->setProperty ("shape",
                      juce::String (lfoShapeString (
                          (int) std::round (paramValue (apvts, params::ids::lfoShape)))));
    lfo->setProperty ("rate",    (double) paramValue (apvts, params::ids::lfoRate));
    lfo->setProperty ("depth",   (double) paramValue (apvts, params::ids::lfoDepth));
    lfo->setProperty ("keySync", paramValue (apvts, params::ids::lfoKeySync) > 0.5f);
    root->setProperty ("lfo", juce::var (lfo));

    auto* master = new juce::DynamicObject();
    master->setProperty ("level", (double) paramValue (apvts, params::ids::masterLevel));
    root->setProperty ("master", juce::var (master));

    return juce::var (root);
}

bool applyTo (BrocotAudioProcessor& proc, const juce::var& json, juce::String* outError)
{
    auto* root = asObject (json);
    if (root == nullptr)
    {
        if (outError) *outError = "Root is not a JSON object";
        return false;
    }

    // v1 -> v2 migration: synthesise a provenance block from the old flat
    // `author` / `description` / `category` fields. Existing v1 presets
    // (factory v1 + any user-saved v1 files) thus continue to load. The
    // saver always emits v2, so the migration is lazy / one-way at load.
    const juce::String fileVersion = root->getProperty ("version").toString();
    if (! root->hasProperty ("provenance") && fileVersion.startsWith ("1."))
    {
        auto* synth = new juce::DynamicObject();
        synth->setProperty ("kind",        "manual");
        synth->setProperty ("author",      root->getProperty ("author").toString());
        synth->setProperty ("description", root->getProperty ("description").toString());
        synth->setProperty ("category",
                            root->hasProperty ("category")
                                ? root->getProperty ("category").toString()
                                : juce::String ("user"));
        root->setProperty ("provenance", juce::var (synth));
    }

    auto& apvts = proc.getAPVTS();

    if (auto* engine = asObject (root->getProperty ("engine")))
    {
        setParam (apvts, params::ids::octaveShift,
                  (float) (int) engine->getProperty ("octaveShift"), false);

        const juce::String poly = engine->getProperty ("polyphony");
        const float polyIdx = (poly == "poly") ? 2.0f : (poly == "duo") ? 1.0f : 0.0f;
        setParam (apvts, params::ids::polyphony, polyIdx, false);
        // maxVoices absent (older presets) ⇒ leave at the param default (8).
        if (engine->hasProperty ("maxVoices"))
            setParam (apvts, params::ids::maxVoices,
                      (float) (int) engine->getProperty ("maxVoices"), false);

        const juce::String pathMode = engine->getProperty ("pathMode");
        const bool sharedMode = pathMode != "free";
        setParam (apvts, params::ids::pathMode, sharedMode ? 0.0f : 1.0f, false);

        if (sharedMode)
        {
            const juce::String sp = engine->getProperty ("sharedPath");
            proc.setSharedPath (sp);
        }

        if (engine->getProperty ("operators").isArray())
        {
            const auto* opsArr = engine->getProperty ("operators").getArray();
            const int n = juce::jmin ((int) opsArr->size(), params::kNumOperators);
            for (int i = 0; i < n; ++i)
            {
                auto* op = asObject ((*opsArr)[i]);
                if (op == nullptr) continue;

                setParam (apvts, params::opId (i, "enabled"),
                          (bool) op->getProperty ("enabled") ? 1.0f : 0.0f, false);
                setParam (apvts, params::opId (i, "depth"),
                          (float) (double) op->getProperty ("depth"), false);
                // Optional per-op detune / key-scaling — absent ⇒ 0 (so older
                // presets without these keys load as exact-ratio, no scaling).
                setParam (apvts, params::opId (i, "detune"),
                          op->hasProperty ("detune")
                              ? (float) (double) op->getProperty ("detune") : 0.0f, false);
                setParam (apvts, params::opId (i, "keyScale"),
                          op->hasProperty ("keyScale")
                              ? (float) (double) op->getProperty ("keyScale") : 0.0f, false);
                // Self-feedback (op 1 / index 0 only) — absent ⇒ 0 (clean sine).
                if (i == 0)
                    setParam (apvts, params::opId (0, "feedback"),
                              op->hasProperty ("feedback")
                                  ? (float) (double) op->getProperty ("feedback") : 0.0f, false);

                if (sharedMode)
                    setParam (apvts, params::opId (i, "position"),
                              (float) (int) op->getProperty ("position"), false);
                else
                    proc.setOpPath (i, op->getProperty ("path").toString());
            }
            // Reset ops beyond what the preset specifies — important now that
            // kNumOperators is 16 but factory presets still ship with 8
            // entries. Without this, loading an 8-op preset after manually
            // enabling op 15 leaves op 15 still on.
            for (int i = n; i < params::kNumOperators; ++i)
            {
                setParam (apvts, params::opId (i, "enabled"),  0.0f, false);
                setParam (apvts, params::opId (i, "depth"),    0.0f, false);
                setParam (apvts, params::opId (i, "position"), 0.0f, false);
                setParam (apvts, params::opId (i, "detune"),   0.0f, false);
                setParam (apvts, params::opId (i, "keyScale"), 0.0f, false);
                if (i == 0)
                    setParam (apvts, params::opId (0, "feedback"), 0.0f, false);
                if (! sharedMode)
                    proc.setOpPath (i, "");
            }
        }
    }

    if (auto* filter = asObject (root->getProperty ("filter")))
    {
        setParam (apvts, params::ids::filterType,
                  (float) filterTypeIndex (filter->getProperty ("type").toString()), false);
        setParam (apvts, params::ids::filterCutoff,
                  (float) (double) filter->getProperty ("cutoff"), false);
        setParam (apvts, params::ids::filterResonance,
                  (float) (double) filter->getProperty ("resonance"), false);
    }

    if (auto* env = asObject (root->getProperty ("envelope")))
    {
        setParam (apvts, params::ids::envAttack,  (float) (double) env->getProperty ("attack"),  false);
        setParam (apvts, params::ids::envDecay,   (float) (double) env->getProperty ("decay"),   false);
        setParam (apvts, params::ids::envSustain, (float) (double) env->getProperty ("sustain"), false);
        setParam (apvts, params::ids::envRelease, (float) (double) env->getProperty ("release"), false);
    }

    if (auto* lfo = asObject (root->getProperty ("lfo")))
    {
        setParam (apvts, params::ids::lfoShape,
                  (float) lfoShapeIndex (lfo->getProperty ("shape").toString()), false);
        setParam (apvts, params::ids::lfoRate,    (float) (double) lfo->getProperty ("rate"),    false);
        setParam (apvts, params::ids::lfoDepth,   (float) (double) lfo->getProperty ("depth"),   false);
        setParam (apvts, params::ids::lfoKeySync, (bool)  lfo->getProperty ("keySync") ? 1.0f : 0.0f, false);
    }

    if (auto* master = asObject (root->getProperty ("master")))
        setParam (apvts, params::ids::masterLevel,
                  (float) (double) master->getProperty ("level"), false);

    return true;
}

} // namespace brocot::preset
