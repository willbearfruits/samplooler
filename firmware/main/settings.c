#include "settings.h"
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "recorder.h"
#include "audio.h"
#include "diag.h"

static const char *TAG = "settings";
#define NS   "samplooler"
#define KEY  "settings"

// One entry per setting: a stable tag and where it lives. Floats are stored x1000.
typedef struct { char tag[4]; int32_t v; } __attribute__((packed)) entry_t;
enum { T_INT, T_BOOL, T_F1000 };
typedef struct { const char *tag; int type; size_t off; } field_t;
#define F(tag, type, m) { tag, type, offsetof(settings_t, m) }
static const field_t fields[] = {
    F("mic ", T_INT, mic_db),   F("mode", T_INT, mode),     F("ptch", T_INT, pitch_st), F("tilt", T_BOOL, tilt),
    F("smax", T_F1000, spd_max), F("smin", T_F1000, spd_min), F("snap", T_BOOL, snap), F("grn ", T_INT, grain_ms),
    F("vol ", T_INT, vol_pct),  F("inpt", T_INT, input),    F("slot", T_INT, slot),
};
#define NF (sizeof fields / sizeof fields[0])

static bool    ready, dirty;
static int64_t t_change;

static int32_t get(const settings_t *s, const field_t *f)
{
    const char *p = (const char *)s + f->off;
    switch (f->type) {
    case T_BOOL:  return *(const bool *)p ? 1 : 0;
    case T_F1000: return (int32_t)(*(const float *)p * 1000.0f + (*(const float *)p < 0 ? -0.5f : 0.5f));
    default:      return *(const int *)p;
    }
}
static void put(settings_t *s, const field_t *f, int32_t v)
{
    char *p = (char *)s + f->off;
    switch (f->type) {
    case T_BOOL:  *(bool *)p = v != 0; break;
    case T_F1000: *(float *)p = (float)v / 1000.0f; break;
    default:      *(int *)p = (int)v; break;
    }
}

bool settings_init(settings_t *s)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS %s: erasing it", esp_err_to_name(e));
        nvs_flash_erase(); e = nvs_flash_init();
    }
    if (e != ESP_OK) { ESP_LOGE(TAG, "NVS unavailable (%s): settings will not persist", esp_err_to_name(e)); return false; }
    ready = true;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) { ESP_LOGI(TAG, "no saved settings, using defaults"); return true; }
    entry_t ent[32]; size_t len = sizeof ent;
    e = nvs_get_blob(h, KEY, ent, &len);
    nvs_close(h);
    if (e != ESP_OK || len % sizeof(entry_t)) { ESP_LOGI(TAG, "no saved settings, using defaults"); return true; }
    int n = (int)(len / sizeof(entry_t)), used = 0;
    for (int i = 0; i < n; i++)
        for (unsigned k = 0; k < NF; k++)
            if (memcmp(ent[i].tag, fields[k].tag, 4) == 0) { put(s, &fields[k], ent[i].v); used++; break; }
    ESP_LOGI(TAG, "loaded %d of %d saved settings", used, n);
    return true;
}

static void write_now(const settings_t *s)
{
    dirty = false;
    if (!ready) return;
    entry_t ent[NF];
    for (unsigned k = 0; k < NF; k++) { memcpy(ent[k].tag, fields[k].tag, 4); ent[k].v = get(s, &fields[k]); }
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) { ESP_LOGE(TAG, "cannot open NVS for writing"); return; }
    diag_crumb("settings save");
    audio_flash_hint();
    esp_err_t e = nvs_set_blob(h, KEY, ent, sizeof ent);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    diag_crumb(NULL);
    if (e != ESP_OK) ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(e));
    else ESP_LOGI(TAG, "saved");
}

void settings_changed(void) { dirty = true; t_change = esp_timer_get_time(); }

void settings_erase(void)
{
    dirty = false;
    if (!ready) return;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    diag_crumb("settings erase");
    audio_flash_hint();
    esp_err_t e = nvs_erase_all(h);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    diag_crumb(NULL);
    ESP_LOGW(TAG, "erased (%s)", esp_err_to_name(e));
}
bool settings_pending(void) { return dirty; }

void settings_poll(const settings_t *s)
{
    if (!dirty || !ready) return;
    int64_t age = esp_timer_get_time() - t_change;
    if (age < (int64_t)SETTINGS_DEBOUNCE_MS * 1000) return;
    int st = recorder_state();
    if (st == REC_RECORDING) return;                                                 // never: the take is being written
    if (st == REC_PLAYING && age < (int64_t)SETTINGS_MAX_DEFER_MS * 1000) return;    // wait for an idle moment
    write_now(s);
}

void settings_flush(const settings_t *s) { if (dirty && recorder_state() != REC_RECORDING) write_now(s); }
