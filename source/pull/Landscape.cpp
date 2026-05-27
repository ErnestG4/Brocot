#include "Landscape.h"

#include "../audio/Path.h"

#include <cmath>

namespace brocot
{

namespace
{
    juce::DynamicObject* asObject (const juce::var& v)
    {
        return v.isObject() ? v.getDynamicObject() : nullptr;
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

std::vector<float> Landscape::ratioHistogram (const std::vector<PullIndex::OpState>& ops)
{
    // Depth-weighted log2-ratio histogram: a cheap, FFT-free param signature.
    // Each active op drops its depth into the log2(ratio) bin over [-5, +5]
    // octaves (covers the curated Stern-Brocot ratio span); L1-normalised so
    // configs of different op counts / overall depth compare on shape.
    std::vector<float> h ((size_t) kHistBins, 0.0f);
    constexpr float lo = -5.0f, hi = 5.0f;
    for (const auto& o : ops)
    {
        if (! o.enabled || o.depth <= 0.0f)
            continue;
        const double r = Path (o.path).ratio();          // empty path → 1.0
        if (! (r > 0.0))
            continue;
        const float l2 = (float) std::log2 (r);
        const float x  = (l2 - lo) / (hi - lo);           // → [0,1]
        int bin = (int) std::floor (x * kHistBins);
        bin = juce::jlimit (0, kHistBins - 1, bin);
        h[(size_t) bin] += o.depth;
    }
    float s = 0.0f;
    for (float v : h) s += v;
    if (s > 0.0f)
        for (float& v : h) v /= s;
    return h;
}

bool Landscape::load (const juce::File& file, juce::String* outError)
{
    if (! file.existsAsFile())
    {
        if (outError) *outError = "Landscape graph not found: " + file.getFullPathName();
        return false;
    }

    const auto json = juce::JSON::parse (file);
    auto* root = asObject (json);
    if (root == nullptr)
    {
        if (outError) *outError = "Landscape graph is not a JSON object";
        return false;
    }

    nodes.clear();
    familyNames.clear();
    nextHop.clear();

    if (auto* fnArr = root->getProperty ("family_names").getArray())
        for (const auto& v : *fnArr)
            familyNames.add (v.toString());

    if (auto* nodeArr = root->getProperty ("nodes").getArray())
    {
        nodes.reserve ((size_t) nodeArr->size());
        for (const auto& nv : *nodeArr)
        {
            Node n;
            if (auto* no = asObject (nv))
            {
                n.family   = (int) no->getProperty ("family");
                n.feedback = (float) (double) no->getProperty ("feedback");
                n.x        = (float) (double) no->getProperty ("x");
                n.y        = (float) (double) no->getProperty ("y");
                n.bright   = (float) (double) no->getProperty ("bright");
                n.dense    = (float) (double) no->getProperty ("dense");
                n.odd      = (float) (double) no->getProperty ("odd");
                n.tonal    = (float) (double) no->getProperty ("tonal");
                n.crit     = (float) (double) no->getProperty ("crit");
                if (auto* opsArr = no->getProperty ("ops").getArray())
                    for (const auto& opv : *opsArr)
                        n.ops.push_back (parseOp (opv));
            }
            n.ratioHist = ratioHistogram (n.ops);
            nodes.push_back (std::move (n));
        }
    }

    if (auto* nh = asObject (root->getProperty ("next_hop")))
    {
        nextHop.assign ((size_t) familyNames.size(), {});
        for (const auto& prop : nh->getProperties())
        {
            const int fi = familyNames.indexOf (prop.name.toString());
            if (fi < 0) continue;
            std::vector<int> field;
            if (auto* arr = prop.value.getArray())
            {
                field.reserve ((size_t) arr->size());
                for (const auto& x : *arr) field.push_back ((int) x);
            }
            nextHop[(size_t) fi] = std::move (field);
        }
    }

    if (nodes.empty())
    {
        if (outError) *outError = "Landscape graph has no nodes";
        return false;
    }
    return true;
}

juce::StringArray Landscape::getFamilyNames() const { return familyNames; }

int Landscape::familyIndex (const juce::String& name) const
{
    return familyNames.indexOf (name);
}

bool Landscape::hasFamily (const juce::String& name) const
{
    const int fi = familyIndex (name);
    return fi >= 0 && fi < (int) nextHop.size() && ! nextHop[(size_t) fi].empty();
}

int Landscape::nearestNode (const std::vector<PullIndex::OpState>& current) const
{
    if (nodes.empty())
        return -1;
    const auto q = ratioHistogram (current);
    int best = 0;
    float bestD = std::numeric_limits<float>::max();
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        const auto& h = nodes[i].ratioHist;
        float d = 0.0f;
        for (size_t b = 0; b < h.size(); ++b)
            d += std::abs (h[b] - q[b]);
        if (d < bestD) { bestD = d; best = (int) i; }
    }
    return best;
}

std::vector<int> Landscape::route (int startNode, const juce::String& family) const
{
    std::vector<int> path;
    const int fi = familyIndex (family);
    if (fi < 0 || fi >= (int) nextHop.size())
        return path;
    const auto& field = nextHop[(size_t) fi];
    if (startNode < 0 || startNode >= (int) field.size())
        return path;

    int node = startNode;
    int guard = (int) nodes.size() + 1;          // cycle / runaway guard
    path.push_back (node);
    while (guard-- > 0)
    {
        const int nh = field[(size_t) node];
        if (nh < 0)                               // -1 arrived, -2 unreachable
            break;
        if (nh >= (int) nodes.size())
            break;
        node = nh;
        path.push_back (node);
    }
    return path;
}

std::vector<Landscape::Waypoint>
Landscape::waypoints (int startNode, const juce::String& family) const
{
    std::vector<Waypoint> out;
    for (int idx : route (startNode, family))
        if (auto* n = node (idx))
            out.push_back ({ n->ops, n->feedback });
    return out;
}

juce::File Landscape::findFile()
{
   #if defined (BROCOT_SOURCE_DIR)
    {
        juce::File devFile (juce::String (BROCOT_SOURCE_DIR)
                            + "/resources/landscape_graph.json");
        if (devFile.existsAsFile()) return devFile;
    }
   #endif

    auto app = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
    for (const auto& c : {
             app.getParentDirectory().getChildFile ("../../../resources/landscape_graph.json"),
             app.getParentDirectory().getChildFile ("resources/landscape_graph.json"),
             juce::File::getCurrentWorkingDirectory().getChildFile ("resources/landscape_graph.json")
         })
        if (c.existsAsFile()) return c;
    return {};
}

} // namespace brocot
