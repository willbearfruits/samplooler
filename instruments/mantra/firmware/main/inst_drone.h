// DRONE (inst_drone.c): the wall of amps under the Buddha. Owned by the DRONE builder; refine it, but report every
// change.
//
// It is WRECK's AMP at its doom end (instruments/wreck/firmware/main/inst_amp.c: the same engine, so that a MANTRA
// and a WRECK on the link make one sound): a power chord (root, fifth, octave: PolyBLEP saws a few cents apart) from
// the song's chord in the TUNING's register, through the fuzz (two asymmetric clipping stages at twice the rate between
// halfband filters), the scooped tone stack and MIDS, the power amp and the cab; the bass and the SUB (a sine an octave
// under) around the cab; the amp's HUM; the FEEDBACK (an upper partial of the root fed into the fuzz, growing over the
// drone until it sings, climbing a partial now and then); the ORGAN (the chord held clean under it all). One chord held:
// it swells in over SWELL, a new chord slides in on a bar line (CHANGE: every bar, 2, 4 or 8 bars, or never: 4 is the
// pace of WRECK's DRONE zone). Sunn O))), Earth, Electric Wizard's slow end: nothing fast lives here.
#pragma once
#include "core.h"

// What the host passes it every tick (the module's own params do the rest). Built 2026-10-18 (inst_drone.c has the
// port's notes): the chord is taken from `song` on the bar lines in the audio task; with no `host_degree_at(frame)`
// in the host (WRECK's has one; here a weak reference) the take waits 12 ms for the song's chord to be fresh, and a
// chord that comes later still (the link's live chord) is taken within 22 ms of the line. Level is a 30 ms ramp;
// the send gain is level x SEND x `send` (0.5 from the host: SEND 100 then sends half, as the voices do).
typedef struct {
    float swell;               // 0..1 the breath (the box pumped): the gain opens +8 dB, the swell lifts to 1.3
    float bend;                // semitones on everything (the stick's x), smoothed over 20 ms here
    float dive;                // 0..1 everything sinks, an octave at 1 (the stick down: the voices hum, the amps dive).
                               // The stick's only: the score's hum (the monks' M) must not reach it
    bool  hit; float hit_s;    // a knock this tick, how hard (0..1): a chord hit if DRONE > KNOCK says so
    bool  running;             // false: nothing new starts, what rings fades out (0.5 s)
    float level;               // 0..1 MIX > DRONE (the module smooths it; at 0 the chord is let go and the module sleeps:
                               // turned up again, the chord is struck fresh and swells in)
    float send;                // 0..1 into the echo and the hall (the module scales its send by level and its own SEND)
} dr_ctl_t;
void  dr_set(const dr_ctl_t *c);                 // UI task, every tick (after the module's tick)
void  dr_process(float *out, float *send, int n);   // audio: ADD the amps (dry, and the FX send)

// For the screen (UI task): the wall of amps behind the Buddha
typedef struct {
    float level;               // 0..1 what it plays now (peak, decaying)
    float feedback;            // 0..1 how much the feedback sings
    float swell;               // 0..1 where the swell is
    int   note;                // the root it plays, MIDI; -1 none
    bool  playing;             // a chord is held
} dr_view_t;
void  dr_view(dr_view_t *v);
