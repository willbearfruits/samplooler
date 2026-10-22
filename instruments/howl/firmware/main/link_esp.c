// The link's radio on the device: ESP-NOW broadcasts on Wi-Fi channel 1, station mode without connecting,
// power save off (a sleeping radio would add tens of milliseconds to some packets). The receive callback runs
// in the Wi-Fi task: it stamps the time and queues the packet for link_tick() (link.c).
//
// Transmit power is kept low on purpose: the boards are in one room, and every packet sent is a burst of
// current from the same supply as the DAC and the speaker amp. NEAR 2 dBm, ROOM 8 dBm, FAR 14 dBm.
#include "link.h"
#include "audio.h"
#include <string.h>
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "esp_log.h"

static const char *TAG = "link";
static bool up;
static const uint8_t bcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
#define CHANNEL 1

static void rx(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    int64_t t = esp_timer_get_time();
    if (!info || !data) return;
    link_rx(info->src_addr, data, len, t, info->rx_ctrl ? info->rx_ctrl->rssi : 0);
}

bool plat_link_start(int power)
{
    if (up) return true;
    esp_err_t e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) { ESP_LOGE(TAG, "event loop: %s", esp_err_to_name(e)); return false; }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if ((e = esp_wifi_init(&cfg)) != ESP_OK) { ESP_LOGE(TAG, "wifi init: %s", esp_err_to_name(e)); return false; }
    if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK || esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK || esp_wifi_start() != ESP_OK) {
        ESP_LOGE(TAG, "wifi would not start"); esp_wifi_deinit(); return false;
    }
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_channel(CHANNEL, WIFI_SECOND_CHAN_NONE);
    static const int8_t q[3] = { 8, 32, 56 };          // quarter dBm
    esp_wifi_set_max_tx_power(q[power < 0 ? 0 : power > 2 ? 2 : power]);
    if ((e = esp_now_init()) != ESP_OK) { ESP_LOGE(TAG, "esp-now: %s", esp_err_to_name(e)); esp_wifi_stop(); esp_wifi_deinit(); return false; }
    esp_now_register_recv_cb(rx);
    esp_now_peer_info_t p = { .channel = CHANNEL, .ifidx = WIFI_IF_STA, .encrypt = false };
    memcpy(p.peer_addr, bcast, 6);
    if (!esp_now_is_peer_exist(bcast)) esp_now_add_peer(&p);
    up = true;
    ESP_LOGI(TAG, "ESP-NOW on channel %d, %d dBm", CHANNEL, q[power < 0 ? 0 : power > 2 ? 2 : power] / 4);
    return true;
}

void plat_link_stop(void)
{
    if (!up) return;
    esp_now_unregister_recv_cb();
    esp_now_deinit();
    esp_wifi_stop();
    esp_wifi_deinit();
    up = false;
    ESP_LOGI(TAG, "radio off");
}

bool plat_link_send(const void *b, int n) { return up && esp_now_send(bcast, b, (size_t)n) == ESP_OK; }
int64_t plat_link_us(void) { return esp_timer_get_time(); }
int64_t plat_link_epoch(void) { return audio_epoch_us(); }
void plat_link_mac(uint8_t mac[6]) { esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY); }
