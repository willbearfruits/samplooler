// The tuning table (tune.h). Pure C apart from the NVS part at the end.
#include "tune.h"
#include <stdio.h>
#include <string.h>

#define TUNE_DEF(id, nm, kd, lo, hi, st, df, un, lb, hp) \
    [TU_##id] = { .name = nm, .unit = un, .labels = lb, .help = hp, .kind = kd, .min = lo, .max = hi, .step = st, .def = df },
const tune_def_t tune_def[TU_N] = { TUNE_TABLE(TUNE_DEF) };
#undef TUNE_DEF

float tune_v[TU_N];
static uint32_t gen = 1;
static bool dirty;
static void (*action_fn)(int id);

void tune_defaults(void)
{
    for (int i = 0; i < TU_N; i++) tune_v[i] = tune_def[i].def;     // exact: no snapping, so v1 is v1 to the bit
    gen++; dirty = true;
}

float tune_set(int i, float v)
{
    if (i < 0 || i >= TU_N) return 0;
    const tune_def_t *d = &tune_def[i];
    if (d->kind == TK_ACTION) { if (v >= 1.0f && action_fn) action_fn(i); return 0; }
    uint32_t bits; memcpy(&bits, &v, 4);
    if ((bits & 0x7f800000u) == 0x7f800000u) return tune_v[i];       // NaN or inf: kept as it was
    if (v < d->min) v = d->min;
    if (v > d->max) v = d->max;
    if (d->kind != TK_FLOAT) v = (float)(int)(v + (v >= 0 ? 0.5f : -0.5f));
    if (v != tune_v[i]) { tune_v[i] = v; gen++; dirty = true; }
    return tune_v[i];
}

uint32_t tune_gen(void) { return gen; }
bool tune_dirty(void) { return dirty; }
void tune_on_action(void (*fn)(int id)) { action_fn = fn; }

int tune_find(const char *name)
{
    for (int i = 0; i < TU_N; i++) if (!strcmp(tune_def[i].name, name)) return i;
    return -1;
}

static const char *KIND[] = { "float", "int", "enum", "bool", "action" };

int tune_line(int i, char *b, int n)
{
    if (i < 0 || i >= TU_N) return 0;
    const tune_def_t *d = &tune_def[i];
    int k = snprintf(b, n, "i=%d\nn=%s\nk=%s\nlo=%g\nhi=%g\nst=%g\ndf=%g\nv=%g\nu=%s\nl=%s\nh=%s",
                     i, d->name, KIND[d->kind], (double)d->min, (double)d->max, (double)d->step, (double)d->def,
                     (double)tune_v[i], d->unit, d->labels, d->help);
    return k < n ? k : n - 1;
}

// Presets: changes from v1. A name, then (id, value) pairs.
typedef struct { const char *name; int n; struct { int id; float v; } set[8]; } preset_t;
static const preset_t PRESETS[] = {
    { "v1", 0, { { 0, 0 } } },
    { "gyro", 4, { { TU_MO_FUSION, 1 }, { TU_MO_RATE, 1 }, { TU_MO_COMP_TAU, 0.6f }, { TU_MO_ACC_GATE, 0.2f } } },
    { "gyro detents", 6, { { TU_MO_FUSION, 1 }, { TU_MO_RATE, 1 }, { TU_MO_COMP_TAU, 0.6f }, { TU_MO_ACC_GATE, 0.2f },
                           { TU_TI_DET, 2 }, { TU_TI_DET_W, 0.12f } } },
    { "mahony", 5, { { TU_MO_FUSION, 2 }, { TU_MO_RATE, 1 }, { TU_MO_KP, 2 }, { TU_MO_KI, 0.05f }, { TU_MO_ACC_GATE, 0.2f } } },
};
#define NPRESET (int)(sizeof PRESETS / sizeof PRESETS[0])

int tune_presets(void) { return NPRESET; }
const char *tune_preset_name(int p) { return p >= 0 && p < NPRESET ? PRESETS[p].name : ""; }
void tune_preset(int p)
{
    if (p < 0 || p >= NPRESET) return;
    tune_defaults();
    for (int k = 0; k < PRESETS[p].n; k++) tune_set(PRESETS[p].set[k].id, PRESETS[p].set[k].v);
}

static inline uint32_t fnv(const char *s) { uint32_t h = 2166136261u; while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; } return h; }

#ifdef ESP_PLATFORM
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "audio.h"
static const char *TAG = "tune";
typedef struct { uint32_t h; float v; } pair_t;

void tune_load(void)
{
    tune_defaults();
    dirty = false;
    esp_err_t e = nvs_flash_init();          // settings_init() erases a full or old partition later; nothing to read then
    if (e != ESP_OK) return;
    nvs_handle_t h;
    if (nvs_open("tune", NVS_READONLY, &h) != ESP_OK) { ESP_LOGI(TAG, "no saved tuning: v1"); return; }
    static pair_t p[TU_N + 16];
    size_t len = sizeof p;
    e = nvs_get_blob(h, "t", p, &len);
    nvs_close(h);
    if (e != ESP_OK) return;
    int got = 0, n = (int)(len / sizeof(pair_t));
    for (int k = 0; k < n; k++)
        for (int i = 0; i < TU_N; i++)
            if (tune_def[i].kind != TK_ACTION && fnv(tune_def[i].name) == p[k].h) { tune_set(i, p[k].v); got++; break; }
    dirty = false;
    ESP_LOGI(TAG, "%d tuning values loaded", got);
}

bool tune_save(void)
{
    static pair_t p[TU_N];
    int n = 0;
    for (int i = 0; i < TU_N; i++) if (tune_def[i].kind != TK_ACTION) p[n++] = (pair_t){ fnv(tune_def[i].name), tune_v[i] };
    nvs_handle_t h;
    if (nvs_open("tune", NVS_READWRITE, &h) != ESP_OK) return false;
    audio_flash_hint();                         // the write stalls the cache: count it as a flash stall, as settings.c does
    bool ok = nvs_set_blob(h, "t", p, n * sizeof(pair_t)) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    if (ok) dirty = false;
    ESP_LOGI(TAG, "tuning %s (%d values)", ok ? "saved" : "NOT saved", n);
    return ok;
}

void tune_erase(void)
{
    nvs_handle_t h;
    if (nvs_open("tune", NVS_READWRITE, &h) == ESP_OK) { audio_flash_hint(); nvs_erase_all(h); nvs_commit(h); nvs_close(h); }
}
#else
void tune_load(void) { tune_defaults(); dirty = false; }
bool tune_save(void) { dirty = false; return true; }
void tune_erase(void) { }
#endif
