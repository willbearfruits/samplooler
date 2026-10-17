// ETHER inside: instrument.c (params, sounds, both radios -> one stream of events), inst_sound.c (one
// audio engine for both worlds), inst_scene.c (one screen), inst_radio.c (Wi-Fi, from SNIFF),
// inst_ble.c + inst_adv.c (Bluetooth LE, from CHIRP). This header is what they share.
#pragma once
#include "core.h"
#include "dsp.h"
#include "inst_radio.h"
#include "inst_ble.h"

// ---------------------------------------------------------------- params (instrument.c's table, in this order: CC 20 + index)
enum {
    // MIX
    P_MODE, P_BAL, P_PKT, P_ROOM, P_SENS, P_STORM, P_BIRDS, P_DENS, P_REACH, P_NEXT,
    // TONE
    P_SCALE, P_KEY, P_REG, P_SPREAD, P_TRANS, P_WVOICE, P_BVOICE, P_RING, P_BRIGHT,
    // BIRDS
    P_FLOCK, P_CLEN, P_REST, P_PHRASE, P_SLOW, P_MAXB,
    // RHYTHM
    P_GRID, P_TEMPO, P_SWING, P_PERSTEP,
    // SPACE
    P_VERB, P_SIZE, P_DIST, P_ECHO, P_ETIME, P_EFB,
    // CONTROL
    P_CHAN, P_DWELL, P_BTTIME, P_BEACON, P_TILT, P_LEAN, P_STX, P_STY,
    P_N
};
enum { MODE_LISTEN, MODE_HUNT };
enum { FL_CHORUS, FL_CLOCKS, FL_SWARM };
enum { WV_AUTO, WV_PLUCK, WV_BELL, WV_KALIMBA, WV_DROP, WV_BEEP, WV_STATIC, WV_N };
enum { CH_AUTO = 0, CH_HOP = 1 };                     // then Wi-Fi channels 1..13 at 2..14
enum { W_WIFI, W_BT };                                // the two worlds

// ---------------------------------------------------------------- the sound engine (inst_sound.c)
// Wi-Fi: a frame strikes a voice (SNIFF's)
enum { T_PLUCK, T_BELL, T_KALIMBA, T_DROP, T_BEEP, T_STATIC, T_TICK, T_N };
typedef struct {
    uint32_t at;         // audio frame to start at (late ones start at once)
    float    f, amp, ring, bright, wet;
    uint8_t  timbre;     // T_*
} strike_t;
// Bluetooth: a bird calls (CHIRP's)
enum { SP_CHIRP, SP_WHISTLE, SP_TRILL, SP_WARBLE, SP_PIP, SP_COO, SP_BELL, SP_DROP, SP_N };
enum { TR_CALL, TR_PULSE, TR_HELLO, TR_BYE, TR_GRAIN };
typedef struct {
    uint32_t at;         // audio frame to start on
    float    hz, near, len;
    uint8_t  species, kind;
} trig_t;

#define PAD_N 8
typedef struct {         // UI -> audio: plain floats, ramped on the audio side
    float wifi, bt;                         // BALANCE as two bus gains, 0..1
    float pkt, room, storm, birds;          // layer levels 0..1
    float bright, sweep, dist;              // 0..1; the birds' glides (1 = as CHIRP); DISTANCE 0..1
    float verb, size, echo, efb, echo_s;    // reverb mix, size 0..1, echo mix, echo feedback, echo time (s)
    float pad_f[PAD_N], pad_g[PAD_N], motion;   // ROOM
    float crackle, rumble;                  // STORM
    float rain;                             // Wi-Fi frames over the budget, as drops a second
    float air;                              // the Bluetooth crowd as wind, 0..1
    float hunt, hunt_rate, hunt_pitch;      // HUNT's beeper
    uint32_t thunder;                       // counts up: a clap
} snd_ctl_t;
extern volatile snd_ctl_t sc;
extern volatile float snd_out_wifi, snd_out_bt, snd_out_room, snd_out_storm;   // audio -> UI: each layer's level
extern volatile int   snd_voices;
extern volatile uint32_t snd_beeps;

void snd_init(void);                 // UI task, at boot: the reverb and echo lines (PSRAM)
bool snd_strike(const strike_t *s);  // UI task; false when the queue is full
bool snd_call(const trig_t *t);
void snd_audio(const float *in, float *out, int n);

// ---------------------------------------------------------------- what the screen shows (instrument.c fills it)
#define NDEV 40
typedef struct {                     // a Wi-Fi sender
    uint32_t who;                    // 0: a free slot
    float    rssi, rate, pulse;      // dBm (smoothed), frames a second, 1 when it just sounded
    uint32_t last;                   // tick it was last heard
    uint16_t hits, csi;
    uint8_t  chan, ap;               // its channel; 1 if it sends beacons (a router)
} wdev_t;

#define MAXB 24
typedef struct {                     // a Bluetooth advertiser, a bird
    uint32_t who;
    bool     alive, leaving;
    uint8_t  cat, species, wire, slot;
    float    near, rssi;             // 0..1 from RSSI; dBm, smoothed
    float    interval, phase;        // seconds between adverts (estimated); CLOCKS: 0..1 to the next pulse
    float    call_t, arrive, leave, seen;   // seconds since its last call; 0 -> 1 flying in / off; seconds since heard
    uint32_t adverts;
} bird_t;

typedef struct {
    wdev_t   dev[NDEV];
    int      ndev;                   // Wi-Fi senders heard in the last 30 s
    bird_t   birds[MAXB];
    int      nbirds;
    int      chan;                   // the Wi-Fi channel now
    float    hist[14];               // frames a second per channel
    float    rate, adverts;          // Wi-Fi frames a second; adverts a second
    float    shape[CSI_BANDS];       // the room's radio shape, 1 = average
    bool     room_live;
    float    motion, storm, thunder; // 0..1; thunder: 1 at a clap, falls
    float    sniff;                  // 1 when a Wi-Fi frame sounded, falls
    float    look;                   // 1 when a near bird sang, falls: the dog looks up
    int      look_bird;
    bool     wifi_ok, bt_ok, scanning;
    float    bal_shown;              // seconds left to show the balance bar
    int      mode, flock;
    // HUNT: one device from either radio
    int      tw;                     // W_WIFI or W_BT
    int      target;                 // index into dev[] or birds[], -1: none
    float    target_rssi, prox;
    int      trend, target_rank, target_of;
    bool     lost;
} ether_t;
extern ether_t E;

void radios_start(void);             // init() starts both; tools/sim calls it again to fail one
void scene_draw(const view_t *v);
bool scene_splash(int frame);
void scene_saver(uint32_t frame);
void scene_heard(int dev);           // a Wi-Fi sender's frame sounded
void scene_call(int bird);           // a bird called
