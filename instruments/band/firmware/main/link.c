// See link.h. No device code here: link_esp.c is the radio, tools/sim and tools/linktest stand in for it.
#include "link.h"
#include "core.h"
#include "spsc.h"
#include <string.h>
#include <stdlib.h>

#define MAGIC        0x314B4C53u        // "SLK1"
#define PEER_TIMEOUT 1.5f               // s without a packet: gone
#define NSAMP        32                 // clock samples kept per peer (3 to 6 s)
#define SAMP_MAX_AGE 12.0f              // s
#define DELAY_MAX_US 60000              // a round trip longer than this is not a clock sample
#define JUMP_FRAMES  64                 // the mapping is off by more than 2 ms: jump instead of slewing
#define SLEW_MAX     (65536 / 20)       // 0.05 frame per tick, 5 frames per second: 156 ppm, far below hearing
#define FX           65536              // fixed point for frames: 1/65536 frame

typedef struct __attribute__((packed)) { uint32_t to; int64_t t1, t2; } echo_t;
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  group, prio, flags, nech;
    char     name[LINK_NAME_LEN];
    uint32_t id, seq, ref;              // ref: the board the sender takes as the clock
    int64_t  t_send, epoch;             // the sender's clock at sending, and at its audio frame 0
    link_time_t tl;
    link_harm_t hm;
    echo_t   ech[LINK_PEERS_MAX];       // the last packet heard from each peer: its send time, our receive time
} pkt_t;
#define PKT_HEAD ((int)sizeof(pkt_t) - (int)sizeof(echo_t) * LINK_PEERS_MAX)

typedef struct { uint8_t mac[6]; uint8_t n; int8_t rssi; int64_t t; uint8_t b[sizeof(pkt_t)]; } rxpkt_t;
SPSC(link_rxq, rxpkt_t, 8)

typedef struct { int64_t theta, delta; float age; } samp_t;   // age: seconds since it was taken
typedef struct {
    bool     used;
    uint32_t id, ref, last_seq, heard, lost;
    char     name[LINK_NAME_LEN + 1];
    uint8_t  prio, flags;
    int8_t   rssi;
    float    age;
    int64_t  epoch;
    int64_t  ping_t1, ping_t2;          // its last packet: its send time and our receive time
    bool     ping;                      // owed an echo
    samp_t   s[NSAMP];
    int      ns, si;
    float    rtt_ms;
} peer_t;

struct link_state {
    bool     init, radio;
    char     name[LINK_NAME_LEN + 1];
    uint32_t id, seq;
    uint8_t  prio, flags, group;
    peer_t   p[LINK_PEERS_MAX];
    link_time_t tl;
    link_harm_t hm;
    uint32_t seen_tl, seen_hm;          // the highest versions heard, so a change of ours goes above them
    bool     tl_news, hm_news, clash;
    float    clash_age;                 // since a better session at another tempo than our loop's was last heard
    uint32_t loop_fpb;                  // the tempo our loop was made at (flags & LINKF_LOOP)
    uint8_t  loop_beats;                // and the session's loop then
    uint32_t clock;                     // the id of the network clock
    bool     d_ok;                      // the mapping to the clock is known
    bool     tl_local;                  // tl.origin is in our frames: the clock changed and the mapping is not known yet
    bool     meet;                      // a new clock board: take its session as it is (first contact), whatever the versions
    int64_t  d, d_want;                 // network frame - ours, 1/65536 frame (modulo 2^32 frames)
    int64_t  rate;                      // how fast d moves (the crystals' difference), 1/65536 frame per tick, x 1024
    float    send_ms, err_ms;
    bool     send_now;
    uint32_t rnd;
    link_rxq_t q;
    // for the audio task (seqlock)
    volatile uint32_t pub_seq, pub_origin, pub_fpb;
    volatile float    pub_frac, pub_off_f;
    volatile int32_t  pub_off_i;
    uint32_t loc_origin; float loc_frac;   // what was last published
};

static UI_RAM struct link_state builtin;               // the UI task's and the radio's (PSRAM when allowed)
static struct link_state *L = &builtin;

void link_use(link_state_t *s) { L = s ? s : &builtin; }
#ifndef ESP_PLATFORM
link_state_t *link_new_state(void) { return calloc(1, sizeof(struct link_state)); }
#endif

// ---------------------------------------------------------------- helpers
static bool newer(uint32_t va, uint8_t pa, uint32_t oa, uint32_t vb, uint8_t pb, uint32_t ob)
{
    if (va != vb) return va > vb;
    if (pa != pb) return pa > pb;
    return oa > ob;
}
static bool tl_better(const link_time_t *a, const link_time_t *b)
{
    if (a->owner == b->owner) return a->ver > b->ver;    // the maker's own later word stands, loop or not
    if ((a->beats > 0) != (b->beats > 0)) return a->beats > 0;
    return newer(a->ver, a->prio, a->owner, b->ver, b->prio, b->owner);
}
static bool hm_better(const link_harm_t *a, const link_harm_t *b) { return newer(a->ver, a->prio, a->owner, b->ver, b->prio, b->owner); }
static uint32_t id_of(const uint8_t m[6]) { return (uint32_t)m[2] << 24 | (uint32_t)m[3] << 16 | (uint32_t)m[4] << 8 | m[5]; }
static void publish(void)
{
    // beat 0 in our frames: the network origin minus the mapping, a whole frame and a fraction
    int64_t x = ((int64_t)L->tl.origin * FX) - L->d;
    uint32_t o = (uint32_t)(x >> 16);
    float f = (float)(x & 0xFFFF) * (1.0f / 65536.0f);
    L->loc_origin = o; L->loc_frac = f;
    __atomic_add_fetch(&L->pub_seq, 1, __ATOMIC_ACQ_REL);
    L->pub_origin = o; L->pub_frac = f; L->pub_fpb = L->tl.fpb;
    L->pub_off_i = (int32_t)(uint32_t)(L->d >> 16); L->pub_off_f = (float)(L->d & 0xFFFF) * (1.0f / 65536.0f);
    __atomic_add_fetch(&L->pub_seq, 1, __ATOMIC_ACQ_REL);
}

void link_origin(uint32_t *origin, float *frac)
{
    uint32_t s;
    do {
        s = __atomic_load_n(&L->pub_seq, __ATOMIC_ACQUIRE);
        *origin = L->pub_origin; *frac = L->pub_frac;
    } while ((s & 1u) || s != __atomic_load_n(&L->pub_seq, __ATOMIC_ACQUIRE));
}
uint32_t link_fpb(void) { return L->pub_fpb; }
void link_offset(int32_t *whole, float *frac)
{
    uint32_t s;
    do {
        s = __atomic_load_n(&L->pub_seq, __ATOMIC_ACQUIRE);
        *whole = L->pub_off_i; *frac = L->pub_off_f;
    } while ((s & 1u) || s != __atomic_load_n(&L->pub_seq, __ATOMIC_ACQUIRE));
}

// our frame now, as a network frame (1/65536): only to convert origins
static int64_t frames_from_us_fx(int64_t us) { return us * (FX * 32) / 1000; }   // 32 frames per ms

// ---------------------------------------------------------------- setup
void link_init(const char *name, int prio)
{
    if (!L->init) {
        uint8_t mac[6]; plat_link_mac(mac);
        L->id = id_of(mac);
        L->rnd = L->id | 1u;
        L->tl = (link_time_t){ .ver = 0, .owner = L->id, .fpb = 19200, .origin = 0, .bar = 4 };
        L->hm = (link_harm_t){ .ver = 0, .owner = L->id, .live = -1 };
        L->clock = L->id;
        L->init = true;
    }
    int i = 0;
    for (; name[i] && i < LINK_NAME_LEN; i++) L->name[i] = name[i];
    L->name[i] = 0;
    L->prio = (uint8_t)(prio < 0 ? 0 : prio > 255 ? 255 : prio);
    L->tl.prio = L->hm.prio = L->prio;
    link_rxq_clear(&L->q);
    publish();
}

void link_enable(bool on, int group, int power)
{
    L->group = (uint8_t)(group < 1 ? 1 : group > 8 ? 8 : group);
    if (on && !L->radio) {
        L->radio = plat_link_start(power);
        if (!L->radio) app_log("link: the radio would not start");
        L->send_now = true;
    } else if (!on && L->radio) {
        plat_link_stop();
        L->radio = false;
    }
    if (!L->radio) {                                   // alone: our frames are the clock again
        int64_t keep = ((int64_t)L->tl.origin * FX) - L->d;
        memset(L->p, 0, sizeof L->p);
        L->d = L->d_want = 0; L->d_ok = false; L->clock = L->id;
        L->tl.origin = (uint32_t)(keep >> 16);
        publish();
    }
}
bool link_radio(void) { return L->radio; }

// ---------------------------------------------------------------- the session
const link_time_t *link_time(void) { return &L->tl; }
const link_harm_t *link_harm(void) { return &L->hm; }
uint8_t link_flags_any(void)
{
    uint8_t f = L->flags;
    for (int i = 0; i < LINK_PEERS_MAX; i++) if (L->p[i].used) f |= L->p[i].flags;
    return f;
}
bool link_time_news(void) { bool n = L->tl_news; L->tl_news = false; return n; }
bool link_harm_news(void) { bool n = L->hm_news; L->hm_news = false; return n; }
bool link_tempo_clash(void) { return L->clash; }
bool link_is_clock(void) { return L->clock == L->id; }
bool link_synced(void) { return L->clock == L->id || L->d_ok; }
float link_error_ms(void) { return L->err_ms; }

void link_set_time(uint32_t fpb, uint32_t origin_local, int bar, int beats)
{
    if (beats < 0) beats = 0;
    if (beats > 255) beats = 255;
    if (fpb < 3200) fpb = 3200;                        // 600 BPM .. 20 BPM
    if (fpb > 96000) fpb = 96000;
    // our frame to the network's: + the mapping
    int64_t x = (int64_t)origin_local * FX + L->d;
    uint32_t on = (uint32_t)((x + FX / 2) >> 16);
    if (L->tl.fpb == fpb && L->tl.origin == on && L->tl.bar == (uint8_t)bar && L->tl.beats == (uint8_t)beats) return;
    uint32_t v = (L->tl.ver > L->seen_tl ? L->tl.ver : L->seen_tl) + 1;
    L->tl = (link_time_t){ .ver = v, .owner = L->id, .fpb = fpb, .origin = on, .bar = (uint8_t)bar, .beats = (uint8_t)beats, .prio = L->prio };
    L->tl_local = false;
    if (beats) { L->loop_fpb = fpb; L->loop_beats = (uint8_t)beats; }
    L->clash = false;
    L->send_now = true;
    publish();
}

void link_boot_time(uint32_t fpb, uint32_t origin_local, int bar)
{
    if (fpb < 3200) fpb = 3200;
    if (fpb > 96000) fpb = 96000;
    L->tl = (link_time_t){ .ver = 0, .owner = L->id, .fpb = fpb, .origin = origin_local, .bar = (uint8_t)bar, .prio = L->prio };
    L->tl_local = false;
    publish();
}

void link_boot_harm(int key, int scale, int prog, int hold, int bars)
{
    L->hm = (link_harm_t){ .ver = 0, .owner = L->id, .prio = L->prio, .key = (uint8_t)key, .scale = (uint8_t)scale,
                           .prog = (uint8_t)prog, .hold = (uint8_t)hold, .bars = (uint8_t)bars, .live = -1 };
}

void link_set_harm(int key, int scale, int prog, int hold, int bars, int live)
{
    link_harm_t h = { .key = (uint8_t)key, .scale = (uint8_t)scale, .prog = (uint8_t)prog, .hold = (uint8_t)hold,
                      .bars = (uint8_t)bars, .live = (int8_t)(live < 0 ? -1 : live) };
    if (h.key == L->hm.key && h.scale == L->hm.scale && h.prog == L->hm.prog && h.hold == L->hm.hold && h.bars == L->hm.bars
        && h.live == L->hm.live) return;
    h.ver = (L->hm.ver > L->seen_hm ? L->hm.ver : L->seen_hm) + 1;
    h.owner = L->id; h.prio = L->prio;
    L->hm = h;
    L->send_now = true;
}

void link_set_flags(uint8_t f)
{
    bool had = L->flags & LINKF_LOOP, has = f & LINKF_LOOP;
    L->flags = f;
    if (has && !had) {
        L->loop_fpb = L->tl.fpb;
        if (L->tl.beats) L->loop_beats = L->tl.beats;
        else if (L->loop_beats) {                        // the session's loop is ours now: say so
            L->tl.beats = L->loop_beats; L->tl.ver = (L->tl.ver > L->seen_tl ? L->tl.ver : L->seen_tl) + 1; L->tl.owner = L->id; L->tl.prio = L->prio;
        }
    }
    if (!has && had) {
        L->clash = false;
        L->loop_beats = 0;
        if (L->tl.beats && L->tl.owner == L->id) {       // our loop is gone: the tempo is free again (unless a peer keeps it)
            L->tl.beats = 0; L->tl.ver = (L->tl.ver > L->seen_tl ? L->tl.ver : L->seen_tl) + 1;
        }
    }
    if (has != had) L->send_now = true;
}

// ---------------------------------------------------------------- peers
static peer_t *peer_find(uint32_t id, bool make)
{
    peer_t *free_p = NULL, *oldest = NULL;
    for (int i = 0; i < LINK_PEERS_MAX; i++) {
        peer_t *p = &L->p[i];
        if (p->used && p->id == id) return p;
        if (!p->used && !free_p) free_p = p;
        if (p->used && (!oldest || p->age > oldest->age)) oldest = p;
    }
    if (!make) return NULL;
    peer_t *p = free_p ? free_p : oldest;
    memset(p, 0, sizeof *p);
    p->used = true; p->id = id;
    return p;
}

int link_peers(link_peer_t *out, int max)
{
    int n = 0;
    for (int i = 0; i < LINK_PEERS_MAX && n < max; i++) {
        const peer_t *p = &L->p[i];
        if (!p->used) continue;
        link_peer_t *o = &out[n++];
        memcpy(o->name, p->name, sizeof o->name);
        o->id = p->id; o->rssi = p->rssi; o->prio = p->prio; o->flags = p->flags; o->clock = p->id == L->clock;
        o->age_s = p->age; o->rtt_ms = p->rtt_ms; o->heard = p->heard; o->lost = p->lost;
    }
    return n;
}

static void choose_clock(void)
{
    uint32_t best = L->id; uint8_t bp = L->prio;
    for (int i = 0; i < LINK_PEERS_MAX; i++) {
        const peer_t *p = &L->p[i];
        if (!p->used) continue;
        if (p->prio > bp || (p->prio == bp && p->id < best)) { best = p->id; bp = p->prio; }
    }
    if (best == L->clock) return;
    // the session's origin was in the old clock's frames: keep it where it is in ours
    int64_t keep = ((int64_t)L->tl.origin * FX) - L->d;
    L->clock = best;
    L->d = L->d_want = 0;
    L->d_ok = best == L->id;
    L->meet = best != L->id;
    L->tl_local = !L->d_ok;                              // until the mapping is known, the origin stays in our frames
    L->tl.origin = (uint32_t)((keep + FX / 2) >> 16);
    app_log("link: the clock is %s", best == L->id ? "this board" : "another board");
}

// ---------------------------------------------------------------- receive
void link_rx(const uint8_t *mac, const void *b, int n, int64_t t_us, int rssi)
{
    if (n < PKT_HEAD || n > (int)sizeof(pkt_t)) return;
    rxpkt_t r;
    memcpy(r.mac, mac, 6); r.n = (uint8_t)n; r.rssi = (int8_t)rssi; r.t = t_us;
    memcpy(r.b, b, (size_t)n);
    link_rxq_push(&L->q, &r);
}

static void handle(const rxpkt_t *r)
{
    pkt_t k;
    memset(&k, 0, sizeof k);
    memcpy(&k, r->b, r->n);
    if (k.magic != MAGIC || k.group != L->group || k.id == L->id) return;
    int nech = k.nech > LINK_PEERS_MAX ? LINK_PEERS_MAX : k.nech;
    if (r->n < PKT_HEAD + nech * (int)sizeof(echo_t)) return;
    peer_t *p = peer_find(k.id, true);
    bool fresh = p->heard == 0;
    if (!fresh && k.seq > p->last_seq + 1 && k.seq - p->last_seq < 1000) p->lost += k.seq - p->last_seq - 1;
    p->last_seq = k.seq; p->heard++;
    memcpy(p->name, k.name, LINK_NAME_LEN); p->name[LINK_NAME_LEN] = 0;
    p->prio = k.prio; p->flags = k.flags; p->rssi = r->rssi; p->age = 0; p->epoch = k.epoch; p->ref = k.ref;
    p->ping_t1 = k.t_send; p->ping_t2 = r->t; p->ping = true;
    if (fresh) { app_log("link: %s joined", p->name); L->send_now = true; }
    // our ping, answered: an NTP sample (their clock minus ours, and the round trip without their wait)
    for (int i = 0; i < nech; i++) {
        if (k.ech[i].to != L->id) continue;
        int64_t t1 = k.ech[i].t1, t2 = k.ech[i].t2, t3 = k.t_send, t4 = r->t;
        int64_t delta = (t4 - t1) - (t3 - t2);
        if (delta < 0 || delta > DELAY_MAX_US) break;
        samp_t *s = &p->s[p->si];
        s->theta = ((t2 - t1) + (t3 - t4)) / 2; s->delta = delta; s->age = 0;
        p->si = (p->si + 1) % NSAMP;
        if (p->ns < NSAMP) p->ns++;
        break;
    }
    // the session: only from a board on the same clock (the origin is in its frames)
    link_time_t ktl; link_harm_t khm;                   // aligned copies (the packet is packed)
    memcpy(&ktl, &k.tl, sizeof ktl); memcpy(&khm, &k.hm, sizeof khm);
    if (ktl.ver > L->seen_tl) L->seen_tl = ktl.ver;
    if (khm.ver > L->seen_hm) L->seen_hm = khm.ver;
    choose_clock();
    bool same = ktl.ver == L->tl.ver && ktl.owner == L->tl.owner && ktl.prio == L->tl.prio && ktl.beats == L->tl.beats;
    // meeting a clock board: its session is the one to join (two boards that ran apart do not keep whichever made
    // more changes), unless we have a loop and it has none: then ours outranks it and it will take ours
    if (L->meet && k.id == L->clock && k.ref == L->clock) {
        L->meet = false;
        if (!((L->flags & LINKF_LOOP) && !ktl.beats)) {
            if (!((L->flags & LINKF_LOOP) && ktl.fpb != L->loop_fpb)) {
                bool moved = ktl.fpb != L->tl.fpb || ktl.origin != L->tl.origin || ktl.beats != L->tl.beats || L->tl_local;
                L->tl = ktl; L->tl_local = false; if (moved) L->tl_news = true;
            } else { L->clash = true; L->clash_age = 0; }
            L->hm = khm; L->hm_news = true;
            return;
        }
    }
    if (k.ref == L->clock && tl_better(&ktl, &L->tl)) {
        if ((L->flags & LINKF_LOOP) && ktl.fpb != L->loop_fpb) { L->clash = true; L->clash_age = 0; }   // our loop keeps its tempo
        else {
            bool moved = ktl.fpb != L->tl.fpb || ktl.origin != L->tl.origin || ktl.bar != L->tl.bar || ktl.beats != L->tl.beats || L->tl_local;
            L->tl = ktl;
            L->tl_local = false;
            L->clash = false;
            if (moved) L->tl_news = true;
        }
    } else if (same && k.id == L->clock && k.ref == L->clock && (ktl.origin != L->tl.origin || L->tl_local)) {
        // the same session as the clock board writes it: its frames are the network's, so its numbers win
        int32_t dd = (int32_t)(ktl.origin - L->tl.origin);
        L->tl.origin = ktl.origin;
        if (L->tl_local || dd > JUMP_FRAMES || dd < -JUMP_FRAMES) L->tl_news = true;
        L->tl_local = false;
    }
    // the harmony too only from a board that has joined this clock: a board just met sends what it had alone, and the
    // clock board must not take that before the newcomer has taken the clock board's
    if (k.ref == L->clock && hm_better(&khm, &L->hm)) { L->hm = khm; L->hm_news = true; }
}

// ---------------------------------------------------------------- the clock mapping
static void map_clock(void)
{
    if (L->clock == L->id) { L->d = L->d_want = 0; L->rate = 0; L->d_ok = true; L->err_ms = 0; return; }
    peer_t *c = peer_find(L->clock, false);
    if (!c || c->ns == 0) { L->err_ms = 0; return; }
    // the sample with the shortest round trip lately is the one least bent by waiting in a queue
    int best = -1;
    for (int i = 0; i < c->ns; i++) if (c->s[i].age < SAMP_MAX_AGE && (best < 0 || c->s[i].delta < c->s[best].delta)) best = i;
    if (best < 0) return;
    c->rtt_ms = (float)c->s[best].delta * 0.001f;
    // our frame f is at our time epoch + f / 32 ms; that is their time + theta; their frame there is the network's.
    // The sample is a little old: carry it forward at the rate the mapping has been moving.
    int64_t us = plat_link_epoch() + c->s[best].theta - c->epoch;
    L->d_want = frames_from_us_fx(us) + (L->rate / 1024) * (int64_t)(c->s[best].age * 100.0f);
    int64_t e = L->d_want - L->d;
    e = (int64_t)((uint64_t)e << 16) >> 16;              // modulo 2^32 frames (the frame counters wrap)
    L->err_ms = (float)(e < 0 ? -e : e) * (1.0f / (FX * 32.0f));
    if (!L->d_ok || e > (int64_t)JUMP_FRAMES * FX || e < -(int64_t)JUMP_FRAMES * FX) {
        if (L->d_ok) app_log("link: the clock moved %.1f ms: jumping", (double)L->err_ms);
        L->d = L->d_want; L->d_ok = true; L->tl_news = true; L->rate = 0;
        if (L->tl_local) {                               // the origin we kept in our frames, now in the network's
            L->tl.origin = (uint32_t)(((int64_t)L->tl.origin * FX + L->d + FX / 2) >> 16);
            L->tl_local = false;
        }
        return;
    }
    // a PI servo, as NTP and PTP use: the integral learns the crystals' difference, so the mapping follows the
    // drift without lagging behind it and the noise of single samples averages out over seconds
    L->rate += e / 80;                                   // Ki 1/80000 per tick (x 1024 in rate)
    const int64_t rmax = (int64_t)SLEW_MAX * 1024;
    if (L->rate > rmax) L->rate = rmax;
    if (L->rate < -rmax) L->rate = -rmax;
    int64_t step = L->rate / 1024 + e / 200;             // Kp 1/200: damping about 0.7, settles in about 20 s
    if (step > SLEW_MAX) step = SLEW_MAX;
    if (step < -SLEW_MAX) step = -SLEW_MAX;
    L->d += step;
}

// ---------------------------------------------------------------- send
static void send(void)
{
    pkt_t k;
    memset(&k, 0, sizeof k);
    k.magic = MAGIC; k.group = L->group; k.prio = L->prio; k.flags = L->flags;
    memcpy(k.name, L->name, LINK_NAME_LEN);
    k.id = L->id; k.seq = ++L->seq; k.ref = L->tl_local ? 0xFFFFFFFFu : L->clock;   // an origin not yet in network frames is not to be taken
    k.tl = L->tl; k.hm = L->hm;
    int n = 0;
    for (int i = 0; i < LINK_PEERS_MAX; i++) {
        peer_t *p = &L->p[i];
        if (!p->used || !p->ping) continue;
        k.ech[n++] = (echo_t){ .to = p->id, .t1 = p->ping_t1, .t2 = p->ping_t2 };
        p->ping = false;
    }
    k.nech = (uint8_t)n;
    k.epoch = plat_link_epoch();
    k.t_send = plat_link_us();
    plat_link_send(&k, PKT_HEAD + n * (int)sizeof(echo_t));
}

void link_tick(float dt)
{
    if (!L->init) return;
    rxpkt_t r;
    for (int i = 0; i < 8 && link_rxq_pop(&L->q, &r); i++) handle(&r);
    // peers age and go
    bool gone = false;
    for (int i = 0; i < LINK_PEERS_MAX; i++) {
        peer_t *p = &L->p[i];
        if (!p->used) continue;
        p->age += dt;
        for (int k = 0; k < p->ns; k++) p->s[k].age += dt;
        if (p->age > PEER_TIMEOUT) { app_log("link: %s left", p->name); p->used = false; gone = true; }
    }
    if (gone) choose_clock();
    map_clock();
    // our loop at a tempo whose session lost its loop (the board that made it cleared it): ours keeps it
    if ((L->flags & LINKF_LOOP) && !L->tl.beats && L->loop_beats && L->tl.fpb == L->loop_fpb) {
        L->tl.beats = L->loop_beats; L->tl.ver = (L->tl.ver > L->seen_tl ? L->tl.ver : L->seen_tl) + 1; L->tl.owner = L->id; L->tl.prio = L->prio;
        L->send_now = true;
    }
    L->clash_age += dt;
    if (L->clash && (!(L->flags & LINKF_LOOP) || L->clash_age > 1.0f)) L->clash = false;
    if (link_synced()) publish();
    if (!L->radio) return;
    L->send_ms -= dt * 1000.0f;
    if (L->send_now || L->send_ms <= 0) {
        send();
        L->send_now = false;
        L->rnd ^= L->rnd << 13; L->rnd ^= L->rnd >> 17; L->rnd ^= L->rnd << 5;
        L->send_ms = (float)LINK_PERIOD_MS - 10.0f + (float)(L->rnd % 21u);   // a little jitter: two boards never lock in step
    }
}
