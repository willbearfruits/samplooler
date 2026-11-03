// NYLON's contract between its files: the params, what the UI side asks of the sound (E), what the sound tells
// the UI side, the looper's commands, the ear (onsets, the pitch tracker's ring) and the neck.
//
//   instrument.c   UI side: params, sounds, hints, REC, the modes' neck mappings, MIDI, LED
//   inst_ear.c     UI side: the neck (setup, home, lift and lean), slaps from the accelerometer, the pitch tracker
//   inst_engine.c  audio: the chain (low cut, the engines, the drums, the room, the looper), the onsets
//   inst_loop.c    audio: the looper (tap to record, tap to close, tap to add, hold to stop, hold on to erase)
//   inst_grain.c   audio: CLOUD (grains from the last seconds) and BLOOM (the last note held by grains)
//   inst_fx.c      audio: the room (+ shimmer), ECHO, WAH, OCTAVE, WOBBLE
//   inst_drum.c    audio: GOLPE's drums (KNOCK's voices, a cajon)
//   inst_synth.c   audio: VOICE's synth, played by the pitch tracker
//   inst_scene.c   UI side: the screen, the splash
#pragma once
#include "core.h"
#include <stdbool.h>
#include <stdint.h>

// ---------------------------------------------------------------- params
enum {
    P_MODE, P_WET, P_DRY, P_SPACE, P_RSIZE, P_LOWCUT, P_HICUT, P_GUARD,
    P_CSIZE, P_CDENS, P_CREACH, P_CUP, P_CDOWN, P_CREV, P_CFREEZE,
    P_BSUST, P_BSWELL, P_BHARM, P_BSUB, P_BCATCH,
    P_ETIME, P_EFB, P_ETONE, P_EWOW, P_ESYNC,
    P_WQ, P_WENV, P_WFUZZ,
    P_SHIM, P_SHINT,
    P_OSUB, P_OUP, P_OFIFTH,
    P_MDEPTH, P_MRATE,
    P_DTOP, P_DSIDE, P_DLEVEL, P_DTUNE, P_DDECAY,
    P_VTYPE, P_VOCT, P_VGLIDE, P_VMIDI,
    P_LNECK, P_LLEVEL, P_KEEP, P_LBARS, P_TEMPO, P_BEATS, P_METRO, P_START, P_TRIG, P_UNDO, P_ERASE,
    P_SETUP, P_HOME, P_RANGE, P_LRANGE, P_STILL, P_NECKON, P_LEAN, P_SLAP, P_SENSE, P_SLAPSET,
    P_HX, P_HY, P_HZ, P_AX, P_AY, P_AZ, P_AXSET, P_SLAPTHR,
    P_N
};

enum { M_CLEAN, M_CLOUD, M_BLOOM, M_ECHO, M_WAH, M_SHIMMER, M_OCTAVE, M_WOBBLE, M_GOLPE, M_VOICE, NMODE };
enum { FRZ_STICK, FRZ_SLAP, FRZ_NECK, FRZ_OFF };
enum { MET_OFF, MET_LIGHT, MET_COUNT, MET_SOUND };
enum { CATCH_ALL, CATCH_LOUD, CATCH_HOLD };
enum { LN_OFF, LN_LEVEL, LN_TAPE };
enum { ST_NOTE, ST_NOW };
enum { SLAP_AUTO, SLAP_DRUM, SLAP_OFF };
enum { LEAN_OFF, LEAN_NORMAL, LEAN_FLIP };
enum { VT_BASS, VT_BOW, VT_ORGAN, VT_PIPE, NVT };
// GOLPE's drums (DR_OFF: nothing)
enum { DR_OFF, DR_KICK, DR_SNARE, DR_HAT, DR_OHAT, DR_TOM, DR_CLAP, DR_RIM, DR_BELL, DR_SHAKER, DR_CONGA, DR_CAJON_LO, DR_CAJON_HI, NDRUM };
extern const char *const drum_names[NDRUM];
extern const int drum_gm[NDRUM];

// ---------------------------------------------------------------- what the UI asks of the sound (written per tick, read per block)
typedef struct {
    int   mode;
    float dry, wet, space, rsize, shim, shim_int, lowcut, hicut;
    float notch_hz[4];         // HOWL GUARD: the notches in the input (0: none), each faded in and out
    int   notch_on[4];
    // CLOUD
    float c_size, c_dens, c_reach, c_up, c_down, c_rev; int c_frozen;
    // BLOOM
    float b_hold, b_swell, b_harm, b_sub; int b_choke, b_catch;
    // ECHO
    float e_time, e_fb, e_tone, e_wow;
    // WAH
    float w_pos, w_q, w_env, w_fuzz;
    // OCTAVE
    float o_sub, o_up, o_fifth;
    // WOBBLE
    float m_depth, m_rate;
    // drums
    float d_tune, d_decay, d_level;
    // VOICE
    int   v_type, v_gate; float v_hz, v_level, v_glide;
    // the loop
    float l_level, l_rate, l_keep, l_trig;
    // bars and the metronome: the grid (beat 0's frame, frames a beat), beats a bar, bars a take (0: free), whether
    // takes and closes go to the grid (the METRONOME on), whether the click sounds now
    float l_fpb;
    uint32_t l_grid0;
    int   l_beats, l_bars, l_snap, m_click;
} eng_ctl_t;
extern volatile eng_ctl_t E;

// what the sound tells the UI side
extern volatile float eng_in_lvl, eng_out_lvl, eng_wet_lvl;   // peaks, decaying
extern volatile float eng_env;                              // the input's envelope (fast)
extern volatile float eng_bloom_lvl, eng_bloom_age;         // BLOOM: how loud the held note is, seconds since caught
extern volatile int   eng_bloom_caps;                       //   notes caught so far
extern volatile uint32_t eng_bloom_cancel;                  //   UI: the note being caught was a slap, drop it
extern volatile float eng_bloom_period[3];                  //   UI: the caught note's period, samples (0: none)
extern volatile int   eng_bloom_cur;                        //   which buffer holds the note now
#define BLOOM_CAP_N 9600
const int16_t *bloom_buf(int k);
extern volatile float eng_echo_lvl;                         // ECHO: the repeats' level
extern volatile float eng_wah_pos;                          // WAH: where the peak is, 0..1
extern volatile float eng_shim_lvl;                         // the shimmer's level
extern volatile int   eng_frozen;                           // CLOUD: the ring is frozen
extern volatile float eng_synth_lvl;                        // VOICE: the synth's level
extern volatile int   eng_beat;                             // the metronome's last beat (counting from the grid's 0), -1 none
extern volatile uint32_t eng_drum_seq; extern volatile int eng_drum_last;   // the last drum played

// grains for the screen: each one started, with where it came from (seconds back) and how loud
typedef struct { float back, amp, rate; uint8_t rev, kind; } grain_ev_t;   // kind 0 CLOUD, 1 BLOOM
bool eng_grain_pop(grain_ev_t *g);
extern volatile uint32_t eng_grains;      // grains started so far (CLOUD)

// onsets heard (the input's envelope jumping): for the slaps, the pitch tracker, the screen
typedef struct { uint32_t frame; float level, bright; } onset_t;
bool eng_onset_pop(onset_t *o);

// drums: the UI side asks, the sound plays
typedef struct { uint8_t drum, vel; } trig_t;
void eng_trigger(const trig_t *t);

// the ear: the input at 16 kHz for the pitch tracker (UI side reads, audio writes)
#define EAR_N 4096
extern float eng_ear[EAR_N];
extern volatile uint32_t eng_ear_w;
// the input at 32 kHz, after the cuts and before the notches, for HOWL GUARD (inst_guard.c)
#define HF_N 2048
extern float eng_hf[HF_N];
extern volatile uint32_t eng_hf_w;

void eng_init(void);
void eng_audio(const float *in, float *out, int n);
void eng_reset_mode(int m);              // UI: a sound left that mode for good (its buffers can be cleared lazily)

// the parts (audio task)
void fx_init(void);
void room_run(const float *send, float *io, int n);            // the room with its shimmer, added to io
void echo_run(const float *in, float *wet, int n, float feed);
void wah_run(const float *in, float *wet, int n, float feed);
void oct_run(const float *in, float *wet, int n, float feed);
void wob_run(const float *in, float *wet, int n, float feed);
void echo_clear_step(void);              // a slice of the echo's line to zero (after it was left)
void echo_reset(void);                   // start clearing it
bool echo_quiet(void);

void grain_init(void);
void ring_write(const float *in, int n, bool frozen);         // the last seconds, for the grains
void cloud_run(float *wet, int n, float feed);
void bloom_run(const float *in, float *wet, int n, float feed);
void bloom_onset(uint32_t frame, float level);                 // a note: catch it
void bloom_clear(void);

void drum_init(void);
void drum_run(float *io, int n);         // adds the drums
void synth_run(float *wet, int n, float feed);

// ---------------------------------------------------------------- the looper
typedef enum { L_EMPTY, L_ARMED, L_REC, L_PLAY, L_DUB, L_STOP } lstate_t;
enum { LC_ARM, LC_DISARM, LC_REC_AT, LC_CLOSE_AT, LC_DUB_AT, LC_DUB_OFF, LC_STOP, LC_PLAY, LC_ERASE, LC_UNDO };
typedef struct { uint8_t cmd; uint32_t at; } lcmd_t;
bool  loop_init(void);                  // at boot: PSRAM; false without it
void  loop_cmd(int cmd, uint32_t at);   // UI task
void  loop_process(const float *src, float *io, int n, float onset_lvl, bool onset_now, uint32_t onset_frame);
lstate_t loop_state(void);
float loop_pos(void);                   // 0..1
float loop_len_s(void);                 // the loop; while recording, so far
float loop_max_s(void);
int   loop_layers(void);
bool  loop_can_undo(void);              // a layer that can still be taken off
bool  loop_undo_fits(void);             // the loop is short enough to keep a copy for UNDO
float loop_rate_now(void);
int   loop_bar(int *of);                // the bar it is in (recording or playing), and of how many (0: free)

// ---------------------------------------------------------------- the ear (UI side, inst_ear.c)
typedef struct {
    float neck, lean;                   // degrees from home: neck lifted > 0, the top tipped towards you > 0
    float n, l;                         // -1..1 after the still zone and NECK RANGE
    bool  ok;                           // an IMU, a home
    bool  upright;                      // the board stands up (on a guitar's top), not flat
} neck_t;
extern neck_t NK;
void ear_tick(const ctl_t *c);          // the neck, the slaps, the tracker (every tick)
void ear_set_home(void);                // where it is now is home (the knob held, SET HOME)
void ear_setup_begin(void);             // NECK SETUP: sit still; lift the neck; back; tip the top up
enum { SU_OFF, SU_STILL, SU_HOME, SU_LIFT, SU_BACK, SU_TIP, SU_DONE };
int  ear_setup_state(void);
float ear_setup_progress(void);
float ear_setup_angle(void);            // degrees from home now (in the steps that move)
void ear_setup_result(float *lift, float *tip);   // what DONE shows: the lift and the tip measured (0: skipped)
void ear_setup_cancel(void);
void ear_setup_skip(void);              // the tip step: done without it
bool ear_setup_wanted(const ctl_t *c);  // the first time on a guitar, with no setup done: start it by itself
// slaps: hits on the body, from the accelerometer, confirmed by the mic
typedef struct { uint32_t frame; float jolt; int top; } slap_t;   // top: 1 on the top (or back), 0 on a side
bool ear_slap_pop(slap_t *s);
float ear_jolt(void);                   // the last tick's worst jolt, g
float ear_slap_thr(void);               // a slap's jolt, g: SLAP SETUP's, scaled by SLAP SENSE
// SLAP SETUP: strum hard (the jolts strumming makes), then slap the top three times; the threshold between them
enum { SS_OFF, SS_STRUM, SS_SLAP, SS_DONE };
void ear_slapset_begin(void);
int  ear_slapset_state(void);
void ear_slapset_cancel(void);
float ear_slapset_progress(void);
void ear_slapset_result(float *strum, float *slap, float *thr, bool *apart);
// the pitch tracker (VOICE)
void  ear_track(bool on);
float ear_hz(void);                     // 0: no note
float ear_clarity(void);
float ear_period(const int16_t *x, int n, float *clarity);   // a buffer's period in samples at 32 kHz (0: none)

// ---------------------------------------------------------------- HOWL GUARD (UI side, inst_guard.c)
void guard_tick(void);                  // every other tick: a howl found in the input gets a notch
int  guard_count(void);                 // notches set since boot
int  guard_active(float *hz, int max);  // the notches in place now

// ---------------------------------------------------------------- what the screen shows (instrument.c fills it, inst_scene.c draws it)
typedef struct {
    int   mode;
    bool  frozen;                 // CLOUD held still
    const char *neck_what;        // what the neck does in this sound ("REPEATS"), NULL: nothing
    float neck_amt;               // how much, 0..1 (or -1..1 where it goes both ways)
    bool  neck_signed;
    int   slap_seq, slap_top, slap_drum; float slap_vel;
    int   note; float cents;      // VOICE: the note heard (-1 none)
    int   take_ms;                // the take being recorded, so far
    int   lstate;                 // loop_state() this tick
    float tape;                   // the loop's speed
} nyui_t;
extern nyui_t NY;
void scene_draw(const view_t *v);
bool scene_splash(int f);
void scene_setup(const view_t *v);   // NECK SETUP's screens
void scene_slapset(const view_t *v); // SLAP SETUP's
