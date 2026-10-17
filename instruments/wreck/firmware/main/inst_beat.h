// BEAT (inst_beat.c): the chopper. Owned by the BEAT builder; the host, AMP and the screen use what is here.
//
// It plays kit_break()'s slices on the machine's grid (wreck.h: wr_grid, steps are sixteenths of the machine's beat).
// What plays each step comes from the axis and the sound: the break as it is, its slices rearranged, rolled (two to
// sixty-four times a step, rising or falling), reversed, re-pitched, cut short, rushed into fills; bars of 7, 5 or 9
// eighths against the flock's 4/4 at the BREAKCORE end; a gabber kick under it at HARDCORE (and blast beats);
// half and quarter time with only the big hits, pitched down and let ring at SLUDGE and DRONE. perf.chaos adds
// edits, perf.roll rolls the slice now, perf.stutter repeats the last bit, perf.smear plays backwards, perf.dive
// pitches it down. A knock is a fill. Holding the knob opens the step grid (16 steps: each a slice, with a trick),
// drawn over the scene.
#pragma once
#include "core.h"

void  beat_process(float *out, float *send, int n, uint32_t f0);   // audio: ADD the drums (dry, and the reverb send)
void  beat_fill(float strength);              // UI task: a knock (0..1): a fill now, longer when harder
bool  beat_hold(void);                        // UI task: the knob held in play: open the grid (true if it did)
bool  beat_editing(void);                     // the grid is open (the host then draws beat_draw_editor())
void  beat_draw_editor(const view_t *v);      // rows 16..63
uint32_t beat_reading(void);                  // the oldest break serial a voice still reads (KIT waits for it)
// The kicks of the bar now (the break's kick slices and the gabber kick, as planned at the bar's first step, in the
// audio task before AMP's block): bit (st & 31) is set when machine step st (wr_step_at's count) has a kick. In 4/4
// that is bit i = step i of a two-bar window; odd bars are mapped onto the same step numbers. 0 while stopped.
// A plain 32-bit read, safe in the audio task.
uint32_t beat_kicks(void);
// Where the bar now started (a step number as above) and its length: 16, or 14, 10, 18 in odd bars (ODD BARS).
int32_t  beat_bar_start(void);
int      beat_bar_len(void);
// for the screen: what played last (UI task)
typedef struct {
    int   step;                // the step now (0..steps-1 of the pattern)
    int   steps;               // the pattern's length (16, or 14 for a bar of 7/8 ...)
    int   slice;               // the slice playing, -1 none
    bool  reversed;
    int   roll;                // 0, or the repeats in this step
    float pitch;               // semitones
    float level;               // 0..1, decaying
    uint32_t hits;             // counts every slice started (the screen flashes on a change)
} beat_view_t;
void  beat_view(beat_view_t *v);
