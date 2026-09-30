#pragma once

#include "FactoryPresets.h"

namespace nedd
{
class NeddPEAudioProcessor;

/**
    Preset library: factory sounds (compiled in) plus user presets stored as .neddpe XML files in
    Documents/NeddPE/Presets/<Category>/. Favourites are kept in Documents/NeddPE/favorites.xml.
    Message thread only.
*/
class PresetManager
{
public:
    struct Entry
    {
        juce::String name;
        juce::String category;
        juce::String author;
        juce::String description;
        bool factory = false;
        int factoryIndex = -1;
        juce::File file;
        bool favourite = false;

        juce::String key() const { return factory ? "factory:" + name : "user:" + file.getFullPathName(); }
    };

    explicit PresetManager (NeddPEAudioProcessor& processor);

    void rescan();
    const std::vector<Entry>& getEntries() const noexcept { return entries; }

    /** Entries matching a category ("All", "Favourites" or a category name) and a search string. */
    std::vector<int> filter (const juce::String& category, const juce::String& search) const;

    PresetState loadState (const Entry& entry) const;
    bool load (int entryIndex);                    // applies to the processor (one undo step)
    bool loadIntoMorphTarget (int entryIndex);     // sets morph B from a preset
    void loadNext (int direction);                 // steps through the full list

    /** Saves the current sound as a user preset. Returns the entry index, or -1 on failure. */
    int saveUser (const juce::String& name, const juce::String& category, const juce::String& author, const juce::String& description);
    bool deleteUser (int entryIndex);
    void toggleFavourite (int entryIndex);

    int getCurrentIndex() const noexcept { return currentIndex; }
    juce::File getUserDirectory() const;
    juce::String getLastError() const { return lastError; }

    static constexpr const char* kFileExtension = ".neddpe";

private:
    void loadFavourites();
    void saveFavourites() const;
    juce::File favouritesFile() const;

    NeddPEAudioProcessor& processor;
    std::vector<Entry> entries;
    juce::StringArray favourites;
    int currentIndex = -1;
    juce::String lastError;
};

} // namespace nedd
