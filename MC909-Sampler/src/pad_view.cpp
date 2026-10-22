// Prints the 4 x 4 pad grid of a sampler project: each pad's file, tuning and playback mode.
#include "padmap.h"
#include "project.h"

#include <cstdio>
#include <string>

using namespace mc909;

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("usage: mc909_pad_view <project.mc909>\n"); return 2; }
    Project p; std::string err;
    if (!loadProject(argv[1], p, err)) { std::printf("error: %s\n", err.c_str()); return 1; }
    std::printf("project: %s\n\n", p.name.c_str());
    // the MPC-style layout: bottom row first, as on the hardware pad grid
    for (int row = 3; row >= 0; --row)
    {
        for (int col = 0; col < 4; ++col)
        {
            const int pad = row * 4 + col;
            std::string label = "-";
            for (const auto& e : p.pads)
                if (e.pad == pad)
                {
                    std::string name = e.file.substr(e.file.find_last_of('/') + 1);
                    if (name.size() > 14) name = name.substr(0, 13) + "~";
                    char t[64]; std::snprintf(t, sizeof t, "%s %+.0f%s", name.c_str(), e.semitones, e.loop ? " loop" : (e.reverse ? " rev" : ""));
                    label = t;
                }
            std::printf("[%2d %-22s] ", pad + 1, label.c_str());
        }
        std::printf("\n");
    }
    return 0;
}
