// KNOCK's sound engine (inst_engine.c), shared with the UI side (instrument.c). Runs in the audio task.
//
// What happens to a knock:
//   1. the mic's onset detector fires (a fast envelope jumping over a slow one, over a floor that follows
//      the room, plus a share of what the box itself is playing so its own drums do not count);
//      the next 32 ms of the mic are captured into a snippet (PSRAM) and the onset goes to the UI;
//   2. BODY, ONE pitch: the live mic, through an expander that opens on knocks, rings the kit's modal
//      resonators at once (no latency; rubs and scrapes ring too);
//   3. the UI waits ~10 ms for the accelerometer's push, decides the side (or the tone), and triggers that
//      side: its VOICE (a synthesized drum) and, with BY SIDE pitch, its own resonator bank excited by the
//      captured snippet (so each side is a note);
//   4. the event looper replays hits with their snippets, quantized and swung at playback;
//   5. the drum bus: DJ filter, drive, punch (compressor), tape echo, room, then the stutter (beat repeat).
#pragma once
#include "core.h"
#include "spsc.h"

// ---- params (the table in instrument.c, in this order)
enum {
    // KIT
    P_FACE, P_LEFT, P_RIGHT, P_TOP, P_BOTTOM, P_VLEVEL, P_TUNE, P_DECAY, P_PUNCHV,
    // BODY
    P_BODY, P_BLEVEL, P_BPITCH, P_ROOT, P_SCALE, P_RING, P_BRIGHT, P_TOUCH,
    // FEEL
    P_SENS, P_SIDES, P_VEL, P_TILT, P_JOYY,
    // LOOP
    P_BEATS, P_GRID, P_SNAP, P_SWING, P_MUTATE, P_LLEVEL, P_UNDO, P_CLEAR,
    // BUS
    P_FILTER, P_DRIVE, P_COMP, P_ECHO, P_ETIME, P_EFB, P_ROOM, P_RSIZE,
    P_N
};

enum { SD_FACE, SD_LEFT, SD_RIGHT, SD_TOP, SD_BOTTOM, NSIDE };
enum { V_NONE, V_KICK, V_SNARE, V_HAT, V_OHAT, V_TOM, V_CLAP, V_RIM, V_COWBELL, V_SHAKER, V_CONGA, V_ZAP, NVTYPE };
enum { B_OFF, B_WOOD, B_MARIMBA, B_BELL, B_GLASS, B_TABLA, B_STEEL, B_MEMBRANE, B_METAL, NBODY };
enum { SRC_LIVE, SRC_LOOP, SRC_MIDI, SRC_ROLL };

#define SNIP_LEN   1024             // 32 ms of mic per knock (int16, PSRAM)
#define SNIP_PRE   24               // of which this much before the onset
#define NSNIP      288              // 256 can be pinned by the loop, the rest rotate
#define MAXHITS    256

typedef struct { uint32_t frame; float bright, level; int16_t snip; } onset_t;            // audio -> UI
typedef struct { uint8_t side, vel, src; int16_t snip; } trig_t;                          // UI -> audio
typedef struct { uint32_t frame; uint8_t side, vel, src; int16_t snip; } played_t;        // audio -> UI

// the loop: the UI writes hits (then publishes eng_nhits), the audio task reads them
typedef struct {
    uint32_t off;                   // frames from the loop's start, as knocked
    uint8_t  side, vel, layer;
    volatile uint8_t dead;          // undone or cleared: skipped
    int16_t  snip;                  // the knock's snippet, -1: none (MIDI, roll)
    uint16_t born;                  // the pass it was added in: not replayed in that same pass
} hit_t;
extern hit_t eng_hits[MAXHITS];
extern volatile int      eng_nhits;
extern volatile uint32_t eng_loop_len;     // 0: no loop
extern volatile uint32_t eng_loop_start;   // frame where pass 0 began
extern volatile int      eng_loop_run;     // 1 playing, 0 stopped

// controls from the UI (plain stores)
extern volatile float eng_tune;            // semitones, tilt and TUNE together
extern volatile float eng_mutate;          // 0..1
extern volatile float eng_filter;          // -1 (low pass) .. 1 (high pass), FILTER and the joystick
extern volatile float eng_roll_hz;         // 0: no roll
extern volatile int   eng_roll_side, eng_roll_vel;
extern volatile float eng_stutter;         // 0: off, else the slice in seconds
extern volatile float eng_knock_avg;       // a typical knock's level (the UI keeps it), for the BODY's gain
extern volatile uint8_t eng_pinned[NSNIP]; // snippets the loop holds on to

// meters for the screen
extern volatile float eng_env, eng_thr;    // the mic's fast envelope and the knock threshold
extern volatile float eng_body_lvl[NSIDE]; // how much each side's resonators ring
extern volatile float eng_out_lvl;

void eng_init(void);                       // allocates the snippets, the echo and the stutter buffer
void eng_audio(const float *in, float *out, int n);
void eng_changed(void);                    // params moved: recompute what depends on them (UI task)
bool eng_onset_pop(onset_t *o);
void eng_trigger(const trig_t *t);
bool eng_played_pop(played_t *p);
uint32_t eng_pass(uint32_t frame);         // which pass of the loop a frame falls in
uint32_t eng_qoff(const hit_t *h);         // where a hit plays in the loop after GRID, SNAP and SWING
int   eng_beats(void);                     // beats in the loop: BEATS, or with AUTO what gives 75..165 BPM
float eng_bpm(void);                       // 0 without a loop
const int16_t *eng_snip(int s);            // a snippet (for the screen)

// tables shared with the screen and the sounds
extern const char *const vt_label[NVTYPE];       // short names for the screen
extern const int vt_gm[NVTYPE];                  // General MIDI drum notes
extern const int side_gm[NSIDE];                 // a side's note when its voice is NONE
extern const int8_t scale_deg[][NSIDE];          // BY SIDE: each side's degree in the scale
