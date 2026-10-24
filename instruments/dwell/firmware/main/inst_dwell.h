// DWELL: a drone you live inside. The sustained-tone lineage on a box you hold.
//
//   La Monte Young (Dream House)   a drone is a set of whole-number ratios on one fundamental: just intonation. Here
//                                  a lattice of ratios (fifths one way, sevenths the other: 7-limit, Young's, or 5 or 3)
//                                  that the tilt walks; the tones you pass stay behind you (the trail) up to VOICES.
//   Phill Niblock                  many near-copies of one tone, cents apart, loud: the beats are the music. Here each
//                                  voice is a cluster of COPIES spread so neighbours beat at BEAT Hz (or at the flock's
//                                  tempo), and REC catches your own tone to stack (WAVE).
//   Eliane Radigue                 nothing changes faster than you can notice: DRIFT (default 20 s) is how long every
//                                  change takes to arrive, by a swell (new tone in, old out) or a glide.
//   Alvin Lucier                   the room is a filter: ROOM PASS plays the drone through the speaker, records it back,
//                                  plays that, again, PASSES times; what is left (the room's resonances) is the ROOM source.
//   Charlemagne Palestine          a knock strums every voice; the strum decays back into the drone.
//   Tony Conrad, Sarah Davachi,    BOW, REED and SHRUTI sources; TANPURA with its curved bridge (the jawari that makes the
//   Pauline Oliveros, India,       overtones climb), GURDY with the wheel's rumble and the trompette's buzz.
//   the hurdy-gurdy
//
// One knob, DENSE: 0 is PURE (sines at exact ratios, no beats: the Dream House), 100 is a wall (every voice a cluster,
// harmonics in, louder: Niblock).
//
// The second round (the user, 2026-10-24: "dynamics and crescendo, the gestures additive, overdrive for overtones, a
// relatively big movement, think Kevin Drumm, shaking should do stuff, oscillators multiplied by gestures, reverb and
// delay, saturation, wet and dry, a tap fades in the next fundamental", and the radios as interference):
//
//   ENERGY      movement (the box swung, turned, waved: rotation over a dead zone, and its jolts) pumps energy in, and
//               it falls back slowly when the box is still. Energy is additive: gesture after gesture builds a
//               crescendo, from a quiet floor (REST dB) to the full wall. It multiplies the oscillators (the VCA), and
//               raises DENSE, the harmonics and the drive. HAND multiplies them by the movement itself: at 100 it
//               sounds only while you move, like a bow.
//   SHAKE       a shake (the jolts of back and forth) throws grit into the drive, beats the copies faster and swells the
//               echo's feedback for a moment.
//   TAP         a single knock on a still box fades the whole drone onto the next fundamental (the cell you lean towards,
//               or a fixed step), over FADE seconds: two layers of voices, one fading out while the other fades in.
//   DIRT        overdrive on the whole drone (just ratios saturate into more just ratios): DRIVE, TONE, DIRT MIX.
//   ECHO        a delay with a saturating, darkening feedback: time (or on the beat), feedback, mix. Then the hall.
//   RADIO       optional: Wi-Fi frames and Bluetooth adverts the antenna hears crackle into the drive and chop the
//               drone, as a phone near a speaker does (inst_air.c). Nothing about who sent them is kept.
//
// This header is the contract between instrument.c (the UI task: params, sounds, the tilt's walk on the lattice, the
// trail, DRIFT's swells, REC, the knock, the room pass's control, the link, MIDI, the LED, energy and shake, the radio's
// events), inst_lattice.c (ratios and their names), inst_engine.c (the audio task: the voices and their clusters, every
// source, the difference tone, the strum, the VCA, the dirt, the echo, the hall, the interference, the take, the room
// pass's recording and playback), inst_wave.c (the WAVE table), inst_air.c (the radios) and inst_scene.c (the screens).
#pragma once
#include "core.h"

// ---------------------------------------------------------------- params (the table in instrument.c, in this order)
enum {
    // DRONE
    P_DENSE, P_VOICES, P_ROOT, P_DRIFT, P_MOVE, P_LEVEL,
    // ENERGY
    P_CLIMB, P_FALL, P_REST, P_TODENSE, P_TODRIVE, P_SHAKE, P_GRIT,
    // BOW
    P_HAND, P_AXES, P_RING, P_PRESS, P_BCHANGE,
    // TAP
    P_KNOCK, P_NEXT, P_FADE, P_STRUM, P_SENSE, P_HOME,
    // LATTICE
    P_LIMIT, P_OCTAVE, P_TSTEP, P_DWELLMS,
    // CLUSTER
    P_COPIES, P_BEAT, P_BSYNC, P_BRIGHT, P_DIFF,
    // SOURCE
    P_SOURCE, P_PLUCK, P_JAWARI, P_WHEEL, P_BREATH, P_BODY,
    // DIRT
    P_DRIVE, P_TONE, P_DMIX,
    // ECHO
    P_ECHO, P_ESYNC, P_EFB, P_EMIX,
    // SPACE
    P_SPACE, P_TAIL,
    // FEEDBACK
    P_FBMODE, P_FEEDBACK, P_TOFB, P_FBLEVEL, P_FBRING, P_FBSWELL,
    // TOGETHER
    P_SHROOT, P_SHENERGY, P_NEARFAR, P_FARBEAT, P_FIFTH, P_SPREAD,
    // RADIO
    P_RADIO, P_ZAP, P_CHAN,
    // ROOM
    P_PASSES, P_PASSLEN, P_PASS,
    // SONG
    P_TEMPO, P_KEY, P_SCALE, P_FOLLOW, P_LINK, P_GROUP, P_RANGE, P_PEERS,
    P_N
};
enum { AX_ONE, AX_SPLIT };
enum { SH_OWN, SH_SHARED, SH_TRADED };
enum { FIFTH_OFF, FIFTH_UP, FIFTH_DOWN };
enum { KN_OFF, KN_STRUM, KN_NEXT };
enum { NX_LEAD, NX_FIFTH, NX_FOURTH, NX_SEVENTH, NX_SEPTIMAL, NX_TONE, NNX };
enum { RAD_OFF, RAD_WIFI, RAD_BLE, RAD_BOTH };
enum { MOVE_SWELL, MOVE_GLIDE };
enum { LIM_7, LIM_5, LIM_3 };
enum { SRC_SINE, SRC_REED, SRC_BOW, SRC_TANPURA, SRC_SHRUTI, SRC_GURDY, SRC_WAVE, SRC_ROOM, NSRC };

// ---------------------------------------------------------------- the lattice (inst_lattice.c, any task, no state)
// A cell is (b, c): the ratio 3^b * p^c, octave-reduced into [1, 2), p = 7, 5 or 3 (LIMIT; with LIMIT 3 the c axis
// is the octave itself: c = 0 always). b in -LAT_B..LAT_B, c in -LAT_C..LAT_C.
#define LAT_B 3
#define LAT_C 2
float lat_ratio(int limit, int b, int c);                       // in [1, 2)
void  lat_name(int limit, int b, int c, char *buf, int n);      // "7/4", "1/1", "21/16"
float lat_cents(float ratio);                                   // cents above 1/1 (0..1200)
float lat_cents_et(float ratio);                                // cents off the nearest equal-tempered semitone (-50..50)
int   lat_nearest(int limit, float cents, int *b, int *c);      // the cell nearest to that many cents above 1/1; returns 1 if found

// ---------------------------------------------------------------- the engine (inst_engine.c)
#define NVL     6                  // voices in a layer (what the player has)
#define NV      (2 * NVL)          // the engine's slots: two layers, so a new fundamental can fade in over the old
#define NCOPY   8                  // copies per voice at most
#define TAKE_S  2                  // REC: seconds of your tone kept
#define WAVE_N  1024               // the WAVE table: one period of your tone (16 harmonics: plenty of points)
#define ROOM_MAX_S 6               // ROOM PASS: the longest pass
#define HALL_N  4                  // the hall's delay lines

// Controls, UI -> audio: plain 32-bit stores, read once per block by the audio task.
// Every slot: a target frequency (0 = silent) and a target level; the audio task glides both at eng_glide_s (a
// one-pole; "arrived" means within 5 % after that long). The UI decides what each slot plays (instrument.c).
// Slots 0..NVL-1 are layer 0, NVL..NV-1 layer 1. The layers crossfade (equal power, linear in time over eng_fade_s) to
// eng_layer; a slot whose layer is all but silent takes its targets at once (it cannot be heard arriving). While both
// layers sound, the copies are shared out so no more than 40 play at once.
extern volatile float eng_slot_hz[NV];
extern volatile float eng_slot_lvl[NV];        // 0..1
extern volatile float eng_glide_s;             // DRIFT: seconds to arrive (pitch and level)
extern volatile float eng_dense;               // 0 pure .. 1 dense (the knob, smoothed here)
extern volatile int   eng_copies;              // COPIES: copies per voice at DENSE 100 (1..NCOPY); at DENSE d: 1 + round((COPIES-1) d)
extern volatile float eng_beat_hz;             // neighbouring copies beat at this rate (their frequencies BEAT Hz apart)
extern volatile float eng_bright;              // 0..1 (BRIGHT); for SINE it is scaled by DENSE so PURE is sines
extern volatile float eng_diff;                // 0..1: a sub at the difference tone of the two lowest voices
extern volatile int   eng_source;              // SRC_*: a change crossfades (10 ms duck, the tables rebuilt, back in)
extern volatile float eng_pluck_s;             // TANPURA: one string plucked every so often, the slots in turn
extern volatile float eng_jawari;              // TANPURA: 0..1 the curved bridge's buzz (the overtones climb)
extern volatile float eng_wheel_hz;            // GURDY: the wheel's turn (the rumble's rate)
extern volatile float eng_breath;              // SHRUTI: 0..1 the bellows (a slow swell and air noise)
extern volatile float eng_body;                // BOW/REED/GURDY: 0..1 how much body resonance
extern volatile float eng_level;               // LEVEL 0..1
extern volatile float eng_space;               // the hall's send, 0..1
extern volatile float eng_tail;                // the hall's decay, 0 (a small room, about 0.4 s) .. 1 (near endless, about 20 s)
extern volatile float eng_strum_s;             // how long a strum takes to decay
extern volatile float eng_rest;                // 1: the drone rests (REC tapped): everything fades out over 2 s; 0: it plays
extern volatile int   eng_layer;               // the layer that sounds (0, 1): the other fades out
extern volatile float eng_fade_s;              // how long that crossfade takes
extern volatile float eng_vca;                 // the energy's gain on all the voices (0..1); glides over about 30 ms
extern volatile float eng_drive;               // 0..1: the overdrive (energy and shake included)
extern volatile float eng_tone;                // 0..1: the low pass after it (800 Hz .. 16 kHz)
extern volatile float eng_dmix;                // 0..1: how much of the overdriven sound replaces the clean
extern volatile float eng_grit;                // 0..1: noise thrown into the overdrive (a shake)
extern volatile float eng_echo_s;              // the echo's time, 0.02..1.6 s; glides (a tape's bend)
extern volatile float eng_echo_fb;             // 0..0.97
extern volatile float eng_echo_mix;            // 0..1, equal power
extern volatile float eng_zap_gate;            // 0..1: how far each interference burst chops the drone
// BOW (HAND), per slot, what the hand does now (UI -> audio, every tick): the bow's level 0..1.6 (a bow change's
// accent over 1), a brightness push -1..1.5 (pressure, the bow's place on the string), the growl an octave down 0..1.
// The engine follows the level in about 10 ms and lets it ring down over eng_ring_s; eng_hand (HAND) mixes from the
// drone as it is (0) to the bowed one (1), brightness and growl with it.
extern volatile float eng_slot_hand[NV], eng_slot_bright[NV], eng_slot_sub[NV];
extern volatile float eng_hand, eng_ring_s;
extern volatile float eng_detune_hz;           // NEAR FAR: added to every voice's frequency (glides over about 0.2 s)
// FEEDBACK (inst_howl.c, HOWL's loop): the mic through narrow band passes on the tones that sound, back out, held at a
// level by a governor. The bank follows the layer that sounds; each band weighs as its tone (and its bow) does.
enum { FB_OFF, FB_AIR, FB_INNER, FB_BOTH };
extern volatile int   eng_fb_mode;
extern volatile float eng_fb_drive;            // the loop gain asked for (0.92 howls in the inner loop), the energy's push in it
extern volatile float eng_fb_level;            // dB: the loudest a howl gets (the ceiling is 1.25 x that)
extern volatile float eng_fb_ring;             // the band passes' Q
extern volatile float eng_fb_swell;            // s: how slowly a howl may grow
extern volatile float eng_fb_env, eng_fb_gov;  // reported: the howl's level, the governor's gain in dB
// An interference burst: at audio frame `at` (late ones play at once), `len` frames, `amp` 0..1; kind 0 Wi-Fi (a
// noise crackle), 1 Bluetooth (a short buzz). UI task; false when the queue is full.
enum { ZAP_WIFI, ZAP_BLE };
bool  eng_zap(uint32_t at, int len, float amp, int kind);

// What the audio task reports (for the screen and the tests; read any time)
extern volatile float eng_slot_hz_now[NV];     // where each slot's pitch is now (0 when silent)
extern volatile float eng_slot_lvl_now[NV];
extern volatile float eng_out_lvl;             // peak of the output, decaying
extern volatile float eng_diff_hz;             // the difference tone sounding now (0: none)
extern volatile float eng_strum_env;           // the strum's envelope, 0..1+
extern volatile float eng_beat_phase;          // 0..1 at eng_beat_hz, for the screen's breathing
extern volatile int   eng_copies_now;          // copies per voice sounding now
extern volatile float eng_layer_x;             // the crossfade's position: 0 = layer 0 alone .. 1 = layer 1 alone
extern volatile float eng_zap_env;             // the interference sounding now, 0..1 (for the screen)
extern volatile float eng_drive_now;           // the overdrive now, 0..1

void eng_init(void);                           // init(): allocate (app_alloc_fast for lines, app_alloc for the take and the room)
void eng_audio(const float *in, float *out, int n);
void eng_strum(float strength);                // UI: a knock (0..1): every voice swells and brightens, TANPURA plucks all, GURDY buzzes
void eng_hush(void);                           // UI: everything fades out in 10 ms (before a structural change)

// REC: your tone. The audio task records the mic into the take (TAKE_S) from eng_take_begin() to eng_take_end() or
// until full; eng_take_frames says how much. wave_analyse() (UI task, call it every tick until it returns true) finds
// the tone's period and builds the WAVE table (one period, WAVE_N points); eng_wave_hz is the tone it heard (0: no
// steady tone found: the table is left as it was). The WAVE source plays the table at every voice's frequency.
void  eng_take_begin(void);
void  eng_take_end(void);
extern volatile int eng_take_on;               // recording now
extern volatile int eng_take_frames;           // frames in the take so far
extern volatile float eng_take_level;          // what goes in, peak, decaying
bool  wave_analyse(void);                      // UI: a step of the analysis; true when done
extern volatile float eng_wave_hz;             // the tone the WAVE table came from (0: none yet; a built-in default table at boot)
extern volatile int   eng_wave_ready;          // a table exists

// ROOM PASS (Lucier). eng_room_start(passes, frames, base_hz): pass 0 records the mic for `frames` while the drone
// plays as usual; each later pass fades the drone out, plays the last recording (scaled so its peak is -6 dBFS)
// through the output while recording the mic again; after `passes` passes the last recording is the ROOM loop
// (eng_room_state = ROOM_DONE) and the UI switches the source to ROOM. The ROOM source plays the loop at
// each voice's frequency over base_hz (so the lattice transposes the room), each copy its own head, the loop's
// ends crossfaded (20 ms). eng_room_stop() ends it early (what was recorded last stays the loop if a pass finished).
// eng_room_peak is the peak of the last finished pass: the UI aborts with a word when it is near silence
// (headphones: the mic hears no room).
enum { ROOM_IDLE, ROOM_PASSING, ROOM_DONE };
void  eng_room_start(int passes, int frames, float base_hz);
void  eng_room_stop(void);
extern volatile int   eng_room_state;
extern volatile int   eng_room_pass;           // the pass running (0..passes-1) or finished
extern volatile int   eng_room_passes;
extern volatile int   eng_room_pos;            // frames into the pass
extern volatile int   eng_room_frames;         // frames per pass
extern volatile float eng_room_peak;           // peak of the last finished pass
extern volatile float eng_room_base_hz;        // the fundamental when it was made
extern volatile int   eng_room_have;           // a ROOM loop exists (the ROOM source has something to play)
const int16_t *eng_room_loop(int *frames);     // the loop (UI task, for the screen's waveform); NULL if none

// the cost (host only, for the tests): nanoseconds in eng_audio
extern double eng_audio_ns, eng_audio_max_ns;
extern long   eng_audio_blocks;

// ---------------------------------------------------------------- the UI's state, for the screen (instrument.c fills it)
typedef struct {
    int   on;                  // sounding
    int8_t b, c;               // its cell
    int   is_lead, is_root;
    float hz, lvl;             // now (from the engine)
    float age;                 // seconds since it took this cell
    char  name[10];            // "7/4"
} dw_slot_t;

typedef struct {
    int   lead_b, lead_c;      // the cursor's cell (the tilt)
    float cur_b, cur_c;        // the cursor itself, in cells (continuous)
    int   limit;               // LIM_*
    float fund_hz;             // 1/1
    int   voices, root_on, move;
    dw_slot_t slot[NVL];       // the layer that sounds
    char  lead_name[10];       // the lead cell's ratio
    float lead_cents;          // its cents from 1/1
    float lead_et;             // its cents off equal temperament
    float lead_hz;
    float dense;               // the knob 0..1
    int   source;
    float beat_hz, diff_hz;
    float strum;               // the strum's envelope
    int   rest;                // REC tapped: resting
    // REC / WAVE
    int   rec; float rec_s; int analysing; float wave_hz;
    // ROOM PASS
    int   room_state, room_pass, room_passes; float room_frac; float room_peak;
    // the link
    int   peers, rank, spread, follow; float bpm;
    int   knock_frames;        // frames since the last knock (for the screen), big when none
    // ENERGY, SHAKE, the fade, the radios
    float energy;              // 0..1
    float motion;              // the movement now, 0..1
    float shake;               // 0..1
    float fade;                // the crossfade to a new fundamental: 0..1 while it runs, -1 when none
    int   root_b, root_c;      // the fundamental, as a cell from the key's 1/1 (fifths, the LIMIT's prime)
    float root_cents;          // its cents above the key's note, 0..1200
    char  root_name[12];       // "3/2", "" when it is 1/1, "+702C" when too far for a name
    int   radio;               // RAD_*
    float zap;                 // interference now, 0..1
    // TOGETHER and FEEDBACK
    int   dwells;              // other DWELLs heard
    float near;                // NEAR FAR: 1 held together .. 0 across the room; -1 with no DWELL heard
    float apart_hz;            // how fast this box's tones beat against the other's now
    float peer_energy;         // the other DWELL's energy, 0..1
    int   fifth;               // FIFTH_*
    int   npeer;               // the other DWELL's sounding cells, placed on this lattice
    int8_t peer_b[NVL], peer_c[NVL];
    float howl;                // the room's howl now, as a share of FB LEVEL (0..1.25)
    int   fb_mode;             // FB_*
} dw_ui_t;
extern dw_ui_t D;

// ---------------------------------------------------------------- the radios (inst_air.c)
// What the antenna hears, reduced to when, how strong and how long; no address, name or payload is read or kept.
// Wi-Fi: promiscuous mode on the link's own Wi-Fi when the link is on (its channel, 1), or on Wi-Fi of its own on CHANNEL
// when the link is off. Bluetooth: a NimBLE passive scan, started the first time it is wanted. On the host
// (tools/sim) a pretend room: a router's beacons, a phone's traffic, a few Bluetooth devices.
typedef struct { uint32_t frame; uint16_t len; int8_t rssi; uint8_t kind; } air_ev_t;   // kind: ZAP_WIFI, ZAP_BLE
void air_want(bool wifi, bool ble, int channel, bool link_on);   // UI task, every tick: what should be listening
bool air_pop(air_ev_t *e);                                       // the next event heard (UI task)
void air_release(void);                                          // UI: let go of Wi-Fi now (the link is about to start it)
int  air_state(void);                                            // bits: 1 Wi-Fi listening, 2 Bluetooth listening
#ifndef ESP_PLATFORM
extern float air_sim_busy;                                       // the pretend room's traffic, 0 (none) .. 1 (busy), default 0.5
#endif

// ---------------------------------------------------------------- the screens (inst_scene.c)
void scene_draw(const view_t *v);
bool scene_splash(int frame);
void scene_saver(uint32_t frame);
void scene_knock(float strength);              // a knock landed (a ripple)
void scene_cell_new(int b, int c);             // the lead moved to a new cell (a flash there)
