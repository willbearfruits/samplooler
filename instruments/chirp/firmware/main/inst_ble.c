// The radio, on the device: NimBLE as an observer only. A passive scan with the window as long as the
// interval (always listening) and duplicates let through, so every advertisement that arrives is handed
// over. The callback runs in NimBLE's host task: it hashes the address, guesses the kind of device
// (inst_adv.c) and pushes that to instrument.c's queue. Nothing else leaves this file.
#include "inst_chirp.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"

static const char *TAG = "ble";
static ble_cb_t user_cb;
static uint32_t salt;
static volatile bool running;

static int gap_event(struct ble_gap_event *ev, void *arg);

static void start_scan(void)
{
    uint8_t own;
    if (ble_hs_id_infer_auto(0, &own) != 0) own = BLE_OWN_ADDR_PUBLIC;
    // 100 ms interval and window (units of 0.625 ms): always listening, hopping 37 -> 38 -> 39
    struct ble_gap_disc_params p = { .itvl = 160, .window = 160, .filter_policy = 0, .limited = 0, .passive = 1, .filter_duplicates = 0 };
    int rc = ble_gap_disc(own, BLE_HS_FOREVER, &p, gap_event, NULL);
    if (rc) ESP_LOGW(TAG, "scan would not start (%d)", rc);
    else { running = true; ESP_LOGI(TAG, "listening"); }
}

static int gap_event(struct ble_gap_event *ev, void *arg)
{
    (void)arg;
    if (ev->type == BLE_GAP_EVENT_DISC) {
        const struct ble_gap_disc_desc *d = &ev->disc;
        ble_adv_t a = { .rssi = d->rssi, .adv_type = d->event_type, .len = d->length_data };
        a.who = adv_hash(d->addr.val, d->addr.type, salt);
        a.cat = adv_category(d->data, d->length_data, d->event_type);
        ble_cb_t cb = user_cb;
        if (cb) cb(&a);
    } else if (ev->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        running = false;
        start_scan();                                           // it should not end; if it does, go again
    }
    return 0;
}

static void on_sync(void) { start_scan(); }
static void on_reset(int reason) { running = false; ESP_LOGW(TAG, "host reset (%d)", reason); }
static void host_task(void *p) { (void)p; nimble_port_run(); nimble_port_freertos_deinit(); }

bool ble_start(ble_cb_t cb)
{
    user_cb = cb;
    salt = esp_random();                                        // a new salt every boot: the same device is a new bird tomorrow
    esp_err_t e = nimble_port_init();
    if (e != ESP_OK) { ESP_LOGE(TAG, "NimBLE would not start: %s", esp_err_to_name(e)); return false; }
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    nimble_port_freertos_init(host_task);
    return true;
}

void ble_tick(void) { }
bool ble_running(void) { return running; }
