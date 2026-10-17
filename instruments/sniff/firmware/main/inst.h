// SNIFF inside: instrument.c (params, sounds, what the radio heard -> events), inst_sound.c (the audio
// engine), inst_scene.c (the screen), inst_radio.c (Wi-Fi, device only). This header is what they share.
#pragma once
#include "core.h"
#include "dsp.h"
#include "inst_radio.h"

// ---------------------------------------------------------------- params (instrument.c's table, in this order)
enum {
    // EARS
    P_MODE, P_PKT, P_ROOM, P_SENS, P_STORM, P_TICKS, P_REACH, P_DENS, P_NEXT,
    // TONE
    P_VOICE, P_SCALE, P_KEY, P_REG, P_SPREAD, P_TRANS, P_RING, P_LENR, P_BRIGHT,
    // RHYTHM
    P_GRID, P_TEMPO, P_SWING, P_PERSTEP,
    // SPACE
    P_VERB, P_SIZE, P_DIST, P_ECHO, P_ETIME, P_EFB,
    // RADIO
    P_CHAN, P_DWELL, P_BEACON, P_BROKEN,
    // MOTION
    P_TILT, P_LEAN, P_STX, P_STY,
    P_N
};
enum { MODE_LISTEN, MODE_HUNT };
enum { V_AUTO, V_PLUCK, V_BELL, V_KALIMBA, V_DROP, V_CHIRP, V_BEEP, V_STATIC, V_N };
enum { CH_AUTO = 0, CH_HOP = 1 };                       // then channels 1..13 at 2..14

// ---------------------------------------------------------------- the sound engine (inst_sound.c)
enum { T_PLUCK, T_BELL, T_KALIMBA, T_DROP, T_CHIRP, T_BEEP, T_STATIC, T_TICK, T_N };
typedef struct {
    uint32_t at;         // audio frame to start at (late ones start at once)
    float    f;          // Hz
    float    amp;        // 0..1
    float    ring;       // seconds to fade by 60 dB (T_BEEP: its length)
    float    bright;     // 0..1
    float    wet;        // 0..1 of it to the reverb (far devices are wetter)
    uint8_t  timbre;     // T_*
} strike_t;

#define PAD_N 8
typedef struct {         // UI -> audio: plain floats, smoothed on the audio side
    float pkt, room, storm, ticks;          // layer levels 0..1
    float bright;                           // 0..1
    float verb, size, echo, efb, echo_s;    // reverb mix, size 0..1, echo mix, echo feedback, echo time (s)
    float pad_f[PAD_N], pad_g[PAD_N];       // ROOM: the chord and its band gains
    float motion;                           // ROOM: how much the room's signal is moving, 0..1
    float crackle;                          // STORM: crackles a second
    float rumble;                           // STORM: 0..1
    float rain;                             // drops a second (packets over the DENSITY budget)
    float hunt, hunt_rate, hunt_pitch;      // HUNT: on (0/1), beeps a second, Hz
    uint32_t thunder;                       // counts up: a clap
} snd_ctl_t;
extern volatile snd_ctl_t sc;
extern volatile float snd_out_pkt, snd_out_room, snd_out_storm;   // audio -> UI: each layer's level
extern volatile int   snd_voices;                                  // voices sounding
extern volatile uint32_t snd_beeps;                                // HUNT beeps so far

void snd_init(void);                 // UI task, at boot: the reverb and echo lines
bool snd_strike(const strike_t *s);  // UI task; false when the queue is full
void snd_audio(const float *in, float *out, int n);

// ---------------------------------------------------------------- what the screen shows (instrument.c fills it)
#define NDEV 40
typedef struct {
    uint32_t who;        // 0: a free slot
    float    rssi;       // dBm, smoothed
    float    rate;       // frames a second, smoothed
    float    pulse;      // 1 when it just sent, falls to 0 (the radar's ring)
    float    ang;        // radar angle, radians, from the hash
    uint32_t last;       // tick it was last heard
    uint16_t hits, csi;  // frames and CSI frames since the last second
    uint8_t  chan, ap;   // its channel; 1 if it sends beacons (a router)
} wdev_t;

typedef struct {
    wdev_t    dev[NDEV];
    int      ndev;                 // heard in the last 30 s
    int      chan;                 // the channel listened to now
    float    hist[14];             // frames a second per channel (1..13)
    float    rate;                 // frames a second, all
    float    shape[CSI_BANDS];     // the room's radio shape, 1 = average
    bool     room_live;            // CSI arriving from a source
    float    motion;               // 0..1
    float    storm;                // 0..1
    float    thunder;              // 1 at a clap, falls
    float    sniff;                // 1 when a frame was heard, falls (the dog's nose)
    bool     radio_ok;
    bool     scanning;             // AUTO is looking for the busiest channel
    // HUNT
    int      mode;
    int      target;               // index into dev[], -1: none
    float    target_rssi;          // smoothed
    float    prox;                 // 0..1
    int      trend;                // -1 colder, 0 steady, 1 warmer
    bool     lost;                 // not heard for 5 s
    int      target_rank, target_of;
} sniff_t;
extern sniff_t S;

void scene_draw(const view_t *v);
bool scene_splash(int frame);
void scene_saver(uint32_t frame);
void scene_heard(int dev, int kind);   // a device sent a frame that sounded (radar pulse, a scent for the dog)
