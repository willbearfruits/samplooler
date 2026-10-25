// AIRDWELLER: one instrument in two boxes, one in each hand (or one box alone). AIR's air drumming and DWELL's
// pumping, with sounds made from nothing the way SOPHIE made hers (FACESHOPPING, PONYBOY): a material first (rubber,
// latex, metal, glass), then how it is struck, stretched, scraped and squeezed.
//
// Each box has a ROLE:
//   DRUM   AIR's strokes: swing the box like a stick; forward, left, right and back are four pads; every hit lands on
//          the beat grid. The pads are synthesized drums (DV_*). The LEFT hand's kit is low (the rubber kick, the latex
//          snap, a whip, a sub), the RIGHT hand's high (metal, glass, a hat, the creak); one box alone gets a mixed kit.
//          Hold REC: a loop of whole bars, four stacked (AIR's loops).
//   PAINT  a material that rings: struck by the other hand's hits (tuned to its kick, at the same instant on the shared
//          clock) and by this hand's own swings. The hand shapes the ring while it rings: tilt moves through the
//          materials, twisting bends the pitch, the roll is brightness, moving sustains it (a bow), shaking scrapes it.
//   PUMP   the same material bowed instead of struck: DWELL's energy (movement pumps it in, stillness lets it fall)
//          drives a drone on the material's chord, ducked by the other hand's hits so the two interlock.
// Two boxes find each other on the link (name AIRDWELL): one beat, one key; every hit a DRUM box makes, live or from a
// loop, is sent with its tick on the shared clock (link_set_app), so a kick in the left hand and the metal it strikes
// in the right sound together. Tap REC: CUT, a hard silence (SOPHIE's stops), back in on the next beat.
//
// The files: instrument.c (UI task: params, sounds, the strokes, the loops, the energy, the hands, the link),
// inst_engine.c (audio task: the drum voices, the material, the loops' playback, the bus), inst_scene.c (the screens).
#pragma once
#include "core.h"
#include "spsc.h"

// ---------------------------------------------------------------- params (the table in instrument.c, in this order)
enum {
    // PLAY
    P_ROLE, P_HAND, P_PAIR, P_DUCK, P_LEVEL,
    // KIT (DRUM)
    P_FRONT, P_LEFT, P_RIGHT, P_BACK, P_TUNE, P_DECAY, P_SQUEEZE,
    // MATERIAL (PAINT, PUMP)
    P_MAT, P_MORPH, P_TUNETO, P_RATIO, P_CHORD, P_RING, P_BRIGHT, P_BEND, P_CREAK,
    // ENERGY
    P_CLIMB, P_FALL, P_REST, P_SHAKE,
    // LOOPS
    P_TEMPO, P_LOOP1, P_LOOP2, P_LOOP3, P_LOOP4, P_ERASE_LAST, P_ERASE_ALL, P_QUANT, P_SWING,
    // FX
    P_DRIVE, P_TONE, P_ECHO, P_ESYNC, P_EFB, P_EMIX, P_ROOM, P_RSIZE,
    // FEEL (device settings: they stay when the sound changes)
    P_SENS, P_CONFIRM, P_FLIP, P_SIDES,
    // SONG
    P_KEY, P_LINK, P_GROUP, P_RANGE, P_PEERS,
    P_N
};
enum { ROLE_DRUM, ROLE_PAINT, ROLE_PUMP, NROLE };
enum { HAND_AUTO, HAND_LEFT, HAND_RIGHT };
enum { PD_FRONT, PD_LEFT, PD_RIGHT, PD_BACK, NPAD };
// the synthesized drums
enum {
    DV_KICK,        // rubber kick: a click that drops to the sub in milliseconds, folded and squeezed, a long sub tail
    DV_SNAP,        // latex snap: a resonant band pass whipped down over a noise burst and a pitched pop, a squeak on top
    DV_CLANG,       // metal clang: a struck plate, inharmonic modes, a hard bright attack, a ringing tail
    DV_HAT,         // metal hat: a high cluster of modes and filtered noise, short
    DV_GLASS,       // glass: an FM bell, high, the index falling, a wet blip
    DV_SUB,         // sub drop: a sine falling an octave or more over a second, fat
    DV_WHIP,        // whip crack: a band pass swept up through noise in a few ms, a crack
    DV_CREAK,       // creak: friction, a jittered pulse train through metal modes (PONYBOY's mechanical animal)
    DV_OFF,
    NDV
};
#define DV_AUTO NDV   // a pad set to AUTO takes its voice from the hand's kit (pad_kit() in instrument.c)
enum { MAT_METAL, MAT_GLASS, MAT_RUBBER, MAT_LATEX, NMAT };
enum { TT_KICK, TT_KEY };                     // TUNE TO: the other hand's kick (or this box's), or the key
enum { CH_ONE, CH_FIFTH, CH_OCTAVE, CH_TRIAD };   // the material's chord: 1/1; + 3/2; + 3/2 + 2/1; 1/1 5/4 3/2 7/4
enum { LP_EMPTY, LP_OFF, LP_ON };             // a LOOP n setting
enum { SRC_LIVE, SRC_LOOP, SRC_REMOTE };

// ---------------------------------------------------------------- the grid and the loops (AIR's)
#define NLOOP      4
#define LOOP_HITS  128
#define BAR_MAX    8
#define PPB        96                         // ticks per beat
#define BAR_TICKS  (4 * PPB)
typedef struct { uint32_t fpb, origin; float frac; } grid_t;   // beat 0 in our frames, frames per beat
void     eng_set_grid(const grid_t *g);      // UI, each tick (a seqlock)
void     eng_get_grid(grid_t *g);            // any task
int64_t  eng_tick_at(const grid_t *g, uint32_t frame);   // the tick at a frame, rounded down
uint32_t eng_frame_of(const grid_t *g, int64_t tick);    // the frame a tick falls on
int64_t  eng_floor_div(int64_t a, int64_t b);
typedef struct { uint16_t tick; uint8_t pad, vel; int32_t born; } hit_t;   // born: the cycle it sounded live in
typedef struct {
    hit_t hits[LOOP_HITS];
    volatile int n;
    volatile int64_t start;                   // the tick of its first bar
    volatile int bars;                        // 0: empty
    volatile int on;
    uint32_t serial;
} loop_t;
extern loop_t eng_loops[NLOOP];
extern volatile int eng_run;                  // 0: every loop is quiet
uint16_t eng_hit_tick(const loop_t *l, const hit_t *h);   // where a hit plays in its loop after SWING

// ---------------------------------------------------------------- the engine (inst_engine.c)
// DRUM: a pad hit. at: the frame to play on (0, or a frame already past: now); the engine plays the pad's voice
// (eng_pad_voice) at the pad's tune. In PAINT the same call strikes the material instead (the pad picks the strike:
// LEFT heavy and low, RIGHT middle, FRONT light and high, BACK a scrape). Every hit that sounds is reported back
// (eng_played_pop) with its voice, live or from a loop.
typedef struct { uint8_t pad, vel, src; uint32_t at; } trig_t;
typedef struct { uint32_t frame; uint8_t pad, vel, src, voice; } played_t;
void  eng_trigger(const trig_t *t);           // UI
bool  eng_played_pop(played_t *p);            // UI
// PAINT and PUMP: the other hand's hit strikes the material at frame `at` (late: now). voice: the striking drum (DV_*):
// a kick strikes heavy, low and long, a hat light, high and short, a snap or a whip bright, a creak scrapes.
void  eng_strike(uint32_t at, int vel, int voice);   // UI

extern volatile int   eng_role;               // ROLE_*
extern volatile int   eng_pad_voice[NPAD];    // DV_* (never DV_AUTO: the UI resolves it)
extern volatile float eng_pad_tune[NPAD];     // semitones, each pad (the kit's own spread plus TUNE)
extern volatile float eng_kick_hz;            // the kick's body after its drop (the key's note, about 40..80 Hz)
extern volatile float eng_drum_decay;         // 0.25..4: times each voice's own decay
extern volatile float eng_squeeze;            // 0..1: the drums folded and driven harder (plastic, squeezed)
// the material
extern volatile float eng_mat_morph;          // 0..3: continuous over MAT_METAL, GLASS, RUBBER, LATEX (the tilt)
extern volatile float eng_mat_hz;             // its fundamental (glides over about 50 ms)
extern volatile int   eng_mat_chord;          // CH_*
extern volatile float eng_mat_ring;           // s: how long a strike rings (T60 of the lowest mode)
extern volatile float eng_mat_bright;         // 0..1: the upper modes, the strike's brightness
extern volatile float eng_bend;               // semitones, glides (the twist)
extern volatile float eng_bow;                // 0..1: bowing (friction excitation): PUMP's energy, PAINT's movement
extern volatile float eng_creak;              // 0..1: a shake's scrape (stick-slip bursts through the material)
extern volatile float eng_mat_level;          // the material's gain: REST LEVEL .. 1 with the energy (glides 30 ms)
extern volatile float eng_duck;               // 0..1: how far each strike from the other hand ducks the bow's drone
// the bus (everything): drive, tone, echo, room, level, cut
extern volatile float eng_drive;              // 0..1: an asymmetric soft clip with ADAA (DWELL's)
extern volatile float eng_tone;               // 0..1: a low pass after it, 800 Hz .. 16 kHz
extern volatile float eng_echo_s, eng_echo_fb, eng_echo_mix;   // s; 0..0.95; 0..1 (equal power)
extern volatile float eng_room, eng_room_size;                  // 0..1 wet (equal power); 0..1 (0.3 s .. 6 s)
extern volatile float eng_level;              // 0..1
extern volatile int   eng_cut;                // 1: a hard silence (3 ms), the loops keep time; 0: back
// what the engine reports (any time)
extern volatile float eng_out_lvl;            // peak, decaying
extern volatile float eng_mat_lvl;            // the material's ring, 0..1-ish
extern volatile float eng_mode_amp[8];        // the material's eight modes now (its first voice), for the screen
extern volatile int   eng_voices_on;          // drum voices sounding

void eng_init(void);                          // init(): allocate (echo in PSRAM, the room's lines in internal RAM)
void eng_audio(const float *in, float *out, int n);

// the cost (host only, for the tests): nanoseconds in eng_audio
extern double eng_audio_ns, eng_audio_max_ns;
extern long   eng_audio_blocks;

// ---------------------------------------------------------------- the screen's state (instrument.c fills it each tick)
typedef struct {
    int   role, hand;                         // ROLE_*, HAND_LEFT/RIGHT (what HAND came to; HAND_AUTO alone)
    int   paired;                             // another AIRDWELLER heard
    int   peer_role, peer_hand;               // its role and hand
    int   pad_voice[NPAD];                    // DV_* each pad plays (resolved)
    float flash[NPAD];                        // 1 when a pad sounded, decaying (this box's hits)
    float peer_flash;                         // 1 when a hit came from the other box, decaying
    int   peer_voice;                         // the voice of the other box's last hit
    float beat_phase;                         // 0..1 within the beat
    int   beat_in_bar;                        // 0..3
    float bpm;
    int   rec;                                // 0 idle, 1 waiting for the bar, 2 recording, 3 closing
    int   rec_slot;
    int   loop_bars[NLOOP], loop_on[NLOOP];
    float loop_pos[NLOOP];                    // 0..1 through each loop
    float energy, motion, shake;              // 0..1
    float morph;                              // eng_mat_morph
    float bend;                               // semitones
    float mat_hz, kick_hz;
    int   chord;
    float mat_lvl;                            // eng_mat_lvl
    int   cut;
    int   gest;                               // the stroke detector's state (0 idle, 1 a stroke, 2 done)
    int   last_pad;                           // the last stroke's pad
    vec3_t gyro;                              // deg/s now
    int   key;                                // 0..11
} ad_ui_t;
extern ad_ui_t D;
extern const char *const dv_name[NDV + 1];    // "KICK" ... "OFF", "AUTO"
extern const char *const mat_name[NMAT];      // "METAL" ...

// ---------------------------------------------------------------- the screens (inst_scene.c)
void scene_draw(const view_t *v);
bool scene_splash(int frame);
void scene_saver(uint32_t frame);
void scene_hit(int pad, int vel, int src);      // a hit sounded: SRC_LIVE or SRC_LOOP (this box's), SRC_REMOTE (pad -1)
