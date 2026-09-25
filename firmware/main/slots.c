#include "slots.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"
#include "recorder.h"

static const char *TAG = "slots";
static wl_handle_t wl = WL_INVALID_HANDLE;
static bool ready;

bool slots_init(void)
{
    esp_vfs_fat_mount_config_t cfg = { .max_files = 2, .format_if_mount_failed = true, .allocation_unit_size = 4096 };
    esp_err_t e = esp_vfs_fat_spiflash_mount_rw_wl("/storage", "storage", &cfg, &wl);
    if (e != ESP_OK) { ESP_LOGW(TAG, "no storage partition (%s): slots off", esp_err_to_name(e)); return false; }
    ready = true;
    uint64_t total = 0, freeb = 0;
    esp_vfs_fat_info("/storage", &total, &freeb);
    ESP_LOGI(TAG, "slots on /storage, %u KB free of %u", (unsigned)(freeb / 1024), (unsigned)(total / 1024));
    return true;
}

static void path(int slot, char *b, size_t n) { snprintf(b, n, "/storage/slot%d.wav", slot + 1); }

typedef struct __attribute__((packed)) {
    char riff[4]; uint32_t size; char wave[4];
    char fmt[4]; uint32_t fmt_len; uint16_t type, ch; uint32_t rate, byte_rate; uint16_t align, bits;
    char data[4]; uint32_t data_len;
} wav_hdr_t;

bool slots_save(int slot, const int16_t *samples, int n)
{
    if (!ready || slot < 0 || slot >= SLOT_N || n <= 0) return false;
    char p[40]; path(slot, p, sizeof p);
    FILE *f = fopen(p, "wb");
    if (!f) { ESP_LOGE(TAG, "cannot write %s", p); return false; }
    wav_hdr_t h = { "RIFF", 36 + n * 2, "WAVE", "fmt ", 16, 1, 1, REC_SAMPLE_RATE, REC_SAMPLE_RATE * 2, 2, 16, "data", n * 2 };
    bool ok = fwrite(&h, sizeof h, 1, f) == 1;
    for (int i = 0; ok && i < n; i += 4096) {
        int c = n - i < 4096 ? n - i : 4096;
        ok = fwrite(samples + i, 2, c, f) == (size_t)c;
    }
    fclose(f);
    ESP_LOGI(TAG, "%s slot %d: %.2f s", ok ? "saved" : "FAILED", slot + 1, (double)n / REC_SAMPLE_RATE);
    return ok;
}

int slots_load(int slot, int16_t *dst, int max)
{
    if (!ready || slot < 0 || slot >= SLOT_N) return 0;
    char p[40]; path(slot, p, sizeof p);
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    wav_hdr_t h;
    int n = 0;
    if (fread(&h, sizeof h, 1, f) == 1 && memcmp(h.riff, "RIFF", 4) == 0 && h.ch == 1 && h.bits == 16) {
        int want = (int)(h.data_len / 2); if (want > max) want = max;
        while (n < want) {
            int c = want - n < 4096 ? want - n : 4096;
            int got = (int)fread(dst + n, 2, c, f);
            if (got <= 0) break;
            n += got;
        }
        if (h.rate != REC_SAMPLE_RATE) ESP_LOGW(TAG, "slot %d is %u Hz, playing at %d", slot + 1, (unsigned)h.rate, REC_SAMPLE_RATE);
    }
    fclose(f);
    ESP_LOGI(TAG, "loaded slot %d: %.2f s", slot + 1, (double)n / REC_SAMPLE_RATE);
    return n;
}

int slots_length(int slot)
{
    if (!ready || slot < 0 || slot >= SLOT_N) return 0;
    char p[40]; path(slot, p, sizeof p);
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    wav_hdr_t h; int n = 0;
    if (fread(&h, sizeof h, 1, f) == 1) n = (int)(h.data_len / 2);
    fclose(f);
    return n;
}
