#include "settings.h"
#include "params.h"
#include "front.h"
#include "audio.h"
#include "diag.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "settings";
static char ns[16];
static bool ok;
#define META_KEY "meta1"          // a new key when front_meta_t changes shape; the old blob is then ignored

void settings_init(void)
{
    int i = 0;
    for (; INST.name[i] && i < 15; i++) ns[i] = (char)tolower((unsigned char)INST.name[i]);
    ns[i] = 0;
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS unusable as it is (%s): formatting it", esp_err_to_name(e));
        nvs_flash_erase();
        e = nvs_flash_init();
    }
    ok = e == ESP_OK;
    if (!ok) ESP_LOGE(TAG, "NVS: %s: settings will not be kept", esp_err_to_name(e));
}

int settings_load(bool with_params)
{
    nvs_handle_t h;
    if (!ok || nvs_open(ns, NVS_READONLY, &h) != ESP_OK) { ESP_LOGI(TAG, "first run: defaults"); return 0; }
    static UI_RAM uint8_t buf[PARAMS_MAX * 8];
    size_t n = sizeof buf;
    int got = 0;
    if (with_params && nvs_get_blob(h, "cur", buf, &n) == ESP_OK) got = params_unpack(buf, (int)n, false);
    front_meta_t m; size_t mn = sizeof m;
    if (nvs_get_blob(h, META_KEY, &m, &mn) == ESP_OK && mn == sizeof m) meta = m;
    nvs_close(h);
    ESP_LOGI(TAG, "loaded %d params; sound %d, tilt %s, %d user sounds", got, meta.sound, meta.calibrated ? "set" : "not set",
             __builtin_popcount(meta.user_mask));
    return got;
}

void settings_save(void)
{
    nvs_handle_t h;
    if (!ok || nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return;
    static UI_RAM uint8_t buf[PARAMS_MAX * 8];
    int n = params_pack(buf, sizeof buf, false);
    diag_crumb("settings save");
    audio_flash_hint();
    bool good = nvs_set_blob(h, "cur", buf, (size_t)n) == ESP_OK && nvs_set_blob(h, META_KEY, &meta, sizeof meta) == ESP_OK
             && nvs_commit(h) == ESP_OK;
    diag_crumb(NULL);
    nvs_close(h);
    if (!good) ESP_LOGW(TAG, "save failed");
}

static void ukey(int k, char *b) { snprintf(b, 4, "u%d", k); }

bool settings_user_save(int k, const uint8_t *b, int n)
{
    nvs_handle_t h;
    if (!ok || k < 0 || k > 7 || nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return false;
    char key[4]; ukey(k, key);
    diag_crumb("user save");
    audio_flash_hint();
    bool good = nvs_set_blob(h, key, b, (size_t)n) == ESP_OK && nvs_commit(h) == ESP_OK;
    diag_crumb(NULL);
    nvs_close(h);
    return good;
}

int settings_user_load(int k, uint8_t *b, int cap)
{
    nvs_handle_t h;
    if (!ok || k < 0 || k > 7 || nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return 0;
    char key[4]; ukey(k, key);
    size_t n = (size_t)cap;
    int r = nvs_get_blob(h, key, b, &n) == ESP_OK ? (int)n : 0;
    nvs_close(h);
    return r;
}

void settings_erase(void)
{
    nvs_handle_t h;
    if (!ok || nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return;
    audio_flash_hint();
    nvs_erase_key(h, "cur");
    for (int k = 0; k < 8; k++) { char key[4]; ukey(k, key); nvs_erase_key(h, key); }
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGW(TAG, "settings and user sounds erased");
}
