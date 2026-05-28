#include "PullIndex.h"

namespace brocot
{

namespace
{
    juce::DynamicObject* asObject (const juce::var& v)
    {
        return v.isObject() ? v.getDynamicObject() : nullptr;
    }

    template <typename T>
    std::vector<T> toFloatVec (const juce::var& v)
    {
        std::vector<T> out;
        if (auto* arr = v.getArray())
        {
            out.reserve ((size_t) arr->size());
            for (const auto& x : *arr) out.push_back ((T) (double) x);
        }
        return out;
    }

    PullIndex::OpState parseOp (const juce::var& v)
    {
        PullIndex::OpState o;
        if (auto* obj = asObject (v))
        {
            o.path    = obj->getProperty ("path").toString();
            o.depth   = (float) (double) obj->getProperty ("depth");
            o.enabled = (bool) obj->getProperty ("enabled");
        }
        return o;
    }
}

bool PullIndex::load (const juce::File& file, juce::String* outError)
{
    if (! file.existsAsFile())
    {
        if (outError) *outError = "Pull index not found: " + file.getFullPathName();
        return false;
    }

    const auto json = juce::JSON::parse (file);
    auto* root = asObject (json);
    if (root == nullptr)
    {
        if (outError) *outError = "Pull index is not a JSON object: "
                                  + file.getFullPathName();
        return false;
    }

    qMax        = (int) root->getProperty ("q_max");
    fCarrierRef = (double) root->getProperty ("f_carrier_ref");

    categories.clear();

    auto cats = root->getProperty ("categories");
    auto* catsObj = asObject (cats);
    if (catsObj == nullptr)
    {
        if (outError) *outError = "Pull index missing 'categories' object";
        return false;
    }

    for (const auto& prop : catsObj->getProperties())
    {
        Category c;
        c.name = prop.name.toString();

        if (auto* obj = asObject (prop.value))
        {
            c.presetCount   = (int) obj->getProperty ("preset_count");
            c.bestObjPreset = obj->getProperty ("best_obj_preset").toString();
            c.centroidAmps  = toFloatVec<float> (obj->getProperty ("centroid_amps"));

            if (auto* exArr = obj->getProperty ("exemplars").getArray())
            {
                c.exemplars.reserve ((size_t) exArr->size());
                for (const auto& exv : *exArr)
                {
                    if (auto* exo = asObject (exv))
                    {
                        Exemplar e;
                        e.preset    = exo->getProperty ("preset").toString();
                        e.objective = (double) exo->getProperty ("objective");
                        e.nOps      = (int) exo->getProperty ("n_ops");
                        e.amps      = toFloatVec<float> (exo->getProperty ("amps"));
                        if (auto* opsArr = exo->getProperty ("ops").getArray())
                        {
                            e.ops.reserve ((size_t) opsArr->size());
                            for (const auto& opv : *opsArr)
                                e.ops.push_back (parseOp (opv));
                        }
                        c.exemplars.push_back (std::move (e));
                    }
                }
            }
        }

        categories.push_back (std::move (c));
    }

    // Sort categories alphabetically — the source order is JSON-object
    // dependent, which JUCE happens to preserve but we shouldn't rely
    // on that for the UI. Alpha order is also nice for the steer tab.
    std::sort (categories.begin(), categories.end(),
               [] (const Category& a, const Category& b)
               { return a.name.compareIgnoreCase (b.name) < 0; });

    return true;
}

juce::File PullIndex::findIndexFile()
{
   #if defined (BROCOT_SOURCE_DIR)
    {
        juce::File devFile (juce::String (BROCOT_SOURCE_DIR)
                            + "/resources/pull_index.json");
        if (devFile.existsAsFile()) return devFile;
    }
   #endif

    auto app = juce::File::getSpecialLocation (
                   juce::File::currentApplicationFile);
    for (const auto& c : {
             app.getParentDirectory().getChildFile ("../../../resources/pull_index.json"),
             app.getParentDirectory().getChildFile ("resources/pull_index.json"),
             juce::File::getCurrentWorkingDirectory().getChildFile ("resources/pull_index.json")
         })
        if (c.existsAsFile()) return c;
    return {};
}

juce::StringArray PullIndex::getCategoryNames() const
{
    juce::StringArray out;
    out.ensureStorageAllocated ((int) categories.size());
    for (const auto& c : categories) out.add (c.name);
    return out;
}

juce::StringArray PullIndex::getCategoryNamesStartingWith (juce::StringRef prefix) const
{
    juce::StringArray out;
    for (const auto& c : categories)
        if (c.name.startsWith (prefix))
            out.add (c.name);
    return out;
}

const PullIndex::Category* PullIndex::getCategory (const juce::String& name) const
{
    for (const auto& c : categories)
        if (c.name == name) return &c;
    return nullptr;
}

const std::vector<PullIndex::OpState>*
PullIndex::getBestOps (const juce::String& category) const
{
    if (auto* c = getCategory (category))
        if (! c->exemplars.empty())
            return &c->exemplars.front().ops;   // exemplars are obj-sorted in the JSON
    return nullptr;
}

juce::String PullIndex::pathWalk (const juce::String& from,
                                   const juce::String& to,
                                   float alpha)
{
    alpha = juce::jlimit (0.0f, 1.0f, alpha);
    if (from == to)               return from;
    if (alpha <= 0.0f)            return from;
    if (alpha >= 1.0f)            return to;

    // Longest common prefix = position of the SB LCA along the walk.
    const int lenF = from.length();
    const int lenT = to.length();
    int lca = 0;
    while (lca < lenF && lca < to.length() && from[lca] == to[lca])
        ++lca;

    const int upSteps   = lenF - lca;  // parent-trim steps from `from`
    const int downSteps = lenT - lca;  // L/R-append steps to reach `to`
    const int totalSteps = upSteps + downSteps;
    if (totalSteps == 0) return from;  // identical paths, already handled

    // Round-to-nearest gives equal-width segments centred on transition
    // points (better than truncation which leaves a 1.0-only segment
    // when totalSteps is small).
    const int step = juce::jlimit (0, totalSteps,
                                   juce::roundToInt (alpha * (float) totalSteps));

    if (step <= upSteps)
        return from.substring (0, lenF - step);

    // We've passed the LCA; descend toward `to`.
    return to.substring (0, lca + (step - upSteps));
}

std::vector<PullIndex::OpState>
PullIndex::morph (const std::vector<OpState>& current,
                  const std::vector<OpState>& target,
                  float alpha)
{
    alpha = juce::jlimit (0.0f, 1.0f, alpha);

    // Pad the shorter input with implicit-disabled slots so a
    // shorter target fades the extra current slots OUT to silence,
    // and a shorter current fades the extra target slots IN from
    // silence. Otherwise morphing a 64-op current toward a 16-slot
    // exemplar only walks the first 16 slots — the rest stay loud
    // and the morph is inaudible. Symptom Will hit on the
    // Textured-Smooth row before this fix.
    const size_t n = juce::jmax (current.size(), target.size());
    static const OpState kImplicitOff {};   // disabled, depth=0, empty path
    std::vector<OpState> out;
    out.reserve (n);

    for (size_t i = 0; i < n; ++i)
    {
        const auto& cur = (i < current.size() ? current[i] : kImplicitOff);
        const auto& tgt = (i < target.size()  ? target[i]  : kImplicitOff);
        OpState o;

        // Effective depths: a disabled slot contributes 0 to the lerp
        // regardless of its stored depth value. This is what makes
        // enable transitions click-free — a slot that's transitioning
        // false→true ramps from 0 to tgt.depth as α grows.
        const float curEffective = cur.enabled ? cur.depth : 0.0f;
        const float tgtEffective = tgt.enabled ? tgt.depth : 0.0f;
        o.depth = juce::jmap (alpha, curEffective, tgtEffective);

        // Enabled: only force off at α=1 against an off target; at the
        // far endpoints we exactly match cur / tgt. Intermediate values
        // keep the slot on whenever EITHER side wants it on, so depth
        // can ramp through silence to the final state without the
        // engine muting the slot mid-fade.
        if (alpha <= 0.0f)
            o.enabled = cur.enabled;
        else if (alpha >= 1.0f)
            o.enabled = tgt.enabled;
        else
            o.enabled = cur.enabled || tgt.enabled;

        // Path: stepped SB-tree walk; small ratio nudges that the
        // audio-thread smoother absorbs.
        o.path = pathWalk (cur.path, tgt.path, alpha);

        // Waveform is discrete — can't lerp sine→saw — so snap at the midpoint
        // (the half nearer the destination wins).
        o.waveform = (alpha < 0.5f ? cur : tgt).waveform;

        out.push_back (std::move (o));
    }
    return out;
}

} // namespace brocot
