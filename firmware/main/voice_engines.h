// SING's voice engines (instruments/sing in this repo), on the Samplooler's input: pitch tracking and the voices
// (autotune, harmonies, choir, pitch and formant shift), a vocoder, the input stage (gate, compressor, de-esser, the
// speaker guard) and an FX bus (echo, reverb, shimmer). sing_voice.c, sing_vocoder.c and sing_fx.c are SING's
// inst_voice.c, inst_vocoder.c and inst_fx.c with their include and the FX bus's names (vfx_*: fx.c is the
// Samplooler's own roll effects) changed; keep them in step with SING's. voice.c drives them (voice.h).
#pragma once
#include "voice_dsp.h"

// ---------------------------------------------------------------- sing_voice.c
// The voice engine. Analysis: the mic's pitch (YIN on a decimated copy) and its glottal pulses (pitch marks).
// Synthesis: TD-PSOLA. Each voice places grains two periods wide, one per output period, each taken from the
// pulse nearest in time; a voice's pitch is the spacing of its grains, its formants the rate the grain is read
// at. Unvoiced sound (s, t, breath) keeps the input's own grain spacing, so consonants stay as sung.
// The voices come out of a fixed delay (VOICE_LATENCY_MS: a grain needs a period after its pulse); the
// dry voice does not, so the host delays it the same when it mixes them together (voice_dry_delayed()).
#define VOICE_EXTRA     7      // voices besides the lead
#define VOICE_LATENCY_MS 16
enum { HARM_OFF, HARM_SCALE, HARM_CHORD, HARM_SEMIS };      // harmony intervals: in scale steps, the chord's nearest tones, semitones
enum { CHOIR_UNISON, CHOIR_OCTAVES, CHOIR_CHORD };
typedef struct {
    // the lead
    uint16_t scale_mask;       // the pitch classes the lead may land on (bit 0 = C); 0 = every semitone
    float    tune;             // 0..1: how far it is pulled to the nearest allowed note (0 = not tuned)
    float    speed_ms;         // how fast it gets there: 0 = at once (the hard robot step) .. 400 (gentle)
    float    keep_vib;         // 0..1: how much of the singer's own vibrato and slides survive the tuning
    float    shift;            // semitones added to the lead (and to every extra voice), -24..24
    float    formant;          // semitones the formants move, -12 (bigger) .. 12 (smaller), the pitch kept
    float    lead_gain;        // 0..1
    // the extra voices: up to three harmonies, then the choir in what is left
    int      harm_mode;        // HARM_*
    int8_t   harm[3];          // intervals: scale steps (2 = a third above), semitones, or (CHORD) 1 = next chord tone up,
                               // -1 next down, 0 = none
    float    harm_gain[3];     // 0..1 each
    uint16_t chord_mask;       // the chord's pitch classes now
    float    choir;            // 0..1: how many voices (0 none, 1 all that are free) and how loud
    int      choir_mode;       // CHOIR_*
    float    choir_spread;     // cents of detune between choir voices, 0..40
    float    choir_time_ms;    // how far apart they start, 0..60 (singers never start together)
    float    humanize;         // 0..1: slow random drift of the extra voices' pitch and timing
} voice_params_t;
typedef struct {
    bool  voiced;              // a pitch is heard
    float f0;                  // Hz (0: none)
    float level;               // the input's envelope, 0..1
    float in_note;             // MIDI note sung, with the fraction (0: none)
    float target;              // the note the lead is pulled to (MIDI)
    float out_note;            // the note the lead sings now
    int   nvoices;             // extra voices sounding
    float vnote[VOICE_EXTRA];  // their notes
    float vlevel[VOICE_EXTRA]; // their levels 0..1
} voice_state_t;
void  voice_init(void);                                    // UI task, at init(): allocates
void  voice_set(const voice_params_t *p);                  // UI task: picked up at the next block, every change smoothed
// Audio task. in: the voice after vfx_input(). lead: the lead voice; extra: every extra voice summed. Both start
// VOICE_LATENCY_MS late.
void  voice_process(const float *in, float *lead, float *extra, int n);
void  voice_state(voice_state_t *s);                       // any task
// The input delayed by the same latency as the voices (call after voice_process for the same block).
const float *voice_dry_delayed(void);

// ---------------------------------------------------------------- sing_vocoder.c
// A channel vocoder: the voice (the modulator) is split into bands and its envelope in each band shapes the same
// band of a carrier: a synth playing the song's chord, the note the voice is tuned to (a robot that sings in
// tune), both, or noise (a whisper). The voice's own s and t sounds go on top so it stays understandable.
enum { VOC_CHORD, VOC_NOTE, VOC_CHORD_NOTE, VOC_NOISE };
typedef struct {
    int      carrier;          // VOC_*
    int      chord[4]; int chord_n;   // MIDI notes of the chord (VOC_CHORD, VOC_CHORD_NOTE)
    float    note;             // MIDI note of the tuned voice (VOC_NOTE, VOC_CHORD_NOTE); 0: none (silent for VOC_NOTE)
    int      octave;           // -2..2 octaves for the carrier
    float    bands;            // 8..20
    float    sharp;            // 0..1: wide and smooth .. narrow and ringing bands
    float    release_ms;       // 5..300: how long a band rings after the voice leaves it
    float    sibilance;        // 0..1: the voice's s and t on top
    float    bright;           // 0..1: the carrier's tone (dark saw .. bright buzz)
    float    detune;           // cents between the carrier's oscillators, 0..30
} voc_params_t;
void  vocoder_init(void);
void  vocoder_set(const voc_params_t *p);                  // UI task
void  vocoder_process(const float *mod, float *out, int n);  // audio task: writes out
int   vocoder_bands(float *lv, int max);                    // any task: each band's level now (0..1), for the screen

// ---------------------------------------------------------------- sing_fx.c
// The input stage and the FX bus.
typedef struct {
    float gate;                // 0..1: how hard the gate closes on quiet (0 off)
    float comp;                // 0..1: how much the voice is evened out (a 3:1 compressor at 1, make-up gain automatic)
    float guard;               // 0..1: the speaker guard: how hard a ringing tone that keeps growing is cut
    float deess;               // 0..1: sharp s sounds tamed
} fx_in_params_t;
typedef struct {
    float echo;                // 0..1 how loud the echo returns
    float echo_frames;         // the echo's time in frames (the host sets it from the song: 1/8, 1/4 dotted ...)
    float echo_fb;             // 0..0.95 repeats
    float echo_tone;           // 0..1 dark (tape) .. bright
    float echo_duck;           // 0..1: the echo stays down while you sing and swells in the gaps
    float reverb;              // 0..1 how loud
    float reverb_s;            // 0.3..12 seconds to die away
    float reverb_tone;         // 0..1 dark .. bright
    float shimmer;             // 0..1 an octave up fed back into the reverb
    float throw_;              // 0..1: the roll's throw: the echo opens and its repeats grow (smoothed inside)
} fx_bus_params_t;
void  vfx_init(void);
void  vfx_set_input(const fx_in_params_t *p);              // UI task
void  vfx_set_bus(const fx_bus_params_t *p);               // UI task
void  vfx_input(float *io, int n, float out_env);          // audio task, in place. out_env: the output's envelope (for the guard)
void  vfx_bus(const float *send, float *io, int n);        // audio task: adds the echo and reverb of send[] to io[]
float vfx_gate_open(void);                                 // 0..1 for the screen
float vfx_guard_cut(void);                                 // dB the guard is cutting now (0 none), for the screen
int   vfx_echo_taps(float *lv, int max);                   // the echo's last repeats' levels, newest first, for the screen

