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

std::vector<PullIndex::OpState>
PullIndex::morph (const std::vector<OpState>& current,
                  const std::vector<OpState>& target,
                  float alpha)
{
    alpha = juce::jlimit (0.0f, 1.0f, alpha);
    const size_t n = juce::jmin (current.size(), target.size());
    std::vector<OpState> out;
    out.reserve (n);

    const bool snap = alpha >= 0.5f;

    for (size_t i = 0; i < n; ++i)
    {
        OpState o;
        o.depth   = juce::jmap (alpha, current[i].depth, target[i].depth);
        o.enabled = snap ? target[i].enabled : current[i].enabled;
        o.path    = snap ? target[i].path    : current[i].path;
        out.push_back (std::move (o));
    }
    return out;
}

} // namespace brocot
