#include "slots.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"
#include "recorder.h"
#include "audio.h"
#include "diag.h"

static const char *TAG = "slots";
static wl_handle_t wl = WL_INVALID_HANDLE;
static bool ready;
static int  len_cache[SLOT_N];                        // each slot's length once read, -1 unknown: the menu asks every frame

#define CHUNK 8192                                    // samples per step: 16 KB, four sectors

typedef struct __attribute__((packed)) {
    char riff[4]; uint32_t size; char wave[4];
    char fmt[4]; uint32_t fmt_len; uint16_t type, ch; uint32_t rate, byte_rate; uint16_t align, bits;
    char data[4]; uint32_t data_len;
} wav_hdr_t;

// the running job
static enum { J_NONE, J_SAVE, J_LOAD } job;
static FILE          *jf;
static int            jslot, jn, jdone;
static const int16_t *jsrc;
static int16_t       *jdst;
static bool           jok;
static int            jloaded;
static char           jpath[40], jtmp[40];

bool slots_init(void)
{
    diag_crumb("slots mount");
    esp_vfs_fat_mount_config_t cfg = { .max_files = 2, .format_if_mount_failed = true, .allocation_unit_size = 4096 };
    esp_err_t e = esp_vfs_fat_spiflash_mount_rw_wl("/storage", "storage", &cfg, &wl);
    diag_crumb(NULL);
    if (e != ESP_OK) { ESP_LOGW(TAG, "no storage partition (%s): slots off", esp_err_to_name(e)); return false; }
    ready = true;
    uint64_t total = 0, freeb = 0;
    esp_vfs_fat_info("/storage", &total, &freeb);
    ESP_LOGI(TAG, "slots on /storage, %u KB free of %u", (unsigned)(freeb / 1024), (unsigned)(total / 1024));
    unlink("/storage/slot.tmp");                      // a save that lost power
    for (int i = 0; i < SLOT_N; i++) len_cache[i] = -1;
    return true;
}

static void path(int slot, char *b, size_t n) { snprintf(b, n, "/storage/slot%d.wav", slot + 1); }

int slots_length(int slot)
{
    if (!ready || slot < 0 || slot >= SLOT_N) return 0;
    if (len_cache[slot] >= 0) return len_cache[slot];
    char p[40]; path(slot, p, sizeof p);
    FILE *f = fopen(p, "rb");
    if (!f) return len_cache[slot] = 0;
    wav_hdr_t h; int n = 0;
    if (fread(&h, sizeof h, 1, f) == 1 && memcmp(h.riff, "RIFF", 4) == 0 && h.ch == 1 && h.bits == 16) n = (int)(h.data_len / 2);
    fclose(f);
    return len_cache[slot] = n;
}

static void finish(bool ok)
{
    if (jf) { fclose(jf); jf = NULL; }
    if (job == J_SAVE) {
        if (ok) { unlink(jpath); ok = rename(jtmp, jpath) == 0; }
        if (!ok) unlink(jtmp);
        len_cache[jslot] = ok ? jn : -1;
        ESP_LOGI(TAG, "%s slot %d: %.2f s", ok ? "saved" : "FAILED to save", jslot + 1, (double)jn / REC_SAMPLE_RATE);
    } else {
        jloaded = ok ? jdone : 0;
        ESP_LOGI(TAG, "%s slot %d: %.2f s", ok ? "loaded" : "FAILED to load", jslot + 1, (double)jloaded / REC_SAMPLE_RATE);
    }
    jok = ok; job = J_NONE;
    diag_crumb(NULL);
}

bool slots_save_begin(int slot, const int16_t *samples, int n)
{
    if (!ready || job != J_NONE || slot < 0 || slot >= SLOT_N || !samples || n <= 0) return false;
    jslot = slot; jsrc = samples; jn = n; jdone = 0; job = J_SAVE;
    path(slot, jpath, sizeof jpath); snprintf(jtmp, sizeof jtmp, "/storage/slot.tmp");
    diag_crumb("slot save");
    audio_flash_hint();
    jf = fopen(jtmp, "wb");
    if (!jf) { ESP_LOGE(TAG, "cannot write %s", jtmp); finish(false); return false; }
    wav_hdr_t h = { "RIFF", 36 + n * 2, "WAVE", "fmt ", 16, 1, 1, REC_SAMPLE_RATE, REC_SAMPLE_RATE * 2, 2, 16, "data", n * 2 };
    if (fwrite(&h, sizeof h, 1, jf) != 1) { finish(false); return false; }
    return true;
}

bool slots_load_begin(int slot, int16_t *dst, int max)
{
    if (!ready || job != J_NONE || slot < 0 || slot >= SLOT_N || !dst || max <= 0) return false;
    jslot = slot; jdst = dst; jdone = 0; jloaded = 0; job = J_LOAD;
    path(slot, jpath, sizeof jpath);
    diag_crumb("slot load");
    jf = fopen(jpath, "rb");
    if (!jf) { ESP_LOGW(TAG, "slot %d is empty", slot + 1); finish(false); return false; }
    wav_hdr_t h;
    if (fread(&h, sizeof h, 1, jf) != 1 || memcmp(h.riff, "RIFF", 4) != 0 || h.ch != 1 || h.bits != 16) {
        ESP_LOGW(TAG, "slot %d is not a mono 16-bit WAV", slot + 1); finish(false); return false;
    }
    if (h.rate != REC_SAMPLE_RATE) ESP_LOGW(TAG, "slot %d is %u Hz, playing at %d", slot + 1, (unsigned)h.rate, REC_SAMPLE_RATE);
    jn = (int)(h.data_len / 2); if (jn > max) jn = max;
    if (jn <= 0) { finish(false); return false; }
    return true;
}

bool slots_step(void)
{
    if (job == J_NONE) return false;
    int c = jn - jdone < CHUNK ? jn - jdone : CHUNK;
    audio_flash_hint();
    if (job == J_SAVE) {
        if (fwrite(jsrc + jdone, 2, c, jf) != (size_t)c) { finish(false); return false; }
    } else {
        int got = (int)fread(jdst + jdone, 2, c, jf);
        if (got <= 0) { finish(jdone > 0); return false; }
        c = got;
    }
    jdone += c;
    if (jdone >= jn) { finish(true); return false; }
    return true;
}

bool  slots_busy(void)      { return job != J_NONE; }
float slots_progress(void)  { return jn > 0 ? (float)jdone / (float)jn : 0.0f; }
bool  slots_result_ok(void) { return jok; }
int   slots_loaded(void)    { return jloaded; }
