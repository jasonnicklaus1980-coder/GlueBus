// Da Maze Muncher tests: the maze, the game rules (played by a bot for many games), and the plugin as MPC loads it
// (parameters, pads as MIDI notes, screen updates, audio).
#include "vst2.h"
#include "Game.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <vector>

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++fails; std::printf ("  FAIL: "); } else std::printf ("  ok:   "); \
                              std::printf (__VA_ARGS__); std::printf ("\n"); } while (0)
using namespace mm;

// ------------------------------------------------------------------ a bot: walk towards the nearest dot (BFS), away from bugs
static int botDir (const Game& g)
{
    static int dist[H][W], first[H][W];
    for (int r = 0; r < H; ++r) for (int c = 0; c < W; ++c) dist[r][c] = -1;
    int qr[H * W], qc[H * W], qh = 0, qt = 0;
    dist[g.pl.r][g.pl.c] = 0; qr[qt] = g.pl.r; qc[qt++] = g.pl.c;
    int goalR = -1, goalC = -1;
    while (qh < qt)
    {
        const int r = qr[qh], c = qc[qh++];
        if ((g.tile[r][c] == T_DOT || g.tile[r][c] == T_POWER) && dist[r][c] > 0) { goalR = r; goalC = c; break; }
        for (int d = 0; d < 4; ++d)
        {
            const int rr = r + DR[d], cc = (c + DC[d] + W) % W;
            if (! g.open (rr, cc, false, false) || dist[rr][cc] >= 0) continue;
            bool danger = false;                                           // don't path through a hunting bug
            for (const Enemy& e : g.en) if (e.mode == E_ACTIVE && ! e.scared && std::abs (e.r - rr) + std::abs (e.c - cc) <= 1) danger = true;
            if (danger && dist[r][c] < 3) continue;
            dist[rr][cc] = dist[r][c] + 1; first[rr][cc] = dist[r][c] == 0 ? d : first[r][c];
            qr[qt] = rr; qc[qt++] = cc;
        }
    }
    return goalR >= 0 ? first[goalR][goalC] : g.pl.dir;
}

static void testMaze()
{
    std::printf ("[maze]\n");
    bool widths = true;
    for (int r = 0; r < H; ++r) if ((int) std::strlen (kMaze[r]) != W) widths = false;
    CHECK (widths && (int) (sizeof kMaze / sizeof kMaze[0]) == H, "maze is %d x %d", W, H);
    Game g; g.init();
    int dead = 0, reach = 0, walk = 0;
    static bool seen[H][W] {}; int sr[H * W], sc[H * W], sp = 0;
    seen[g.startR][g.startC] = true; sr[sp] = g.startR; sc[sp++] = g.startC;
    while (sp > 0)
    {
        const int r = sr[--sp], c = sc[sp]; ++reach;
        for (int d = 0; d < 4; ++d) { const int rr = r + DR[d], cc = (c + DC[d] + W) % W; if (g.open (rr, cc, false, false) && ! seen[rr][cc]) { seen[rr][cc] = true; sr[sp] = rr; sc[sp++] = cc; } }
    }
    for (int r = 0; r < H; ++r)
        for (int c = 0; c < W; ++c)
        {
            if (! g.open (r, c, false, false)) continue;
            ++walk; int n = 0;
            for (int d = 0; d < 4; ++d) if (g.open (r + DR[d], c + DC[d], false, false)) ++n;
            if (n < 2) ++dead;
        }
    CHECK (dead == 0, "no dead ends");
    CHECK (reach == walk, "every path cell reachable (%d of %d)", reach, walk);
    CHECK (g.nCells == 175 && g.dotsLeft > 100, "%d screen cells, %d dots", g.nCells, g.dotsLeft);
}

static void testGame()
{
    std::printf ("[game rules, bot-played]\n");
    int clears = 0, deaths = 0, overs = 0, scaredEats = 0; bool wallOk = true, scoreOk = true, bonusSeen = false, eyesHome = false;
    long totalScore = 0;
    for (int seed = 1; seed <= 40; ++seed)
    {
        Game g; g.init(); g.rng.s = 0x9e3779b9u * seed + 1; g.newGame();
        int lastScore = 0, lastLives = g.lives, lastLevel = 1;
        for (int step = 0; step < 240 * 60 * 6 && g.state != S_OVER; ++step)   // up to 6 minutes per game
        {
            g.wantDir = botDir (g);
            g.update (1.0 / 240.0);
            const uint32_t s = g.takeSfx();
            if (s & (1u << SFX_EAT)) ++scaredEats;
            if (g.bonusR >= 0) bonusSeen = true;
            if (! g.open (g.pl.r, g.pl.c, false, false)) wallOk = false;
            for (const Enemy& e : g.en) { if (g.tile[e.r][(e.c + W) % W] == T_WALL) wallOk = false; if (e.mode == E_EYES) eyesHome = true; }
            if (g.score < lastScore) scoreOk = false;
            lastScore = g.score;
            if (g.lives < lastLives) ++deaths;
            lastLives = g.lives;
            if (g.level > lastLevel) { ++clears; lastLevel = g.level; }
        }
        if (g.state == S_OVER) ++overs;
        totalScore += g.score;
    }
    CHECK (wallOk, "no one ever walks into a wall; the Muncher never enters the bug house");
    CHECK (scoreOk, "score never goes down");
    CHECK (clears > 0, "the bot cleared %d mazes in 40 games (levels are winnable)", clears);
    CHECK (deaths > 0 && overs > 0, "bugs catch the Muncher (%d lives lost, %d games over)", deaths, overs);
    CHECK (scaredEats > 0 && eyesHome, "power records let the Muncher eat bugs (%d eaten); eaten bugs go home", scaredEats);
    CHECK (bonusSeen, "the bonus note appears");
    std::printf ("  (average bot score %ld)\n", totalScore / 40);

    Game g; g.init(); g.newGame(); g.update (3.0);
    CHECK (g.state == S_PLAY, "READY -> PLAY after ~2 s");
    g.pressStart(); const int r0 = g.pl.r, c0 = g.pl.c; g.update (2.0);
    CHECK (g.state == S_PAUSED && g.pl.r == r0 && g.pl.c == c0, "START pauses; nothing moves while paused");
    g.pressStart(); g.wantDir = LEFT; g.update (1.0);
    CHECK (g.state == S_PLAY && g.pl.c < c0, "START resumes; the Muncher moves left (%d -> %d)", c0, g.pl.c);
}

// ------------------------------------------------------------------ plugin as MPC loads it
typedef AEffect* (*MainFn) (audioMasterCallback);
typedef const char* (*KeyFn) (int);
static KeyFn keyOf; static AEffect* fx; static int automates = 0;
static intptr_t host (AEffect*, int32_t op, int32_t, intptr_t, void*, float) { if (op == audioMasterAutomate) ++automates; return op == 1 ? 2400 : 0; }
static intptr_t D (int op, int idx = 0, intptr_t val = 0, void* p = nullptr, float opt = 0) { return fx->dispatcher (fx, op, idx, val, p, opt); }
static int idx (const char* k) { for (int i = 0; i < fx->numParams; ++i) if (std::strcmp (keyOf (i), k) == 0) return i; std::printf ("no param %s\n", k); std::exit (2); }
static std::string text (const char* k) { char b[256] = {}; D (effGetParamDisplay, idx (k), 0, b); return b; }
static double run (double seconds, float* peak = nullptr)
{
    const int n = 256; float L[n], R[n]; float* out[2] { L, R };
    double e = 0; int blocks = (int) (seconds * 44100 / n);
    for (int b = 0; b < blocks; ++b)
    {
        fx->processReplacing (fx, nullptr, out, n);
        for (int i = 0; i < n; ++i)
        {
            if (! std::isfinite (L[i])) return -1;
            e += L[i] * L[i];
            if (peak) *peak = std::fmax (*peak, std::fabs (L[i]));
        }
    }
    return e;
}
static void pad (int note)
{
    VstMidiEvent m {}; m.type = kVstMidiType; m.byteSize = sizeof m; m.midiData[0] = (char) 0x90; m.midiData[1] = (char) note; m.midiData[2] = 100;
    VstEvents ev {}; ev.numEvents = 1; ev.events[0] = (VstEvent*) &m;
    D (effProcessEvents, 0, 0, &ev);
}
static int cellFrames (int frame)
{
    int n = 0;
    for (int i = 0; i < fx->numParams; ++i) if (std::strncmp (keyOf (i), "cell", 4) == 0 && std::lround (fx->getParameter (fx, i) * 127) == frame) ++n;
    return n;
}

static void testPlugin (const char* path)
{
    std::printf ("[plugin]\n");
    void* h = dlopen (path, RTLD_NOW);
    if (h == nullptr) { std::printf ("dlopen: %s\n", dlerror()); std::exit (1); }
    MainFn main_ = (MainFn) dlsym (h, "VSTPluginMain"); keyOf = (KeyFn) dlsym (h, "MM_ParamKey");
    fx = main_ (host);
    D (effOpen); D (effSetSampleRate, 0, 0, nullptr, 44100.f); D (effSetBlockSize, 0, 256); D (effMainsChanged, 0, 1);
    CHECK (fx->magic == kEffectMagic && (fx->flags & effFlagsIsSynth) && fx->numInputs == 0 && fx->numOutputs == 2, "VST2 instrument, stereo out");
    CHECK (fx->numParams == 11 + 175, "%d parameters (11 controls + 175 screen cells)", fx->numParams);
    char nm[64] = {}; D (effGetProductString, 0, 0, nm); CHECK (std::string (nm) == "Da Maze Muncher", "product %s", nm);
    CHECK (D (effCanDo, 0, 0, (void*) "receiveVstMidiEvent") == 1, "takes MIDI (the pads)");

    run (0.5);
    CHECK (text ("msg") == "PRESS START" && cellFrames (F_DOT) > 100 && cellFrames (F_PLAYER + LEFT * 2) == 1, "title screen: maze full of dots (%d), Muncher at the start, \"%s\"", cellFrames (F_DOT), text ("msg").c_str());
    float pk = 0; double silent = run (0.5, &pk);
    CHECK (silent == 0.0, "silent until the game starts");

    automates = 0;
    fx->setParameter (fx, idx ("start"), 1.f);                            // the START key on screen
    run (0.3, &pk);
    CHECK (text ("msg") == "READY!", "START: \"%s\" and the start tune (peak %.2f)", text ("msg").c_str(), pk);
    run (2.2);
    pad (36 + 6);                                                          // pad 7 = right
    run (0.05);
    CHECK (text ("dir") == "Right", "pad 7 steers right (screen D-pad follows: %s)", text ("dir").c_str());
    pk = 0; double e = run (3.0, &pk);
    CHECK (e > 0 && pk < 1.0f, "game sounds and beat playing (peak %.2f)", pk);
    CHECK (std::atoi (text ("score").c_str()) > 0, "dots eaten: score %s", text ("score").c_str());
    CHECK (automates > 30, "screen updates sent to MPC (%d)", automates);
    pad (36 + 15);                                                         // pad 16 = start/pause
    run (0.1);
    const std::string s1 = text ("score"); run (1.0);
    CHECK (text ("msg") == "PAUSED" && text ("score") == s1, "pad 16 pauses");
    pad (36 + 12); run (0.1);                                              // pad 13 resumes
    CHECK (text ("msg") != "PAUSED", "pad 13 resumes (%s)", text ("msg").c_str());
    // play on with the pads in a circle until game over: the plugin must stay stable
    const int seq[] = { 45, 40, 41, 42 };
    bool ok = true;
    for (int k = 0; k < 600 && text ("msg").find ("GAME OVER") == std::string::npos; ++k) { pad (seq[k % 4]); if (run (0.25) < 0) ok = false; }
    CHECK (ok && text ("msg").find ("GAME OVER") != std::string::npos, "plays to GAME OVER without trouble (score %s, high %s)", text ("score").c_str(), text ("high").c_str());
    CHECK (std::atoi (text ("hiscore").c_str()) == std::atoi (text ("high").c_str()), "high score kept as a saved parameter (%s)", text ("hiscore").c_str());
    D (effClose);
}

int main (int argc, char** argv)
{
    testMaze();
    testGame();
    testPlugin (argc > 1 ? argv[1] : "build/native/mazemuncher.so");
    std::printf ("\n%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
