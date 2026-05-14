#include "PresetManager.h"
#include "PresetSchema.h"
#include "../PluginProcessor.h"

namespace brocot
{

PresetManager::PresetManager (BrocotAudioProcessor& processor)
    : proc (processor)
{
    rescan();
}

juce::File PresetManager::getUserDirectory() const
{
    auto dir = juce::File::getSpecialLocation (
                   juce::File::userApplicationDataDirectory)
                   .getChildFile ("Brocot").getChildFile ("Presets");
    if (! dir.exists()) dir.createDirectory();
    return dir;
}

juce::File PresetManager::getFactoryDirectory() const
{
    // Try dev-mode source dir first (set by CMake), then a couple of
    // relative paths for packaged builds. The first hit wins.
   #if defined (BROCOT_SOURCE_DIR)
    {
        juce::File devDir (juce::String (BROCOT_SOURCE_DIR) + "/resources/presets/factory");
        if (devDir.isDirectory()) return devDir;
    }
   #endif

    auto app = juce::File::getSpecialLocation (
                   juce::File::currentApplicationFile);
    for (const auto& c : {
             app.getParentDirectory().getChildFile ("../../../resources/presets/factory"),
             app.getParentDirectory().getChildFile ("resources/presets/factory"),
             juce::File::getCurrentWorkingDirectory().getChildFile ("resources/presets/factory")
         })
        if (c.isDirectory()) return c;
    return {};
}

void PresetManager::rescan()
{
    entries.clear();

    auto add = [this] (const juce::File& f, juce::StringRef category, bool factory)
    {
        Entry e;
        e.file = f;
        e.category = category;
        e.factory = factory;
        // Try to read the preset's friendly name; fall back to filename.
        if (auto json = juce::JSON::parse (f); auto* obj = json.getDynamicObject())
            e.name = obj->getProperty ("name").toString();
        if (e.name.isEmpty())
            e.name = f.getFileNameWithoutExtension();
        entries.push_back (std::move (e));
    };

    // P3.11: scan the top-level factory dir (32 hand-authored anchors)
    // plus the `curated/` subdir (search-derived best-of-category
    // anchors copied in by `phase3/curate_factory.py`). The deeper
    // `discovered/`, `discovered_p37/`, and `experimental_*` trees
    // hold the full search bulk — they feed the runtime PullIndex but
    // stay invisible to the preset menu so the user sees a tidy ~48
    // canonical list rather than 2200+ variants. Shaping is the sound
    // selection; presets are the anchor points.
    if (auto fac = getFactoryDirectory(); fac.isDirectory())
    {
        for (const auto& f : fac.findChildFiles (juce::File::findFiles, false, "*.brocot"))
            add (f, "factory", true);

        if (auto curated = fac.getChildFile ("curated"); curated.isDirectory())
            for (const auto& f : curated.findChildFiles (juce::File::findFiles, false, "*.brocot"))
                add (f, "factory", true);
    }

    auto user = getUserDirectory();
    if (user.isDirectory())
        for (const auto& f : user.findChildFiles (juce::File::findFiles, true, "*.brocot"))
            add (f, "user", false);

    std::sort (entries.begin(), entries.end(),
               [] (const Entry& a, const Entry& b) {
                   if (a.factory != b.factory) return a.factory;   // factory first
                   return a.name.compareIgnoreCase (b.name) < 0;
               });
}

bool PresetManager::load (const juce::File& file, juce::String* outError)
{
    if (! file.existsAsFile())
    {
        if (outError) *outError = "File does not exist: " + file.getFullPathName();
        return false;
    }
    const auto json = juce::JSON::parse (file);
    if (! json.isObject())
    {
        if (outError) *outError = "Not valid JSON: " + file.getFullPathName();
        return false;
    }
    if (! preset::applyTo (proc, json, outError))
        return false;

    if (auto* obj = json.getDynamicObject())
        currentName = obj->getProperty ("name").toString();
    if (currentName.isEmpty()) currentName = file.getFileNameWithoutExtension();
    return true;
}

bool PresetManager::save (const juce::File& file, const juce::String& name,
                          juce::String* outError) const
{
    auto json = preset::serialise (proc);
    if (auto* obj = json.getDynamicObject())
        obj->setProperty ("name", name);

    if (! file.getParentDirectory().exists())
        file.getParentDirectory().createDirectory();

    if (! file.replaceWithText (juce::JSON::toString (json, true /*allOnOneLine=false*/)))
    {
        if (outError) *outError = "Could not write " + file.getFullPathName();
        return false;
    }
    return true;
}

} // namespace brocot
