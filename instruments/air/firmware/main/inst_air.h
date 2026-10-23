// AIR: drum on nothing. Swing the box through the air and it is a stick. The gyro sees the stroke: forward is
// the hi-hat, left the kick, right the snare, back the open hat. There is always a beat (TEMPO, or the flock's);
// every hit is pulled to its nearest sixteenth. Tap REC and make a sound: the sound is cut from where it starts
// to where it dies and lands on the next pad clockwise (the samples survive a power cycle on the FAT partition the
// Samplooler's slots live on). Hold REC and play: a loop of whole bars, up to four stacked, each on or off, in
// musical time, so they follow the tempo wherever it goes.
//
// This header is the contract between instrument.c (the UI task: gestures, the loops, the link, the screens),
// inst_engine.c (the audio task: voices, samples, loop playback, the bus) and inst_store.c (flash, device only).
#pragma once
#include "core.h"
#include "spsc.h"

// ---- params (the table in instrument.c, in this order)
enum {
    // LOOPS
    P_TEMPO, P_LOOP1, P_LOOP2, P_LOOP3, P_LOOP4, P_ERASE_LAST, P_ERASE_ALL, P_QUANT, P_SWING, P_LLEVEL, P_MUTATE,
    // PADS
    P_FRONT, P_LEFT, P_RIGHT, P_BACK, P_SAMPLE, P_FORGET, P_SMAX, P_SKEEP, P_LEVEL, P_TUNE, P_DECAY, P_PUNCH,
    // FEEL
    P_SETUP, P_SENS, P_VEL, P_HAND, P_FLIP, P_CONFIRM, P_TILT, P_STICK,
    // FX
    P_FILTER, P_DRIVE, P_CRUSH, P_COMP, P_ECHO, P_ETIME, P_EFB, P_ROOM, P_RSIZE, P_GRAIN, P_GSIZE,
    // BREAK (the kit's own, KP_* from P_KBREAK: inst_kit.h; its last three are hidden, so they sit last on the page)
    P_STRETCH,
    P_KBREAK, P_KPRINT, P_KROOM, P_KTUNE, P_KTONE, P_KDECAY, P_KSWING, P_KGHOSTS, P_KHUMAN, P_KTAKE, P_KLENGTH, P_KSLICES, P_KGATE,
    // LINK
    P_LINK, P_GROUP, P_RANGE, P_HANDS, P_PEERS,
    // kept, not shown: the next pad a sample goes to, and what SWING SETUP measured
    P_NEXT, P_CALV, P_CALH,
    P_N
};

enum { PD_FRONT, PD_LEFT, PD_RIGHT, PD_BACK, NPAD };
enum { V_SAMPLE, V_KICK, V_SNARE, V_HAT, V_OHAT, V_TOM, V_CLAP, V_RIM, V_COWBELL, V_SHAKER, V_CONGA, V_ZAP, V_PART1, V_PART2, V_PART3, V_PART4, V_OFF, NVTYPE };
enum { HANDS_AUTO, HANDS_ONE, HANDS_LEFT, HANDS_RIGHT };
enum { LP_EMPTY, LP_OFF, LP_ON };            // a LOOP n setting
enum { STICK_LOOPS, STICK_ROLL, STICK_OFF };
enum { SRC_LIVE, SRC_LOOP, SRC_MIDI, SRC_ROLL };

#define NLOOP      4
#define LOOP_HITS  128                  // hits a loop can hold
#define BAR_MAX    8
#define PPB        96                   // ticks per beat: musical time, so a loop follows the tempo
#define BAR_TICKS  (4 * PPB)
#define PAD_MAX    (1 * AUDIO_SR)       // a sample: 1 s at most (the breaks take a megabyte of the PSRAM)
#define PAD_PRE    96                   // 3 ms kept from before the sound that starts a take

typedef struct { uint8_t pad, vel, src; uint32_t at; } trig_t;             // UI -> audio; at: the frame to play on (0: now)
typedef struct { uint32_t frame; uint8_t pad, vel, src; } played_t;        // audio -> UI

// the grid: beat 0 in our frames and the frames per beat, published by the UI each tick, read per block
typedef struct { uint32_t fpb, origin; float frac; } grid_t;
void   eng_set_grid(const grid_t *g);                   // UI
void   eng_get_grid(grid_t *g);                         // any task
int64_t eng_tick_at(const grid_t *g, uint32_t frame);   // the grid's tick (1/PPB beat) at a frame, rounded down
uint32_t eng_frame_of(const grid_t *g, int64_t tick);   // the frame a tick falls on
int64_t eng_floor_div(int64_t a, int64_t b);

// a loop: hits at ticks from its first bar, cycling every bars * BAR_TICKS. The UI writes hits (then publishes n),
// the audio task reads them. born: the cycle a hit was recorded live in (not replayed in that one)
typedef struct { uint16_t tick; uint8_t pad, vel; int32_t born; } hit_t;
typedef struct {
    hit_t hits[LOOP_HITS];
    volatile int n;
    volatile int64_t start;         // the tick of its first bar
    volatile int bars;              // 0: empty
    volatile int on;
    uint32_t serial;                // recording order (ERASE LAST takes the highest)
} loop_t;
extern loop_t eng_loops[NLOOP];
extern volatile int eng_run;        // 0: every loop is quiet (hold the knob)

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
int   eng_voices_on(void);                 // voices sounding now (for the screen and the tests)
int   eng_part_kind(int part);             // what a part mostly is (KS_*, inst_kit.h) from the break's slices, -1 none
uint16_t eng_hit_tick(const loop_t *l, const hit_t *h);   // where a hit plays in its loop after SWING

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
