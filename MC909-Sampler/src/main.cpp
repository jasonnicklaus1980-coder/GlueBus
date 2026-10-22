// Import check: reports what each file is and whether it can be read in full. Folders are scanned recursively.
#include "import_check.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;

static std::vector<uint8_t> readAll(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static void report(const fs::path& p)
{
    auto bytes = readAll(p);
    auto r = mc909::analyze(bytes);
    std::printf("%s\n  %s\n  status: %s\n", p.string().c_str(), r.summary.c_str(),
                r.supported ? "supported (read in full)" : (r.kind == mc909::Kind::Unknown ? "unsupported" : "partially decoded"));
    for (auto& w : r.warnings) std::printf("  warning: %s\n", w.c_str());
}

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("usage: mc909_import_check <file-or-folder>...\n"); return 2; }
    for (int i = 1; i < argc; ++i)
    {
        fs::path p(argv[i]);
        if (fs::is_directory(p)) { for (auto& e : fs::recursive_directory_iterator(p)) if (e.is_regular_file()) report(e.path()); }
        else report(p);
    }
    return 0;
}
