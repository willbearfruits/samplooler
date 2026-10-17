#include "sim_ble.h"
#include "inst_ble.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

sim_dev_t sim_dev[SIM_DEVS];
float sim_loss = 0.12f, sim_rssi_noise = 3.0f;
static ble_cb_t cb;
static uint32_t salt = 0x5a17u, seed = 0xb1e5u;
static bool running;

static float urand(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return (float)(seed >> 8) / 16777216.0f; }

// payloads as real devices send them (AD structures: length, type, data)
typedef struct { const char *name; uint8_t kind, adv_type, len; uint8_t d[31]; } payload_t;
static const payload_t P[] = {
    { "iPhone (Nearby Info)", CAT_PHONE, 0, 14, { 2, 0x01, 0x1A, 10, 0xFF, 0x4C, 0x00, 0x10, 0x05, 0x0B, 0x1C, 0x7E, 0x22, 0x5C } },
    { "Samsung phone", CAT_PHONE, 0, 12, { 2, 0x01, 0x1A, 8, 0xFF, 0x75, 0x00, 0x42, 0x09, 0x01, 0x02, 0x03 } },
    { "AirPods (case open)", CAT_EARBUDS, 0, 15, { 2, 0x01, 0x1A, 11, 0xFF, 0x4C, 0x00, 0x07, 0x19, 0x01, 0x0E, 0x20, 0x55, 0xAA, 0xB4 } },
    { "Fast Pair headphones", CAT_EARBUDS, 0, 10, { 2, 0x01, 0x06, 6, 0x16, 0x2C, 0xFE, 0x00, 0x01, 0x02 } },
    { "Garmin watch", CAT_WATCH, 0, 9, { 2, 0x01, 0x06, 5, 0xFF, 0x87, 0x00, 0x11, 0x22 } },
    { "heart-rate band", CAT_WATCH, 0, 7, { 2, 0x01, 0x06, 3, 0x03, 0x0D, 0x18 } },
    { "AirTag (Find My)", CAT_TRACKER, 3, 15, { 2, 0x01, 0x00, 11, 0xFF, 0x4C, 0x00, 0x12, 0x19, 0x10, 0x88, 0x01, 0x02, 0x03, 0x04 } },
    { "Tile", CAT_TRACKER, 0, 11, { 2, 0x01, 0x06, 3, 0x03, 0xED, 0xFE, 3, 0x16, 0xED, 0xFE } },
    { "iBeacon", CAT_BEACON, 3, 30, { 2, 0x01, 0x06, 26, 0xFF, 0x4C, 0x00, 0x02, 0x15, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 0, 1, 0, 2, 0xC5 } },
    { "Eddystone", CAT_BEACON, 3, 11, { 3, 0x03, 0xAA, 0xFE, 6, 0x16, 0xAA, 0xFE, 0x10, 0x00, 0x01 } },
    { "Windows laptop", CAT_LAPTOP, 3, 12, { 2, 0x01, 0x1A, 8, 0xFF, 0x06, 0x00, 0x01, 0x09, 0x20, 0x02, 0x5B } },
    { "speaker (appearance)", CAT_SPEAKER, 0, 7, { 2, 0x01, 0x06, 3, 0x19, 0x41, 0x08 } },
    { "flags only", CAT_OTHER, 0, 3, { 2, 0x01, 0x06 } },
};
#define NP_ ((int)(sizeof P / sizeof P[0]))
int sim_payload_count(void) { return NP_; }
int sim_payload_kind(int p) { return P[p].kind; }
const char *sim_payload_name(int p) { return P[p].name; }

void sim_crowd_clear(void) { memset(sim_dev, 0, sizeof sim_dev); }

int sim_crowd_add(int payload, float rssi, float interval)
{
    for (int i = 0; i < SIM_DEVS; i++) {
        if (sim_dev[i].on) continue;
        sim_dev_t *d = &sim_dev[i];
        memset(d, 0, sizeof *d);
        d->on = true; d->payload = payload; d->kind = P[payload].kind; d->rssi = rssi; d->interval = interval;
        for (int k = 0; k < 6; k++) d->addr[k] = (uint8_t)(urand() * 256.0f);
        d->addr[5] |= 0xC0;                                    // a random static address
        d->who = adv_hash(d->addr, 1, salt);
        d->next = (float)audio_frames() / (float)AUDIO_SR + urand() * interval;
        return i;
    }
    return -1;
}

void sim_crowd_default(void)
{
    sim_crowd_clear();
    sim_crowd_add(0, -58, 0.20f);    // an iPhone on the table
    sim_crowd_add(1, -76, 0.50f);    // a Samsung across the room
    sim_crowd_add(2, -50, 0.25f);    // AirPods, case open
    sim_crowd_add(4, -66, 1.00f);    // a watch
    sim_crowd_add(6, -82, 2.00f);    // an AirTag in a bag
    sim_crowd_add(8, -70, 0.10f);    // an iBeacon on the wall
    sim_crowd_add(10, -86, 1.10f);   // a laptop next door
    sim_crowd_add(11, -62, 0.30f);   // a speaker
    sim_crowd_add(7, -89, 0.95f);    // a Tile
    sim_crowd_add(9, -93, 0.50f);    // an Eddystone beacon, far
    sim_crowd_add(12, -78, 0.60f);   // something
    sim_crowd_add(3, -72, 0.35f);    // headphones
}

bool sim_ble_fail;                      // the next ble_start() fails (ETHER's one-radio-down check)
int  sim_ble_share = 50;
bool ble_start(ble_cb_t c)
{
    if (sim_ble_fail) { cb = NULL; running = false; return false; }
    cb = c; running = true; if (!sim_dev[0].on) sim_crowd_default(); return true;
}
void ble_set_share(int pct) { sim_ble_share = pct; }
bool ble_running(void) { return running; }

void ble_tick(void)
{
    if (!cb) return;
    float now = (float)audio_frames() / (float)AUDIO_SR;
    for (int i = 0; i < SIM_DEVS; i++) {
        sim_dev_t *d = &sim_dev[i];
        if (!d->on) continue;
        int guard = 0;
        while (d->next <= now && guard++ < 50) {
            d->next += d->interval + 0.010f * urand();            // advDelay: 0..10 ms on top of the interval
            if (urand() < sim_loss || urand() * 100.0f > (float)sim_ble_share + 30.0f) { d->lost++; continue; }   // lost, or Wi-Fi had the air
            const payload_t *p = &P[d->payload];
            ble_adv_t a = { .who = d->who, .adv_type = p->adv_type, .len = p->len };
            float r = d->rssi + sim_rssi_noise * (urand() + urand() + urand() - 1.5f) * 1.4f;
            a.rssi = (int8_t)lroundf(r < -127 ? -127 : r > 0 ? 0 : r);
            a.cat = adv_category(p->d, p->len, p->adv_type);
            d->sent++;
            cb(&a);
        }
        if (d->next < now - 1.0f) d->next = now;                 // a device that was just added far in the past
    }
}
int sim_payload_parse(int p) { return adv_category(P[p].d, P[p].len, P[p].adv_type); }
