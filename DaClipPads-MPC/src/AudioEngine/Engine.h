#pragma once
// The audio engine (audio thread only, except the atomic request slots and the display snapshot).
//
// Timing: every block is cut into segments of at most 32 samples, and additionally at every scheduled event
// (MIDI note, quantised launch or stop, follow action, scene change), so events land on their exact sample.
// A launch scheduled for beat t starts on the sample where the timeline reaches t.
//
// Signal flow per clip: voices (main + 2 crossfade tails + slice voices) -> FILTER -> DRIVE -> VOLUME / PAN /
// MUTE -> the SAMPLER bus (VINTAGE on) or the clean bus, plus post-fader REVERB and DELAY sends.
// Master: SAMPLER stage -> + clean bus + reverb / delay returns -> master chain (saturation, EQ, compressor,
// volume, limiter).
#include "../Chopper/Chop.h"
#include "../ClipEngine/Clip.h"
#include "../Effects/MasterFX.h"
#include "../MIDI/Midi.h"
#include "../Sampler/Vintage.h"

namespace cp
{
constexpr int kSeg = 32;
constexpr int kScenes = 4;
constexpr int kSliceVoices = 8;

enum SceneFollow { SF_OFF, SF_NEXT, SF_PREV, SF_RANDOM, SF_REPEAT, SF_COUNT };
enum HostFollow { HF_OFF, HF_RESTART, HF_RESTART_STOP, HF_COUNT };
enum Quality { QL_ECO, QL_NORMAL, QL_HIGH, QL_COUNT };

// Global settings the plugin copies in at the start of each block.
struct EngineParams
{
    double tempo = 90.0; bool sync = true; float swing = 0.5f; int quant = Q_BAR; int sceneQuant = -1;   // -1 = global
    int sceneFollow = SF_OFF; double sceneBars = 4; int hostFollow = HF_RESTART_STOP; int quality = QL_NORMAL;
    float velSens = 0.5f; int sliceNote = 60; int midiChannel = 0; bool muteAll = false; int selected = 0;
    bool samplerOn = true; VintageParams vintage; MasterParams master;
    double lateMs = 35.0;                 // a press this soon after a grid point still starts on it
};

enum ClipState { CS_EMPTY, CS_STOPPED, CS_QUEUED, CS_PLAYING, CS_LOOPING, CS_STOPPING, CS_MUTED, CS_COUNT };

struct ClipView                               // for the screen (written and read on the audio thread)
{
    int state = CS_EMPTY; float progress = 0.f; bool playing = false;
};

struct Channel
{
    Svf f[2]; SvfCoefs co; float cut = 20000.f; float res = -1.f;
    float gl = 0.f, gr = 0.f, rs = 0.f, ds = 0.f;
    int quiet = 0;
    float bufL[kSeg], bufR[kSeg];
};

struct ClipRun
{
    Sample* smp = nullptr;
    Voice main, tail[2];
    bool qStart = false; double qStartBeat = 0; float qVel = 1.f; double qFrac = -1.0;   // qFrac >= 0: legato start
    bool qStop = false; double qStopBeat = 0;
    double followAt = -1.0;                  // timed follow action (beat), < 0: none
    bool gate = false;                       // Gate mode: note held
    float vel = 1.f;
    Channel ch;
    bool anyOn() const { return main.on || tail[0].on || tail[1].on; }
};

struct SliceVoice { Voice v; int clip = -1; int slice = -1; float vel = 1.f; };

struct Engine
{
    float sr = 44100.f;
    Transport tr;
    ClipSettings* set = nullptr;              // the plugin's clip settings
    Rel<int>* sceneMask = nullptr;            // kScenes masks (bit per clip)
    ClipRun run[kClips];
    SliceVoice sv[kSliceVoices];
    ClipView view[kClips];
    Vintage vintage;
    Reverb reverb; Delay delay; MasterChain chain;
    Rng rng;
    float muteGain = 1.f;

    // scenes
    int activeScene = -1, queuedScene = -1; double sceneAt = 0, sceneFollowAt = -1;

    // requests from other threads (UI / host)
    std::atomic<int> trigReq[kClips], stopReq[kClips], shareReq[kClips], killReq[kClips];
    std::atomic<int> previewReq { -1 }, sceneReq { -1 }, sliceReq { -1 };
    std::atomic<bool> stopAllReq { false }, retrigAllReq { false }, restartReq { false };
    std::atomic<Sample*> pending[kClips];
    std::atomic<Sample*> retireRing[64];
    std::atomic<int> learnNote { -1 };        // last note-on (for LEARN), -1 none
    std::atomic<Sample*> uiSample[kClips];    // for the UI thread (chop, trim): freed only seconds after retiring

    MidiQueue midi;
    float peakL = 0.f, peakR = 0.f;
    // idle detection: a section sleeps once its input is silent and its output has died away
    int revIdle = 0, dlyIdle = 0, vinIdle = 0, masterIdle = 0;
    static bool silent (const float* a, const float* b, int m) { for (int i = 0; i < m; ++i) if (a[i] != 0.f || b[i] != 0.f) return false; return true; }
    static float peakOf (const float* a, const float* b, int m) { float p = 0.f; for (int i = 0; i < m; ++i) p = std::fmax (p, std::fmax (std::fabs (a[i]), std::fabs (b[i]))); return p; }
    bool asleep (int& idle, bool inputSilent, float outPeak, int m, double holdSec)
    {
        if (! inputSilent || outPeak > 1e-6f) { idle = 0; return false; }
        idle += m;
        return idle > (int) (holdSec * sr);
    }

    // bus buffers
    float dryL[kSeg], dryR[kSeg], vinL[kSeg], vinR[kSeg], revL[kSeg], revR[kSeg], dlyL[kSeg], dlyR[kSeg];

    Engine()
    {
        for (int c = 0; c < kClips; ++c) { trigReq[c] = 0; stopReq[c] = 0; shareReq[c] = 0; killReq[c] = 0; pending[c] = nullptr; uiSample[c] = nullptr; }
        for (auto& r : retireRing) r = nullptr;
        rng.seed (0x1234567u);
    }
    void prepare (float rate)
    {
        sr = rate; tr.sr = rate;
        vintage.prepare (rate); reverb.prepare (rate); delay.prepare (rate); chain.prepare (rate);
    }

    // ------------------------------------------------------------------------------------------ samples
    void retire (Sample* s)
    {
        if (s == nullptr || --s->refs > 0) return;
        for (auto& r : retireRing) { Sample* e = nullptr; if (r.compare_exchange_strong (e, s)) return; }
        // ring full (never in practice): leak rather than free memory another voice may still read
    }
    void setSample (int c, Sample* s)
    {
        ClipRun& r = run[c];
        if (s != nullptr) ++s->refs;
        retire (r.smp);
        r.smp = s;
        uiSample[c].store (s);
        r.main.on = r.tail[0].on = r.tail[1].on = false;
        r.qStart = r.qStop = false; r.followAt = -1;
        for (auto& v : sv) if (v.clip == c) v.v.on = false;
    }

    // ------------------------------------------------------------------------------------------ helpers
    int clipQuant (int c) const { const int q = set[c].quant; return q <= 0 ? p.quant : q - 1; }
    EngineParams p;
    double lateBeats() const { return p.lateMs * 0.001 * tr.tempo / 60.0; }
    bool loaded (int c) const { return run[c].smp != nullptr; }
    bool playing (int c) const { return run[c].main.on && ! run[c].main.releasing; }

    static int autoBeats (double lenSec, double tempo)
    {
        int best = 4; double err = 1e9;
        for (int b = 1; b <= 64; b *= 2)
        {
            const double e = std::fabs (std::log ((b * 60.0 / lenSec) / tempo));
            if (e < err) { err = e; best = b; }
        }
        return best;
    }
    double fitBeats (int c, const Region& R) const
    {
        const int sl = set[c].slen;
        const double bpb = tr.beatsPerBar;
        switch (sl)
        {
            case SL_1_4: return 1; case SL_1_2: return 2; case SL_1BAR: return bpb; case SL_2BAR: return 2 * bpb;
            case SL_4BAR: return 4 * bpb; case SL_8BAR: return 8 * bpb;
            case SL_AUTO: return autoBeats (fitLen (c, R) / sr, tr.tempo);
            default: return 0;
        }
    }
    double fitLen (int c, const Region& R) const { return set[c].loops() && R.nseq == 0 ? (R.le - R.ls) : R.L; }

    Rates rates (int c, const Region& R, int extraSemis = 0) const
    {
        const ClipSettings& s = set[c];
        Rates x;
        const double P = std::pow (2.0, ((int) s.semis + extraSemis + (int) s.cents / 100.0) / 12.0);
        const double fix = R.s ? R.s->rate / sr : 1.0;
        const double beats = fitBeats (c, R);
        const double T = beats > 0 ? fitLen (c, R) / (beats * tr.framesPerBeat()) : 1.0;
        x.cubic = p.quality != QL_ECO;
        x.grains = p.quality == QL_HIGH ? 4 : 2;
        x.grainLen = grainMs (s.stype) * 0.001 * sr;
        if (s.pmode == PM_STRETCH && (std::fabs (P - 1.0) > 1e-6 || std::fabs (T - 1.0) > 1e-6))
        { x.stretch = true; x.T = T * fix; x.P = P * fix; }
        else x.step = P * T * fix;
        return x;
    }

    // ------------------------------------------------------------------------------------------ actions
    void startClip (int c, double atBeat, float vel, double legatoFrac = -1.0)
    {
        ClipRun& r = run[c];
        if (r.smp == nullptr) return;
        Region R; if (! buildRegion (set[c], r.smp, sr, R)) return;
        if (r.main.on) spawnTail (c, r.main, msToFrames (4.0, sr));                    // retrigger: old voice fades out
        const Rates x = rates (c, R);
        // late start: already this many frames into the clip
        const double late = std::fmax (0.0, (tr.beat - atBeat) * tr.framesPerBeat());
        double at = late * (x.stretch ? x.T : x.step);
        if (legatoFrac >= 0.0) at = legatoFrac * R.L;
        if (R.loop && at >= R.le) at = R.ls + fmodd (at - R.ls, R.le - R.ls);
        if (at >= R.L) return;
        r.main.start (at, R);
        r.vel = vel;
        r.qStart = false; r.qStop = false;
        const double fb = followBeats (set[c].ftime, tr.beatsPerBar);
        r.followAt = (set[c].follow != FA_OFF && fb > 0) ? atBeat + fb : -1.0;
    }
    void stopClip (int c)
    {
        ClipRun& r = run[c];
        r.main.release (std::fmax (msToFrames (5.0, sr), msToFrames ((float) set[c].fadeOut, sr)));
        r.qStart = false; r.qStop = false; r.followAt = -1; r.gate = false;
    }
    Voice* spawnTail (int c, const Voice& from, double frames)
    {
        ClipRun& r = run[c];
        Voice& t = ! r.tail[0].on ? r.tail[0] : (! r.tail[1].on ? r.tail[1] : (r.tail[0].env < r.tail[1].env ? r.tail[0] : r.tail[1]));
        t = from; t.tail = true; t.releasing = true;
        t.envStep = -(float) (std::fmax (t.env, 1e-3f) / std::fmax (1.0, frames));
        return &t;
    }
    // schedule a launch with quantisation q (late presses start right away, in step)
    void queueStart (int c, int q, float vel, bool fromUser = true)
    {
        ClipRun& r = run[c];
        if (r.smp == nullptr) return;
        const double t = tr.launchBeat (tr.beat, q, p.swing, fromUser ? lateBeats() : 0.0);
        if (t <= tr.beat + 1e-9) { startClip (c, t, vel); return; }
        r.qStart = true; r.qStartBeat = t; r.qVel = vel; r.qFrac = -1.0; r.qStop = false;
    }
    void queueStop (int c, int q)
    {
        ClipRun& r = run[c];
        if (r.qStart && ! playing (c)) { r.qStart = false; return; }                     // cancel a pending launch
        r.qStart = false;
        if (! playing (c)) return;
        const double t = tr.launchBeat (tr.beat, q, p.swing, 0.0);
        if (t <= tr.beat + 1e-9 || q == Q_NONE) { stopClip (c); return; }
        r.qStop = true; r.qStopBeat = t;
    }
    // a pad hit / note-on, by trigger mode
    void trigger (int c, float vel, bool midi)
    {
        if (! loaded (c)) return;
        const int q = clipQuant (c), mode = set[c].mode;
        ClipRun& r = run[c];
        if (mode == M_TOGGLE || (mode == M_GATE && ! midi))
        {
            if (r.qStop) { r.qStop = false; return; }                                     // second press: keep playing
            if (playing (c)) { queueStop (c, q); return; }
            if (r.qStart) { r.qStart = false; return; }
        }
        if (mode == M_GATE && midi) r.gate = true;
        queueStart (c, q, vel);
    }
    void release (int c)                                                               // note-off
    {
        if (set[c].mode != M_GATE) return;
        ClipRun& r = run[c];
        r.gate = false;
        if (r.qStart) { r.qStart = false; return; }
        if (playing (c)) stopClip (c);
    }
    void stopAll()
    {
        for (int c = 0; c < kClips; ++c) { stopClip (c); }
        for (auto& v : sv) v.v.release (msToFrames (5.0, sr));
        activeScene = queuedScene = -1; sceneFollowAt = -1;
    }
    void retrigAll (int q)
    {
        const double t = tr.launchBeat (tr.beat, q, p.swing, 0.0);
        for (int c = 0; c < kClips; ++c)
            if (playing (c)) { ClipRun& r = run[c]; r.qStart = true; r.qStartBeat = t; r.qVel = r.vel; r.qFrac = -1; r.qStop = false; }
    }
    void launchScene (int s, bool now = false)
    {
        if (s < 0 || s >= kScenes) return;
        const int mask = sceneMask[s];
        const int q = p.sceneQuant < 0 ? p.quant : p.sceneQuant;
        const double t = now ? tr.beat : tr.launchBeat (tr.beat, q, p.swing, lateBeats());
        for (int c = 0; c < kClips; ++c)
        {
            ClipRun& r = run[c];
            if ((mask >> c) & 1)
            {
                if (! loaded (c)) continue;
                if (t <= tr.beat + 1e-9) startClip (c, t, 1.f);
                else { r.qStart = true; r.qStartBeat = t; r.qVel = 1.f; r.qFrac = -1; r.qStop = false; }
            }
            else if (playing (c) || r.qStart)
            {
                r.qStart = false;
                if (t <= tr.beat + 1e-9) stopClip (c); else { r.qStop = true; r.qStopBeat = t; }
            }
        }
        queuedScene = s; sceneAt = t;
        if (t <= tr.beat + 1e-9) sceneStarted();
    }
    void sceneStarted()
    {
        activeScene = queuedScene; queuedScene = -1;
        sceneFollowAt = p.sceneFollow != SF_OFF ? sceneAt + p.sceneBars * tr.beatsPerBar : -1.0;
    }
    int nextLoaded (int c, int dir) const
    {
        for (int k = 1; k <= kClips; ++k) { const int j = ((c + dir * k) % kClips + kClips) % kClips; if (loaded (j)) return j; }
        return -1;
    }
    void follow (int c, double atBeat)
    {
        ClipRun& r = run[c];
        const int fa = set[c].follow;
        r.followAt = -1;
        auto go = [&] (int j, double frac = -1.0)
        {
            if (j < 0) return;
            if (j != c) stopClip (c);
            startClip (j, atBeat, r.vel, frac);
        };
        switch (fa)
        {
            case FA_STOP: stopClip (c); break;
            case FA_REPEAT: startClip (c, atBeat, r.vel); break;
            case FA_NEXT: go (nextLoaded (c, 1)); break;
            case FA_PREV: go (nextLoaded (c, -1)); break;
            case FA_RANDOM:
            {
                int cand[kClips], n = 0;
                for (int j = 0; j < kClips; ++j) if (loaded (j) && j != c) cand[n++] = j;
                go (n ? cand[rng.below (n)] : c);
                break;
            }
            case FA_RANDOM_SCENE:
            {
                int mask = 0;
                if (activeScene >= 0 && ((sceneMask[activeScene] >> c) & 1)) mask = sceneMask[activeScene];
                else for (int s = 0; s < kScenes; ++s) if ((sceneMask[s] >> c) & 1) { mask = sceneMask[s]; break; }
                int cand[kClips], n = 0;
                for (int j = 0; j < kClips; ++j) if (((mask >> j) & 1) && loaded (j) && j != c) cand[n++] = j;
                go (n ? cand[rng.below (n)] : c);
                break;
            }
            case FA_CONTINUE:                                                           // next clip, same place in the bar
            {
                Region R; buildRegion (set[c], r.smp, sr, R);
                const double frac = R.L > 0 ? r.main.pos / R.L : 0.0;
                go (nextLoaded (c, 1), frac);
                break;
            }
            case FA_SCENE: queuedScene = clampi (set[c].fscene, 0, kScenes - 1); sceneAt = atBeat; launchSceneAt (queuedScene, atBeat); break;
            default: break;
        }
    }
    void launchSceneAt (int s, double atBeat)
    {
        const int mask = sceneMask[s];
        for (int c = 0; c < kClips; ++c)
        {
            if ((mask >> c) & 1) { if (loaded (c)) startClip (c, atBeat, 1.f); }
            else if (playing (c)) stopClip (c);
        }
        queuedScene = s; sceneAt = atBeat; sceneStarted();
    }
    void sceneFollowFire()
    {
        int s = activeScene;
        auto nonEmpty = [&] (int k) { return sceneMask[k] != 0; };
        switch (p.sceneFollow)
        {
            case SF_NEXT: for (int k = 1; k <= kScenes; ++k) { const int j = (activeScene + k) % kScenes; if (nonEmpty (j)) { s = j; break; } } break;
            case SF_PREV: for (int k = 1; k <= kScenes; ++k) { const int j = ((activeScene - k) % kScenes + kScenes) % kScenes; if (nonEmpty (j)) { s = j; break; } } break;
            case SF_RANDOM: { int cand[kScenes], n = 0; for (int j = 0; j < kScenes; ++j) if (nonEmpty (j) && j != activeScene) cand[n++] = j; if (n) s = cand[rng.below (n)]; break; }
            default: break;
        }
        if (s >= 0) launchSceneAt (s, sceneFollowAt);
    }
    void playSlice (int c, int k, float vel)
    {
        if (! loaded (c) || k < 0 || k >= set[c].sliceCount) return;
        SliceVoice* best = &sv[0];
        for (auto& v : sv) { if (! v.v.on) { best = &v; break; } if (v.v.env < best->v.env) best = &v; }
        for (auto& v : sv) if (v.v.on && v.clip == c && v.slice == k) { best = &v; break; }  // retrigger the same slice
        Region R; if (! buildRegion (set[c], run[c].smp, sr, R, k)) return;
        best->clip = c; best->slice = k; best->vel = vel;
        best->v.start (0.0, R); best->v.slice = k;
    }

    // ------------------------------------------------------------------------------------------ requests
    void serveRequests()
    {
        for (int c = 0; c < kClips; ++c)
        {
            if (killReq[c].exchange (0)) setSample (c, nullptr);
            if (const int src = shareReq[c].exchange (0)) if (src - 1 != c && run[src - 1].smp != nullptr) setSample (c, run[src - 1].smp);
            if (pending[c].load() != nullptr)
            {
                if (run[c].anyOn()) { if (! run[c].main.releasing) stopClip (c); }             // let it fade, swap next block
                else setSample (c, pending[c].exchange (nullptr));
            }
        }
        if (stopAllReq.exchange (false)) stopAll();
        for (int c = 0; c < kClips; ++c)
        {
            for (int n = trigReq[c].exchange (0); n > 0; --n) trigger (c, 1.f, false);
            if (stopReq[c].exchange (0)) queueStop (c, clipQuant (c));
        }
        const int pv = previewReq.exchange (-1);
        if (pv >= 0 && pv < kClips) { if (playing (pv)) stopClip (pv); else startClip (pv, tr.beat, 1.f); }
        const int sc = sceneReq.exchange (-1);
        if (sc >= 0) launchScene (sc);
        const int sl = sliceReq.exchange (-1);
        if (sl >= 0) playSlice (p.selected, sl, 1.f);
        if (retrigAllReq.exchange (false)) retrigAll (p.quant);
        if (restartReq.exchange (false)) retrigAll (Q_BAR);
    }
    void midiEvent (const MidiNote& n)
    {
        if (n.on) learnNote.store (n.note);
        const float vel = 1.f - p.velSens * (1.f - n.vel / 127.f);
        for (int c = 0; c < kClips; ++c)
            if ((int) set[c].note == n.note) { if (n.on) trigger (c, vel, true); else release (c); }
        const int k = n.note - p.sliceNote;
        if (n.on && k >= 0 && k < kMaxSlices) playSlice (p.selected, k, vel);
    }

    // ------------------------------------------------------------------------------------------ render
    void shift (double d)                                  // host timeline jumped: keep distances to "now"
    {
        for (auto& r : run) { if (r.qStart) r.qStartBeat += d; if (r.qStop) r.qStopBeat += d; if (r.followAt >= 0) r.followAt += d; }
        if (queuedScene >= 0) sceneAt += d;
        if (sceneFollowAt >= 0) sceneFollowAt += d;
    }
    // frames from now until beat t (0 if due)
    int framesUntil (double t) const
    {
        const double f = (t - tr.beat) / tr.bps();
        return f <= 0.5 ? 0 : (int) std::ceil (f - 0.5);
    }
    void fireDue()
    {
        const double eps = 0.5 * tr.bps();
        for (int guard = 0; guard < 4; ++guard)
        {
            bool any = false;
            if (queuedScene >= 0 && sceneAt <= tr.beat + eps) { any = true; sceneStarted(); }
            for (int c = 0; c < kClips; ++c)
            {
                ClipRun& r = run[c];
                if (r.qStop && r.qStopBeat <= tr.beat + eps) { any = true; r.qStop = false; if (! r.qStart) stopClip (c); }
                if (r.qStart && r.qStartBeat <= tr.beat + eps)
                {
                    any = true; const double f = r.qFrac;
                    startClip (c, r.qStartBeat, r.qVel, f);
                }
                if (r.followAt >= 0 && r.followAt <= tr.beat + eps) { any = true; follow (c, r.followAt); }
            }
            if (sceneFollowAt >= 0 && sceneFollowAt <= tr.beat + eps) { any = true; sceneFollowFire(); }
            if (! any) break;
        }
    }
    int nextEventFrames (int limit) const
    {
        int f = limit;
        auto consider = [&] (double t) { const int k = framesUntil (t); if (k < f) f = k; };
        for (const ClipRun& r : run) { if (r.qStart) consider (r.qStartBeat); if (r.qStop) consider (r.qStopBeat); if (r.followAt >= 0) consider (r.followAt); }
        if (queuedScene >= 0) consider (sceneAt);
        if (sceneFollowAt >= 0) consider (sceneFollowAt);
        return f;
    }

    void renderClip (int c, int m)
    {
        ClipRun& r = run[c];
        Channel& ch = r.ch;
        const ClipSettings& s = set[c];
        bool active = r.anyOn();
        for (auto& v : sv) if (v.clip == c && v.v.on) active = true;
        if (! active && ch.quiet > (int) sr) return;                                    // silent and settled
        std::memset (ch.bufL, 0, sizeof (float) * (size_t) m); std::memset (ch.bufR, 0, sizeof (float) * (size_t) m);
        if (active && r.smp != nullptr)
        {
            Region R; buildRegion (s, r.smp, sr, R);
            const Rates x = rates (c, R);
            Voice* newTail = nullptr; int tailFrom = m;
            auto tailer = [&] (const Voice& v, double frames, int from) { newTail = spawnTail (c, v, frames); tailFrom = from; };
            auto noTail = [] (const Voice&, double, int) {};
            for (auto& t : r.tail) if (t.on) renderVoice (t, R, x, ch.bufL, ch.bufR, m, noTail);
            if (r.main.on)
            {
                const int fl = renderVoice (r.main, R, x, ch.bufL, ch.bufR, m, tailer);
                if (newTail != nullptr && tailFrom < m) renderVoice (*newTail, R, x, ch.bufL + tailFrom, ch.bufR + tailFrom, m - tailFrom, noTail);
                if ((fl & (RV_ENDED | RV_WRAPPED)) && ! r.main.releasing && s.follow != FA_OFF && s.ftime == FT_END) follow (c, tr.beat);
                if ((fl & RV_ENDED) && s.mode == M_GATE) r.gate = false;
            }
            for (auto& v : sv)
                if (v.clip == c && v.v.on)
                {
                    Region SR; if (! buildRegion (s, r.smp, sr, SR, v.slice)) { v.v.on = false; continue; }
                    Rates one;                                                           // slices: resample, pitch only
                    one.cubic = x.cubic;
                    one.step = std::pow (2.0, ((int) s.semis + (int) s.slicePitch[clampi (v.slice, 0, kMaxSlices - 1)] + (int) s.cents / 100.0) / 12.0)
                             * (r.smp->rate / sr);
                    float tl[kSeg] = {}, tr_[kSeg] = {};
                    renderVoice (v.v, SR, one, tl, tr_, m, noTail);
                    for (int i = 0; i < m; ++i) { ch.bufL[i] += tl[i] * v.vel; ch.bufR[i] += tr_[i] * v.vel; }
                }
        }
        // channel strip
        const float target = clampf (s.cutoff, 20.f, 20000.f);
        ch.cut = std::exp (std::log (ch.cut) + (std::log (target) - std::log (ch.cut)) * 0.35f);
        const int ft = s.filt;
        if (ft != FT_OFF) { ch.co.set (ch.cut, sr, s.res); }
        const float drive = clampf (s.drive, 0.f, 1.f), dk = 1.f + 5.f * drive, dc = 1.f / std::sqrt (dk);
        const float vol = dbToGain (s.vol) * (s.mute || p.muteAll ? 0.f : 1.f) * r.vel;
        const float pan = clampf (s.pan, -1.f, 1.f);
        const float tgl = vol * std::fmin (1.f, 1.f - pan), tgr = vol * std::fmin (1.f, 1.f + pan);
        const float trs = clampf (s.rsend, 0.f, 1.f), tds = clampf (s.dsend, 0.f, 1.f);
        const float k = 1.f - std::exp (-1.f / (0.004f * sr));                         // 4 ms gain smoothing
        float* busL = s.vintage && p.samplerOn ? vinL : dryL; float* busR = s.vintage && p.samplerOn ? vinR : dryR;
        float pk = 0.f;
        for (int i = 0; i < m; ++i)
        {
            float l = ch.bufL[i], rr = ch.bufR[i];
            if (ft != FT_OFF) { l = svfTick (ch.f[0], ch.co, l, ft); rr = svfTick (ch.f[1], ch.co, rr, ft); }
            if (drive > 0.001f) { l = tanhApprox (l * dk) * dc; rr = tanhApprox (rr * dk) * dc; }
            ch.gl += (tgl - ch.gl) * k; ch.gr += (tgr - ch.gr) * k; ch.rs += (trs - ch.rs) * k; ch.ds += (tds - ch.ds) * k;
            const float ol = l * ch.gl, orr = rr * ch.gr;
            busL[i] += ol; busR[i] += orr;
            revL[i] += ol * ch.rs; revR[i] += orr * ch.rs;
            dlyL[i] += ol * ch.ds; dlyR[i] += orr * ch.ds;
            pk = std::fmax (pk, std::fabs (ol) + std::fabs (orr));
        }
        ch.quiet = (active || pk > 1e-5f) ? 0 : ch.quiet + m;
        if (! active && ch.quiet > (int) sr) { ch.f[0].reset(); ch.f[1].reset(); }
    }

    void process (float* outL, float* outR, int n, const VstTimeInfo* ti)
    {
        const double jump = tr.beginBlock (ti, p.sync, p.tempo);
        if (jump != 0.0) shift (jump);
        if (tr.startedEdge && p.hostFollow != HF_OFF)                                   // sequencer start: loops restart in step
            for (int c = 0; c < kClips; ++c) if (playing (c)) startClip (c, tr.beat, run[c].vel);
        if (tr.stoppedEdge && p.hostFollow == HF_RESTART_STOP) stopAll();
        serveRequests();
        int mi = 0;
        int pos = 0;
        while (pos < n)
        {
            while (mi < midi.count && midi.ev[mi].frame <= pos) midiEvent (midi.ev[mi++]);
            fireDue();
            int m = std::min (kSeg, n - pos);
            if (mi < midi.count) m = std::min (m, std::max (1, midi.ev[mi].frame - pos));
            m = std::max (1, std::min (m, nextEventFrames (m)));
            std::memset (dryL, 0, sizeof dryL); std::memset (dryR, 0, sizeof dryR); std::memset (vinL, 0, sizeof vinL); std::memset (vinR, 0, sizeof vinR);
            std::memset (revL, 0, sizeof revL); std::memset (revR, 0, sizeof revR); std::memset (dlyL, 0, sizeof dlyL); std::memset (dlyR, 0, sizeof dlyR);
            for (int c = 0; c < kClips; ++c) renderClip (c, m);
            for (auto& v : sv) if (v.clip >= 0 && run[v.clip].smp == nullptr) v.v.on = false;
            if (p.samplerOn)
            {
                const bool in = silent (vinL, vinR, m);
                if (! (in && vinIdle > (int) (0.2 * sr))) { vintage.process (vinL, vinR, m, p.vintage); asleep (vinIdle, in, peakOf (vinL, vinR, m), m, 0.2); }
                else std::memset (vinL, 0, sizeof vinL), std::memset (vinR, 0, sizeof vinR);
            }
            for (int i = 0; i < m; ++i) { dryL[i] += vinL[i]; dryR[i] += vinR[i]; }
            const MasterParams& mp = p.master;
            if (mp.revReturn > 0.f && reverb.ok())
            {
                const bool in = silent (revL, revR, m);
                if (! (in && revIdle > (int) (0.5 * sr)))
                {
                    reverb.setup (mp.revSize, mp.revDamp);
                    float tl[kSeg] = {}, tr_[kSeg] = {};
                    reverb.process (revL, revR, tl, tr_, m);
                    for (int i = 0; i < m; ++i) { dryL[i] += tl[i] * mp.revReturn; dryR[i] += tr_[i] * mp.revReturn; }
                    asleep (revIdle, in, peakOf (tl, tr_, m), m, 0.5);
                }
            }
            if (mp.dlyReturn > 0.f && delay.ok())
            {
                const bool in = silent (dlyL, dlyR, m);
                const double hold = clampd (mp.delayBeats * tr.framesPerBeat() / sr, 0.0, 4.2) + 0.1;   // a full pass of the line
                if (! (in && dlyIdle > (int) (hold * sr)))
                {
                    float tl[kSeg] = {}, tr_[kSeg] = {};
                    delay.process (dlyL, dlyR, tl, tr_, m, mp.delayBeats * tr.framesPerBeat(), clampf (mp.fb, 0.f, 0.95f), mp.tone);
                    for (int i = 0; i < m; ++i) { dryL[i] += tl[i] * mp.dlyReturn; dryR[i] += tr_[i] * mp.dlyReturn; }
                    asleep (dlyIdle, in, peakOf (tl, tr_, m), m, hold);
                }
            }
            {
                const bool in = silent (dryL, dryR, m);
                if (! (in && masterIdle > (int) (0.5 * sr))) { chain.process (dryL, dryR, m, mp); asleep (masterIdle, in, peakOf (dryL, dryR, m), m, 0.5); }
            }
            for (int i = 0; i < m; ++i)
            {
                outL[pos + i] = dryL[i]; outR[pos + i] = dryR[i];
                peakL = std::fmax (peakL, std::fabs (dryL[i])); peakR = std::fmax (peakR, std::fabs (dryR[i]));
            }
            tr.advance (m);
            pos += m;
        }
        while (mi < midi.count) midiEvent (midi.ev[mi++]);
        midi.clear();
        updateView();
    }
    void updateView()
    {
        for (int c = 0; c < kClips; ++c)
        {
            ClipView& v = view[c]; const ClipRun& r = run[c];
            v.playing = playing (c);
            if (r.smp == nullptr) { v.state = CS_EMPTY; v.progress = 0; continue; }
            if (r.qStop) v.state = CS_STOPPING;
            else if (r.qStart) v.state = CS_QUEUED;
            else if (v.playing) v.state = (set[c].mute || p.muteAll) ? CS_MUTED : (set[c].loops() ? CS_LOOPING : CS_PLAYING);
            else v.state = CS_STOPPED;
            if (v.playing)
            {
                Region R; buildRegion (set[c], r.smp, sr, R);
                v.progress = R.L > 0 ? (float) clampd (r.main.pos / R.L, 0.0, 1.0) : 0.f;
            }
            else v.progress = 0.f;
        }
    }
};
} // namespace cp
