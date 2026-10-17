// CHIRP's own pieces, shared by instrument.c, the synth (inst_synth.c), the scenes (inst_scene.c), the
// advertisement parser (inst_adv.c), the radio (inst_ble.c on the device, tools/sim/sim_ble.c on the host).
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "core.h"

// ---------------------------------------------------------------- what the radio hands over
// A coarse guess at what sent an advertisement, from its manufacturer, services and appearance.
enum { CAT_PHONE, CAT_EARBUDS, CAT_WATCH, CAT_TRACKER, CAT_BEACON, CAT_LAPTOP, CAT_SPEAKER, CAT_OTHER, CAT_N };
extern const char *const cat_name[CAT_N];

typedef struct {
    uint32_t who;              // salted hash of the address (a new salt every boot); never 0
    int8_t   rssi;             // dBm
    uint8_t  cat;              // CAT_*
    uint8_t  adv_type;         // 0 connectable, 1 directed, 2 scannable, 3 not connectable, 4 scan response
    uint8_t  len;              // payload bytes
} ble_adv_t;

typedef void (*ble_cb_t)(const ble_adv_t *a);   // runs in the radio's task: push it somewhere and return
bool ble_start(ble_cb_t cb);   // UI task, at init; false if Bluetooth would not start
void ble_tick(void);           // UI task, every tick (the host stand-in makes its crowd here; a no-op on the device)
bool ble_running(void);

// inst_adv.c: the category from the raw advertising data (AD structures); the salted hash
uint8_t  adv_category(const uint8_t *data, int len, uint8_t adv_type);
uint32_t adv_hash(const uint8_t addr[6], uint8_t addr_type, uint32_t salt);

// ---------------------------------------------------------------- the synth (inst_synth.c, audio task)
enum { SP_CHIRP, SP_WHISTLE, SP_TRILL, SP_WARBLE, SP_PIP, SP_COO, SP_BELL, SP_DROP, SP_N };
enum { TR_CALL, TR_PULSE, TR_HELLO, TR_BYE, TR_GRAIN, TR_GEIGER };
typedef struct {
    uint32_t at;               // audio frame to start on (audio_frames() clock); the past means now
    float    hz;               // its pitch
    float    near;             // 0 far .. 1 near: loudness, brightness, how dry
    float    len;              // length multiplier
    uint8_t  species, kind;    // SP_*, TR_*
    uint8_t  spot;             // the spotlit bird: louder
} trig_t;
void synth_init(void);                         // allocates the reverb (UI task, at init)
bool synth_trigger(const trig_t *t);           // UI task; false if the queue is full
void synth_audio(const float *in, float *out, int n);
// the synth's settings, written by the UI task (plain floats), read per block
typedef struct {
    float sweep, bright, reverb, room, dist, air, tone, swarm_air;   // tone in Hz; swarm_air: the crowd's rate 0..1
} synth_set_t;
extern volatile synth_set_t S;
extern volatile float synth_level;             // what came out, for the LED and the scene (peak, decaying)

// ---------------------------------------------------------------- the birds (instrument.c), for the scenes
#define MAXB 24
typedef struct {
    uint32_t who;
    bool     alive, leaving;
    uint8_t  cat, species, wire, slot;
    float    near;             // 0..1 from RSSI, smoothed
    float    rssi;             // dBm, smoothed
    float    interval;         // seconds between its adverts (estimated)
    float    phase;            // CLOCKS: 0..1 to the next pulse
    int      midi;             // its note
    float    call_t;           // seconds since its last call (beak, bob, note)
    float    arrive;           // 0 -> 1 flying in
    float    leave;            // 0 -> 1 flying off
    float    seen;             // seconds since its last advert
    float    born;             // when it arrived (seconds since boot)
    uint32_t adverts;
} bird_t;
extern bird_t birds[MAXB];
extern int    spot;            // the spotlit bird (index) or -1
extern int    mode;            // MODE_*
enum { MODE_CHORUS, MODE_CLOCKS, MODE_SWARM, MODE_HUNT };
extern float  calls_per_s, adverts_per_s;
extern int    bird_count;
extern float  hunt_trend;      // dB per second, + = getting nearer
extern bool   radio_failed;

// inst_scene.c
void scene_note(int b, float near);            // a call: open the beak, pop a note (UI task)
void scene_draw(const view_t *v);
bool scene_splash(int f);
void scene_saver(uint32_t f);
uint32_t chirp_rand(void);
