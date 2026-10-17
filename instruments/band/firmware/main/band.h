// BAND: a band on one board, as layers. DRUMS (knock, or draw a 4 x 8 grid), BASS, LEAD, KEYS (piano, electric
// piano, organ), BREATH (ocarina and friends) and HOWL (tuned feedback). One layer is live at a time (the knob
// picks it): the mic, the tilt, the stick and REC are its. The others keep playing what they recorded or what
// they play by themselves (the grid, the bass's FOLLOW, the keys' COMP, the lead's ARP).
//
// BAND grew out of JAM (../jam): the host and DRUMS, BASS, BREATH and HOWL are JAM's, with the song on a whole
// number of frames per beat and on the link (link.h): two boards (a BAND and a SING, or any that use the link)
// share the tempo, the bar, the loop's length and the harmony.
//
//   instrument.c     the host: INST (one param table built from the layers' tables), the song (tempo, bar,
//                    loop, key, chord) and the link, the layer bar, the audio tracks of BREATH and HOWL, the mixer,
//                    the shared FX bus (echo + reverb), the sounds (whole-band presets), hints, splash
//   layer_drums.c    KNOCK as a layer (owns its event loop) and the GRID: 4 drums x 8 steps, drawn with the knob
//   layer_bass.c     the bass (owns its event loop)
//   layer_lead.c     a lead synth: hum it, tilt it or let it arpeggiate (owns its event loop)
//   layer_keys.c     piano, electric piano, organ, pad, clav: chords on the song (owns its event loop)
//   layer_breath.c   HUFF as a layer (the host records it into an audio track)
//   layer_howl.c     HOWL as a layer (the host records it into an audio track)
//
// The layers talk to the host only through this header. Changes are announced in BAND_NOTES.md.
#pragma once
#include "core.h"
#include "dsp.h"

enum { L_DRUMS, L_BASS, L_LEAD, L_KEYS, L_BREATH, L_HOWL, L_N };

// ---------------------------------------------------------------- the song
// Written by the host in the UI task; read anywhere (fields are 32-bit; a change lands within a block).
//
// Time: every beat is `fpb` frames (a whole number; 60 * 32000 / BPM). Beat 0 of the session is at local frame
// `origin` (the link moves it when another board leads; it never moves while a loop plays unless the session
// changes). A loop, once there is one, is `beats` beats long (a whole number of bars) and starts on a beat:
// `loop_start` is a frame of beat 0 at or before now, so (frame - loop_start) % loop_len is the position in it.
// Every loop on every layer has that length (a board linked to this one may loop a multiple of it).
//
// Before any loop the tempo is SONG > TEMPO (or the session's, when linked). The first loop recorded anywhere
// (on any layer, or on a linked board) fixes the loop's length: whole bars at the tempo (SONG > FIRST LOOP:
// FITS TEMPO), or, with SETS TEMPO and nothing else in time, the length held sets the tempo as JAM did.
typedef struct {
    float    bpm;              // beats per minute (60 * 32000 / fpb)
    uint32_t fpb;              // frames per beat
    uint32_t origin;           // the local frame of beat 0
    uint32_t loop_len;         // frames in the loop (beats * fpb); 0: no loop yet
    uint32_t loop_start;       // a frame where the loop's position 0 fell (a beat 0 of the session), at or before now
    int      beats;            // beats in the loop; 0 without a loop
    int      bar_beats;        // beats per bar (4)
    int      key;              // 0..11, C = 0
    const int8_t *scale;       // semitones above the key, scale_n of them (7 for a major scale)
    int      scale_n;
    int      degree;           // the chord now: a degree of the scale, 0 = I
    int      chord[5];         // its notes, MIDI, root first, root between 36 and 47 (C2..B2)
    int      chord_n;
    uint32_t bar;              // counts bars from loop position 0 (from beat 0 without a loop); progressions step on it
    uint64_t kicks;            // the drums' kicks in the loop (or the grid's bar) as sixteenths, for the bass
} song_t;
extern song_t song;

#define SONG_WRAP_BEATS 12288  // song_beat() counts up to this and starts again (a multiple of 48: every bar and progression lines up)
static inline int32_t song_mod(int32_t a, int32_t m) { int32_t r = a % m; return r < 0 ? r + m : r; }
// Beats since loop position 0 (since beat 0 without a loop), with the fraction. Any task.
static inline float song_beat(uint32_t frame)
{
    if (song.loop_len && song.beats) {
        int32_t ph = song_mod((int32_t)(frame - song.loop_start), (int32_t)song.loop_len);
        return (float)ph * (float)song.beats / (float)song.loop_len;
    }
    uint32_t fpb = song.fpb ? song.fpb : 19200;
    return (float)song_mod((int32_t)(frame - song.origin), (int32_t)(fpb * SONG_WRAP_BEATS)) / (float)fpb;
}
// Where a frame falls in the loop, 0..loop_len-1 (0 without a loop). Any task.
static inline uint32_t song_pos(uint32_t frame)
{
    return song.loop_len ? (uint32_t)song_mod((int32_t)(frame - song.loop_start), (int32_t)song.loop_len) : 0;
}
// Frames per beat now.
static inline float song_frames_per_beat(void) { return (float)(song.fpb ? song.fpb : 19200); }
// The MIDI note of scale degree d (any integer; 7 = an octave up in a 7-note scale) above the key in octave o
// (o = 4: from middle C).
static inline int scale_note(int d, int o)
{
    int n = song.scale_n > 0 ? song.scale_n : 7;
    int oct = d >= 0 ? d / n : -((-d + n - 1) / n), step = d - oct * n;
    int semis = song.scale ? song.scale[step] : step * 2;
    return 12 * (o + 1 + oct) + song.key + semis;
}

// ---------------------------------------------------------------- a layer
typedef struct {
    const char *name;            // "DRUMS": the layer bar, the menus
    char        letter;          // every tag of its params starts with it: 'd' drums, 'b' bass, 'l' lead, 'k' keys,
                                 // 'h' breath, 'w' howl (the host uses 's' for the song, 'm' for the mix, 'f' fx,
                                 // 'n' the link)
    const char *const *pages;    // its two menu pages ("DRUMS", "DRUM KIT"); its param_t.group is 0 or 1
    int         npages;          // 2
    const param_t *params;       // its params (at most LAYER_PARAMS_MAX), read as LP(layer, i)
    int         nparams;
    bool        events;          // true: it loops itself (an event loop: DRUMS, BASS, LEAD, KEYS); while live it gets
                                 // REC in ctl. false: the host records what it plays into the layer's audio track
    void (*init)(void);          // boot, UI task, before audio(); allocate here (PSRAM budget below)
    // Audio task. ADD what it plays into dry[] and what it sends to the shared echo/reverb into send[] (both come
    // zeroed). mic is the mic (high-passed, gained). live: the mic and the controls are this layer's.
    void (*audio)(const float *mic, float *dry, float *send, int n, bool live);
    // UI task, 100 Hz, every layer. The live layer gets the real controls. A layer that is not live gets
    // ctl with rec, tilt, spin, stick and knob neutral and has_imu / has_joy false: it keeps what they last set.
    void (*tick)(const ctl_t *c, bool live);
    void (*draw)(const view_t *v);   // its scene while live: rows 16..63 (the host draws the header band, rows 0..15)
    void (*icon)(int x, int y, bool lit);   // 12x9 for the layer bar (lit: drawn white on black by the host)
    void (*changed)(int p);      // its own param index, -1: all of them. May run before init()
    void (*clear)(void);         // events layers: forget the loop (the host clears audio tracks itself)
    void (*midi_in)(uint8_t status, uint8_t d1, uint8_t d2);   // channel messages while live (may be NULL)
    float       activity;        // unused by the host; a layer may keep its own
    void (*action)(int p);       // one of its PK_ACT params was run from the menu (its own index). May be NULL
    int  (*state)(void);         // events layers: its loop for the layer bar, a LOOP_* below. May be NULL
    // The knob held 0.6 s in play while this layer is live: true if the layer used it (an editor of its own, which
    // then takes the knob with knob_grab() and gets it in ctl.knob*; it gives it back with knob_grab(false)).
    // false or NULL: the core holds the motion as usual. While a layer holds the knob it stays live.
    bool (*hold)(void);
    bool (*playing)(void);       // it plays in time by itself now (the grid, FOLLOW, COMP, ARP): the tempo is heard. May be NULL
} layer_t;
enum { LOOP_NONE, LOOP_ON, LOOP_OFF, LOOP_REC };   // nothing recorded; plays; recorded but muted; recording now
extern const layer_t layer_drums, layer_bass, layer_lead, layer_keys, layer_breath, layer_howl;

#define LAYER_PARAMS_MAX 26
extern int layer_base[L_N];      // where each layer's params start in pv[]: layer * LAYER_PARAMS_MAX
#define LP(layer, i) pv[layer_base[(layer)] + (i)]
// A layer changing one of its own params (UI task): clamps, saves, tells changed(), sends the CC.
#define LP_SET(layer, i, v) param_set(layer_base[(layer)] + (i), (v))

// ---------------------------------------------------------------- asked of the host
int   host_live(void);                         // which layer is live
// The loop's length: an events layer that records while there is no loop calls this when REC is let go, with the
// frame its take started at and how long REC was held. The host makes the loop (whole bars at the tempo, or with
// SETS TEMPO the length held sets the tempo), sets song.loop_len, beats and loop_start, and returns loop_len.
// loop_start is on the session's beat grid, NOT necessarily the take's start: a layer places what it recorded by
// its absolute frames ((frame - loop_start) mod loop_len, song_pos()), never by its offset from the press.
uint32_t host_first_loop(uint32_t start_frame, uint32_t held_frames);
// The chord now, from a layer (the BASS's live chord): a scale degree, 0 = I; -1 hands it back to the
// progression (SONG > PROGRESSION). Any task; the host applies it at its next tick (and the link shares it).
void  host_chord_override(int degree);
// The chord's degree at a given frame (the progression, HOLD, or a chord played live): exact on a bar line, where
// song.degree may still be the last bar's for up to a tick. UI or audio task.
int   host_degree_at(uint32_t frame);
// The whole band's output (after the mixer and the FX, before the master), a peak envelope updated every block.
// A layer that listens to the mic uses it to tell what the speaker plays from what is played to it.
extern volatile float host_out_env;
// The whole band's previous block (AUDIO_BLOCK samples, before the master): what the speaker played into the mic.
const float *host_last_out(void);
// MIDI: each layer on its own channel (DRUMS 10; BREATH the base channel, BASS base + 1, HOWL base + 2,
// LEAD base + 3, KEYS base + 4).
void  host_midi_note(int layer, int note, int vel);
void  host_midi_cc(int layer, int cc, int val);

// ---------------------------------------------------------------- budgets (the device is one S3 at 240 MHz)
// Host time per 64-frame block (tools/sim, this machine), worst case of the whole band (drums loop and grid +
// bass + lead + keys + one live layer + the two audio tracks + FX): <= 30 us (the board runs about 70x slower:
// 2.1 ms of a 2 ms block would be too much; 30 us is about 75 %). Per layer: DRUMS <= 9, BASS <= 3, LEAD <= 3,
// KEYS <= 5 (a full chord ringing out over the last one), BREATH live <= 7, HOWL live <= 6; a layer that has nothing
// to do: ~0.
// PSRAM (2 MB): the host's two audio tracks 2 x 512 KB (LOOP_MAX 8 s); DRUMS <= 320 KB (knock snippets);
// BASS <= 32 KB; LEAD <= 32 KB; KEYS <= 64 KB; BREATH <= 96 KB; HOWL <= 64 KB; the FX bus <= 160 KB.
// Internal RAM: keep big tables in PSRAM; the radio (link) needs about 60 KB of it.
#define JAM_LOOP_MAX_S 8
