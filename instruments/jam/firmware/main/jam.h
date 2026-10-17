// JAM: KNOCK, HUFF and HOWL on one firmware as layers, plus a BASS. One layer is live at a time (the knob
// picks it): the mic, the tilt, the stick and REC are its. The others keep playing what they recorded.
//
//   instrument.c     the host: INST (one param table built from the layers' tables), the song (tempo, loop,
//                    key, chord), the layer bar, the audio tracks of BREATH and HOWL, the mixer, the shared FX
//                    bus (echo + reverb), the sounds (whole-jam presets), hints, splash
//   layer_drums.c    KNOCK as a layer (owns its event loop)
//   layer_bass.c     the bass (owns its event loop)
//   layer_breath.c   HUFF as a layer (the host records it into an audio track)
//   layer_howl.c     HOWL as a layer (the host records it into an audio track)
//
// The layers talk to the host only through this header. Each layer file is owned by one builder; the host
// owns jam.h and instrument.c (changes to jam.h are announced in JAM_NOTES.md).
#pragma once
#include "core.h"
#include "dsp.h"

enum { L_DRUMS, L_BASS, L_BREATH, L_HOWL, L_N };

// ---------------------------------------------------------------- the song
// Written by the host in the UI task; read anywhere (fields are 32-bit; a change lands within a block).
// Before any loop exists the tempo is SONG > TEMPO and the beat runs from boot. The first loop recorded on
// any layer fixes the loop: its length, and a whole number of beats (the tempo that fits between 75 and 165
// BPM). Every later loop on every layer has that length.
typedef struct {
    float    bpm;              // beats per minute
    uint32_t loop_len;         // frames in the loop; 0: no loop yet
    uint32_t loop_start;       // the audio frame where the loop's position 0 fell (beats count from here too)
    int      beats;            // beats in the loop; 0 without a loop
    int      key;              // 0..11, C = 0
    const int8_t *scale;       // semitones above the key, scale_n of them (7 for a major scale)
    int      scale_n;
    int      degree;           // the chord now: a degree of the scale, 0 = I
    int      chord[5];         // its notes, MIDI, root first, root between 36 and 47 (C2..B2)
    int      chord_n;
    uint32_t bar;              // counts bars (4 beats) from loop position 0; progressions step on it
    uint64_t kicks;            // the drums layer's kicks in the loop as sixteenths (bit i = sixteenth i), for the bass
} song_t;
extern song_t song;

// Beats since loop position 0 (or since boot without a loop), with the fraction. Any task.
static inline float song_beat(uint32_t frame)
{
    if (song.loop_len && song.beats) {
        uint32_t ph = (frame - song.loop_start) % song.loop_len;
        return (float)ph * (float)song.beats / (float)song.loop_len;
    }
    return (float)frame * song.bpm / (60.0f * (float)AUDIO_SR);
}
// Frames per beat now.
static inline float song_frames_per_beat(void) { return 60.0f * (float)AUDIO_SR / (song.bpm > 1.0f ? song.bpm : 1.0f); }
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
    char        letter;          // every tag of its params starts with it: 'd' drums, 'b' bass, 'h' breath, 'w' howl
                                 // (the host uses 's' for the song and 'm' for the mix)
    const char *const *pages;    // its two menu pages ("DRUMS", "DRUM KIT"); its param_t.group is 0 or 1
    int         npages;          // 2
    const param_t *params;       // its params (at most LAYER_PARAMS_MAX), read as LP(layer, i)
    int         nparams;
    bool        events;          // true: it loops itself (an event loop: DRUMS, BASS); while live it gets REC in ctl.
                                 // false: the host records what it plays into the layer's audio track (BREATH, HOWL)
    void (*init)(void);          // boot, UI task, before audio(); allocate here (PSRAM budget below)
    // Audio task. ADD what it plays into dry[] and what it sends to the shared echo/reverb into send[] (both come
    // zeroed). mic is the mic (high-passed, gained). live: the mic and the controls are this layer's.
    void (*audio)(const float *mic, float *dry, float *send, int n, bool live);
    // UI task, 100 Hz, every layer. The live layer gets the real controls. A layer that is not live gets
    // ctl with rec, tilt, spin and stick neutral and has_imu / has_joy false: it keeps what they last set.
    void (*tick)(const ctl_t *c, bool live);
    void (*draw)(const view_t *v);   // its scene while live: rows 16..63 (the host draws the header band, rows 0..15)
    void (*icon)(int x, int y, bool lit);   // 12x9 for the layer bar (lit: drawn white on black by the host)
    void (*changed)(int p);      // its own param index, -1: all of them. May run before init()
    void (*clear)(void);         // events layers: forget the loop (the host clears audio tracks itself)
    void (*midi_in)(uint8_t status, uint8_t d1, uint8_t d2);   // channel messages while live (may be NULL)
    float       activity;        // unused by the host; a layer may keep its own
    void (*action)(int p);       // one of its PK_ACT params was run from the menu (its own index). May be NULL
    int  (*state)(void);         // events layers: its loop for the layer bar, a LOOP_* below. May be NULL
} layer_t;
enum { LOOP_NONE, LOOP_ON, LOOP_OFF, LOOP_REC };   // nothing recorded; plays; recorded but muted; recording now
extern const layer_t layer_drums, layer_bass, layer_breath, layer_howl;

#define LAYER_PARAMS_MAX 16
extern int layer_base[L_N];      // where each layer's params start in pv[]: layer * LAYER_PARAMS_MAX
#define LP(layer, i) pv[layer_base[(layer)] + (i)]
// A layer changing one of its own params (UI task): clamps, saves, tells changed(), sends the CC.
#define LP_SET(layer, i, v) param_set(layer_base[(layer)] + (i), (v))

// ---------------------------------------------------------------- asked of the host
int   host_live(void);                         // which layer is live
// A layer's loop length request: an events layer that records the very first loop calls this when REC is
// let go, with the length it held (frames from its start). The host fixes song.loop_len/beats/bpm/loop_start
// (rounding the length to whole beats) and returns the loop's frames; layers then quantize to it.
uint32_t host_first_loop(uint32_t start_frame, uint32_t held_frames);
// The chord now, from a layer (the BASS's live chord): a scale degree, 0 = I; -1 hands it back to the
// progression (SONG > PROGRESSION). Any task; the host applies it at its next tick.
void  host_chord_override(int degree);
// The whole jam's output (after the mixer and the FX, before the master), a peak envelope updated every block.
// A layer that listens to the mic uses it to tell what the speaker plays from what is played to it.
extern volatile float host_out_env;
// The whole jam's previous block (AUDIO_BLOCK samples, before the master): what the speaker played into the mic.
const float *host_last_out(void);
// MIDI: each layer on its own channel (DRUMS 10, BREATH the base channel, BASS base + 1, HOWL base + 2).
void  host_midi_note(int layer, int note, int vel);
void  host_midi_cc(int layer, int cc, int val);

// ---------------------------------------------------------------- budgets (the device is one S3 at 240 MHz)
// Host time per 64-frame block (tools/sim, this machine), worst case of the whole jam (drums loop + bass +
// one live layer + the two audio tracks + FX): <= 25 us. Per layer: DRUMS <= 9, BASS <= 3, BREATH live <= 7,
// HOWL live <= 6, a layer that is not live and has nothing to do: ~0 (BREATH and HOWL fade out and stop
// unless their own HOLD/FREEZE keeps them going).
// PSRAM (2 MB): the host's two audio tracks 2 x 512 KB (LOOP_MAX 8 s); DRUMS <= 320 KB (knock snippets);
// BASS <= 32 KB; BREATH <= 96 KB; HOWL <= 64 KB; the FX bus <= 160 KB. Internal RAM: keep big tables in PSRAM.
#define JAM_LOOP_MAX_S 8
