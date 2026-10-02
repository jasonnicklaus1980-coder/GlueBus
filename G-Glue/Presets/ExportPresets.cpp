// Writes every factory preset as a .gglue file: <out>/<CATEGORY>/<Name>.gglue, plus an index (FactoryPresets.md).
#include "GGluePresets.h"
#include <cstdio>
#include <fstream>

int main (int argc, char** argv)
{
    namespace fs = std::filesystem;
    const fs::path out = argc > 1 ? fs::path (argv[1]) : fs::path ("Resources/FactoryPresets");
    std::error_code ec;
    fs::remove_all (out, ec);
    std::ofstream index;
    int n = 0;
    std::string md = "# G-Glue factory presets\n\n| # | Category | Preset | Threshold | Makeup | Attack | Release | Ratio | SC Filter | Mix | Input | Output | Analog |\n"
                     "|---|---|---|---|---|---|---|---|---|---|---|---|---|\n";
    for (const auto& p : gglue::factoryPresets())
    {
        const fs::path dir = out / p.category;
        fs::create_directories (dir, ec);
        std::ofstream f (dir / (gglue::sanitizePresetName (p.name) + ".gglue"), std::ios::binary);
        f << gglue::presetToText (p);
        if (! f) { std::fprintf (stderr, "can't write %s\n", (dir / p.name).string().c_str()); return 1; }
        md += "| " + std::to_string (++n) + " | " + p.category + " | " + p.name;
        for (int i = 0; i < gglue::kAnalog + 1; ++i) md += " | " + gglue::formatValue (i, p.values[(size_t) i]);
        md += " |\n";
    }
    std::ofstream (out / "FactoryPresets.md") << md;
    std::printf ("wrote %d presets to %s\n", n, out.string().c_str());
    return 0;
}
