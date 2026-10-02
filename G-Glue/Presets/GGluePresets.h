#pragma once
// G-Glue presets and plugin state, shared by every target. Plain C++17 (std::filesystem), no plugin framework.
//
// Preset file (.gglue, UTF-8 text):          State (host chunk / project):
//   G-Glue Preset 1                            G-Glue State 1
//   name=Clean Glue                            preset=Clean Glue
//   category=MIX BUS                           modified=0
//   threshold=-14                              slot=A
//   makeup=2 ...                               A.threshold=-14 ... B.threshold=-10 ...
// Values are plain values (dB, choice index, 0/1), written by parameter ID, so files stay valid when parameters are
// added later; unknown keys are ignored and missing keys keep their defaults.
#include "../Parameters/GGlueParameters.h"
#include <filesystem>
#include <string>
#include <vector>

namespace gglue
{
struct Preset
{
    std::string name, category;
    ParamValues values = defaultValues();
    bool factory = false;
    std::filesystem::path file;            // user presets only
};

const std::vector<Preset>& factoryPresets();
const std::vector<std::string>& presetCategories();

std::string presetToText (const Preset& p);
bool presetFromText (const std::string& text, Preset& p, std::string* error = nullptr);
std::string sanitizePresetName (const std::string& name);    // safe as a file name, 1..64 chars

// ---- user preset library (one folder of .gglue files + favorites.txt)
class PresetLibrary
{
public:
    explicit PresetLibrary (std::filesystem::path userFolder);
    const std::filesystem::path& folder() const { return dir; }

    void refresh();                                            // rescan the folder
    const std::vector<Preset>& presets() const { return list; } // factory first, then user (sorted by name)
    int indexOf (const std::string& name, bool factory) const;
    int indexOf (const std::string& name) const;               // user preset wins over a factory one of the same name

    // search: case-insensitive match on name or category; category "" = all; favoritesOnly filters
    std::vector<int> search (const std::string& query, const std::string& category = "", bool favoritesOnly = false) const;
    int nextIndex (int current, int direction, const std::vector<int>& visible) const;   // wraps around

    // user presets: return false and set error on failure
    bool save (const std::string& name, const std::string& category, const ParamValues& v, bool overwrite, std::string* error = nullptr);
    bool rename (int index, const std::string& newName, std::string* error = nullptr);
    bool remove (int index, std::string* error = nullptr);
    bool importFile (const std::filesystem::path& src, std::string* importedName = nullptr, std::string* error = nullptr);
    bool exportFile (int index, const std::filesystem::path& dst, std::string* error = nullptr) const;
    static bool exportValues (const std::string& name, const std::string& category, const ParamValues& v,
                              const std::filesystem::path& dst, std::string* error = nullptr);

    bool isFavorite (int index) const;
    void setFavorite (int index, bool favorite);

private:
    std::string favoriteKey (int index) const;
    void loadFavorites();
    void saveFavorites() const;
    std::filesystem::path dir;
    std::vector<Preset> list;
    std::vector<std::string> favorites;
};

// ---- complete plugin state, including A/B comparison
struct PluginState
{
    ParamValues current = defaultValues();     // the active slot (what the engine plays)
    ParamValues other = defaultValues();       // the inactive slot
    char activeSlot = 'A';
    std::string presetName = "Default";
    bool modified = false;

    void switchSlot() { std::swap (current, other); activeSlot = activeSlot == 'A' ? 'B' : 'A'; }
    void copyActiveToOther() { other = current; }
    const ParamValues& slotA() const { return activeSlot == 'A' ? current : other; }
    const ParamValues& slotB() const { return activeSlot == 'B' ? current : other; }
};

std::string stateToText (const PluginState& s);
bool stateFromText (const std::string& text, PluginState& s);
} // namespace gglue
