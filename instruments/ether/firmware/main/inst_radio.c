// SNIFF's radio on the device (see inst_radio.h). Wi-Fi in WIFI_MODE_NULL with promiscuous mode on: it
// never joins or sends anything. The callbacks run in the Wi-Fi task on core 0; they read the header,
// hash the sender, push 16 bytes into a queue and return.
#include "inst_radio.h"
#include "core.h"
#include "spsc.h"
#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_random.h"

static const char *TAG = "radio";
SPSC(pktq, radio_pkt_t, 512)
SPSC(csiq, radio_csi_t, 32)
static pktq_t pkts;
static csiq_t csis;
static uint32_t salt, dropped;
static volatile uint32_t follow;
static bool running, broken;

static uint32_t hash6(const uint8_t *m)
{
    uint32_t x = 2166136261u ^ salt;                        // FNV-1a, salted
    for (int i = 0; i < 6; i++) { x ^= m[i]; x *= 16777619u; }
    return x ? x : 1;
}

static void rx(void *buf, wifi_promiscuous_pkt_type_t type)
{
    const wifi_promiscuous_pkt_t *p = buf;
    radio_pkt_t o = { .len = (uint16_t)p->rx_ctrl.sig_len, .rssi = (int8_t)p->rx_ctrl.rssi, .noise = (int8_t)p->rx_ctrl.noise_floor,
                      .chan = (uint8_t)p->rx_ctrl.channel, .ofdm = p->rx_ctrl.sig_mode || p->rx_ctrl.rate > 3, .frame = audio_frames() };
    if (p->rx_ctrl.rx_state) o.kind = PKT_BAD;
    else {
        o.kind = type == WIFI_PKT_MGMT ? PKT_MGMT : type == WIFI_PKT_CTRL ? PKT_CTRL : PKT_DATA;
        const uint8_t *h = p->payload;
        o.sub = (h[0] >> 4) & 0x0F;
        // the transmitter (addr2) sits at offset 10 in management and data frames, and in an RTS
        if (p->rx_ctrl.sig_len >= 16 && (o.kind != PKT_CTRL || o.sub == 11)) o.who = hash6(h + 10);
    }
    if (!pktq_push(&pkts, &o)) dropped++;
}

static void csi(void *ctx, wifi_csi_info_t *info)
{
    (void)ctx;
    if (!info || !info->buf || info->len < 128) return;
    uint32_t who = hash6(info->mac), f = follow;
    if (f && who != f) return;
    // LLTF: 64 subcarriers as (imaginary, real) pairs, ordered 0..31 then -32..-1. The data subcarriers
    // are -26..-1 and 1..26: four to a band, from -26 upwards.
    uint16_t sum[CSI_BANDS] = { 0 };
    int k = 0;
    for (int sc = -26; sc <= 26; sc++) {
        if (sc == 0) continue;
        int idx = sc >= 0 ? sc : 64 + sc;
        int im = info->buf[2 * idx], re = info->buf[2 * idx + 1];
        im = im < 0 ? -im : im; re = re < 0 ? -re : re;
        int a = im > re ? im + re / 2 : re + im / 2;          // |z| within about 12 %
        sum[k++ / 4] += (uint16_t)a;
    }
    radio_csi_t c = { .who = who, .rssi = (int8_t)info->rx_ctrl.rssi };
    for (int b = 0; b < CSI_BANDS; b++) c.amp[b] = (uint8_t)(sum[b] > 1020 ? 255 : sum[b] / 4);
    csiq_push(&csis, &c);
}

static void filters(void)
{
    wifi_promiscuous_filter_t f = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA | WIFI_PROMIS_FILTER_MASK_CTRL
                                                   | (broken ? WIFI_PROMIS_FILTER_MASK_FCSFAIL : 0) };
    wifi_promiscuous_filter_t cf = { .filter_mask = WIFI_PROMIS_CTRL_FILTER_MASK_ALL };
    esp_wifi_set_promiscuous_filter(&f);
    esp_wifi_set_promiscuous_ctrl_filter(&cf);
}

bool radio_start(void)
{
    if (running) return true;
    esp_err_t e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) { ESP_LOGE(TAG, "event loop: %s", esp_err_to_name(e)); return false; }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if ((e = esp_wifi_init(&cfg)) != ESP_OK) { ESP_LOGE(TAG, "wifi init: %s", esp_err_to_name(e)); return false; }
    salt = esp_random();
    pktq_clear(&pkts); csiq_clear(&csis);
    if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK || esp_wifi_set_mode(WIFI_MODE_NULL) != ESP_OK || esp_wifi_start() != ESP_OK) {
        ESP_LOGE(TAG, "wifi would not start"); esp_wifi_deinit(); return false;
    }
    filters();
    if (esp_wifi_set_promiscuous_rx_cb(rx) != ESP_OK || esp_wifi_set_promiscuous(true) != ESP_OK) {
        ESP_LOGE(TAG, "promiscuous mode would not start"); esp_wifi_stop(); esp_wifi_deinit(); return false;
    }
    wifi_csi_config_t cc = { .lltf_en = true, .htltf_en = false, .stbc_htltf2_en = false, .ltf_merge_en = false,
                             .channel_filter_en = false, .manu_scale = false };
    if (esp_wifi_set_csi_config(&cc) != ESP_OK || esp_wifi_set_csi_rx_cb(csi, NULL) != ESP_OK || esp_wifi_set_csi(true) != ESP_OK)
        ESP_LOGW(TAG, "no CSI: ROOM will stay quiet");
    running = true;
    radio_set_channel(1);
    ESP_LOGI(TAG, "listening (promiscuous, no address kept)");
    return true;
}

void radio_stop(void)
{
    if (!running) return;
    esp_wifi_set_csi(false);
    esp_wifi_set_promiscuous(false);
    esp_wifi_stop();
    esp_wifi_deinit();
    running = false;
}

void radio_set_channel(int ch) { if (running && ch >= 1 && ch <= 13) esp_wifi_set_channel((uint8_t)ch, WIFI_SECOND_CHAN_NONE); }
void radio_set_broken(bool on) { if (on != broken) { broken = on; if (running) filters(); } }
bool radio_pkt(radio_pkt_t *p) { return pktq_pop(&pkts, p); }
bool radio_csi(radio_csi_t *c) { return csiq_pop(&csis, c); }
void radio_follow(uint32_t who) { follow = who; }
uint32_t radio_dropped(void) { return dropped; }
