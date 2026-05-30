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
            o.path     = obj->getProperty ("path").toString();
            o.depth    = (float) (double) obj->getProperty ("depth");
            o.enabled  = (bool) obj->getProperty ("enabled");
            o.waveform = (int) obj->getProperty ("wave");   // absent ⇒ 0 (sine)
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

    // The graph ships gzipped (.json.gz) to keep the artifact under control as
    // we add per-depth maps. Decompress on the fly, then parse. Raw .json still
    // loads (no .gz extension) for ad-hoc debugging.
    juce::String text;
    if (file.getFileExtension() == ".gz")
    {
        juce::FileInputStream raw (file);
        if (! raw.openedOk())
        {
            if (outError) *outError = "Could not open landscape graph: " + file.getFullPathName();
            return false;
        }
        juce::GZIPDecompressorInputStream gz (
            &raw, false, juce::GZIPDecompressorInputStream::gzipFormat);
        text = gz.readEntireStreamAsString();
    }
    else
    {
        text = file.loadFileAsString();
    }

    const auto json = juce::JSON::parse (text);
    auto* root = asObject (json);
    if (root == nullptr)
    {
        if (outError) *outError = "Landscape graph is not a JSON object";
        return false;
    }

    nodes.clear();
    familyNames.clear();
    nextHop.clear();

    nOps = (int) root->getProperty ("n_ops");
    if (nOps <= 0) nOps = 6;          // older graphs without the field
    // Normalize to a uniform "<N>-op[ <descriptor>]" label. Embedded names use
    // a "<N>op <desc>" form ("6op sine", "4op mix"); strip that leading
    // op-count token and re-prefix with the real depth so every map reads the
    // same way ("6-op sine", "4-op mix"). Maps with no embedded name (the plain
    // per-depth maps) become just "<N>-op".
    const auto rawName    = root->getProperty ("name").toString();
    const auto descriptor = rawName.isEmpty()
                                ? juce::String()
                                : rawName.fromFirstOccurrenceOf ("op", false, false).trim();
    mapName = juce::String (nOps) + "-op"
                  + (descriptor.isNotEmpty() ? " " + descriptor : juce::String());

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
    // Locate on this map's slice only — the first nOps ops (the gross tone the
    // map governs) — so the depth-N map ignores the patch's higher texture ops.
    const int sliceN = juce::jmin ((int) current.size(), nOps);
    const std::vector<PullIndex::OpState> slice (current.begin(), current.begin() + sliceN);
    const auto q = ratioHistogram (slice);
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
    // Prefer the gzipped artifact; fall back to raw JSON if someone left one
    // around for debugging. Same dev-source-then-app-relative search as PullIndex.
    static const char* kNames[] = { "landscape_graph.json.gz",
                                    "landscape_graph.json" };
   #if defined (BROCOT_SOURCE_DIR)
    for (const char* name : kNames)
    {
        juce::File devFile (juce::String (BROCOT_SOURCE_DIR) + "/resources/" + name);
        if (devFile.existsAsFile()) return devFile;
    }
   #endif

    auto app = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
    for (const char* name : kNames)
        for (const auto& c : {
                 app.getParentDirectory().getChildFile ("../../../resources/" + juce::String (name)),
                 app.getParentDirectory().getChildFile ("resources/" + juce::String (name)),
                 juce::File::getCurrentWorkingDirectory().getChildFile ("resources/" + juce::String (name))
             })
            if (c.existsAsFile()) return c;
    return {};
}

juce::Array<juce::File> Landscape::findAllFiles()
{
    // Resolve the resources dir from the first place a graph exists, then list
    // every per-depth map there, sorted by depth (the _N suffix; bare = 6).
    auto anchor = findFile();
    juce::Array<juce::File> files;
    if (anchor == juce::File()) return files;

    anchor.getParentDirectory()
        .findChildFiles (files, juce::File::findFiles, false, "landscape_graph*.json.gz");

    auto depthOf = [] (const juce::File& f)
    {
        auto s = f.getFileName().fromFirstOccurrenceOf ("landscape_graph", false, false)
                                .upToFirstOccurrenceOf (".json", false, false);   // "" or "_N"
        return s.isEmpty() ? 6 : s.trimCharactersAtStart ("_").getIntValue();
    };
    std::sort (files.begin(), files.end(),
               [&] (const juce::File& a, const juce::File& b)
               {
                   const int da = depthOf (a), db = depthOf (b);
                   if (da != db) return da < db;
                   // Deterministic order within a depth (e.g. the six 6-op
                   // maps): fall back to filename. The bare map sorts first
                   // since '.' < '_'.
                   return a.getFileName().compare (b.getFileName()) < 0;
               });
    return files;
}

} // namespace brocot
