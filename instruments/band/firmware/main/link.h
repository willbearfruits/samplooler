// The flock: boards that hear each other over ESP-NOW (Espressif's own protocol on the Wi-Fi radio: no router,
// no pairing, a few milliseconds per packet). Linked boards keep one clock and share a session:
//
//   the time      the tempo (a whole number of frames per beat) and where beat 0 falls, as Ableton Link does:
//                 leaderless, any board may change it and the newest change wins. A board that has a loop
//                 outranks one that has none, so a board joining late never moves a loop that is playing.
//   the harmony   key, scale, chord progression and a chord played live, numbered as JAM, BAND and SING do
//   flags         what each board has (a loop), so a first loop knows whether it may still set the tempo
//
// The clock: the board with the highest priority (then the lowest MAC) is the network clock. Every other board
// measures its offset to it the way NTP does (each packet carries the sender's send time and echoes the times
// of the packets it last heard; the round trips with the smallest delay win) and maps its own audio frames to
// the network's. That offset slews slowly (crystals drift by a few parts per million), so a loop on a board
// that is not the clock stays on the beat without clicks; it jumps only when the first sync lands.
//
// Packets: one broadcast every 100 ms from each board, and one at once after a change of the session. Boards in
// another GROUP (1..8) are ignored. Without the radio the same calls keep a local session: an instrument uses
// one code path, linked or not.
//
// Threads: link_tick() and the setters run in the UI task; link_rx() is called by the platform from its receive
// callback (it only queues); link_origin() and link_offset() may be read from the audio task (lock-free).
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define LINK_PEERS_MAX 7
#define LINK_NAME_LEN  8
#define LINK_PERIOD_MS 100

typedef struct {
    uint32_t ver, owner;     // the change's number and the board that made it (the highest pair wins)
    uint32_t fpb;            // frames per beat at 32 kHz: 60 * 32000 / BPM, a whole number
    uint32_t origin;         // the network frame of beat 0
    uint8_t  bar;            // beats per bar
    uint8_t  beats;          // the session's loop in beats (the first loop made anywhere), 0: none. A session with a
                             // loop outranks any without one
    uint8_t  prio;           // the maker's priority: between equal versions the higher wins (then the owner)
    uint8_t  pad;
} link_time_t;

typedef struct {
    uint32_t ver, owner;
    uint8_t  key, scale, prog, hold, bars;   // 0..11 from C; scale; progression; held chord; chord length (JAM's tables)
    int8_t   live;           // a chord played live (a scale degree), -1 for none
    uint8_t  prio, pad;
} link_harm_t;

enum { LINKF_LOOP = 1 };     // the board has a loop

typedef struct {
    char     name[LINK_NAME_LEN + 1];
    uint32_t id;             // low 32 bits of its MAC
    int8_t   rssi;           // dBm of its last packet
    uint8_t  prio, flags;
    bool     clock;          // it is the network clock
    float    age_s;          // since its last packet
    float    rtt_ms;         // the best round trip lately (0 until measured)
    uint32_t heard, lost;    // packets heard, and missed by its sequence numbers
} link_peer_t;

// ---------------------------------------------------------------- the instrument's side (UI task)
void  link_init(const char *name, int prio);   // once, at init(); prio: who should be the clock (higher wins)
void  link_enable(bool on, int group, int power); // the radio; group 1..8; power 0 near, 1 room, 2 far
bool  link_radio(void);                        // the radio is running
void  link_tick(float dt);                     // 100 Hz, from tick()
int   link_peers(link_peer_t *out, int max);   // the boards heard in the last 1.5 s
bool  link_synced(void);                       // our frames are mapped to the network clock (alone or the clock: true)
bool  link_is_clock(void);                     // this board's frames are the network clock
float link_error_ms(void);                     // how far the mapping still has to slew, for the screen

// The session. The setters make a local change the session's (and send it at once); a change heard from
// another board is applied by link_tick() and reported once by the *_news() calls. The *_boot() calls set
// what this board starts with: version 0, which gives way to any change made anywhere (a board switched on
// late joins the tempo that is playing); between two boards that both only booted, the higher priority wins.
const link_time_t *link_time(void);
const link_harm_t *link_harm(void);
void  link_set_time(uint32_t fpb, uint32_t origin_local, int bar, int beats);  // origin in OUR frames; beats: the loop, 0 none
void  link_set_harm(int key, int scale, int prog, int hold, int bars, int live);
void  link_boot_time(uint32_t fpb, uint32_t origin_local, int bar);
void  link_boot_harm(int key, int scale, int prog, int hold, int bars);
void  link_set_flags(uint8_t flags);
uint8_t link_flags_any(void);                  // our flags or'ed with every peer's
bool  link_time_news(void);                    // the session's time came from another board since the last call
bool  link_harm_news(void);
bool  link_tempo_clash(void);                  // we keep a loop at another tempo than the session's (not synced)

// ---------------------------------------------------------------- musical time on this board (any task)
// Beat 0 of the session in this board's audio frames: a whole frame and a fraction (0..1) after it. Beat b
// starts at local frame origin + frac + b * fpb.
void  link_origin(uint32_t *origin, float *frac);
uint32_t link_fpb(void);
// The network frame minus ours, whole frames and a fraction (0 on the clock board).
void  link_offset(int32_t *whole, float *frac);

// ---------------------------------------------------------------- the platform (link_esp.c, tools/sim)
bool    plat_link_start(int power);            // ESP-NOW up on the fixed channel; false if the radio would not start
void    plat_link_stop(void);
bool    plat_link_send(const void *b, int n);  // broadcast
int64_t plat_link_us(void);                    // a microsecond clock (esp_timer)
int64_t plat_link_epoch(void);                 // that clock's time at this board's audio frame 0
void    plat_link_mac(uint8_t mac[6]);
// called by the platform from its receive callback (any task, never blocks): the packet, when it arrived
void    link_rx(const uint8_t *mac, const void *b, int n, int64_t t_us, int rssi);

// ---------------------------------------------------------------- tests (tools/sim)
// The state lives in one struct; tests that run several boards in one process switch between them.
typedef struct link_state link_state_t;
link_state_t *link_new_state(void);            // a fresh, zeroed state (host only: malloc)
void  link_use(link_state_t *s);               // the state every call above works on (NULL: the built-in one)
