// SING: the voice, processed, and the Samplooler's loop, on one board. Sing into the mic: it tunes you (from a
// gentle pull to the hard robot step), shifts you, changes the size of your throat (formant), sings harmonies
// with you, makes a choir of you, puts you through a vocoder on the song's chords, and echoes and reverbs it.
// Hold REC to loop what comes out (in whole bars, on the beat of a BAND or another linked board), tilt to play
// the loop faster, slower or backwards as the Samplooler does, roll to throw it into the echo or to stutter it.
//
// The parts (the host talks to each only through this header):
//
//   instrument.c   the host: INST (params, sounds, hints), the song (tempo, key, scale, chords) and the link,
//                  the mixer, the controls (tilt, roll, stick, REC), the screen, the splash
//   inst_voice.c   pitch tracking and the voices: the lead (autotune, shift, formant) and the extra voices
//                  (harmonies, choir), all by pitch-synchronous overlap-add from one analysis of the mic
//   inst_vocoder.c a channel vocoder: the voice shapes a synth playing the chord, the sung note, or noise
//   inst_fx.c      the input stage (gate, compressor, a guard against the speaker feeding back) and the FX bus
//                  (an echo in time with the song, a reverb, a shimmer)
//   inst_looper.c  the loop: synced to the song's bars, overdubs, the tilt's speed and direction, the stutter
//
// Audio path per 64-frame block (the audio task, core 1):
//   mic -> fx_input() -> voice_process() -> lead, extra; vocoder_process() -> voc
//   voice = dry * DRY + lead * LEAD + extra + voc * VOCODER
//   loop_run(voice) -> loop (records the voice, plays the loop)
//   mix = voice * MONITOR + loop * LOOP LEVEL; loop_stutter(mix); fx_bus(mix): the echo and the reverb; the core's master
#pragma once
#include "core.h"
#include "dsp.h"

// ---------------------------------------------------------------- the song (instrument.c, read anywhere)
// As BAND's (band.h): whole frames per beat, beat 0 at a local frame (plus a fraction: SING's loop reads
// between frames so it can follow a linked board's clock without clicks), the harmony.
typedef struct {
    float    bpm;
    uint32_t fpb;              // frames per beat
    uint32_t origin;           // local frame of beat 0
    float    origin_frac;      // and the fraction of a frame after it (0..1)
    int      bar_beats;        // 4
    int      loop_beats;       // the session's loop (the first loop anywhere), 0: none yet
    int      key;              // 0..11
    const int8_t *scale; int scale_n;
    uint16_t scale_mask;       // the scale's pitch classes (bit 0 = C)
    int      degree;           // the chord now, a scale degree
    int      chord[4]; int chord_n;   // its notes, MIDI, root in C3..B3
    uint16_t chord_mask;       // its pitch classes
} song_t;
extern song_t song;

// ---------------------------------------------------------------- the host and the screen
// What the host (instrument.c) shares with the screen (inst_scene.c); written in the UI task.
typedef struct {
    float speed;               // the loop's speed the tilt asks for (1 = in time)
    float throw_, stutter;     // the roll: left into the echo, right the stutter (0..1)
    float bend, formant;       // the stick: semitones of pitch and of formant added now
    int   beat_flash;          // 3 on a downbeat, 2 on another beat, counting down
    bool  rec;                 // REC is down
    float dry, lead, harm, choir, voc;   // the mix (0..1) as the sound sets it
    uint16_t tune_mask;        // the notes the lead is tuned to (bit 0 = C), 0: every note
    int   tune_to;             // SCALE, CHORD, ALL NOTES, PENTATONIC, BLUES
    int   peers;               // linked boards
    bool  midi_chord;          // a MIDI keyboard holds the chord now
} sing_ui_t;
extern sing_ui_t sui;
void  sing_prof(float *us, int n);      // the parts' cost since the last call, us per block on the board (input, voices,
                                        // vocoder, loop + mix, FX); on the host x 0.96 microseconds
void  scene_draw(const view_t *v);       // the whole 128 x 64 frame
bool  scene_splash(int f);
void  scene_saver(uint32_t f);
void  scene_link_glyph(int x, int y, const view_t *v);
const char *scene_note(int midi, char *b, int n);   // "A4"

// ---------------------------------------------------------------- inst_voice.c
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
// Audio task. in: the voice after fx_input(). lead: the lead voice; extra: every extra voice summed. Both start
// VOICE_LATENCY_MS late.
void  voice_process(const float *in, float *lead, float *extra, int n);
void  voice_state(voice_state_t *s);                       // any task
// The input delayed by the same latency as the voices (call after voice_process for the same block).
const float *voice_dry_delayed(void);

// ---------------------------------------------------------------- inst_vocoder.c
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

// ---------------------------------------------------------------- inst_fx.c
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
void  fx_init(void);
void  fx_set_input(const fx_in_params_t *p);              // UI task
void  fx_set_bus(const fx_bus_params_t *p);               // UI task
void  fx_input(float *io, int n, float out_env);          // audio task, in place. out_env: the output's envelope (for the guard)
void  fx_bus(const float *send, float *io, int n);        // audio task: adds the echo and reverb of send[] to io[]
float fx_gate_open(void);                                 // 0..1 for the screen
float fx_guard_cut(void);                                 // dB the guard is cutting now (0 none), for the screen
int   fx_echo_taps(float *lv, int max);                   // the echo's last repeats' levels, newest first, for the screen

// ---------------------------------------------------------------- inst_looper.c
// The Samplooler's loop, on the song's bars. REC held: record (or add); a tap: stop / play; nothing is lost to the
// 0.3 s it takes to tell a hold from a tap (a ring keeps the last half second). A first take starts at the first sound
// after the press (or at the press, LOOP START: PRESS), and REC's own clicks stay out of every take: nothing of the
// first 0.12 s after a press is used, and the moment of letting go is cut out (or silenced, while a take records on
// to the end of the bar). With LOOP SYNC on, the length is
// whole bars at the tempo (a multiple of the session's loop when one exists; with no tempo heard yet, the first
// loop picks the bars and sets the tempo) and the take records on to the bar's end, so it plays in time with a
// linked BAND; off, it is the Samplooler's own (any length). The loop's place is counted on the session's clock,
// so a board that is not the clock follows the other's drift by reading between samples. The tilt plays it
// faster, slower or backwards (as tape, or at the pitch it was sung: keep_pitch); flat again, it slides back onto the
// beat. Stopped (a tap), a hold records a new take in its place. The stutter repeats a slice of whatever
// passes through it, a beat long down to a thirty-second as the box leans further.
// (loop_*, not looper_*: the core's own looper keeps those names; SING does not use it.)
enum { LOOPER_EMPTY, LOOPER_REC, LOOPER_PLAY, LOOPER_DUB, LOOPER_STOP, LOOPER_ARM };   // ARM: REC held, waiting for a sound
#define LOOP_MAX_S 16
typedef struct {
    bool  sync;                // whole bars on the session's beat (else free, the Samplooler's)
    float speed;               // the tilt: 1 = in time, 0 = stopped, negative = backwards (-2..2)
    float level;               // 0..1 how loud it plays
    float keep;                // 0..1 how much of the loop each pass keeps while adding (1 = all)
    float stutter;             // 0..1: 0 off, more = a shorter slice
    bool  from_sound;          // a first take starts at the first sound after the press (the silence before is cut)
    bool  keep_pitch;          // the tilt moves through the loop at its speed but every grain plays at the pitch it was
                               // sung at (forwards, or backwards when the speed is): it stays in key
} loop_ctl_t;
void  loop_init(void);                                     // UI task, at init(): allocates (PSRAM)
void  loop_set(const loop_ctl_t *c);                       // UI task
// UI task, from REC: a press, the press turned out a hold (0.3 s), the release (confirmed false: a tap). The host
// passes the session's clock offset (link_offset) at each of them and every tick (loop_drift), see inst_looper.c.
void  loop_press(void);
void  loop_confirm(void);
void  loop_release(bool confirmed);
void  loop_clear(void);
void  loop_tick(void);                                     // UI task, 100 Hz: the session's clock, the take's end
void  loop_run(const float *in, float *out, int n);       // audio task: records in[] (when recording), writes the loop to out[]
void  loop_stutter(float *io, int n);                      // audio task: the roll's stutter, in place
int   loop_state(void);                                    // LOOPER_*
float loop_pos(void);                                      // 0..1
float loop_len_s(void);                                    // its length; while the first take records, how much so far
int   loop_beats(void);                                    // its length in beats (0: free or none)
float loop_speed(void);                                    // the speed it plays at now
int   loop_layers(void);                                   // takes recorded into it
bool  loop_closing(void);                                  // REC was let go and the take records on to the bar's end
void  loop_envelope(uint8_t *cols, int n);                 // peak per column over the loop, 0..255, for the screen

// asked of the host (instrument.c), UI task
bool  host_tempo_heard(void);                              // a linked board plays in time, or the session has a loop:
                                                           // a first loop fits the tempo instead of setting it
void  host_set_tempo(uint32_t fpb, uint32_t origin);       // a first loop made alone set the tempo (beat 0 at its start)
void  host_loop_made(int beats);                           // a loop on the session's bars exists now (0: it is gone)
