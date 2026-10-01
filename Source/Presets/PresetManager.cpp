#include "PresetManager.h"
#include "PluginProcessor/PluginProcessor.h"

namespace nedd
{
PresetManager::PresetManager (NeddPEAudioProcessor& p) : processor (p)
{
    loadFavourites();
    rescan();
}

juce::File PresetManager::getUserDirectory() const
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("NeddPE").getChildFile ("Presets");
}

juce::File PresetManager::favouritesFile() const
{
    return getUserDirectory().getParentDirectory().getChildFile ("favorites.xml");
}

void PresetManager::loadFavourites()
{
    favourites.clear();
    if (auto xml = juce::XmlDocument::parse (favouritesFile()))
        for (auto* e : xml->getChildIterator())
            favourites.add (e->getStringAttribute ("key"));
}

void PresetManager::saveFavourites() const
{
    juce::XmlElement xml ("NeddPEFavourites");
    for (const auto& f : favourites)
        xml.createNewChildElement ("Favourite")->setAttribute ("key", f);
    favouritesFile().getParentDirectory().createDirectory();
    xml.writeTo (favouritesFile());
}

void PresetManager::rescan()
{
    const auto currentKey = juce::isPositiveAndBelow (currentIndex, (int) entries.size()) ? entries[(size_t) currentIndex].key() : juce::String();
    entries.clear();

    const auto& factory = getFactoryPresets();
    for (int i = 0; i < (int) factory.size(); ++i)
    {
        Entry e;
        e.name = factory[(size_t) i].name;
        e.category = factory[(size_t) i].category;
        e.author = "NeddPE Factory";
        e.description = factory[(size_t) i].description;
        e.factory = true;
        e.factoryIndex = i;
        entries.push_back (e);
    }

    const auto dir = getUserDirectory();
    if (dir.isDirectory())
    {
        for (const auto& file : dir.findChildFiles (juce::File::findFiles, true, juce::String ("*") + kFileExtension))
        {
            auto xml = juce::XmlDocument::parse (file);
            if (xml == nullptr || ! xml->hasTagName ("NeddPE"))
                continue;
            Entry e;
            e.name = xml->getStringAttribute ("preset", file.getFileNameWithoutExtension());
            e.category = xml->getStringAttribute ("category", "User");
            e.author = xml->getStringAttribute ("author");

            // Exported copies of factory presets (e.g. the repository's presets/ folder when it sits in
            // the user preset folder) would list every factory sound twice.
            const bool factoryCopy = e.author == "NeddPE Factory" && std::any_of (factory.begin(), factory.end(),
                                         [&e] (const FactoryPreset& f) { return e.name == f.name; });
            if (factoryCopy)
                continue;
            e.description = xml->getStringAttribute ("description");
            e.file = file;
            entries.push_back (e);
        }
    }

    const auto categories = getPresetCategories();
    std::stable_sort (entries.begin(), entries.end(), [&categories] (const Entry& a, const Entry& b)
    {
        const int ca = categories.indexOf (a.category), cb = categories.indexOf (b.category);
        if (ca != cb) return (ca < 0 ? 999 : ca) < (cb < 0 ? 999 : cb);
        if (a.factory != b.factory) return a.factory;
        return a.name.compareIgnoreCase (b.name) < 0;
    });

    currentIndex = -1;
    for (size_t i = 0; i < entries.size(); ++i)
    {
        entries[i].favourite = favourites.contains (entries[i].key());
        if (entries[i].key() == currentKey)
            currentIndex = (int) i;
    }
}

std::vector<int> PresetManager::filter (const juce::String& category, const juce::String& search) const
{
    std::vector<int> result;
    for (int i = 0; i < (int) entries.size(); ++i)
    {
        const auto& e = entries[(size_t) i];
        if (category == "Favourites" && ! e.favourite) continue;
        if (category != "All" && category != "Favourites" && e.category != category) continue;
        if (search.isNotEmpty() && ! e.name.containsIgnoreCase (search) && ! e.description.containsIgnoreCase (search)
            && ! e.category.containsIgnoreCase (search))
            continue;
        result.push_back (i);
    }
    return result;
}

PresetState PresetManager::loadState (const Entry& e) const
{
    if (e.factory)
        return makeFactoryPreset (e.factoryIndex);

    if (auto xml = juce::XmlDocument::parse (e.file))
        return PresetState::fromValueTree (juce::ValueTree::fromXml (*xml));
    return {};
}

bool PresetManager::load (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) entries.size()))
        return false;

    const auto& e = entries[(size_t) index];
    if (! e.factory && ! e.file.existsAsFile())
    {
        lastError = "Preset file is missing: " + e.file.getFullPathName();
        return false;
    }

    processor.applyState (loadState (e), "Load preset: " + e.name);
    processor.setCurrentPresetName (e.name, e.category);
    currentIndex = index;
    return true;
}

bool PresetManager::loadIntoMorphTarget (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) entries.size()))
        return false;
    processor.setMorphTarget (loadState (entries[(size_t) index]).params);
    return true;
}

void PresetManager::loadNext (int direction)
{
    if (entries.empty())
        return;
    const int n = (int) entries.size();
    const int next = currentIndex < 0 ? (direction > 0 ? 0 : n - 1) : ((currentIndex + direction) % n + n) % n;
    load (next);
}

int PresetManager::saveUser (const juce::String& rawName, const juce::String& category, const juce::String& author, const juce::String& description)
{
    const auto name = rawName.trim().substring (0, 64);
    if (name.isEmpty())
    {
        lastError = "Please enter a preset name.";
        return -1;
    }

    auto state = processor.captureState();
    state.name = name;
    state.category = category.isNotEmpty() ? category : juce::String ("User");
    state.author = author;
    state.description = description;

    const auto dir = getUserDirectory().getChildFile (juce::File::createLegalFileName (state.category));
    if (! dir.createDirectory())
    {
        lastError = "Could not create " + dir.getFullPathName();
        return -1;
    }

    const auto file = dir.getChildFile (juce::File::createLegalFileName (name) + kFileExtension);
    auto xml = state.toValueTree().createXml();
    if (xml == nullptr || ! xml->writeTo (file))
    {
        lastError = "Could not write " + file.getFullPathName();
        return -1;
    }

    processor.setCurrentPresetName (name, state.category);
    rescan();
    for (int i = 0; i < (int) entries.size(); ++i)
        if (! entries[(size_t) i].factory && entries[(size_t) i].file == file)
            currentIndex = i;
    return currentIndex;
}

bool PresetManager::deleteUser (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) entries.size()) || entries[(size_t) index].factory)
        return false;
    const auto file = entries[(size_t) index].file;
    favourites.removeString (entries[(size_t) index].key());
    saveFavourites();
    // Moved to the OS trash where possible, so a deleted preset can still be recovered.
    const bool ok = file.moveToTrash() || file.deleteFile();
    rescan();
    return ok;
}

void PresetManager::toggleFavourite (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) entries.size()))
        return;
    auto& e = entries[(size_t) index];
    e.favourite = ! e.favourite;
    if (e.favourite) favourites.addIfNotAlreadyThere (e.key());
    else favourites.removeString (e.key());
    saveFavourites();
}

} // namespace nedd
