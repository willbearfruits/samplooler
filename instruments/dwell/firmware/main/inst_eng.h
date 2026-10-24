// DWELL's engine, the part its two files share: inst_engine.c (the audio task) and inst_wave.c (the UI task: the
// take's analysis, the built-in WAVE table). Nothing here is for instrument.c: inst_dwell.h is the contract.
#pragma once
#include "inst_dwell.h"

#define TAKE_N (TAKE_S * AUDIO_SR)

extern int16_t *dw_take;                   // the take, TAKE_N frames (PSRAM)
extern float   *dw_wtab[2];                // the WAVE tables, WAVE_N + 1 floats each (internal RAM)
extern volatile int dw_wave_cur;           // the table playing
extern volatile int dw_wave_want;          // the table to crossfade to (the UI sets it after filling the spare)
extern volatile int dw_wave_busy;          // a crossfade is on: the spare is still read, do not write it

void dw_wave_default(float *t);            // the built-in table: a voice-like spectrum (init, UI task)
