// The link's radio on the host (for instruments that use link.c): the board under test and pretend boards in the
// same room, each with a link state of its own and a crystal that runs at its own rate. Packets wait a little
// before the air and on arrival, as on the device; nothing is lost unless a test asks.
//
// The board under test is the one the instrument runs; its clock is its audio frames (frame 0 at 1 s).
// A test adds a pretend board with sim_peer_add(), then acts as it between sim_peer_use(k) and sim_peer_use(-1)
// (any link_* call in between is that board's: change the tempo, the key, say it has a loop).
#include "sim.h"
#include "link.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define NPEER 3
typedef struct { link_state_t *st; uint8_t mac[6]; double off, ppm, epoch; bool on; } speer_t;
static speer_t P[NPEER];
static int npeer, cur = -1;
double sim_link_loss;                            // 0..1: share of packets lost on the way
static uint32_t lseed = 0x51ed;
static double lrnd(void) { lseed ^= lseed << 13; lseed ^= lseed >> 17; lseed ^= lseed << 5; return (double)(lseed % 1000000u) / 1e6; }

static double now_true(void) { return (double)audio_frames() * 31.25 + 1e6; }
static double clk(int k, double t)
{
    if (k < 0 || k >= NPEER) return t;
    return P[k].off + t * (1.0 + P[k].ppm * 1e-6);
}

typedef struct { double t; int to; uint8_t from[6]; int n; uint8_t b[256]; } air_t;
static air_t air[256];
static int nair;
static bool board_on;
static const uint8_t board_mac[6] = { 0x90, 0xDA, 0x72, 0x76, 0x00, 0xD0 };

bool plat_link_start(int power) { (void)power; if (cur < 0) board_on = true; else P[cur].on = true; return true; }
void plat_link_stop(void) { if (cur < 0) board_on = false; else P[cur].on = false; }
bool plat_link_send(const void *b, int n)
{
    double ta = now_true() + lrnd() * 1500.0;
    for (int k = -1; k < npeer; k++) {
        if (k == cur) continue;
        bool on = k < 0 ? board_on : P[k].on;
        if (!on) continue;
        if (lrnd() < sim_link_loss || nair >= 256) continue;
        air_t *a = &air[nair++];
        a->t = ta + 150.0 + lrnd() * 300.0; a->to = k; memcpy(a->from, cur < 0 ? board_mac : P[cur].mac, 6);
        a->n = n; memcpy(a->b, b, (size_t)n);
    }
    return true;
}
int64_t plat_link_us(void) { return (int64_t)clk(cur, now_true()); }
int64_t plat_link_epoch(void) { return cur < 0 ? 1000000 : (int64_t)P[cur].epoch; }
void plat_link_mac(uint8_t mac[6]) { memcpy(mac, cur < 0 ? board_mac : P[cur].mac, 6); }

void sim_peer_use(int k) { cur = k; link_use(k < 0 ? NULL : P[k].st); }

int sim_peer_add(const char *name, int prio, double ppm, double off_us)
{
    if (npeer < 0 || npeer >= NPEER) return -1;
    int k = npeer++;
    speer_t *p = &P[k];
    p->st = link_new_state();
    for (int i = 0; i < 6; i++) p->mac[i] = (uint8_t)(0x40 + 9 * k + i);
    p->off = off_us; p->ppm = ppm;
    p->epoch = clk(k, now_true()) + 0.5e6;
    int was = cur;
    sim_peer_use(k);
    link_init(name, prio);
    link_boot_time(19200, 0, 4);
    link_boot_harm(0, 0, 1, 0, 0);
    link_enable(true, 1, 1);
    sim_peer_use(was);
    return k;
}
void sim_peer_power(int k, bool on) { int was = cur; sim_peer_use(k); link_enable(on, 1, 1); sim_peer_use(was); }

// where a board is in the session's beats at the true time now (k = -1: the board under test)
double sim_peer_beats(int k)
{
    int was = cur;
    sim_peer_use(k);
    uint32_t o; float f; link_origin(&o, &f);
    double fr = k < 0 ? (double)audio_frames() : (clk(k, now_true()) - P[k].epoch) / 31.25;
    double rel = fmod(fr - (double)o - (double)f, 4294967296.0);
    if (rel < 0) rel += 4294967296.0;
    if (rel > 2147483648.0) rel -= 4294967296.0;
    double b = rel / (double)link_time()->fpb;
    sim_peer_use(was);
    return b;
}
// the beats of a pretend board against the board under test's, ms (modulo a beat)
float sim_peer_gap_ms(int k)
{
    double d = sim_peer_beats(k) - sim_peer_beats(-1);
    d -= floor(d + 0.5);
    int was = cur; sim_peer_use(-1);
    double ms = d * link_time()->fpb * 31.25 / 1000.0;
    sim_peer_use(was);
    return (float)ms;
}

void sim_tick_hook(void)
{
    double t = now_true();
    for (int i = 0; i < nair;) {
        if (air[i].t <= t + 10000.0) {
            int k = air[i].to;
            sim_peer_use(k);
            link_rx(air[i].from, air[i].b, air[i].n, (int64_t)clk(k, air[i].t), -45);
            air[i] = air[--nair];
        } else i++;
    }
    for (int k = 0; k < npeer; k++) { sim_peer_use(k); link_tick(0.01f); }
    sim_peer_use(-1);
}
