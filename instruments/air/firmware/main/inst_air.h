// AIR: drum on nothing. Swing the box through the air and it is a stick. The gyro sees the stroke: forward is
// the hi-hat, left the kick, right the snare, back the open hat. Each pad is a synthesized drum (as KNOCK's) or
// a sample of your own: hold the knob, hit the air for the pad, make a sound into the mic. The samples survive a
// power cycle (the FAT partition the Samplooler's slots live on). REC is an event looper: hold it while you play,
// let go and it loops; on the link the loop is whole beats of the flock's tempo and starts on a beat.
//
// This header is the contract between instrument.c (the UI task: gestures, the looper, the link, the screens),
// inst_engine.c (the audio task: voices, samples, loop playback, the bus) and inst_store.c (flash, device only).
#pragma once
#include "core.h"
#include "spsc.h"

// ---- params (the table in instrument.c, in this order)
enum {
    // PADS
    P_FRONT, P_LEFT, P_RIGHT, P_BACK, P_LEVEL, P_TUNE, P_DECAY, P_PUNCH,
    // BREAK (the kit's own, KP_* from P_KBREAK: inst_kit.h; its last three are hidden, so they sit last on the page)
    P_STRETCH,
    P_KBREAK, P_KPRINT, P_KROOM, P_KTUNE, P_KTONE, P_KDECAY, P_KSWING, P_KGHOSTS, P_KHUMAN, P_KTAKE, P_KLENGTH, P_KSLICES, P_KGATE,
    // FEEL
    P_SENS, P_VEL, P_HAND, P_FLIP, P_TILT, P_JOYY, P_QUANT, P_TEMPO,
    // SAMPLE
    P_SAMPLE, P_SMAX, P_SKEEP, P_FORGET,
    // LOOP
    P_BEATS, P_GRID, P_SNAP, P_SWING, P_MUTATE, P_LLEVEL, P_UNDO, P_CLEAR,
    // BUS
    P_FILTER, P_DRIVE, P_CRUSH, P_COMP, P_ECHO, P_ETIME, P_EFB, P_ROOM, P_RSIZE, P_GRAIN, P_GSIZE,
    // LINK
    P_LINK, P_GROUP, P_RANGE, P_HANDS, P_PEERS,
    P_N
};

enum { PD_FRONT, PD_LEFT, PD_RIGHT, PD_BACK, NPAD };
enum { V_SAMPLE, V_KICK, V_SNARE, V_HAT, V_OHAT, V_TOM, V_CLAP, V_RIM, V_COWBELL, V_SHAKER, V_CONGA, V_ZAP, V_PART1, V_PART2, V_PART3, V_PART4, V_OFF, NVTYPE };
enum { HANDS_ONE, HANDS_LEFT, HANDS_RIGHT };
enum { SRC_LIVE, SRC_LOOP, SRC_MIDI, SRC_ROLL };

#define MAXHITS    256
#define PAD_MAX    (1 * AUDIO_SR)       // a sample: 1 s at most (the breaks take a megabyte of the PSRAM)
#define PAD_PRE    96                   // 3 ms kept from before the sound that starts a take

typedef struct { uint8_t pad, vel, src; uint32_t at; } trig_t;             // UI -> audio; at: the frame to play on (0: now)
typedef struct { uint32_t frame; uint8_t pad, vel, src; } played_t;        // audio -> UI

// the loop: the UI writes hits (then publishes eng_nhits), the audio task reads them
typedef struct {
    uint32_t off;                   // frames from the loop's start, as played
    uint8_t  pad, vel, layer;
    volatile uint8_t dead;          // undone or cleared: skipped
    uint16_t born;                  // the pass it was added in: not replayed in that same pass
} hit_t;
extern hit_t eng_hits[MAXHITS];
extern volatile int      eng_nhits;
extern volatile uint32_t eng_loop_len;     // 0: no loop
extern volatile uint32_t eng_loop_start;   // frame where pass 0 began
extern volatile int      eng_loop_run;     // 1 playing, 0 stopped
extern volatile int      eng_beats_fixed;  // the loop's beats when the link set them; 0: BEATS or a guess

// controls from the UI (plain stores)
extern volatile float eng_tune;            // semitones: TUNE and the tilt
extern volatile float eng_key;             // semitones added to the tuned drums (the flock's key)
extern volatile float eng_mutate;          // 0..1
extern volatile float eng_filter;          // -1 (low pass) .. 1 (high pass), FILTER and the joystick
extern volatile float eng_roll_hz;         // 0: no roll
extern volatile int   eng_roll_pad, eng_roll_vel;
extern volatile float eng_stutter;         // 0: off, else the slice in seconds
extern volatile float eng_part_rate;       // the parts' speed (the break stretched to the tempo), 1 as printed
extern volatile int   eng_part_bar;        // -1: the four parts are the whole break; 0, 1: that bar's four beats (HANDS)

// the samples: one buffer per pad in PSRAM (NULL when out of memory), eng_pad_len samples of it in use
extern int16_t *eng_pad[NPAD];
extern volatile int eng_pad_len[NPAD];
extern volatile float eng_pad_gain[NPAD];  // how much louder a sample plays to reach the drums' level
// capture: the UI arms a pad, the engine waits for a sound, records it until it dies or the time is up, and says
// CAP_DONE; the UI then trims it, sets eng_pad_len and saves it. The drums are muted while it listens.
enum { CAP_IDLE, CAP_ARMED, CAP_REC, CAP_DONE };
extern volatile int eng_cap_state, eng_cap_pad, eng_cap_len;
extern volatile int eng_cap_hold;         // 1: a take runs to the limit (ENDS: AT LONGEST), not until the sound dies
extern volatile float eng_in_env;          // the mic's envelope, for the screen
void eng_capture(int pad, float max_s);    // UI: listen for pad (its old sample is dropped)
void eng_capture_stop(void);               // UI: enough (REC let go, the knob tapped): finish what there is
void eng_capture_cancel(void);

// meters for the screen
extern volatile float eng_out_lvl;

void eng_init(void);                       // allocates the pads, the echo and the stutter buffer
void eng_audio(const float *in, float *out, int n);
void eng_trigger(const trig_t *t);
bool eng_played_pop(played_t *p);
uint32_t eng_pass(uint32_t frame);         // which pass of the loop a frame falls in
uint32_t eng_qoff(const hit_t *h);         // where a hit plays in the loop after GRID, STRENGTH and SWING
int   eng_beats(void);                     // beats in the loop: the link's, BEATS, or with AUTO what gives 75..165 BPM
float eng_bpm(void);                       // 0 without a loop
int   eng_voices_on(void);                 // voices sounding now (for the screen and the tests)
int   eng_part_kind(int part);             // what a part mostly is (KS_*, inst_kit.h) from the break's slices, -1 none

// tables shared with the screen and the sounds
extern const char *const vt_label[NVTYPE];       // short names for the screen
extern const int vt_gm[NVTYPE];                  // General MIDI drum notes
extern const int pad_gm[NPAD];                   // a pad's note when its voice is SAMPLE or OFF

// inst_store.c: the samples in flash (device only; the host's stand-ins keep nothing)
bool  store_init(void);                    // mount; true if the pads can be kept
int   store_load(int pad, int16_t *dst, int max);   // samples read into dst at boot, 0 if none
bool  store_save_begin(int pad, const int16_t *src, int n);   // start writing (the UI mutes first); false: cannot
bool  store_save_step(void);               // one 16 KB step; false when the job is over
bool  store_busy(void);
void  store_forget(int pad);               // delete its file
void  store_mute(bool on);                 // the output's 20 ms fade around a flash write (the host: nothing)
bool  store_silent(void);                  // the fade has reached silence
