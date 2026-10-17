// FX (inst_fx.c): the drums' insert and the master. Owned by the FX builder.
//
// fx_drums(): crush (bits and sample rate, the breakcore grit), drive (from warm to torn to folded), a filter (low or
// high pass with resonance, for sweeps) and DARK; perf.crush and perf.drive push them, the axis moves them (crushed at
// BREAKCORE, driven at HARDCORE, dark at DRONE). fx_master(): the tape (wow and flutter that grow towards DRONE;
// perf.drag slows it, flat again it catches up and is back in time; the brake: a stop or a spin-up), then the reverb
// (a room at BREAKCORE, a cathedral at DRONE), then a little glue.
#pragma once
#include "core.h"

void  fx_drums(float *io, int n);                       // audio, in place
void  fx_master(float *io, const float *send, int n);   // audio, in place: io is the mix; send feeds the reverb
// the tape's brake (UI task): the machine stops (to 0 over `beats` of the song, times BRAKE), or starts again (spins
// up from 0 and lands exactly in time at the end of its `beats`). The host calls it when REC is tapped, and at a
// change of feel (a short stop, then the new tempo comes in). The send goes through the tape too, so the reverb's
// tail rings on after a stop.
void  fx_brake(float beats);
void  fx_spin(float beats);
bool  fx_stopped(void);                                 // the brake reached 0 (the host then stops the machine)
typedef struct { float speed, crush, drive, room; } fx_view_t;   // the tape's speed now (1 = in time), 0..1 each
void  fx_view(fx_view_t *v);

#ifndef ESP_PLATFORM
// tools/sim only (test_fx.c): a second engine with its own state and buffers, so a test can drive the FX directly
// without disturbing the one the instrument plays through. Every fx_* call acts on the engine in use.
typedef struct fx_engine fx_engine_t;
fx_engine_t *fx_engine_new(void);
void  fx_engine_free(fx_engine_t *e);
void  fx_engine_use(fx_engine_t *e);                    // NULL: the live engine again
float fx_tape_lag(void);                                // frames the tape is behind time now (0: in time)
int   fx_subnormals(void);                              // subnormal floats in the reverb's lines and states
void  fx_test_nomod(bool off);                          // the reverb's line drift off (to show what it is for)
#endif
