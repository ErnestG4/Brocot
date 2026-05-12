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
    // Resolved relative to the executable / bundle. The CMake build doesn't
    // copy the resources/ tree into the bundle yet — this returns the source
    // tree path for now (developer-mode), and falls back to a sibling of the
    // app on packaged builds.
    auto app = juce::File::getSpecialLocation (
                   juce::File::currentApplicationFile);
    auto candidates = std::initializer_list<juce::File> {
        app.getParentDirectory().getChildFile ("../../../resources/presets/factory"),
        app.getParentDirectory().getChildFile ("resources/presets/factory"),
        juce::File::getCurrentWorkingDirectory().getChildFile ("resources/presets/factory")
    };
    for (auto& c : candidates)
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

    if (auto fac = getFactoryDirectory(); fac.isDirectory())
        for (const auto& f : fac.findChildFiles (juce::File::findFiles, false, "*.brocot"))
            add (f, "factory", true);

    auto user = getUserDirectory();
    if (user.isDirectory())
        for (const auto& f : user.findChildFiles (juce::File::findFiles, false, "*.brocot"))
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
