#pragma once
// Da Maze Muncher: the game. An original maze-chase game: the Muncher eats every dot in the maze while four
// "glitch bugs" (Kick, Snare, Hat and Clap) hunt it down. Eating a power record scares the bugs for a few seconds,
// and scared bugs can be eaten. Clear the maze to reach the next level, which is faster.
//
// Everything is discrete on the maze grid: a mover steps one cell at a time at its own speed (cells per second),
// so the screen (one image per cell) shows exactly the game state. Deterministic for a given seed: the tests
// replay whole games. No allocation; runs on the audio thread.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include "maze.h"

namespace mm
{
constexpr int W = 21, H = 16, kMaxCells = 200, kEnemies = 4;
enum { UP, DOWN, LEFT, RIGHT };
constexpr int DR[4] = { -1, 1, 0, 0 }, DC[4] = { 0, 0, -1, 1 };
inline int opp (int d) { return d ^ 1; }
// fmod without libm's fmod (its newest ARM symbol needs glibc 2.38; MPC OS has 2.34)
inline double fmodd (double a, double b) { return a - b * std::floor (a / b); }

enum Tile : uint8_t { T_PATH, T_DOT, T_POWER, T_WALL, T_HOUSE, T_DOOR };
// frames of the cell filmstrip (tools/make_skin.py draws them in this order)
enum Frame { F_EMPTY = 0, F_DOT, F_POWER, F_POWER_DIM, F_PLAYER = 4 /* + dir * 2 + mouth */, F_ENEMY = 12 /* + i */,
             F_SCARED = 16, F_SCARED_FLASH, F_EYES, F_DEATH = 19 /* + 0..4 */, F_BONUS = 24, F_DOOR = 25, F_COUNT };
enum State { S_TITLE, S_READY, S_PLAY, S_PAUSED, S_DYING, S_CLEAR, S_OVER };
enum Sfx { SFX_DOT, SFX_POWER, SFX_EAT, SFX_BONUS, SFX_DEATH, SFX_START, SFX_CLEAR, SFX_EXTRA };
enum EnemyMode { E_HOUSE, E_LEAVING, E_ACTIVE, E_EYES };

struct Rng
{
    uint32_t s = 0x2545f491u;
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    int below (int n) { return (int) (next() % (uint32_t) n); }
};

struct Mover { int r = 0, c = 0, dir = LEFT; double acc = 0; };
struct Enemy : Mover { int mode = E_HOUSE; double releaseAt = 0; bool scared = false; int wanderR = 0, wanderC = 0; double wanderAt = 0; };

struct Game
{
    // ---------------------------------------------------------------- maze
    uint8_t tile[H][W] {};
    int cellOf[H][W] {}, cellR[kMaxCells] {}, cellC[kMaxCells] {}, nCells = 0;
    int startR = 0, startC = 0, doorR = 0, doorC = 0, dotsLeft = 0;
    // ---------------------------------------------------------------- state
    State state = S_TITLE, beforePause = S_PLAY;
    double t = 0, stateT = 0, phaseT = 0, scaredUntil = 0, bonusUntil = 0;
    Mover pl; int wantDir = LEFT, mouth = 0;
    Enemy en[kEnemies];
    int score = 0, hiscore = 0, lives = 3, level = 1, combo = 0, dotsEaten = 0, bonusR = -1, bonusC = -1;
    bool extraGiven = false, chase = false;
    float speedMul = 1.f;
    Rng rng;
    uint32_t sfx = 0;                                   // sounds triggered since the last takeSfx()

    void init()
    {
        nCells = 0;
        for (int r = 0; r < H; ++r)
            for (int c = 0; c < W; ++c)
            {
                const char ch = kMaze[r][c];
                cellOf[r][c] = -1;
                if (ch != '#' && nCells < kMaxCells) { cellOf[r][c] = nCells; cellR[nCells] = r; cellC[nCells] = c; ++nCells; }
                if (ch == 'P') { startR = r; startC = c; }
                if (ch == '-') { doorR = r; doorC = c; }
            }
        resetMaze(); resetPositions(); state = S_TITLE;
    }
    void resetMaze()
    {
        dotsLeft = 0;
        for (int r = 0; r < H; ++r)
            for (int c = 0; c < W; ++c)
            {
                const char ch = kMaze[r][c];
                tile[r][c] = ch == '#' ? T_WALL : ch == '.' ? T_DOT : ch == 'o' ? T_POWER : ch == 'H' ? T_HOUSE : ch == '-' ? T_DOOR : T_PATH;
                if (tile[r][c] == T_DOT || tile[r][c] == T_POWER) ++dotsLeft;
            }
        dotsEaten = 0; bonusR = bonusC = -1;
    }
    void resetPositions()
    {
        pl.r = startR; pl.c = startC; pl.dir = LEFT; pl.acc = 0; wantDir = LEFT; mouth = 0;
        const int hr = doorR + 1;                                         // house rows start under the door
        const int pos[kEnemies][2] = { { doorR - 1, doorC }, { hr, doorC }, { hr + 1, doorC - 1 }, { hr + 1, doorC + 1 } };
        const double release[kEnemies] = { 0, 1.5, 4.5, 7.5 };
        for (int i = 0; i < kEnemies; ++i)
        {
            Enemy& e = en[i]; e.r = pos[i][0]; e.c = pos[i][1]; e.acc = 0; e.scared = false;
            e.mode = i == 0 ? E_ACTIVE : E_HOUSE; e.dir = i == 0 ? LEFT : UP; e.releaseAt = release[i]; e.wanderAt = 0;
        }
        phaseT = 0; chase = false; scaredUntil = 0; combo = 0; bonusUntil = 0; bonusR = bonusC = -1;
    }
    void setState (State s) { state = s; stateT = 0; }
    void newGame() { score = 0; lives = 3; level = 1; extraGiven = false; resetMaze(); resetPositions(); setState (S_READY); sfx |= 1u << SFX_START; }

    // START button / pad: start, pause, resume
    void pressStart()
    {
        switch (state)
        {
            case S_TITLE: case S_OVER: newGame(); break;
            case S_PAUSED: setState (beforePause); break;
            case S_PLAY: case S_READY: beforePause = state; setState (S_PAUSED); break;
            default: break;
        }
    }
    uint32_t takeSfx() { const uint32_t s = sfx; sfx = 0; return s; }

    // ---------------------------------------------------------------- rules
    int wrapC (int c) const { return (c + W) % W; }
    bool open (int r, int c, bool enemy, bool passDoor) const
    {
        if (r < 0 || r >= H) return false;
        const uint8_t t_ = tile[r][wrapC (c)];
        if (t_ == T_WALL) return false;
        if (t_ == T_HOUSE || t_ == T_DOOR) return enemy && passDoor;
        return true;
    }
    bool canMove (const Mover& m, int d, bool enemy = false, bool passDoor = false) const { return open (m.r + DR[d], m.c + DC[d], enemy, passDoor); }
    void step (Mover& m, int d) { m.r += DR[d]; m.c = wrapC (m.c + DC[d]); m.dir = d; }
    double levelMul() const { const double k = 1.0 + 0.06 * (level - 1); return (k > 1.45 ? 1.45 : k) * speedMul; }
    double playerSpeed() const { return 7.0 * levelMul(); }
    double scaredTime() const { const double s = 7.0 - 0.75 * (level - 1); return s < 2.0 ? 2.0 : s; }
    bool inTunnel (const Mover& m) const { return kMaze[m.r][wrapC (m.c)] == ' ' && (m.c < 4 || m.c > W - 5); }
    double enemySpeed (const Enemy& e) const
    {
        const double p = playerSpeed();
        if (e.mode == E_EYES) return p * 2.0;
        if (e.mode == E_LEAVING) return p * 0.6;
        double s = e.scared ? p * 0.55 : p * (0.88 + 0.02 * (level > 5 ? 5 : level));
        if (inTunnel (e)) s *= 0.5;
        return s;
    }
    void addScore (int pts)
    {
        score += pts;
        if (! extraGiven && score >= 10000) { extraGiven = true; ++lives; sfx |= 1u << SFX_EXTRA; }
        if (score > hiscore) hiscore = score;
    }
    void eatAt (int r, int c)
    {
        uint8_t& t_ = tile[r][c];
        if (t_ == T_DOT) { t_ = T_PATH; --dotsLeft; ++dotsEaten; addScore (10); sfx |= 1u << SFX_DOT; }
        else if (t_ == T_POWER)
        {
            t_ = T_PATH; --dotsLeft; ++dotsEaten; addScore (50); sfx |= 1u << SFX_POWER;
            scaredUntil = t + scaredTime(); combo = 0;
            for (Enemy& e : en) if (e.mode == E_ACTIVE) { e.scared = true; e.dir = opp (e.dir); }
        }
        if ((dotsEaten == 60 || dotsEaten == 120) && bonusR < 0 && t_ == T_PATH && (r != startR || c != startC))
        { bonusR = startR; bonusC = startC; bonusUntil = t + 9.0; }
        if (bonusR == r && bonusC == c) { addScore (bonusPoints()); bonusR = bonusC = -1; sfx |= 1u << SFX_BONUS; }
    }
    int bonusPoints() const { const int p[] = { 100, 300, 500, 700, 1000, 2000, 3000, 5000 }; return p[level - 1 < 7 ? level - 1 : 7]; }

    // where an enemy heads for (chase / scatter targets give each bug its own character)
    void target (int i, Enemy& e, int& tr, int& tc)
    {
        if (! chase)                                                      // scatter: each to its own corner
        {
            const int cr[kEnemies] = { -2, -2, H + 1, H + 1 }, cc[kEnemies] = { W + 1, -2, W + 1, -2 };
            tr = cr[i]; tc = cc[i]; return;
        }
        switch (i)
        {
            case 0: tr = pl.r; tc = pl.c; return;                                         // Kick: straight at you
            case 1: tr = pl.r + 3 * DR[pl.dir]; tc = pl.c + 3 * DC[pl.dir]; return;      // Snare: cuts in front
            case 2:                                                                       // Hat: hunts up close, else roams
            {
                if (std::abs (e.r - pl.r) + std::abs (e.c - pl.c) <= 7) { tr = pl.r; tc = pl.c; return; }
                if (t >= e.wanderAt) { const int k = rng.below (nCells); e.wanderR = cellR[k]; e.wanderC = cellC[k]; e.wanderAt = t + 3.0; }
                tr = e.wanderR; tc = e.wanderC; return;
            }
            default:                                                                      // Clap: guards the nearest power record
            {
                int best = 1 << 30; tr = pl.r; tc = pl.c;
                for (int r = 0; r < H; ++r)
                    for (int c = 0; c < W; ++c)
                        if (tile[r][c] == T_POWER)
                        {
                            const int d = std::abs (r - pl.r) + std::abs (c - pl.c);
                            if (d < best) { best = d; tr = (r + pl.r) / 2; tc = (c + pl.c) / 2; }
                        }
                return;
            }
        }
    }
    int chooseDir (int i, Enemy& e)
    {
        int opts[4], n = 0;
        for (int d : { UP, LEFT, DOWN, RIGHT }) if (d != opp (e.dir) && canMove (e, d, true, false)) opts[n++] = d;
        if (n == 0) return opp (e.dir);                                    // dead end (never in this maze)
        if (e.scared && e.mode == E_ACTIVE) return opts[rng.below (n)];
        int tr, tc;
        if (e.mode == E_EYES) { tr = doorR - 1; tc = doorC; } else target (i, e, tr, tc);
        int best = opts[0]; long bd = -1;
        for (int k = 0; k < n; ++k)
        {
            const int d = opts[k], r = e.r + DR[d], c = e.c + DC[d];
            const long dist = (long) (r - tr) * (r - tr) + (long) (c - tc) * (c - tc);
            if (bd < 0 || dist < bd) { bd = dist; best = d; }
        }
        return best;
    }
    void moveEnemy (int i, Enemy& e)
    {
        if (e.mode == E_HOUSE) { if (t >= e.releaseAt) e.mode = E_LEAVING; return; }
        if (e.mode == E_LEAVING)                                           // walk to the door column, then up and out
        {
            if (e.c != doorC) step (e, e.c < doorC ? RIGHT : LEFT);
            else step (e, UP);
            if (e.r == doorR - 1) { e.mode = E_ACTIVE; e.dir = rng.below (2) ? LEFT : RIGHT; }
            return;
        }
        if (e.mode == E_EYES && e.r == doorR - 1 && e.c == doorC)          // home: back into the house, revive soon
        {
            e.r = doorR + 1; e.c = doorC; e.mode = E_HOUSE; e.scared = false; e.releaseAt = t + 1.5; e.dir = UP; return;
        }
        step (e, chooseDir (i, e));
    }
    // player and enemy meet (same cell, or swapped cells in the same instant)
    void collide (int pr, int pc)
    {
        for (int i = 0; i < kEnemies; ++i)
        {
            Enemy& e = en[i];
            if (e.mode != E_ACTIVE) continue;
            if (! (e.r == pl.r && e.c == pl.c) && ! (e.r == pr && e.c == pc && prevR[i] == pl.r && prevC[i] == pl.c)) continue;
            if (e.scared)
            {
                addScore (200 << (combo < 3 ? combo : 3)); ++combo;
                e.mode = E_EYES; e.scared = false; sfx |= 1u << SFX_EAT;
            }
            else { setState (S_DYING); sfx |= 1u << SFX_DEATH; return; }
        }
    }
    int prevR[kEnemies] {}, prevC[kEnemies] {};

    void stepPlay (double dt)
    {
        phaseT += dt;
        const bool wasChase = chase;
        const double cyc = 25.0, scatter = level >= 4 ? 3.0 : 5.0;
        chase = fmodd (phaseT, cyc) >= scatter;
        if (chase != wasChase) for (Enemy& e : en) if (e.mode == E_ACTIVE && ! e.scared) e.dir = opp (e.dir);
        if (scaredUntil > 0 && t >= scaredUntil) { scaredUntil = 0; for (Enemy& e : en) e.scared = false; }
        if (bonusR >= 0 && t >= bonusUntil) bonusR = bonusC = -1;

        for (int i = 0; i < kEnemies; ++i) { prevR[i] = en[i].r; prevC[i] = en[i].c; }
        const int pr = pl.r, pc = pl.c;
        // player
        pl.acc += playerSpeed() * dt;
        if (pl.acc >= 1.0)
        {
            pl.acc -= 1.0;
            if (canMove (pl, wantDir)) pl.dir = wantDir;
            if (canMove (pl, pl.dir)) { step (pl, pl.dir); mouth ^= 1; eatAt (pl.r, pl.c); }
            else pl.acc = 0.999;                                           // blocked: turn as soon as the way opens
        }
        else if (wantDir == opp (pl.dir) && canMove (pl, wantDir)) pl.dir = wantDir;   // reversing is instant
        // enemies
        for (int i = 0; i < kEnemies; ++i)
        {
            Enemy& e = en[i];
            e.acc += enemySpeed (e) * dt;
            if (e.acc >= 1.0) { e.acc -= 1.0; moveEnemy (i, e); }
        }
        collide (pr, pc);
        if (state == S_PLAY && dotsLeft == 0) { setState (S_CLEAR); sfx |= 1u << SFX_CLEAR; }
    }

    // advance the game by dt seconds (fixed internal steps of 1/240 s)
    double pending = 0;
    void update (double dt)
    {
        pending += dt;
        const double h = 1.0 / 240.0;
        while (pending >= h)
        {
            pending -= h;
            if (state == S_PAUSED || state == S_TITLE || state == S_OVER) { stateT += h; continue; }
            t += h; stateT += h;
            switch (state)
            {
                case S_READY: if (stateT >= 2.2) setState (S_PLAY); break;
                case S_PLAY: stepPlay (h); break;
                case S_DYING:
                    if (stateT >= 1.8)
                    {
                        if (--lives <= 0) { lives = 0; setState (S_OVER); }
                        else { resetPositions(); setState (S_READY); }
                    }
                    break;
                case S_CLEAR:
                    if (stateT >= 2.4) { ++level; resetMaze(); resetPositions(); setState (S_READY); sfx |= 1u << SFX_START; }
                    break;
                default: break;
            }
        }
    }

    // ---------------------------------------------------------------- what each cell shows
    int frameAt (int k) const
    {
        const int r = cellR[k], c = cellC[k];
        const bool blink = fmodd (state == S_PLAY ? t : stateT, 0.5) < 0.25;
        if (state == S_DYING)
        {
            if (r == pl.r && c == pl.c) { const int f = (int) (stateT / 0.3); return F_DEATH + (f > 4 ? 4 : f); }
        }
        else if (r == pl.r && c == pl.c && state != S_CLEAR) return F_PLAYER + pl.dir * 2 + (state == S_PLAY ? mouth : 0);
        else if (r == pl.r && c == pl.c) return F_PLAYER + pl.dir * 2;
        if (state != S_DYING && state != S_CLEAR)
            for (int i = kEnemies - 1; i >= 0; --i)
            {
                const Enemy& e = en[i];
                if (e.r != r || e.c != c) continue;
                if (e.mode == E_EYES) return F_EYES;
                if (e.scared) return (scaredUntil - t < 2.0 && blink) ? F_SCARED_FLASH : F_SCARED;
                return F_ENEMY + i;
            }
        if (r == bonusR && c == bonusC) return F_BONUS;
        switch (tile[r][c])
        {
            case T_DOT: return F_DOT;
            case T_POWER: return (state == S_PLAY && ! blink) ? F_POWER_DIM : F_POWER;
            case T_DOOR: return F_DOOR;
            default: return F_EMPTY;
        }
    }
    const char* message() const
    {
        switch (state)
        {
            case S_TITLE: return "PRESS START";
            case S_READY: return level > 1 ? "NEXT LEVEL - READY!" : "READY!";
            case S_PAUSED: return "PAUSED";
            case S_DYING: return "OUCH!";
            case S_CLEAR: return "LEVEL CLEAR!";
            case S_OVER: return "GAME OVER - PRESS START";
            default: return chase ? "RUN!" : "MUNCH!";
        }
    }
};
} // namespace mm
