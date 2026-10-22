// The instrument on the host: the firmware's own front.c, params.c, looper.c, ui.c, oled.c and instrument.c,
// with stand-ins for the drivers, fed scripted controls, motion, joystick and mic, in 10 ms ticks of five
// 64-frame blocks like the device, drawing at 25 fps. Screens go to out/<name>/NN_label.pbm (then a contact
// sheet out/<name>.png), animations to out/<name>-<clip>.gif, sound to out/<name>.wav, and every check to
// out/results.json.
#pragma once
#include "core.h"
#include "front.h"
#include "params.h"
#include <stdio.h>
#include <stdbool.h>

#define TICK_FRAMES (AUDIO_SR / 100)           // 320

typedef float (*mic_fn)(float t, void *u);     // the mic, per sample, t in seconds since boot

// What the controls do on the next tick. Set fields and they stay until changed (the knob's tap is one tick).
typedef struct {
    int     enc;               // detents this tick (cleared after it)
    bool    tap;               // cleared after it
    bool    pressed;           // the knob is down (held_ms counts by itself)
    bool    rec;               // REC is down
    bool    imu;               // an IMU is fitted (true by default)
    vec3_t  acc, gyro;         // in screen axes (the sim's tilt setup is the identity)
    vec3_t  burst[64];         // 1 kHz accelerometer for this tick (INST.burst); nburst cleared after it
    int     nburst;
    bool    joy;               // a joystick is fitted
    float   jx, jy;
    mic_fn  mic; void *mic_u;
} sim_in_t;
extern sim_in_t SI;

extern float *sim_out, *sim_mic;               // everything played (after the master) and heard, from boot
extern int    sim_n;
extern float  sim_t;                           // seconds since boot
extern const char *sim_name;                   // lower-case instrument name: out/<name>...

void sim_boot(bool show_splash);
void sim_tick_hook(void);                      // runs before every tick; weak, for stand-ins that need time (link_sim.c)               // a fresh device: defaults, tilt set, first-run screens skipped unless asked
void sim_tick(void);
void sim_run(float seconds);
void sim_turn(int detents);                    // one tick with the knob turned
void sim_tap(void);                            // a short press: down 3 ticks, up (in play: and the menu's wait)
void sim_double_tap(void);                     // two short presses 0.1 s apart: the knob on VOLUME
void sim_hold_knob(void);                      // 0.7 s: past the hold
void sim_rec(float seconds);                   // REC down for that long, then up (runs the ticks)
void sim_tilt(float lean_x_deg, float lean_y_deg);   // the box at rest at those angles (sets SI.acc)
void sim_shot(const char *label);              // the current frame
void sim_gif_begin(const char *clip);          // record every frame drawn until sim_gif_end()
void sim_gif_end(void);
void sim_wav(const char *suffix, float t0, float t1);   // out/<name><suffix>.wav from that stretch

// measurements on what was played, between two times (seconds since boot)
float m_rms(float t0, float t1);
float m_peak(float t0, float t1);
int   m_bad(void);                             // NaN or inf samples
float m_pitch(const float *x, int n, float fmin, float fmax);   // autocorrelation, Hz, 0: none
float m_pitch_at(float t0, float t1, float fmin, float fmax);
float m_goertzel(float t0, float t1, float f);  // power at f
float m_click(float t_change, float *spike, float *ctx);   // a discontinuity at t_change: spike / context (>3 is a click)
float hz_to_midi(float f);
float sim_noise(void);                         // -1..1

// the app's line (front_sysex): each SysEx message the device sent, as text from its letter on
extern char sim_sysex[256][720];
extern int  sim_sysex_n;
extern bool sim_sysex_off;

// checks: printed, counted, and written to out/results.json with the section they ran in
extern int checks_run, checks_failed;
void section(const char *name);
void check(bool ok, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void note(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void metric(const char *key, double v);        // a number for results.json (no pass/fail)

// what the platform saw
typedef struct { float t; uint8_t st, d1, d2; } midi_ev_t;
extern midi_ev_t midi_log[65536];
extern int midi_n;
extern uint8_t sim_led[3];

// from the instrument's tools/sim/test_inst.c
void inst_tests(void);                         // its scenarios and checks
void inst_excite(void);                        // make it sound steadily (for the click checks): set SI.mic etc.
extern const char *const inst_click_skip[];    // param tags the click check leaves out (NULL-terminated), with a reason in the test file

// the link's pretend room (link_sim.c, for instruments that use link.c)
int    sim_peer_add(const char *name, int prio, double ppm, double off_us);   // a pretend board; its index
void   sim_peer_use(int k);                    // link_* calls now act as board k (-1: the board under test)
void   sim_peer_power(int k, bool on);         // its radio
double sim_peer_beats(int k);                  // where board k is in the session's beats now
float  sim_peer_gap_ms(int k);                 // board k's beat against the board under test's, ms
extern double sim_link_loss;
