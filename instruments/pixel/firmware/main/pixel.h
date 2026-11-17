// PIXEL: a chiptune machine. Four parts (DRUMS, BASS, CHORDS, LEAD) written on a grid of sixteenth steps, bars
// chained into a song, played by one of eight old sound chips (the knob walks through them). This header is the
// contract between its parts:
//
//   instrument.c     the host: INST, the params, the clock (tempo, swing, the link), the transport, the hands, the mix
//   inst_song.c      the song: its notes (px_song_t), the factory songs, which notes go with the loaded sound, saving
//   inst_player.c    the player: a song's cells and the settings become, every driver tick, one px_frame_t: what
//                    each part is doing now (which note, how old, how loud; the chord and where its arpeggio is; the
//                    drum hits). The same for every console: slides, vibrato, echo, arpeggio order, rhythms, glitches
//   inst_con_*.c     the consoles: each one is a sound chip and its sound driver in one file. Every tick it gets the
//                    frame and decides how its chip plays it (which voice takes which part, the instruments' shapes,
//                    the drums), then renders the chip's sound. One console plays at a time
//   inst_edit.c      the editor (the knob held): the grid, the song row
//   inst_scene.c     the screen while playing; the splash, the screensaver
//   inst_store.c     the notes' files on the board (device only; tools/sim has a stand-in)
//
// Time. A STEP is a sixteenth; a bar is 16 steps (one pattern). The driver TICK is the old machines' frame: about 60
// a second (or 50), a whole number of ticks per step (tps), so the tick's rate follows the tempo (150 BPM: 6 ticks a
// step at 60 Hz, the classic). Everything a sound driver does happens on ticks: arpeggios, vibrato, volume shapes.
// The host runs the clock in the audio task and splits its blocks at tick boundaries, so ticks are sample exact:
//
//     at a tick:  px_player_tick() -> frame;  console->tick(&frame)
//     between:    console->render(out, n)
//
// Rules (docs/CORE.md has the rest): audio code never allocates, logs, blocks, takes a lock, uses double or calls libm
// per sample. Every console's render() is IRAM_ATTR and the tables it reads per sample DRAM_ATTR (below).
#pragma once
#include "core.h"
#include "dsp.h"

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#else
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif
#ifndef DRAM_ATTR
#define DRAM_ATTR
#endif
#endif

// ---------------------------------------------------------------- the song's notes
#define PX_STEPS      16       // steps in a pattern: one bar of sixteenths
#define PX_PATTERNS   16       // patterns A..P
#define PX_ORDER_MAX  64       // the song: a row of patterns, played in order, then again from the start
enum { PART_DRUMS, PART_BASS, PART_CHORDS, PART_LEAD, PART_N };

// A note cell (BASS, LEAD), one byte per step.
#define NC_ROW     0x3F        // 0: nothing; 1..63: the scale row + 1 (px_row_note)
#define NC_TIE     0x40        // the note before carries on into this step: the same row holds it, another row slides
                               // to it without a new attack. A tie with nothing before it is a plain note
#define NC_ACCENT  0x80        // louder, brighter
#define PX_ROWS    63
// A chord cell (CHORDS), one byte per step. A chord sounds from its step until the next chord cell or a CH_OFF.
#define CH_DEGREE  0x07        // 0: nothing; 1..7: the chord on that degree of the scale (I..VII)
#define CH_KIND    0x18        // CHK_*: which notes
#define CH_OFF     0x80        // the chords stop here
enum { CHK_TRIAD = 0x00, CHK_SEVENTH = 0x08, CHK_SUS = 0x10, CHK_POWER = 0x18 };
// A drum cell (DRUMS), one byte per step: the drums that are hit there.
enum { DR_KICK = 0x01, DR_SNARE = 0x02, DR_HAT = 0x04, DR_OPEN = 0x08, DR_TOM = 0x10, DR_ACCENT = 0x80 };
#define PX_DRUMS   5           // KICK, SNARE, HAT (closed), OPEN (hat), TOM (a pitched blip, the laser tom)

typedef struct {
    uint8_t drums[PX_STEPS];
    uint8_t bass[PX_STEPS];
    uint8_t chords[PX_STEPS];
    uint8_t lead[PX_STEPS];
} px_pattern_t;                // 64 bytes

typedef struct {
    px_pattern_t pat[PX_PATTERNS];
    uint8_t order[PX_ORDER_MAX];   // pattern numbers 0..15
    uint8_t order_n;               // 1..PX_ORDER_MAX
} px_song_t;
extern px_song_t px_song;      // the working song: the UI task edits it (single byte stores), the audio task reads it

// ---------------------------------------------------------------- scales, rows and chords
// The first six are the flock's scales (BAND, SING, WRECK: the link carries the number). The others are PIXEL's own;
// on the link each stands as its parent.
enum { PX_SC_MAJOR, PX_SC_MINOR, PX_SC_DORIAN, PX_SC_MIXO, PX_SC_PHRYGIAN, PX_SC_HARMONIC,
       PX_SC_PENTA_MAJ, PX_SC_PENTA_MIN, PX_SC_BLUES, PX_SC_CHROMATIC, PX_SC_N };
typedef struct { const char *name; uint8_t n; int8_t st[12]; uint8_t parent; } px_scale_t;   // semitones above the key
extern const px_scale_t PX_SCALES[PX_SC_N];
extern const uint8_t PX_PART_BASE[PART_N];   // the MIDI note of row 0 in the key of C: BASS 24 (C1), LEAD 48 (C3), CHORDS 48

// The MIDI note of a scale row (0..62) for a part: row 0 is the key's root in the part's lowest octave, each row the
// next note of the scale up. Clamped to 12..119.
static inline int px_row_note(int part, int row, int key, int scale)
{
    const px_scale_t *s = &PX_SCALES[scale < 0 || scale >= PX_SC_N ? 0 : scale];
    if (row < 0) row = 0;
    int n = PX_PART_BASE[part] + key + 12 * (row / s->n) + s->st[row % s->n];
    return n < 12 ? 12 : n > 119 ? 119 : n;
}
int px_home_octave(int part, int key);       // the octave (above the part's lowest row) where the part mostly plays
// The row whose note is nearest to a MIDI note (for MIDI in, and for a change of scale in the editor).
int px_note_row(int part, int midi, int key, int scale);
// A chord cell's notes: MIDI, low to high, root first (root in C3..B3 before the part's octave is added). Returns 3
// or 4, or 0 for a cell without a chord. The degrees are the scale's parent's (a pentatonic song has major chords).
int px_chord_notes(uint8_t cell, int key, int scale, int notes[4]);
const char *px_chord_name(uint8_t cell, int scale, char *b, int n);   // "IV", "ii7", "Vsus": for the screen

// ---------------------------------------------------------------- the frame: one driver tick, for the console
// A voice of the song (the LEAD, the BASS, the lead's ECHO) as the player hands it to the console.
typedef struct {
    uint8_t  on;               // 1 from the note's first tick to its last
    uint8_t  trig;             // 1 on the tick a new note starts: restart the instrument's shapes. A slide into a tied
                               // note changes `note` without a trig. Always with on = 1
    uint8_t  accent;           // the note is accented (it stays for the whole note)
    uint8_t  sound;            // the part's SOUND setting, 0..3: the console's own instruments for this part
    uint16_t age;              // ticks since the trig (0 on the trig's tick); stops at 65535
    float    note;             // the pitch as a MIDI note, with everything in it: slide, vibrato, bend. 69.0 = 440 Hz
    float    level;            // 0..1: the part's LEVEL (and, for the echo, how far it has faded). The note's own shape
                               // (attack, decay, release after on goes 0) is the console's
} px_voice_t;

typedef struct {
    uint8_t  on;               // the chord sounds (the RHYTHM setting gates it: held, eighths, offbeats ...)
    uint8_t  trig;             // a new chord or a new stab starts this tick
    uint8_t  n;                // notes in the chord: 3 or 4 (0 with on = 0)
    uint8_t  sound;            // CHORDS > SOUND, 0..3
    uint8_t  arp;              // CHORDS > ARP: 0 OFF, 1 SLOW (a note a step), 2 MID, 3 FAST (a note a tick)
    uint8_t  arp_i;            // which of notes[] the arpeggio is on this tick (already stepped at the ARP's speed in
                               // the SHAPE's order; with ARP OFF it stays 0)
    uint16_t age;              // ticks since the trig
    float    notes[4];         // the chord, MIDI, low to high, root first (the bend is in them)
    float    note;             // the one note a single voice plays this tick: notes[arp_i], plus the shape's octave
    float    level;            // 0..1: the part's LEVEL
} px_chord_t;

typedef struct {
    uint32_t tick;             // counts every tick since boot
    float    tick_hz;          // ticks a second now: about 60 (or 50), following the tempo
    uint8_t  tps;              // ticks in a step, 3..16
    uint8_t  tick_in_step;     // 0..tps-1; 0 where a step starts
    uint8_t  step;             // 0..15, the step in the bar
    uint8_t  running;          // the song plays. 0: stopped (notes played by hand may still come)
    uint8_t  restart;          // 1 on the first frame after reset(): every part that sounds has trig set
    uint8_t  drums;            // the drums hit on this tick: DR_KICK | DR_SNARE ... (0 on most ticks). Where one voice
                               // must choose between drums hit together: snare, then kick, tom, open hat, hat
    uint8_t  drum_accent;      // that hit is accented
    uint8_t  kit;              // DRUMS > KIT, 0..3: the console's own kits
    float    drum_level;       // 0..1: the part's LEVEL
    px_voice_t lead, bass;
    px_voice_t echo;           // the lead again, some steps later and quieter (LEAD > ECHO): for a voice that is free.
                               // on = 0 when the echo is off or silent. A console without a free voice ignores it
    px_chord_t chord;
    float    glitch;           // 0..1: the cartridge is tilted. The player already scrambles what it reads; a console
                               // adds what its chip would do with bad contacts (px_glitch on what it writes)
    uint32_t rng;              // a fresh random number each tick, for the console's dice
} px_frame_t;

// ---------------------------------------------------------------- a console
// What one voice of the chip is doing, for the screen (the scene draws a little scope for each).
enum { VW_OFF, VW_PULSE, VW_TRI, VW_SAW, VW_NOISE, VW_WAVE, VW_SAMPLE, VW_FM, VW_BUZZ };
typedef struct {
    uint8_t kind;              // VW_*: the shape to draw
    uint8_t part;              // the part it plays now: PART_*, or 0xFF for none (the echo counts as PART_LEAD)
    float   hz;                // its pitch (0: unpitched)
    float   level;             // 0..1 now
    float   shape;             // a pulse's width 0..1; otherwise whatever varies the picture (0..1)
} px_vview_t;
#define PX_VOICES_MAX 10

typedef struct {
    const char *name;          // the knob's label, at most 8 characters: "NES"
    const char *chip;          // the chip, small print: "2A03"
    const char *year;          // "1983"
    uint8_t     voices;        // how many voices view() describes (<= PX_VOICES_MAX)
    void (*init)(void);        // boot, UI task, before any audio: allocate here (app_alloc, app_alloc_fast), build
                               // tables, make the samples
    void (*reset)(void);       // audio task: the console was switched in. Silence, all state cleared; the next frame
                               // has restart = 1
    void (*tick)(const px_frame_t *f);    // audio task, every driver tick, before that tick's audio
    void (*render)(float *out, int n);    // audio task: write n samples (mono, n <= AUDIO_BLOCK). IRAM_ATTR
    int  (*view)(px_vview_t *v, int max); // UI task: what each voice does now (plain reads of the state); returns voices
    const char *const *names;  // optional: sixteen short names (capitals, at most 8 characters) for the menu: the four
                               // LEAD sounds, the four BASS sounds, the four CHORDS sounds, the four drum KITs. NULL: 1..4
} px_console_t;

enum { CON_BEEPER, CON_ATARI, CON_AY, CON_NES, CON_GAMEBOY, CON_C64, CON_AMIGA, CON_MEGA, CON_N };
extern const px_console_t con_beeper, con_atari, con_ay, con_nes, con_gameboy, con_c64, con_amiga, con_mega;
extern const px_console_t *const PX_CONSOLES[CON_N];   // in the knob's order, thin to fat
// the reference console's functions (inst_con_ref.c), for a console's file to stand in with until it is built
void px_ref_init(void); void px_ref_reset(void); void px_ref_tick(const px_frame_t *f);
void px_ref_render(float *out, int n); int px_ref_view(px_vview_t *v, int max);

// Levels: with the four parts at LEVEL 100 a busy bar of render() peaks around 0.5 and never passes 0.9; its RMS over
// the bench tune (tools/conlab) is 0.10 to 0.16, so the knob does not jump in loudness between consoles. No DC: each
// console ends in its chip's own high pass (or a 20 Hz DC blocker). Silence in, exact zeros out after a short tail
// (flush_denormal on every recursive state).

// For consoles: the frequency of a MIDI note (fractional), no libm. Tick rate use.
static inline float px_hz(float note) { return 440.0f * fast_exp2((note - 69.0f) * (1.0f / 12.0f)); }
// For consoles: bad contacts. With chance `amount` (0..1) it flips one random bit among the low `bits` of v.
static inline uint32_t px_glitch(uint32_t v, int bits, float amount, uint32_t *rng)
{
    if (amount <= 0.0f || bits <= 0) return v;
    uint32_t r = rng_next(rng);
    if ((float)(r >> 8) * (1.0f / 16777216.0f) >= amount) return v;
    return v ^ (1u << ((r & 0xFFu) % (uint32_t)bits));
}

// ---------------------------------------------------------------- the player (inst_player.c)
// What the host hands the player each tick: the song's settings and the hands. Plain values, read once per tick.
enum { ARP_OFF, ARP_SLOW, ARP_MID, ARP_FAST };
enum { SHAPE_UP, SHAPE_DOWN, SHAPE_UPDOWN, SHAPE_OCTAVES, SHAPE_RANDOM, SHAPE_N };
enum { RHY_HELD, RHY_EIGHTHS, RHY_OFFBEAT, RHY_GALLOP, RHY_FOURS, RHY_N };
typedef struct {
    uint8_t key, scale;        // 0..11, PX_SC_*
    int8_t  oct[PART_N];       // octaves added to a part, -2..2 (DRUMS: unused)
    float   level[PART_N];     // 0..1
    uint8_t sound[PART_N];     // 0..3 (DRUMS: the kit)
    uint8_t mute;              // bit per part: it does not play
    uint8_t len_bass, len_lead;   // 10..100: how much of a note's last step sounds, %
    uint8_t arp, shape, rhythm;   // CHORDS: ARP_*, SHAPE_*, RHY_*
    uint8_t vibrato;           // LEAD: 0..100, the depth (100 = half a semitone each way), after a short delay
    uint8_t echo;              // LEAD: 0 off, 1..3: SHORT (2 steps), MID (3), LONG (4)
    uint8_t slide;             // 0..100: how long a slide between tied notes takes (0: it jumps; 100: about a step)
    float   bend;              // semitones added to everything pitched (the roll)
    float   glitch;            // 0..1 (the tilt forward)
    int8_t  live[PART_N];      // a note played by hand on that part now (MIDI), -1: none. BASS and LEAD. It sounds
                               // over what the song has there
    uint8_t live_drums;        // drums hit by hand: taken on the next tick (the host clears it after one tick)
} px_ctl_t;

typedef struct px_player px_player_t;
px_player_t *px_player_new(void);          // boot, UI task (app_alloc_fast). The host has one; tools/conlab makes its own
void px_player_reset(px_player_t *p);      // every part silent
void px_player_restart(px_player_t *p);    // the console was switched: the next frame has restart = 1, trig on what sounds
// One tick. `pat` is the pattern being played and `step` the step in it; pat = NULL: the song is stopped (only live
// notes and tails). `next` is the pattern that follows (to know whether a note at the bar's end is tied on; NULL: pat).
// tick_in_step == 0 starts the step: its cells are read then. tick counts on by itself.
void px_player_tick(px_player_t *p, const px_pattern_t *pat, const px_pattern_t *next, int step, int tick_in_step,
                    int tps, float tick_hz, const px_ctl_t *c, px_frame_t *f);

// ---------------------------------------------------------------- the song's text (inst_song.c)
// The factory songs are written as text and read at load. One line per pattern and part; see inst_song.c.
bool px_song_parse(px_song_t *s, const char *text, int key, int scale, char *err, int errn);
void px_song_clear(px_song_t *s);

// ---------------------------------------------------------------- the host (instrument.c)
// Where the song is, written by the audio task every tick, read by the screen and the editor (plain fields).
enum { PM_SONG, PM_PATTERN };
typedef struct {
    uint8_t  running;          // the song (or a pattern) plays
    uint8_t  armed;            // it starts on the next bar line (it waits for a linked board's beat)
    uint8_t  mode;             // PM_SONG: the order row; PM_PATTERN: one pattern, round and round
    uint8_t  slot;             // the order slot playing (PM_SONG)
    uint8_t  pattern;          // the pattern playing
    uint8_t  step;             // 0..15
    uint8_t  tick, tps;
    uint8_t  console;          // CON_*: the one sounding
    uint8_t  degree;           // the chord sounding: 1..7, 0 none
    float    tick_hz, bpm;
    uint32_t bars;             // bars played since the last start
} px_pos_t;
extern volatile px_pos_t px_pos;
extern volatile uint8_t  px_mute;                // bit per part: muted until it is brought back (the stick's pushes)
extern volatile uint8_t  px_hand_mute;           // bit per part: held out by a hand over the sensor (rev E), while it is there
static inline uint8_t px_muted(void) { return (uint8_t)(px_mute | px_hand_mute); }   // the parts that do not play now
void px_play_song(int slot);                     // start the song at that slot (already playing: from the next bar line)
void px_play_pattern(int pat);                   // loop one pattern
void px_stop(void);
void px_audition(int part, int note, int ms);    // a note by hand (BASS, LEAD) for that long; note < 0 ends it
void px_audition_drums(uint8_t bits);
void px_audition_chord(uint8_t cell, int ms);    // a chord cell's notes, one after the other, on the lead's voice
int  px_key(void);                               // the key and scale that play now (the song's, or a linked board's)
int  px_scale(void);
int  px_peers(void);
const px_console_t *px_console(void);            // the console sounding
float px_glitch_now(void);                       // 0..1, for the screen
float px_bend_now(void);                         // semitones

// ---------------------------------------------------------------- which notes go with the sound (inst_lib.c)
// The core's sounds are PIXEL's songs: a factory sound's notes are in the firmware (inst_demos.c), a user sound's in
// the notes' store beside the core's own blob. The library keeps px_song in step with the sound the core has loaded.
#define PX_NSONGS 8                               // the factory songs (inst_demos.c): the last one is EMPTY
extern const preset_t PX_PRESETS[PX_NSONGS];     // each one's name and settings ("tag=value ...")
extern const char *const PX_SONG_TEXT[PX_NSONGS];   // and its notes, as text (inst_song.c)
int  px_param_nrev(void);                        // the hidden param that stamps the notes (instrument.c)
void lib_init(void);
void lib_loaded(void);                           // from INST.changed(-1): the core loaded a sound
void lib_tick(const ctl_t *c);                   // watches for SAVE AS; saves the working notes in a quiet moment
void px_notes_edited(void);                      // the editor changed px_song
bool lib_notes_edited(void);
// the notes' store (inst_store.c; tools/sim: the stand-in in the same file). Whole blobs by a short name.
bool px_store_write(const char *name, const void *data, int n);
int  px_store_read(const char *name, void *data, int cap);      // bytes read, 0: none
void px_store_erase(void);

// ---------------------------------------------------------------- the editor (inst_edit.c) and the screen (inst_scene.c)
bool edit_active(void);
bool edit_hold(void);                            // the knob held: open the editor, or close it. true: used
void edit_tick(const ctl_t *c);
void edit_draw(const view_t *v);
void edit_action(int what);                      // EA_*: the BAR page's actions
enum { EA_COPY, EA_PASTE, EA_CLEAR, EA_DICE };
int  edit_pattern(void);                         // the pattern under the cursor (or the one playing)
void scene_draw(const view_t *v);
bool scene_splash(int f);
void scene_saver(uint32_t f);
