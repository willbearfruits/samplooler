// The core's looper (instruments with INST.looper): what the instrument plays is recorded into PSRAM and
// played back under it.
//
//   REC held       no loop, or stopped: record a new one, as long as REC is down. Playing: add to it.
//   REC tapped     (shorter than 0.3 s) stop / play. A tap never records and never erases.
//   CLEAR LOOP     (menu) erases it.
//
// Nothing is lost to the 0.3 s it takes to tell a hold from a tap: the last half second of output is
// always kept in a small ring, and once a hold is sure, the part since the press is copied in (or, when
// adding, mixed in at the positions it was played over). The seam is crossfaded: recording runs on for
// 6 ms after the release and that tail is blended into the loop's start. Stop, play, clear and the ends of
// an overdub are 6 ms ramps. LOOP KEEP below 100 % turns the loop into a fading echo: each pass keeps
// that much of what is there.
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { LOOP_EMPTY, LOOP_RECORDING, LOOP_PLAYING, LOOP_DUBBING, LOOP_STOPPED } loop_state_t;
#define LOOP_MAX_S   16
#define LOOP_HOLD_MS 300                 // a press this long is a hold

bool  looper_init(void);                 // UI task, at boot: allocates about 1 MB of PSRAM; false if it could not
bool  looper_ready(void);
// UI task: REC
void  looper_press(void);
void  looper_confirm(void);              // REC has been down for LOOP_HOLD_MS
void  looper_release(bool confirmed);    // confirmed false: it was a tap
void  looper_clear(void);
// audio task, after the instrument: records io, adds the playback (times the level) to it
void  looper_process(float *io, int n, float level, float keep);
// state for the screen (any task)
loop_state_t looper_state(void);
float looper_pos(void);                  // 0..1 through the loop
float looper_len_s(void);                // the loop's length; while recording, how much so far
float looper_fill(void);                 // while recording: how much of LOOP_MAX_S is used
int   looper_layers(void);               // passes recorded or added since it was made
