// MANTRA: a formant voice synth (and a little an effect). A seated Buddha sings: monks chanting OM on a deep drone,
// overtone singing (khoomei, sygyt, kargyraa), and "Daisy Bell" (Harry Dacre, 1892, public domain) sung the way the
// IBM 7094 sang it in 1961. Inspired by that and by the Delay Lama (AudioNerdz' singing-monk VST). Your hands are
// the mouth: tilt opens the jaw through the vowels, roll moves the pitch, turning the box flat (yaw) walks the
// overtone up the harmonics like a prayer wheel, pumping it is the breath, the stick bends and shakes, a knock
// strikes a singing bowl. The mic can go through the same mouth (the effect), or the voice can follow your hum.
//
// The parts (each talks to the others only through this header and the module headers it includes):
//
//   instrument.c   the host: INST (one param table from the modules' tables and its own), the sounds (each module
//                  adds its part), the song (tempo, key, chords) and the link, the hands (hands_t), REC (the core's
//                  looper), the mix, the FX bus (inst_fx.c: SING's echo and reverb), the cost log, the LED
//   inst_voice.c   VOICE (inst_voice.h): the formant voices: a glottal source through a vocal tract (cascade
//                  formants, a nasal branch, frication and bursts), phonemes with their transitions, the overtone
//                  whistle, the undertone, the choir, the 8-bit robot; and the effect (the mic through the mouth,
//                  its pitch tracked)
//   inst_score.c   SCORE (inst_score.h): what the voices sing in each mode, on the song's beat: OM and the chant
//                  (OM MANI PADME HUM), the throat-singing melodies, Daisy Bell (its lyrics as phonemes), the free
//                  vowel player; and the singing bowl (inst_bowl.c)
//   inst_drone.c   DRONE (inst_drone.h): the wall of amps under the Buddha: WRECK's AMP at its doom end (the same
//                  engine, so a MANTRA and a WRECK on the link make one sound): one power chord held in the song's
//                  key through a fuzz and a cab, swelling, the feedback singing over it, the sub, the hum, an organ
//   inst_scene.c   the screen (the host's): the Buddha seated, singing (mouth, breath, halo, sound rings), the monks,
//                  the harmonic ladder, the lyrics, the amps behind him; the splash, the screensaver
//   inst_fx.c      SING's input stage and FX bus (echo in time, reverb, shimmer), unchanged but for the include
//
// Audio per 64-frame block (core 1):
//   mv_process()  -> voice, vsend         the voices
//   bowl_process()-> voice, vsend         the bowl, added
//   mv_effect(mic)-> voice, vsend         the mic through the mouth, added (when the effect is up)
//   dr_process()  -> drone, dsend         the amps (MIX > DRONE)
//   fx_bus(send) + voice + drone -> out   the echo and the reverb; then the core: its looper and master
//
// Rules: the audio rules of docs/CORE.md: no allocation, logging, locks, double or per-sample libm in audio; every
// change smoothed; lines read all over every sample in internal RAM (app_alloc_fast), big sequential buffers in PSRAM
// (app_alloc). Host time per 64-frame block (tools/sim): VOICE <= 8 us with all six voices singing, SCORE + bowl
// <= 1, DRONE <= 5, all of MANTRA <= 20 (the board is about 70 times slower: 20 us there is about 70 % of a 2 ms block).
#pragma once
#include "core.h"
#include "dsp.h"

// ---------------------------------------------------------------- the song (instrument.c; read anywhere)
// As SING's and WRECK's: whole frames per beat, beat 0 at a local frame (the link moves it), the harmony.
typedef struct {
    float    bpm;
    uint32_t fpb;              // frames per beat
    uint32_t origin;           // local frame of beat 0
    float    origin_frac;
    int      bar_beats;        // 4 (Daisy Bell is in 3: the score counts its own bars)
    int      loop_beats;       // the session's loop (the first loop anywhere on the link), 0: none
    int      key;              // 0..11, C = 0
    const int8_t *scale; int scale_n;
    uint16_t scale_mask;       // the scale's pitch classes (bit 0 = C)
    int      degree;           // the chord now, a degree of the scale (0 = I)
    int      chord[4]; int chord_n;   // its notes, MIDI, root in C3..B3
    uint16_t chord_mask;
} song_t;
extern song_t song;
#define SONG_WRAP_BEATS 12288
static inline int32_t mn_mod(int32_t a, int32_t m) { int32_t r = a % m; return r < 0 ? r + m : r; }
// beats since the song's beat 0 at a frame (wraps at SONG_WRAP_BEATS; fine for the UI task's sequencing)
static inline float song_beats_at(uint32_t frame)
{
    uint32_t fpb = song.fpb ? song.fpb : 19200;
    return (float)mn_mod((int32_t)(frame - song.origin), (int32_t)(fpb * SONG_WRAP_BEATS)) / (float)fpb;
}
// the audio frame where song beat b falls next, at or after `from` (b counted as song_beats_at counts)
static inline uint32_t song_frame_of(float b, uint32_t from)
{
    uint32_t fpb = song.fpb ? song.fpb : 19200;
    float now = song_beats_at(from), d = b - now;
    while (d < 0.0f) d += (float)SONG_WRAP_BEATS;
    return from + (uint32_t)(d * (float)fpb + 0.5f);
}
static inline int scale_note(int d, int o)       // MIDI note of scale degree d above the key, octave o (4: middle C)
{
    int n = song.scale_n > 0 ? song.scale_n : 7;
    int oct = d >= 0 ? d / n : -((-d + n - 1) / n), step = d - oct * n;
    int semis = song.scale ? song.scale[step] : step * 2;
    return 12 * (o + 1 + oct) + song.key + semis;
}

// ---------------------------------------------------------------- the hands (the host, every tick; read anywhere)
typedef struct {
    float jaw;                 // 0..1 tipped forward .. back: the mouth through OO OH AH EH EE (0.5 flat = AH)
    float roll;                // -1..1 rolled left .. right (the pitch, by default: up to an octave either way)
    float yaw;                 // the box turned flat on the table, in turns since the last centre (prayer wheel)
    float yaw_rate;            // turns a second now
    float pump;                // 0..1 how hard it is being pumped up and down (the breath)
    bool  pumped;              // a pump's stroke this tick (a syllable, an accent)
    float jx, jy;              // the stick -1..1 (x: bend, y: vibrato up / the hum (mouth shut) down)
    bool  knocked; float knock; // a knock this tick, and how hard (0..1): the bowl
    bool  running;             // the score plays (REC is the looper's; the knob changes the sound)
} hands_t;
extern volatile hands_t hands;
// The knob: the BLEND from the sound's chant (0) to Daisy Bell (1), smoothed over 0.2 s (the host). The score reads it:
// 0..0.25 the chant alone; 0.25..0.5 its voices hum Daisy's melody (no words) over the drone (THROAT: the whistle plays
// it on the drone's harmonics); 0.5..0.75 the choir sings Daisy's words over the chant's drone; 0.75..1 Daisy alone,
// one voice, the waltz under it. Daisy runs on the song's beat all along, so moving the knob never restarts the tune;
// a new zone takes over at Daisy's next bar line.
extern volatile float mn_blend;

// ---------------------------------------------------------------- modules (VOICE, SCORE, DRONE), as WRECK's
#define MOD_PARAMS_MAX 24
enum { M_VOICE, M_SCORE, M_DRONE, M_N };
#define MN_NSOUNDS 13          // the sounds, in this order: OM, MONKS, OM MANI, KHOOMEI, SYGYT, KARGYRAA, DAISY,
                               // DAISY 8-BIT, VOWELS, HUM ALONG, SUNN, DOOM DAISY, ORGAN
typedef struct {
    const char *name;          // "VOICE"
    char        letter;        // its tags' first letter: 'v' voice, 'c' score (chant), 'd' drone; the host's: 's' song, 'm' mix,
                               // 'f' fx, 'n' link, 'h' hands, 'e' effect
    const char *const *pages; int npages;
    const param_t *params; int nparams;
    const char *const *sounds; // MN_NSOUNDS strings of its own tags ("" for none)
    void (*init)(void);
    void (*tick)(const ctl_t *c);   // UI task, 100 Hz, after the hands are set. May be NULL
    void (*changed)(int p);
    void (*action)(int p);     // may be NULL
} module_t;
extern const module_t mod_voice, mod_score, mod_drone;
extern int mod_base[M_N];
#define MP(m, i) pv[mod_base[(m)] + (i)]
#define MP_SET(m, i, v) param_set(mod_base[(m)] + (i), (v))

#include "inst_voice.h"
#include "inst_score.h"
#include "inst_drone.h"

// ---------------------------------------------------------------- inst_fx.c (SING's, unchanged)
typedef struct {
    float gate;                // 0..1: how hard the gate closes on quiet (0 off)
    float comp;                // 0..1: how much the voice is evened out
    float guard;               // 0..1: the speaker guard
    float deess;               // 0..1
} fx_in_params_t;
typedef struct {
    float echo;                // 0..1 how loud the echo returns
    float echo_frames;         // the echo's time in frames
    float echo_fb;             // 0..0.95 repeats
    float echo_tone;           // 0..1 dark .. bright
    float echo_duck;           // 0..1: the echo stays down while the voice sings
    float reverb;              // 0..1 how loud
    float reverb_s;            // 0.3..12 seconds
    float reverb_tone;         // 0..1
    float shimmer;             // 0..1 an octave up fed back into the reverb
    float throw_;              // 0..1 the echo opens and its repeats grow
} fx_bus_params_t;
void  fx_init(void);
void  fx_set_input(const fx_in_params_t *p);
void  fx_set_bus(const fx_bus_params_t *p);
void  fx_input(float *io, int n, float out_env);
void  fx_bus(const float *send, float *io, int n);
float fx_gate_open(void);
float fx_guard_cut(void);
int   fx_echo_taps(float *lv, int max);

// ---------------------------------------------------------------- the host, for the others
float host_out_env(void);                        // the output's peak envelope
const char *host_sound_name(void);               // the sound now ("OM" ...)
void  scene_draw(const view_t *v);
bool  scene_splash(int f);
void  scene_saver(uint32_t f);
