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

    // Packaged macOS build: resources/ is copied into the bundle's
    // Contents/Resources/ (see CMakeLists). currentExecutableFile is the
    // binary inside the bundle (the host app for plugins would be wrong here),
    // so <bundle>/Contents/MacOS/bin -> ../Resources/resources/...
    {
        auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        auto bundled = exe.getParentDirectory().getParentDirectory()
                          .getChildFile ("Resources/resources/presets/factory");
        if (bundled.isDirectory()) return bundled;
    }

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

juce::File PresetManager::getModBankUserDirectory() const
{
    auto dir = juce::File::getSpecialLocation (
                   juce::File::userApplicationDataDirectory)
                   .getChildFile ("Brocot").getChildFile ("ModBanks");
    if (! dir.exists()) dir.createDirectory();
    return dir;
}

void PresetManager::rescan()
{
    entries.clear();
    modBanks.clear();

    auto readName = [] (const juce::File& f)
    {
        juce::String name;
        if (auto json = juce::JSON::parse (f); auto* obj = json.getDynamicObject())
            name = obj->getProperty ("name").toString();
        return name.isEmpty() ? f.getFileNameWithoutExtension() : name;
    };

    auto add = [this, &readName] (const juce::File& f, juce::StringRef category, bool factory)
    {
        Entry e;
        e.file = f;
        e.category = category;
        e.factory = factory;
        e.name = readName (f);
        entries.push_back (std::move (e));
    };

    auto addModBank = [this, &readName] (const juce::File& f, bool factory)
    {
        Entry e;
        e.file = f;
        e.category = factory ? "factory" : "user";
        e.factory = factory;
        e.name = readName (f);
        modBanks.push_back (std::move (e));
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
        // Measured-family labels live beside factory/ (same anchor as the
        // modbanks dir): resources/presets/preset_families.json, keyed by
        // path relative to factory/. JSON::parse yields void on a missing
        // file, so every lookup then falls through to "unclassified".
        const auto families = juce::JSON::parse (
            fac.getParentDirectory().getChildFile ("preset_families.json"));

        auto addFactory = [&] (const juce::File& f)
        {
            add (f, "factory", true);
            entries.back().family =
                familyForRelativePath (families, f.getRelativePathFrom (fac));
        };

        for (const auto& f : fac.findChildFiles (juce::File::findFiles, false, "*.brocot"))
            addFactory (f);

        if (auto curated = fac.getChildFile ("curated"); curated.isDirectory())
            for (const auto& f : curated.findChildFiles (juce::File::findFiles, false, "*.brocot"))
                addFactory (f);
    }

    auto user = getUserDirectory();
    if (user.isDirectory())
        for (const auto& f : user.findChildFiles (juce::File::findFiles, true, "*.brocot"))
            add (f, "user", false);

    // Mod banks (.brocotmod): factory ones live in resources/presets/modbanks/
    // (sibling of factory/); user ones in the ModBanks app-data dir.
    if (auto fac = getFactoryDirectory(); fac.isDirectory())
        if (auto mb = fac.getParentDirectory().getChildFile ("modbanks"); mb.isDirectory())
            for (const auto& f : mb.findChildFiles (juce::File::findFiles, false, "*.brocotmod"))
                addModBank (f, true);

    if (auto mbUser = getModBankUserDirectory(); mbUser.isDirectory())
        for (const auto& f : mbUser.findChildFiles (juce::File::findFiles, true, "*.brocotmod"))
            addModBank (f, false);

    std::sort (entries.begin(), entries.end(), entryLess);

    auto byFactoryThenName = [] (const Entry& a, const Entry& b) {
        if (a.factory != b.factory) return a.factory;   // factory first
        return a.name.compareIgnoreCase (b.name) < 0;
    };
    std::sort (modBanks.begin(), modBanks.end(), byFactoryThenName);
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

bool PresetManager::loadModBank (const juce::File& file, juce::String* outError)
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
    return preset::applyModBankOnly (proc, json, outError);
}

bool PresetManager::saveModBank (const juce::File& file, const juce::String& name,
                                 juce::String* outError) const
{
    auto json = preset::serialiseModBank (proc);
    if (auto* obj = json.getDynamicObject())
        obj->setProperty ("name", name);

    if (! file.getParentDirectory().exists())
        file.getParentDirectory().createDirectory();

    if (! file.replaceWithText (juce::JSON::toString (json, true)))
    {
        if (outError) *outError = "Could not write " + file.getFullPathName();
        return false;
    }
    return true;
}

} // namespace brocot
