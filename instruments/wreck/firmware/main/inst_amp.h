// AMP (inst_amp.c): the guitar and the bass in one downtuned, fuzzed voice. Owned by the AMP builder.
//
// Power chords (root, fifth, octave, a sub under them) from the song's key and chord, through a fuzz and a cab. What
// it plays follows the axis and the sound: silent or a stab at BREAKCORE; palm-muted chugs on BEAT's kicks
// (beat_kicks()) and tremolo picking at HARDCORE (with blast beats, grind); slow riffs at SLUDGE (the minor and
// phrygian notes of the key, bends, slides, the fuzz wide open); one chord held at DRONE, swelling, with feedback
// singing over it and the amp's hum. perf.drive opens the gain, perf.dive bends it down, a knock is a chord hit.
#pragma once
#include "core.h"

void  amp_process(float *out, float *send, int n, uint32_t f0);    // audio: ADD the amp (dry, and the reverb send)
void  amp_hit(float strength);                // UI task: a knock (0..1): a chord hit (at SLUDGE and DRONE)
typedef struct {
    float level;               // 0..1 what it plays now (peak, decaying)
    int   note;                // the root it plays, MIDI; -1 none
    int   mode;                // what it is doing: 0 silent, 1 chug, 2 tremolo, 3 riff, 4 drone
    float feedback;            // 0..1 how much the feedback sings
} amp_view_t;
void  amp_view(amp_view_t *v);                // UI task, for the screen
