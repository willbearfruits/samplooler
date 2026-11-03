// SIFT: a granular synthesizer and recorder, chaos on one side and order on the other.
//
// Everything it hears goes into one long memory (the corpus, 20 s, as long as a Samplooler take). The corpus is cut
// into 32 ms units and each unit is listened to: how loud, how bright, how noisy, and its pitch when it has one. The
// units are dots on a map (two of those qualities as the axes, TONE across and NOISE up by default), and the tilt
// moves a cursor over the map: the grains come from the dots near it. That is the idea of corpus-based granular
// synthesis (CataRT, MuBu, FluCoMa) brought to a box you hold: the grains are chosen by what they sound like, not
// only by where they sit in the recording.
//
// Two modes, one knob from ORDER to CHAOS. DECOMPOSE (the default): a head moves through the recording and at 0 the
// grains follow it exactly, so the recording plays whole; turning up, it comes apart as language does when it is torn:
// the tape wears (a skip, a hole, a wow), syllables come loose (cut where they start, caught and repeated, swapped
// with their neighbours), then phonemes (short grains from wherever sounds like this moment: the map), then dust (a few
// milliseconds from anywhere, thrown in pitch, backwards, hard-edged, driven and crushed). The tilt pushes it there
// and back fast (roll), and moves the head slower, frozen, backwards or faster (pitch); FEEDBACK loops the output
// back into itself, ROT writes the damage into the recording (it stays: Lucier's room). MAP: the dots nearest the
// cursor (the tilt) play as a sorted pattern on the flock's beat, tuned to the key (ORDER), or as a swarm from a wide
// ring round it (CHAOS); with the cursor on VOICE the mic moves it (audio mosaicing). A shake throws either towards
// chaos for a moment.
//
// This header is the contract between instrument.c (the UI task: params, sounds, REC, the cursor, the link, MIDI,
// the LED), inst_map.c (the UI task: listening to the units, the map, the cursor's catch), inst_engine.c (the audio
// task: recording into the corpus, the grains, the space), inst_seed.c (the sounds it starts with), inst_store.c
// (flash, the device only) and inst_scene.c (the screens).
#pragma once
#include "core.h"
#include "spsc.h"
#include "inst_kit.h"

// ---------------------------------------------------------------- params (the table in instrument.c, in this order)
enum {
    // GRAINS
    P_MODE, P_CHAOS, P_SIZE, P_SHAPE, P_PITCH, P_DETUNE, P_LEVEL,
    // DECOMPOSE
    P_SPEED, P_STUTTER, P_JUMPS, P_GAPS, P_BITE,
    // DIRT
    P_DRIVE, P_CRUSH, P_FEEDBACK, P_FBTIME, P_ROT,
    // ORDER
    P_STEP, P_STEPS, P_PATTERN, P_STACK, P_TUNE,
    // CHAOS
    P_DENSITY, P_SPREAD, P_SCATTER, P_REVERSE, P_SHAKE,
    // MAP
    P_CURSOR, P_MAPX, P_MAPY, P_TRANGE, P_MATCH,
    // TAKE
    P_SOURCE, P_TAKES, P_BANK, P_LOAD, P_SAVE, P_CLEAR,
    // SPACE
    P_TONE, P_SPACE, P_TAIL,
    // SONG
    P_TEMPO, P_KEY, P_SCALE, P_LINK, P_GROUP, P_RANGE, P_PEERS,
    // hidden: the material a sound brings (DAISY, AMEN): loaded when the sound is chosen over another built-in
    P_MATL,
    P_N
};
enum { MODE_DECOMP, MODE_MAP };
enum { SH_SOFT, SH_PLUCK, SH_SWELL, SH_SLICE, SH_HARD, NSHAPE };
enum { PAT_UP, PAT_DOWN, PAT_BOUNCE, PAT_PLAYED, PAT_SHUFFLE };
enum { TUNE_OFF, TUNE_ALL, TUNE_SCALE, TUNE_PENTA };
enum { CUR_TILT, CUR_VOICE, CUR_STICK };
enum { D_TIME, D_TONE, D_BRIGHT, D_LOUD, D_NOISE, ND };   // what a unit is measured by (the map's axes)
enum { SRC_MIC, SRC_USB, SRC_SELF, SRC_MIX };
enum { TK_ADD, TK_NEW };
// TAKE > BANK: the built-in sounds (made on the board), SIFT 1..4 (its own files), the Samplooler's slots 1..8 (load
// only). The store (inst_store.c) numbers its files from 0: store bank = BANK - NBUILTIN.
enum { BK_DAISY, BK_AMEN, BK_STARTER, NBUILTIN };
enum { MAT_KEEP, MAT_DAISY, MAT_AMEN, MAT_STARTER };
#define NBANK_OWN 4
#define NSTORE    (NBANK_OWN + 8)
#define NBANK     (NBUILTIN + NSTORE)

// ---------------------------------------------------------------- the corpus
#define UNIT       1024                    // frames per listened-to piece: 32 ms
#define CORP_SECS  20                      // a Samplooler take, so its slots load whole
#define NU_MAX     (CORP_SECS * AUDIO_SR / UNIT)   // 625
extern int16_t *corp;                      // PSRAM, corp_frames long; NULL when out of memory
extern int corp_frames, corp_units;        // corp_frames = corp_units * UNIT

// ---------------------------------------------------------------- recording (the UI asks, the audio task does it)
// A take starts on a unit boundary. It fades in over 2 ms, and when it stops it fades out over 2 ms and the rest
// of its last unit is filled with silence, so a grain never runs from one take into another with a click.
enum { RS_OFF, RS_ON, RS_TAIL };
extern volatile int      eng_rec_state;    // the audio task's
extern volatile uint32_t eng_wr;           // the write head, frames into corp
extern volatile uint32_t eng_take_seq;     // +1 when a take starts
extern volatile int      eng_take_units;   // units finished in the current take, the padded last one included
extern volatile int      eng_rec_full;     // the last take stopped because the corpus was full (TAKES NEW)
extern volatile float    eng_rec_level;    // what goes in, peak, decaying (for the screen)
void eng_rec_start(uint32_t from, bool fresh, int src);   // UI: from a unit boundary; fresh: the old grains fade first
void eng_rec_stop(void);                   // UI
void eng_hush(void);                       // UI: every grain fades out in 10 ms (the corpus is about to change)

// the follower's ear: the last LIVE_N samples of the mic, always written
#define LIVE_N 2048
extern float eng_live[LIVE_N];
extern volatile uint32_t eng_live_w;

// ---------------------------------------------------------------- the catch: what the audio task picks grains from
#define MAXC   24
#define MAXSEQ 32
typedef struct {
    uint32_t start;          // the frame in corp a grain from it starts at (its attack)
    float    semi_order;     // semitones on the order side: tuned to the key (or your voice), plus PITCH
    float    semi_free;      // on the chaos side: PITCH only
    float    gain;           // its take's level: sample * gain peaks near 0.5
    float    wcum;           // the chaos side's draw: cumulative weight, 0..1, the last one 1
    int16_t  unit;
    uint8_t  pitched, pad;
} cand_t;
typedef struct {
    cand_t  c[MAXC]; int n;                // nearest first
    uint8_t seq[MAXSEQ]; int nseq;         // the order side's pattern: indices into c
    float   spread;                        // frames a chaos grain may wander from its unit's attack
} cands_t;
void eng_cands_publish(const cands_t *c);  // UI, once a tick at most

// ---------------------------------------------------------------- controls (UI -> audio, plain stores)
extern volatile float eng_chaos;           // 0 order .. 1 chaos: the knob, plus the shake
extern volatile float eng_size;            // seconds
extern volatile int   eng_shape;
extern volatile float eng_density;         // grains a second at full chaos
extern volatile float eng_reverse;         // 0..1: the share of chaos grains played backwards
extern volatile float eng_scatter;         // semitones a chaos grain may be thrown
extern volatile float eng_detune;          // cents of drift per grain
extern volatile float eng_level;           // 0..1
extern volatile int   eng_div;             // order side: grid steps per beat (1 2 3 4 6 8)
extern volatile int   eng_stack;           // order side: grains per step
extern volatile float eng_gate;            // the cloud's level, smoothed: VOICE follows you, else 1
extern volatile float eng_tone;            // -1 dark .. 1 thin
extern volatile float eng_space;           // 0..1 into the space
extern volatile float eng_fb[4];           // the space's line gains (from TAIL)
extern volatile float eng_size_mul;        // the stick's x
extern volatile uint32_t eng_burst_req;    // +n: throw n grains at once (a shake)
extern volatile int   eng_mode;            // MODE_*
extern volatile float eng_pitch;           // DECOMPOSE: semitones (PITCH and the stick)
extern volatile float eng_speed;           // DECOMPOSE: the head, frames per frame (-2..3; 0 frozen)
extern volatile float eng_stutter, eng_jumps, eng_gaps;   // 0..2: STUTTER, JUMPS, GAPS over 50
extern volatile float eng_bite;            // 0..1: how violent the far end gets
extern volatile float eng_drive, eng_crush;   // 0..1
extern volatile float eng_fb_amt, eng_fb_time;   // FEEDBACK 0..0.95, FB TIME in seconds
extern volatile float eng_rot;             // 0..1: how much of the output is written back where the head reads
// the units as the audio task needs them (the UI writes them as it listens)
extern volatile uint8_t  eng_unit_ok[NU_MAX];    // part of the recording (the head plays it, silence too)
extern volatile float    eng_unit_gain[NU_MAX];  // its take's gain
extern volatile uint8_t  eng_cut[NU_MAX];        // a syllable (or a hit) starts in it ...
extern volatile uint16_t eng_cut_at[NU_MAX];     // ... this many frames in
extern volatile float    eng_head;               // DECOMPOSE: where the head is, frames into corp (the audio task's)
extern volatile float    eng_head_speed;         // the speed it moves at now (smoothed)

// ---------------------------------------------------------------- what the audio did (for the screen, MIDI, the tests)
#define NV 32
enum { GK_ORDER, GK_CHAOS, GK_BURST, GK_REPEAT };      // DECOMPOSE: ORDER = in step with the head, CHAOS = jumped
typedef struct { uint32_t frame, pos; int16_t unit; uint8_t kind, rev; uint16_t len; float semi, amp, dist; } spawn_t;
                                           // pos: where in corp it starts; len: frames; dist: frames from the head
bool eng_spawn_pop(spawn_t *s);            // UI: a grain started
extern volatile int16_t eng_vis_unit[NV];  // each voice now: its unit (-1 off) and its envelope
extern volatile float   eng_vis_env[NV];
extern volatile float   eng_out_lvl;       // peak, decaying
int   eng_voices_on(void);

void eng_init(void);                       // allocates the corpus (as long as memory allows), the space, the windows
void eng_audio(const float *in, float *out, int n);

// ---------------------------------------------------------------- the map (inst_map.c, UI task)
enum { U_EMPTY, U_SILENT, U_OK };
typedef struct {
    float    d[ND];          // TIME (the order it was heard), TONE (log2 Hz), BRIGHT (log2 Hz), LOUD (dB), NOISE (0..1)
    float    pitch;          // a MIDI note, or -1 without a pitch
    float    gain;           // its take's: sample * gain peaks near 0.5
    float    peak;           // |sample| peak
    float    sx, sy;         // where its dot is drawn now (eased towards the map), pixels
    uint16_t onset;          // frames from the unit's start to its attack
    uint16_t take;
    uint8_t  state, born;    // U_*; born: frames since it appeared (for the screen), saturating
} unit_t;
typedef struct { float d[ND]; float pitch, level; } desc_t;   // a piece of sound, measured
extern unit_t units[NU_MAX];
extern float  map_lo[ND], map_hi[ND];      // each measure's range over the playable units
void  map_init(void);
void  map_reset(void);                     // nothing on the map
int   map_take_begin(void);                // a new take: its number
void  map_forget(int u);                   // about to be written over
void  map_listen(int u, int take);         // measure a finished unit
void  map_relisten(int u);                 // measure it again where it is (ROT changed it)
void  map_take_end(int take);              // the take is whole: its level, and what in it is silence
void  map_update(void);                    // ranges, after units changed; hands the audio task the units
void  map_set_cuts(int take, const int32_t *frames, int n);   // a material's own cuts (corp frames) for its take
void  map_measure(const float *x, int n, desc_t *d);   // measure any piece of sound (the follower)
int   map_count(void);                     // playable units
float map_norm(int axis, float v);         // a measure on an axis, 0..1 over the corpus's range
float map_ux(int u);                       // the unit on the map, 0..1 (across, up), for the current axes
float map_uy(int u);
float map_tone_of(int u);                  // its tone as a MIDI note (pitch, or the brightness's)
void  map_set_axes(int ax, int ay);
int   map_axis_x(void), map_axis_y(void);
// the map's area on the screen
#define MAP_X0  2
#define MAP_X1  125
#define MAP_Y0  18
#define MAP_Y1  62
static inline float map_px(float x) { return (float)MAP_X0 + x * (float)(MAP_X1 - MAP_X0); }
static inline float map_py(float y) { return (float)MAP_Y1 - y * (float)(MAP_Y1 - MAP_Y0); }

// ---------------------------------------------------------------- the sounds it is made with
int  seed_render(int16_t *dst, int max);   // inst_seed.c: the starter (plucks, bells, voices, breath, clicks); frames
// inst_daisy.c: Daisy Bell's first two lines, sung by MANTRA's voice, a slice a tick
#define DAISY_NSYL   20
#define DAISY_NLINES 4
int   daisy_length(void);
bool  daisy_begin(int16_t *dst, int max);
int   daisy_step(int frames);              // renders up to that many; frames done so far
bool  daisy_busy(void);
int   daisy_cuts(int32_t *out, int max);   // where each syllable starts (its first consonant), frames into dst
const char *daisy_syl(int j);              // "DAI"
int   daisy_syl_line(int j);
int   daisy_line_first(int k);
const char *daisy_line(int k);             // "DAISY, DAISY,"
int32_t daisy_syl_at(int j);
// inst_amen.c (inst_kit.h): four takes of the Amen

// ---------------------------------------------------------------- flash (inst_store.c; the host keeps it in memory)
// banks here are the store's own: 0..3 SIFT 1..4, 4..11 the Samplooler's slots
bool store_init(void);
bool store_has(int bank);                  // a file is there
int  store_frames(int bank);               // its length, 0 if none
bool store_load_begin(int bank, int16_t *dst, int max);
int  store_load_step(void);                // reads a step; frames read so far, -1 when done (or failed: store_failed)
bool store_save_begin(int bank);           // then store_save_put() the units in order, store_save_end()
bool store_save_put(const int16_t *src, int n, float gain);   // n frames, scaled by gain to full scale
bool store_save_end(bool ok);
bool store_busy(void);
bool store_failed(void);
void store_mute(bool on);                  // the output's 20 ms fade round a flash write (the host: nothing)
bool store_silent(void);

// ---------------------------------------------------------------- the UI's state, for the screens (instrument.c)
enum { REC_IDLE, REC_HOLD, REC_LIVE };
enum { JOB_NONE, JOB_LOAD, JOB_SAVE, JOB_RENDER };
typedef struct {
    float cx, cy;            // the cursor, 0..1 (across, up)
    float ring;              // the chaos side's catch, pixels
    float chaos;             // the knob plus the shake, 0..1
    float shake;             // 0..1
    int   rec;               // REC_*
    float rec_s;             // seconds into the take
    int   pending;           // units heard but not yet listened to
    int   job;               // JOB_*
    float job_frac;
    int   peers;
    bool  voice;             // CURSOR VOICE and you are heard
    float bend, size_mul;    // the stick
    float bpm;
    int   nseq;              // the order side's pattern, for the constellation
    int16_t seq_unit[MAXSEQ];
    int   last_order_unit;   // the order grain that played last (-1)
    uint32_t last_order_frame;
    float axes_shown;        // seconds the axes' names stay up
    bool  empty;             // nothing to play
    int   mode;              // MODE_*
    float speed;             // DECOMPOSE: the head's speed asked for
    int   material;          // MAT_*: what the corpus holds (MAT_KEEP: your own)
    int   job_what;          // JOB_RENDER: BK_DAISY, BK_AMEN
    int   syl_heard;         // DAISY: the syllable the last grain came from (-1)
    uint32_t syl_frame;      // when
} sift_ui_t;
extern sift_ui_t S;
extern uint32_t sift_now;    // audio_frames() at this tick
float sift_beat_phase(void); // 0..1 within the flock's beat, now

// inst_scene.c
void scene_draw(const view_t *v);
bool scene_splash(int frame);
void scene_saver(uint32_t frame);
void scene_spawn(const spawn_t *s);        // a grain started: sparks
int  sift_daisy_syl_of(uint32_t pos);      // DAISY: the syllable a corpus frame falls in (-1 none, or not DAISY)
int  sift_timeline(int16_t *units, int max);   // DECOMPOSE: the recording's units in the order they play
void scene_shake(float amount);            // a shake: the dots scatter
