// DWELL's radios: what the antenna hears, as interference (inst_dwell.h, "the radios"). Each Wi-Fi frame or Bluetooth
// advert is reduced to when it arrived, how strong and how long; no address, name or payload is read or kept. The
// callbacks run in the radios' tasks and only push into a lock-free queue; instrument.c turns the events into bursts.
//
// Wi-Fi: with the link on, its own Wi-Fi (station mode, channel 1) is put in promiscuous mode as well, so the link keeps
// working and DWELL hears channel 1 (the other boards' packets among it). With the link off, DWELL starts Wi-Fi of its
// own (WIFI_MODE_NULL, never joining or sending) on CHANNEL. Bluetooth: NimBLE as a passive observer (ETHER's, from
// CHIRP), started the first time it is wanted and then only paused; ESP-IDF's software coexistence shares the antenna.
//
// The host (tools/sim) has a pretend room instead: a router's beacons every 102.4 ms, a phone's traffic, four Bluetooth
// devices advertising on the three channels.
#include "inst_dwell.h"
#include "spsc.h"
#include "link.h"
#include <string.h>

SPSC(airq, air_ev_t, 256)
static airq_t q;
static bool want_wifi, want_ble;

bool air_pop(air_ev_t *e) { return airq_pop(&q, e); }

#ifdef ESP_PLATFORM
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"

static const char *TAG = "air";
enum { W_NONE, W_ON_LINK, W_OWN };
static int wmode = W_NONE, wchan = 1;
static volatile bool ble_inited, ble_synced, ble_scanning;

static void wifi_rx(void *buf, wifi_promiscuous_pkt_type_t type)
{
    (void)type;
    if (!want_wifi) return;
    const wifi_promiscuous_pkt_t *p = buf;
    air_ev_t e = { .frame = audio_frames(), .len = (uint16_t)p->rx_ctrl.sig_len, .rssi = (int8_t)p->rx_ctrl.rssi, .kind = ZAP_WIFI };
    airq_push(&q, &e);
}

static void promisc(bool on)
{
    if (on) {
        wifi_promiscuous_filter_t f = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA };
        esp_wifi_set_promiscuous_filter(&f);
        esp_wifi_set_promiscuous_rx_cb(wifi_rx);
    }
    esp_wifi_set_promiscuous(on);
}

static bool own_start(int ch)
{
    esp_err_t e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return false;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) { ESP_LOGW(TAG, "wifi init failed"); return false; }
    if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK || esp_wifi_set_mode(WIFI_MODE_NULL) != ESP_OK || esp_wifi_start() != ESP_OK) {
        ESP_LOGW(TAG, "wifi would not start"); esp_wifi_deinit(); return false;
    }
    promisc(true);
    esp_wifi_set_channel((uint8_t)ch, WIFI_SECOND_CHAN_NONE);
    ESP_LOGI(TAG, "listening on channel %d (promiscuous, nothing kept)", ch);
    return true;
}
static void own_stop(void) { promisc(false); esp_wifi_stop(); esp_wifi_deinit(); }

void air_release(void)
{
    if (wmode == W_OWN) own_stop();
    else if (wmode == W_ON_LINK && link_radio()) promisc(false);
    wmode = W_NONE;
}

// Bluetooth, a passive scan: a 100 ms interval, listening half of it (Wi-Fi has the other half)
static int gap_event(struct ble_gap_event *ev, void *arg);
static void scan_start(void)
{
    uint8_t own;
    if (ble_hs_id_infer_auto(0, &own) != 0) own = BLE_OWN_ADDR_PUBLIC;
    struct ble_gap_disc_params p = { .itvl = 160, .window = 80, .filter_policy = 0, .limited = 0, .passive = 1, .filter_duplicates = 0 };
    if (ble_gap_disc(own, BLE_HS_FOREVER, &p, gap_event, NULL) == 0) ble_scanning = true;
}
static int gap_event(struct ble_gap_event *ev, void *arg)
{
    (void)arg;
    if (ev->type == BLE_GAP_EVENT_DISC) {
        if (!want_ble) return 0;
        air_ev_t e = { .frame = audio_frames(), .len = ev->disc.length_data, .rssi = ev->disc.rssi, .kind = ZAP_BLE };
        airq_push(&q, &e);
    } else if (ev->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        ble_scanning = false;
        if (want_ble) scan_start();
    }
    return 0;
}
static void on_sync(void) { ble_synced = true; if (want_ble) scan_start(); }
static void on_reset(int reason) { ble_scanning = false; ESP_LOGW(TAG, "bluetooth host reset (%d)", reason); }
static void host_task(void *p) { (void)p; nimble_port_run(); nimble_port_freertos_deinit(); }

void air_want(bool wifi, bool ble, int channel, bool link_on)
{
    want_wifi = wifi; want_ble = ble;
    // Wi-Fi
    bool lw = link_radio();
    if (wmode == W_ON_LINK && !lw) wmode = W_NONE;                    // the link stopped its Wi-Fi, and our listening with it
    if (wifi) {
        if (lw) {
            if (wmode == W_OWN) own_stop();
            if (wmode != W_ON_LINK) { promisc(true); wmode = W_ON_LINK; ESP_LOGI(TAG, "listening on the link's channel"); }
        } else if (!link_on) {
            if (wmode != W_OWN) { if (own_start(channel)) { wmode = W_OWN; wchan = channel; } }
            else if (channel != wchan) { esp_wifi_set_channel((uint8_t)channel, WIFI_SECOND_CHAN_NONE); wchan = channel; }
        }
    } else if (wmode != W_NONE) air_release();
    // Bluetooth
    if (ble && !ble_inited) {
        if (nimble_port_init() == ESP_OK) {
            ble_hs_cfg.sync_cb = on_sync;
            ble_hs_cfg.reset_cb = on_reset;
            nimble_port_freertos_init(host_task);
            ble_inited = true;
            ESP_LOGI(TAG, "bluetooth listening (passive, nothing kept)");
        } else ESP_LOGW(TAG, "bluetooth would not start");
    }
    if (ble_synced) {
        if (ble && !ble_scanning) scan_start();
        else if (!ble && ble_scanning && ble_gap_disc_active()) { ble_gap_disc_cancel(); ble_scanning = false; }
    }
}

int air_state(void) { return (wmode != W_NONE ? 1 : 0) | (ble_scanning ? 2 : 0); }

#else
// ---------------------------------------------------------------- the host's pretend room
float air_sim_busy = 0.5f;
#include <math.h>
static uint32_t rs = 0x2545F491u;
static uint32_t next_beacon, next_data, next_adv[4];
static bool started;
static float urand(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return (float)(rs >> 8) / 16777216.0f; }
static void push(uint32_t f, int len, int rssi, int kind) { air_ev_t e = { .frame = f, .len = (uint16_t)len, .rssi = (int8_t)rssi, .kind = (uint8_t)kind }; airq_push(&q, &e); }

void air_release(void) { }
void air_want(bool wifi, bool ble, int channel, bool link_on)
{
    (void)channel; (void)link_on;
    want_wifi = wifi; want_ble = ble;
    uint32_t now = audio_frames();
    if (!started) {
        started = true;
        next_beacon = now + 1000; next_data = now + 2000;
        static const int ivl[4] = { 3200, 8000, 19200, 32000 };
        for (int k = 0; k < 4; k++) next_adv[k] = now + (uint32_t)ivl[k] / 2 + (uint32_t)k * 777;
    }
    // a router's beacons, 102.4 ms apart, always
    while ((int32_t)(now - next_beacon) >= 0) { if (wifi) push(next_beacon, 300, -58, ZAP_WIFI); next_beacon += 3277; }
    // a phone's traffic: bursts of frames, as often as the room is busy
    while ((int32_t)(now - next_data) >= 0) {
        if (wifi && air_sim_busy > 0.0f) {
            int k = 1 + (int)(urand() * 5.0f);
            uint32_t f = next_data;
            for (int i = 0; i < k; i++) { push(f, 60 + (int)(urand() * 1400.0f), -48 - (int)(urand() * 25.0f), ZAP_WIFI); f += 10 + (uint32_t)(urand() * 60.0f); }
        }
        float rate = 2.0f + 40.0f * air_sim_busy;                        // bursts a second
        next_data += 1 + (uint32_t)(-logf(1.0f - urand() * 0.999f) / rate * (float)AUDIO_SR);
    }
    // four Bluetooth devices: each advert goes out on channels 37, 38 and 39, 0.4 ms apart
    static const int ivl[4] = { 3200, 8000, 19200, 32000 }, rss[4] = { -62, -70, -78, -85 };
    for (int d = 0; d < 4; d++)
        while ((int32_t)(now - next_adv[d]) >= 0) {
            if (ble) for (int ch = 0; ch < 3; ch++) push(next_adv[d] + (uint32_t)ch * 13, 25, rss[d], ZAP_BLE);
            next_adv[d] += (uint32_t)ivl[d] + (uint32_t)(urand() * 320.0f);   // and 0 to 10 ms of jitter, as the spec asks
        }
}
int air_state(void) { return (want_wifi ? 1 : 0) | (want_ble ? 2 : 0); }
#endif
