// SNIFF's radio on the host (stands in for firmware/main/inst_radio.c): a made-up flat. Three routers on
// channels 1, 6 and 11 (beacons every 102.4 ms, data to their clients), phones, a laptop, a TV, a speaker,
// a tablet, a printer; ACKs after data frames; phones looking for networks now and then. The tests can
// add a person walking near the router (the CSI moves), a microwave oven (broken frames and a raised
// noise floor on channels 5..11), a crowd of busy devices, and move one phone nearer or further (HUNT).
#include "inst_radio.h"
#include "core.h"
#include <math.h>
#include <string.h>

float hr_walker;              // 0..1: someone walking about near the router
float hr_microwave;           // 0..1: a microwave oven running
float hr_target_rssi = -80;   // the phone that gets hunted (device 4)
int   hr_crowd;               // extra busy devices on channel 6 (0..40)
bool  hr_on = true;           // false: the air goes quiet
int   hr_channel = 1;

static bool started, broken = true;
static uint32_t follow, gen_frame, dropped;
static uint32_t seed = 0xa1b2c3d4u;
static float urand(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return (float)(seed >> 8) / 16777216.0f; }
static float nrand(void) { return urand() * 2.0f - 1.0f; }

enum { ROLE_ROUTER, ROLE_PHONE, ROLE_OTHER };
typedef struct { int chan; float rssi, rate; int role; float next_data, next_beacon, next_probe; } hdev_t;
#define NH (10 + 40)
static hdev_t hd[NH] = {
    { 6, -42, 40, ROLE_ROUTER, 0, 0, 0 },  { 1, -78, 10, ROLE_ROUTER, 0, 0, 0 }, { 11, -70, 15, ROLE_ROUTER, 0, 0, 0 },
    { 6, -55, 15, ROLE_PHONE, 0, 0, 0 },   { 6, -80, 6, ROLE_PHONE, 0, 0, 0 },   { 6, -60, 25, ROLE_OTHER, 0, 0, 0 },
    { 6, -72, 8, ROLE_OTHER, 0, 0, 0 },    { 1, -80, 3, ROLE_OTHER, 0, 0, 0 },   { 11, -66, 5, ROLE_OTHER, 0, 0, 0 },
    { 6, -85, 0.5f, ROLE_OTHER, 0, 0, 0 },
};
static uint32_t who_of(int i) { return (0x9e3779b9u * (uint32_t)(i + 1)) ^ 0x5bd1e995u; }

#define Q 4096
static radio_pkt_t q[Q]; static int qn, qr;
static radio_csi_t cq[256]; static int cqn, cqr;

static void push(const radio_pkt_t *p) { if (qn < Q) q[qn++] = *p; else dropped++; }

static void csi_from(int i, float t, int8_t rssi)
{
    uint32_t w = who_of(i);
    if (follow && w != follow) return;
    if (cqn >= 256) return;
    radio_csi_t c = { .who = w, .rssi = rssi };
    for (int b = 0; b < CSI_BANDS; b++) {
        float base = i == 0 ? 120.0f + 60.0f * sinf((float)b * 0.9f) + 30.0f * cosf((float)b * 2.3f) : 90.0f + 40.0f * sinf((float)b * 0.5f + (float)i);
        float walk = i == 0 ? hr_walker * 0.35f * sinf(6.2832f * 0.7f * t + (float)b * 0.6f) * (0.6f + 0.4f * sinf(6.2832f * 0.23f * t + (float)b)) : 0.0f;
        float a = base * (1.0f + walk + 0.03f * nrand());
        c.amp[b] = (uint8_t)(a < 0 ? 0 : a > 255 ? 255 : a);
    }
    cq[cqn++] = c;
}

static void generate(uint32_t f1)
{
    if (!started || !hr_on) { gen_frame = f1; return; }
    float t0 = (float)gen_frame / (float)AUDIO_SR, t1 = (float)f1 / (float)AUDIO_SR;
    hd[4].rssi = hr_target_rssi;
    int nd = 10 + (hr_crowd > 40 ? 40 : hr_crowd);
    for (int i = 10; i < nd; i++) if (hd[i].rate == 0) hd[i] = (hdev_t){ 6, -50.0f - urand() * 40.0f, 30.0f + urand() * 60.0f, ROLE_OTHER, t0, 0, 0 };
    bool oven = hr_microwave > 0.01f && hr_channel >= 5 && hr_channel <= 11;
    int8_t noise = (int8_t)lroundf(-95.0f + (oven ? 14.0f * hr_microwave : 0.0f) + nrand());
    for (int i = 0; i < nd; i++) {
        hdev_t *d = &hd[i];
        if (d->chan != hr_channel || d->rate <= 0) { d->next_data = t1; d->next_beacon = t1; d->next_probe = t1 + 2.0f; continue; }
        while (d->next_data < t1) {
            float t = d->next_data;
            d->next_data += -logf(urand() + 1e-6f) / d->rate;
            if (t < t0) continue;
            int8_t rs = (int8_t)lroundf(d->rssi + 2.0f * nrand());
            radio_pkt_t p = { .who = who_of(i), .frame = (uint32_t)(t * (float)AUDIO_SR), .len = (uint16_t)(60 + (int)(urand() * urand() * 1440.0f)),
                              .rssi = rs, .noise = noise, .chan = (uint8_t)hr_channel, .kind = PKT_DATA, .sub = 8, .ofdm = 1 };
            push(&p);
            radio_pkt_t ack = { .frame = p.frame, .len = 14, .rssi = (int8_t)(rs - 6), .noise = noise, .chan = (uint8_t)hr_channel, .kind = PKT_CTRL, .sub = 13 };
            push(&ack);
            csi_from(i, t, rs);
        }
        if (d->role == ROLE_ROUTER) while (d->next_beacon < t1) {
            float t = d->next_beacon; d->next_beacon += 0.1024f;
            if (t < t0) continue;
            radio_pkt_t p = { .who = who_of(i), .frame = (uint32_t)(t * (float)AUDIO_SR), .len = 280, .rssi = (int8_t)lroundf(d->rssi + nrand()),
                              .noise = noise, .chan = (uint8_t)hr_channel, .kind = PKT_MGMT, .sub = 8 };
            push(&p);
        }
        if (d->role == ROLE_PHONE) while (d->next_probe < t1) {
            float t = d->next_probe; d->next_probe += 4.0f + 4.0f * urand();
            if (t < t0) continue;
            for (int k = 0; k < 3; k++) {
                radio_pkt_t p = { .who = who_of(i), .frame = (uint32_t)((t + 0.02f * (float)k) * (float)AUDIO_SR), .len = 120, .rssi = (int8_t)lroundf(d->rssi + nrand()),
                                  .noise = noise, .chan = (uint8_t)hr_channel, .kind = PKT_MGMT, .sub = 4 };
                push(&p);
            }
        }
    }
    if (oven && broken) {
        float n = hr_microwave * 300.0f * (t1 - t0);
        int k = (int)n + (urand() < n - floorf(n) ? 1 : 0);
        for (int i = 0; i < k; i++) {
            radio_pkt_t p = { .frame = gen_frame + (uint32_t)(urand() * (float)(f1 - gen_frame)), .len = (uint16_t)(30 + urand() * 600),
                              .rssi = (int8_t)(-70 + (int)(urand() * 20)), .noise = noise, .chan = (uint8_t)hr_channel, .kind = PKT_BAD };
            push(&p);
        }
    }
    gen_frame = f1;
}

bool radio_start(void) { started = true; gen_frame = audio_frames(); return true; }
void radio_stop(void) { started = false; }
void radio_set_channel(int ch) { if (ch >= 1 && ch <= 13) hr_channel = ch; }
void radio_set_broken(bool on) { broken = on; }
void radio_follow(uint32_t who) { follow = who; }
uint32_t radio_dropped(void) { return dropped; }

bool radio_pkt(radio_pkt_t *p)
{
    if (qr >= qn) {
        qn = qr = 0;
        uint32_t now = audio_frames();
        if (now != gen_frame) generate(now);
        if (!qn) return false;
    }
    *p = q[qr++];
    return true;
}

bool radio_csi(radio_csi_t *c)
{
    if (cqr >= cqn) { cqn = cqr = 0; return false; }
    *c = cq[cqr++];
    return true;
}
