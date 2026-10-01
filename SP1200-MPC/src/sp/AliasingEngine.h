#pragma once
// AliasingEngine: decides how the sampling / playback / reconstruction stages run, from ALIAS, RECON, QUALITY and
// MACHINE. It adds no processing of its own: aliasing in this plugin is never a synthetic effect, it is whatever the
// sampling and drop-sample reads produce.
//   ALIAS AUTHENTIC  converter instants interpolated per QUALITY, drop-sample memory reads, ZOH per RECON
//   ALIAS ENHANCED   converter instants snapped to the host grid and raw host-grid steps (extra fold-back and
//                    timing jitter beyond the hardware; the "harder" sound)
//   ALIAS BYPASS     memory reads interpolated linearly (non-hardware): the pitch-dependent read aliasing disappears,
//                    the sampling and the ZOH stay
//   QUALITY ECO      nearest-sample converter instants + raw steps (cheapest; approximate)
//   MACHINE S1200 REF a voicing that leans on the documented artifacts: anti-alias filter off, ENHANCED aliasing,
//                    +4 dB into the output stage. Inspired by how Maschine's S1200 mode is described (aliasing that
//                    follows the tuning, output filters); not NI's algorithm.
namespace sp
{
enum Alias { AL_AUTHENTIC, AL_ENHANCED, AL_BYPASS };
enum Recon { RC_ANALOG, RC_RAW, RC_IDEAL };
enum Quality { Q_ECO, Q_NORMAL, Q_ACCURATE, Q_REFERENCE };
enum Machine { MA_SP1200, MA_SP12, MA_S1200REF };

struct AliasPlan
{
    int adcQuality = 1;          // ADC::quality (0 nearest .. 3 sinc16)
    bool blep = true;            // band-limited steps
    bool interpReads = false;    // memory reads interpolated
    bool idealRecon = false;
    bool aaOff = false;
    float driveBoostDb = 0.f;
    int oversample = 1;          // analog nonlinear stages
};

inline AliasPlan planFor (int alias, int recon, int quality, int machine)
{
    AliasPlan p;
    p.adcQuality = quality;
    p.oversample = quality == Q_REFERENCE ? 4 : (quality == Q_ACCURATE ? 2 : 1);
    p.blep = recon != RC_RAW && quality != Q_ECO;
    p.idealRecon = recon == RC_IDEAL;
    if (machine == MA_S1200REF) { alias = AL_ENHANCED; p.aaOff = true; p.driveBoostDb = 4.f; }
    if (alias == AL_ENHANCED) { p.adcQuality = 0; p.blep = false; }
    if (alias == AL_BYPASS) p.interpReads = true;
    return p;
}
} // namespace sp
