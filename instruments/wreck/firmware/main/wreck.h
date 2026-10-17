// WRECK: a breakcore machine that turns into a doom machine. One knob, the AXIS, runs from BREAKCORE (a break
// chopped, rolled, reversed and crushed at twice the flock's tempo) through HARDCORE (a gabber kick, blast beats, a
// guitar chugging on the kicks) and SLUDGE (half time, the break pitched down, fuzz riffs) to DRONE (a quarter of
// the tempo: one chord held with swells and feedback, the break slowed into thunder, a huge reverb).
//
// The parts. Each talks to the others only through this header and the four module headers it includes:
//
//   instrument.c   the host: INST (one param table built from the modules' tables, then its own), the sounds
//                  (each module adds its part of every sound), the song (tempo, key, chords) and the link, the
//                  AXIS and its zones and FEEL, the performance values from the controls (perf), knocks, REC
//                  (sampling: a tap stops and starts the machine), the mixer, the cost log, the LED
//   inst_kit.c     KIT  (inst_kit.h): the break. Built-in breaks are synthesized (an acoustic kit with its room and
//                  some grit, playing classic patterns) into a buffer, so they chop like any recording; a sampled
//                  break (the mic, the USB sound card, or WRECK itself) is cut into slices at its hits
//   inst_beat.c    BEAT (inst_beat.h): the chopper. It plays the break's slices on the machine's grid: the break as
//                  it is, the edits (rearranged, rolled, reversed, pitched, rushed), odd bars, the stutter, fills,
//                  the gabber kick; the step grid you draw (the knob held)
//   inst_amp.c     AMP  (inst_amp.h): a downtuned fuzz guitar and bass in one: chugs on the kicks, tremolo picking,
//                  slow riffs, one chord held as a drone with swells and feedback; in the song's key
//   inst_fx.c      FX   (inst_fx.h): on the drums: crush (bits, rate), drive, filter; on everything: the tape (wow
//                  and flutter, the drag, stop and start, the brake at a change of feel), the reverb
//   inst_scene.c   the screen (the host's): the header, the break chopped up at one end, the amp in smoke at the
//                  other; the splash, the screensaver
//
// Audio, per 64-frame block (the audio task, core 1):
//   beat_process()  -> drums, dsend         the chopped break, the kick
//   fx_drums(drums)                         crush, drive, filter
//   amp_process()   -> amp, asend
//   mix = drums * DRUMS + amp * AMP (+ the USB sound card's input while SOURCE is USB); send likewise
//   fx_master(mix, send)                    the tape, the reverb
//   kit_feed(mic / USB / mix)               while sampling
//
// Rules (docs/CORE.md has the rest): audio code never allocates, logs, blocks, takes a lock, uses double or calls libm
// per sample. Big sequential buffers (the break, the tape) in PSRAM (app_alloc); lines the audio reads all over every
// sample (reverb, comb and grain rings) in internal RAM (app_alloc_fast), at most 48 KB for all of WRECK. Every
// change of level or tone is smoothed. Host time per 64-frame block (tools/sim): KIT <= 0.5 us, BEAT <= 5, AMP <= 5,
// FX <= 5, all of WRECK <= 17 (the board is about 70 times slower: 17 us there is about 60 % of a 2 ms block).
// PSRAM (2 MB, the radio takes some): KIT <= 1100 KB, BEAT <= 128 KB, AMP <= 64 KB, FX <= 256 KB.
#pragma once
#include "core.h"
#include "dsp.h"

// ---------------------------------------------------------------- the song (instrument.c; read anywhere)
// As SING's: whole frames per beat, beat 0 at a local frame (the link moves it when another board leads), the harmony.
typedef struct {
    float    bpm;
    uint32_t fpb;              // frames per beat of the song (the flock's tempo)
    uint32_t origin;           // local frame of beat 0
    float    origin_frac;      // and the fraction of a frame after it (0..1)
    int      bar_beats;        // 4
    int      loop_beats;       // the session's loop (the first loop anywhere on the link), 0: none
    int      key;              // 0..11, C = 0
    const int8_t *scale; int scale_n;   // semitones above the key (7)
    uint16_t scale_mask;       // the scale's pitch classes (bit 0 = C)
    int      degree;           // the chord now, a degree of the scale (0 = I)
    int      chord[4]; int chord_n;     // its notes, MIDI, root first, root in C3..B3 (48..59)
    uint16_t chord_mask;       // its pitch classes
} song_t;
extern song_t song;

#define SONG_WRAP_BEATS 12288  // the song's beat count starts again after this many (a multiple of 48)
static inline int32_t wr_mod(int32_t a, int32_t m) { int32_t r = a % m; return r < 0 ? r + m : r; }
// The MIDI note of scale degree d (any integer; 7 = an octave up) above the key, octave o (4: from middle C).
static inline int scale_note(int d, int o)
{
    int n = song.scale_n > 0 ? song.scale_n : 7;
    int oct = d >= 0 ? d / n : -((-d + n - 1) / n), step = d - oct * n;
    int semis = song.scale ? song.scale[step] : step * 2;
    return 12 * (o + 1 + oct) + song.key + semis;
}

// ---------------------------------------------------------------- the machine's grid
// WRECK's tempo is the song's times 2^feel_q: x2 (BREAKCORE), x1, x1/2, x1/4 (DRONE). So it always lands on the
// flock's beat: at 100 BPM the breaks run at 200, the doom at 50 or 25. A STEP is a sixteenth of the machine's beat.
// Steps are counted from the song's beat 0 (wrapping with it), so any part can tell where it is from a frame alone:
// step k starts at origin + floor(k * fpb / (4 * 2^feel_q)). The host changes feel_q on a song beat; the FX's tape
// brake covers the jump (a part that keeps a voice ringing across it just carries on).
typedef struct {
    int      feel_q;           // 1, 0, -1, -2
    uint32_t fpb;              // the song's frames per beat when it was set
    uint32_t origin;           // the song's beat 0
} grid_t;
extern volatile grid_t wr_grid;   // the host writes it whole in the UI task between blocks (a sequence number guards it)
extern volatile uint32_t wr_grid_seq;   // odd while being written
static inline int     wr_steps_per_beat(int q) { return q >= 0 ? 4 << q : 4 >> (-q); }   // per song beat: 8, 4, 2, 1
// Positions are frames since the song's beat 0, 0 .. fpb * SONG_WRAP_BEATS - 1 (then it starts again; the step count
// is a multiple of 64 there, so every pattern starts again too). Integer arithmetic: exact for any fpb.
static inline int32_t wr_pos(const grid_t *g, uint32_t frame) { return wr_mod((int32_t)(frame - g->origin), (int32_t)(g->fpb * SONG_WRAP_BEATS)); }
static inline int32_t wr_step_of(const grid_t *g, int32_t pos) { return (int32_t)(((int64_t)pos * wr_steps_per_beat(g->feel_q)) / (int64_t)g->fpb); }
static inline int32_t wr_step_start(const grid_t *g, int32_t step)   // the position where a step starts
{
    int S = wr_steps_per_beat(g->feel_q);
    return (int32_t)(((int64_t)step * (int64_t)g->fpb + S - 1) / S);
}
static inline int32_t wr_step_at(const grid_t *g, uint32_t frame) { return wr_step_of(g, wr_pos(g, frame)); }
// frames from `frame` to the start of the next step (1 .. a step's length)
static inline int32_t wr_to_next_step(const grid_t *g, uint32_t frame)
{
    int32_t pos = wr_pos(g, frame);
    return wr_step_start(g, wr_step_of(g, pos) + 1) - pos;
}
static inline float   wr_step_frames(const grid_t *g) { return (float)g->fpb / (float)wr_steps_per_beat(g->feel_q); }
static inline void wr_grid_get(grid_t *g)   // a consistent copy (audio or UI task)
{
    for (;;) {
        uint32_t s = wr_grid_seq;
        if (s & 1u) continue;
        g->feel_q = wr_grid.feel_q; g->fpb = wr_grid.fpb; g->origin = wr_grid.origin;
        if (s == wr_grid_seq) return;
    }
}

// ---------------------------------------------------------------- the performance (the host, every tick)
// What the hands are doing now, for every part to read (UI task writes, any task reads; 32-bit fields).
enum { Z_BREAKCORE, Z_HARDCORE, Z_SLUDGE, Z_DRONE, Z_N };
typedef struct {
    float axis;                // the knob: 0 = BREAKCORE .. 1 = DRONE (smoothed: it glides over 0.15 s)
    int   zone;                // Z_*, from the axis (with a little hysteresis); 0..0.25..0.5..0.75..1
    float chaos;               // 0..1 the box tipped forward: more edits, more mess
    float drag;                // 0..1 tipped back: the tape slower (FX)
    float smear;               // 0..1 rolled left: reverse and smear (BEAT)
    float stutter;             // 0..1 rolled right: the stutter, shorter the further (BEAT)
    float roll;                // 0..1 stick up: a roll on the current slice, faster and rising (BEAT)
    float dive;                // 0..1 stick down: the break and the amp pitched down, up to an octave
    float crush;               // 0..1 stick left: bits and rate (FX)
    float drive;               // 0..1 stick right: drive (FX), and the amp's gain
    bool  running;             // false: stopped (REC tapped; the FX brake has run): BEAT and AMP make no new notes
} perf_t;
extern volatile perf_t perf;

// ---------------------------------------------------------------- a module (KIT, BEAT, AMP, FX)
// Each module has its own params (at most MOD_PARAMS_MAX, on its own menu pages), read as MP(module, i); the host
// builds INST's table from them (unused slots are hidden placeholders). Each sound (preset) is the host's part
// and each module's part for that sound, joined: a module's sounds[k] is "tag=value ..." of its own tags only.
#define MOD_PARAMS_MAX 24
enum { M_KIT, M_BEAT, M_AMP, M_FX, M_N };
#define WR_NSOUNDS 10          // the sounds, in this order (instrument.c has the list): WRECK SNARES DRILL GABBA GRIND
                               // BAROQUE WIZARD EARTH DRONE THUNDER
typedef struct {
    const char *name;          // "KIT"
    char        letter;        // every tag of its params starts with it: 'k' kit, 'b' beat, 'a' amp, 'f' fx
                               // (the host's: 's' song, 'm' mix, 'x' the axis and sampling, 'n' the link)
    const char *const *pages; int npages;   // its menu pages (param_t.group is 0 or 1)
    const param_t *params; int nparams;
    const char *const *sounds; // WR_NSOUNDS strings (may be "")
    void (*init)(void);        // boot, UI task, before audio: allocate here
    void (*tick)(const ctl_t *c);   // UI task, 100 Hz, after perf is set. c is the real controls; the host has used
                               // the knob, REC, the stick and the tilt already (perf): read them only for an editor
                               // of your own while you hold the knob (knob_grab). May be NULL
    void (*changed)(int p);    // its own param index; -1: all of them. May run before init
    void (*action)(int p);     // one of its PK_ACT params was run. May be NULL
} module_t;
extern const module_t mod_kit, mod_beat, mod_amp, mod_fx;
extern int mod_base[M_N];                       // where each module's params start in pv[]: m * MOD_PARAMS_MAX
#define MP(m, i) pv[mod_base[(m)] + (i)]
#define MP_SET(m, i, v) param_set(mod_base[(m)] + (i), (v))   // UI task: clamps, saves, tells changed(), sends the CC

#include "inst_kit.h"
#include "inst_beat.h"
#include "inst_amp.h"
#include "inst_fx.h"

// ---------------------------------------------------------------- asked of the host (instrument.c)
// Each module's cost: add the cycles your audio function took to prof_add(M_*, cycles) (the host measures them
// itself; this is only for a module that runs work outside its own audio call, e.g. KIT's render in the UI task).
float host_out_env(void);                       // the whole output's envelope (peak, decaying), any task
int   host_degree_at(uint32_t frame);           // the chord (a scale degree) at a frame: exact on a bar line, where
                                                // song.degree may lag a tick. UI or audio task
const char *host_zone_name(int z);              // "BREAKCORE" ...
void  scene_draw(const view_t *v);              // inst_scene.c: the whole frame
bool  scene_splash(int f);
void  scene_saver(uint32_t f);
