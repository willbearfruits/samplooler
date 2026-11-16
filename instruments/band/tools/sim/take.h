// A take: a player's real motion, recorded from a board by tools/motion_rec.py (the motion stream, front.h) and
// replayed into the simulator as a test. The file is a JSON header line, then one line per 1 kHz sample:
//
//   frame ax ay az gx gy gz flags
//
// frame in audio frames (32 kHz) from the take's first sample; accel in g, gyro in deg/s, in screen axes (what the
// instrument saw; the sim's tilt setup is the identity, so they go straight back in); flags TAKE_*.
//
//   take_t *t = take_load("../../../../../lab/takes/airdweller/2026-10-25-left-x10-AD-7275FBD4.take");
//   take_begin(t);
//   while (take_feed(t)) sim_tick();          // or take_run(t)
//   check(hits == (int)take_num(t, "label.hits.LEFT", -1), ...);
//   take_free(t);
//
// The feeder puts each sample in the tick whose frames it falls in (by the take's frames, not by how the board's
// ticks grouped them): SI.burst / SI.gburst / SI.nburst, SI.acc and SI.gyro from the tick's last sample (held through
// a tick without samples), SI.rec and SI.pressed from the flags (a press of 40..500 ms is also a tap, as input.c).
// A gap (packets lost on the way, or the board's FIFO reset) is ticks without samples: time runs on, nothing is
// made up. At the end REC and the knob are let go.
#pragma once
#include "sim.h"

enum { TAKE_REC = 1, TAKE_KNOB = 2, TAKE_GYRO = 4, TAKE_GAP = 8 };   // REC down, the knob down, gyro present,
                                                                     // packets lost just before this sample
typedef struct {
    int       n;                       // samples
    int64_t  *frame;                   // audio frames from the first sample, never decreasing
    vec3_t   *acc, *gyro;              // g, deg/s, screen axes
    uint8_t  *flags;                   // TAKE_*
    char     *header;                  // the JSON header line ("" if the file has none)
    int       gaps;                    // samples marked TAKE_GAP
    float     secs;                    // the last sample's time
    // the replay
    int       pos;                     // the next sample to feed
    uint32_t  base;                    // the sim's frame count the take's frame 0 lands on
    bool      rec, knob;               // as the last fed sample left them
    int       knob_ms;                 // how long the knob has been down
} take_t;

take_t *take_load(const char *path);           // the whole file, read at once (malloc); NULL and a message if it cannot be
void    take_free(take_t *t);
void    take_begin(take_t *t);                 // replay from the start: the first sample lands in the next sim_tick()
bool    take_feed(take_t *t);                  // before each sim_tick(): this tick's samples into SI; false once all are fed
bool    take_feed_at(take_t *t, uint32_t frames);   // the same for a tick that will report `frames` (take_feed passes
                                                    // audio_frames() + TICK_FRAMES); for checks that do not tick
bool    take_done(const take_t *t);
float   take_run(take_t *t);                   // take_begin, then feed and tick to the end; the seconds it ran

// The header (JSON): a value by its dotted path ("label.hits.LEFT", "serial", "prompt"), as text (a string without
// its quotes, anything else as written). false / dflt / 0 when it is not there.
bool    take_get(const take_t *t, const char *path, char *out, int cap);
float   take_num(const take_t *t, const char *path, float dflt);
int     take_keys(const take_t *t, const char *path, char keys[][24], int max);   // an object's keys ("label.hits")

// One motion-stream message (front_motion_packet), F0 .. F7, back into numbers; false if it is not a whole one.
typedef struct { uint8_t seq, flags; uint32_t frames; int n; vec3_t acc[MOTION_MAX], gyro[MOTION_MAX]; } take_pkt_t;
bool    take_unpack(const uint8_t *m, int len, take_pkt_t *p);
