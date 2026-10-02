#include "GGluePresets.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace gglue
{
namespace
{
std::string lower (std::string s) { for (auto& c : s) c = (char) std::tolower ((unsigned char) c); return s; }
std::string trim (const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace ((unsigned char) s[a])) ++a;
    while (b > a && std::isspace ((unsigned char) s[b - 1])) --b;
    return s.substr (a, b - a);
}
std::string fmt (float v)
{
    std::ostringstream o; o.imbue (std::locale::classic()); o << v; return o.str();
}
bool parseFloat (const std::string& s, float& out)
{
    std::istringstream i (s); i.imbue (std::locale::classic());
    double v; i >> v;
    if (i.fail()) return false;
    out = (float) v; return true;
}
bool readFile (const fs::path& p, std::string& out)
{
    std::ifstream f (p, std::ios::binary);
    if (! f) return false;
    std::ostringstream o; o << f.rdbuf(); out = o.str();
    return out.size() < 1 << 20;
}
bool writeFileAtomic (const fs::path& p, const std::string& text, std::string* error)
{
    std::error_code ec;
    fs::create_directories (p.parent_path(), ec);
    const fs::path tmp = p.string() + ".tmp";
    {
        std::ofstream f (tmp, std::ios::binary | std::ios::trunc);
        if (! f) { if (error) *error = "can't write " + p.string(); return false; }
        f << text;
        if (! f) { if (error) *error = "write failed: " + p.string(); return false; }
    }
    fs::rename (tmp, p, ec);
    if (ec) { fs::remove (tmp, ec); if (error) *error = "can't write " + p.string(); return false; }
    return true;
}
// "key=value" lines -> pairs, ignoring blank lines and # comments
std::vector<std::pair<std::string, std::string>> keyValues (const std::string& text)
{
    std::vector<std::pair<std::string, std::string>> kv;
    std::istringstream in (text);
    std::string line;
    while (std::getline (in, line))
    {
        line = trim (line);
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find ('=');
        if (eq == std::string::npos) continue;
        kv.emplace_back (trim (line.substr (0, eq)), trim (line.substr (eq + 1)));
    }
    return kv;
}
void writeValues (std::ostringstream& o, const ParamValues& v, const std::string& prefix)
{
    for (int i = 0; i < kNumParams; ++i) o << prefix << spec (i).id << '=' << fmt (v[(size_t) i]) << '\n';
}
bool applyValue (ParamValues& v, const std::string& id, const std::string& text)
{
    const int i = findParam (id);
    if (i < 0) return false;
    float f;
    if (! parseFloat (text, f)) return false;
    v[(size_t) i] = clampPlain (i, f);
    return true;
}
} // namespace

std::string sanitizePresetName (const std::string& name)
{
    std::string s;
    for (char c : trim (name))
    {
        if ((unsigned char) c < 32 || std::string ("/\\:*?\"<>|").find (c) != std::string::npos) continue;
        s += c;
    }
    while (! s.empty() && (s.back() == '.' || s.back() == ' ')) s.pop_back();
    if (s.size() > 64) s.resize (64);
    return s;
}

std::string presetToText (const Preset& p)
{
    std::ostringstream o;
    o << "G-Glue Preset 1\n" << "name=" << p.name << '\n' << "category=" << p.category << '\n';
    ParamValues v = p.values; v[kBypass] = 0.f;            // presets never store bypass
    writeValues (o, v, "");
    return o.str();
}

bool presetFromText (const std::string& text, Preset& p, std::string* error)
{
    if (text.rfind ("G-Glue Preset", 0) != 0) { if (error) *error = "not a G-Glue preset"; return false; }
    Preset r;
    int found = 0;
    for (const auto& [k, v] : keyValues (text))
    {
        if (k == "name") r.name = sanitizePresetName (v);
        else if (k == "category") r.category = v.empty() ? "USER" : v;
        else if (applyValue (r.values, k, v)) ++found;
    }
    if (r.name.empty() || found == 0) { if (error) *error = "preset has no name or no values"; return false; }
    r.values[kBypass] = 0.f;
    p = r;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
PresetLibrary::PresetLibrary (fs::path userFolder) : dir (std::move (userFolder)) { refresh(); }

void PresetLibrary::refresh()
{
    list = factoryPresets();
    std::vector<Preset> user;
    std::error_code ec;
    if (fs::is_directory (dir, ec))
        for (const auto& e : fs::directory_iterator (dir, ec))
        {
            if (! e.is_regular_file (ec) || e.path().extension() != ".gglue") continue;
            std::string text; Preset p;
            if (readFile (e.path(), text) && presetFromText (text, p)) { p.factory = false; p.file = e.path(); user.push_back (p); }
        }
    std::sort (user.begin(), user.end(), [] (const Preset& a, const Preset& b) { return lower (a.name) < lower (b.name); });
    list.insert (list.end(), user.begin(), user.end());
    loadFavorites();
}

int PresetLibrary::indexOf (const std::string& name, bool factory) const
{
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].factory == factory && list[i].name == name) return (int) i;
    return -1;
}
int PresetLibrary::indexOf (const std::string& name) const
{
    const int u = indexOf (name, false);
    return u >= 0 ? u : indexOf (name, true);
}

std::vector<int> PresetLibrary::search (const std::string& query, const std::string& category, bool favoritesOnly) const
{
    std::vector<int> r;
    const std::string q = lower (trim (query));
    for (size_t i = 0; i < list.size(); ++i)
    {
        const Preset& p = list[i];
        if (! category.empty() && ! (category == "USER" ? ! p.factory : p.category == category)) continue;
        if (favoritesOnly && ! isFavorite ((int) i)) continue;
        if (! q.empty() && lower (p.name).find (q) == std::string::npos && lower (p.category).find (q) == std::string::npos) continue;
        r.push_back ((int) i);
    }
    return r;
}

int PresetLibrary::nextIndex (int current, int direction, const std::vector<int>& visible) const
{
    if (visible.empty()) return -1;
    const auto it = std::find (visible.begin(), visible.end(), current);
    if (it == visible.end()) return direction >= 0 ? visible.front() : visible.back();
    const int pos = (int) (it - visible.begin()), n = (int) visible.size();
    return visible[(size_t) (((pos + (direction >= 0 ? 1 : -1)) % n + n) % n)];
}

bool PresetLibrary::save (const std::string& name, const std::string& category, const ParamValues& v, bool overwrite, std::string* error)
{
    const std::string clean = sanitizePresetName (name);
    if (clean.empty()) { if (error) *error = "enter a preset name"; return false; }
    const fs::path file = dir / (clean + ".gglue");
    std::error_code ec;
    if (! overwrite && fs::exists (file, ec)) { if (error) *error = "a user preset called \"" + clean + "\" already exists"; return false; }
    Preset p; p.name = clean; p.category = category.empty() ? "USER" : category; p.values = v;
    if (! writeFileAtomic (file, presetToText (p), error)) return false;
    refresh();
    return true;
}

bool PresetLibrary::rename (int index, const std::string& newName, std::string* error)
{
    if (index < 0 || index >= (int) list.size() || list[(size_t) index].factory) { if (error) *error = "factory presets can't be renamed"; return false; }
    const std::string clean = sanitizePresetName (newName);
    if (clean.empty()) { if (error) *error = "enter a preset name"; return false; }
    Preset p = list[(size_t) index];
    if (clean == p.name) return true;
    const fs::path to = dir / (clean + ".gglue");
    std::error_code ec;
    if (fs::exists (to, ec)) { if (error) *error = "a user preset called \"" + clean + "\" already exists"; return false; }
    const bool fav = isFavorite (index);
    const std::string oldName = p.name;
    p.name = clean;
    if (! writeFileAtomic (to, presetToText (p), error)) return false;
    fs::remove (p.file, ec);
    favorites.erase (std::remove (favorites.begin(), favorites.end(), "U:" + oldName), favorites.end());
    if (fav) favorites.push_back ("U:" + clean);
    saveFavorites();
    refresh();
    return true;
}

bool PresetLibrary::remove (int index, std::string* error)
{
    if (index < 0 || index >= (int) list.size() || list[(size_t) index].factory) { if (error) *error = "factory presets can't be deleted"; return false; }
    std::error_code ec;
    if (! fs::remove (list[(size_t) index].file, ec)) { if (error) *error = "can't delete " + list[(size_t) index].file.string(); return false; }
    favorites.erase (std::remove (favorites.begin(), favorites.end(), favoriteKey (index)), favorites.end());
    saveFavorites();
    refresh();
    return true;
}

bool PresetLibrary::importFile (const fs::path& src, std::string* importedName, std::string* error)
{
    std::string text; Preset p;
    if (! readFile (src, text)) { if (error) *error = "can't read " + src.string(); return false; }
    if (! presetFromText (text, p, error)) return false;
    // never overwrite: "Name", "Name 2", "Name 3" ...
    std::string name = p.name;
    std::error_code ec;
    for (int n = 2; fs::exists (dir / (name + ".gglue"), ec); ++n) name = sanitizePresetName (p.name + " " + std::to_string (n));
    if (! save (name, p.category, p.values, false, error)) return false;
    if (importedName) *importedName = name;
    return true;
}

bool PresetLibrary::exportValues (const std::string& name, const std::string& category, const ParamValues& v, const fs::path& dst, std::string* error)
{
    Preset p; p.name = sanitizePresetName (name); p.category = category; p.values = v;
    if (p.name.empty()) p.name = "G-Glue Preset";
    return writeFileAtomic (dst, presetToText (p), error);
}

bool PresetLibrary::exportFile (int index, const fs::path& dst, std::string* error) const
{
    if (index < 0 || index >= (int) list.size()) { if (error) *error = "no preset selected"; return false; }
    const Preset& p = list[(size_t) index];
    return exportValues (p.name, p.category, p.values, dst, error);
}

std::string PresetLibrary::favoriteKey (int index) const
{
    const Preset& p = list[(size_t) index];
    return (p.factory ? "F:" : "U:") + p.name;
}
bool PresetLibrary::isFavorite (int index) const
{
    if (index < 0 || index >= (int) list.size()) return false;
    return std::find (favorites.begin(), favorites.end(), favoriteKey (index)) != favorites.end();
}
void PresetLibrary::setFavorite (int index, bool favorite)
{
    if (index < 0 || index >= (int) list.size() || isFavorite (index) == favorite) return;
    if (favorite) favorites.push_back (favoriteKey (index));
    else favorites.erase (std::remove (favorites.begin(), favorites.end(), favoriteKey (index)), favorites.end());
    saveFavorites();
}
void PresetLibrary::loadFavorites()
{
    favorites.clear();
    std::string text;
    if (! readFile (dir / "favorites.txt", text)) return;
    std::istringstream in (text); std::string line;
    while (std::getline (in, line)) { line = trim (line); if (line.size() > 2) favorites.push_back (line); }
}
void PresetLibrary::saveFavorites() const
{
    std::string text;
    for (const auto& f : favorites) text += f + '\n';
    writeFileAtomic (dir / "favorites.txt", text, nullptr);
}

// ---------------------------------------------------------------------------------------------------------------
std::string stateToText (const PluginState& s)
{
    std::ostringstream o;
    o << "G-Glue State 1\n" << "preset=" << s.presetName << '\n' << "modified=" << (s.modified ? 1 : 0) << '\n'
      << "slot=" << s.activeSlot << '\n';
    writeValues (o, s.slotA(), "A.");
    writeValues (o, s.slotB(), "B.");
    return o.str();
}

bool stateFromText (const std::string& text, PluginState& s)
{
    if (text.rfind ("G-Glue State", 0) != 0) return false;
    PluginState r;
    ParamValues a = defaultValues(), b = defaultValues();
    char slot = 'A';
    int found = 0;
    for (const auto& [k, v] : keyValues (text))
    {
        if (k == "preset") r.presetName = v;
        else if (k == "modified") r.modified = v == "1";
        else if (k == "slot") slot = v == "B" ? 'B' : 'A';
        else if (k.rfind ("A.", 0) == 0) found += applyValue (a, k.substr (2), v);
        else if (k.rfind ("B.", 0) == 0) found += applyValue (b, k.substr (2), v);
    }
    if (found == 0) return false;
    r.activeSlot = slot;
    r.current = slot == 'A' ? a : b;
    r.other = slot == 'A' ? b : a;
    s = r;
    return true;
}
} // namespace gglue
