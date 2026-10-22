// The samples in flash: one mono 16-bit WAV per pad (air1.wav .. air4.wav) on the wear-levelled FAT partition
// the Samplooler's slots live on (the same mount, so the two firmwares keep each other's files). A save is a
// chunked job driven from tick(): 16 KB a step, the output muted meanwhile (the flash write stalls the audio
// task's reads from PSRAM). On the host there is no flash: the stand-ins keep nothing.
#include "inst_air.h"
#include <stdio.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include <unistd.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"
#include "audio.h"
#include "diag.h"

static const char *TAG = "air";
static wl_handle_t wl = WL_INVALID_HANDLE;
static bool ready;
#define CHUNK 8192                                    // samples per step: 16 KB, four sectors

typedef struct __attribute__((packed)) {
    char riff[4]; uint32_t size; char wave[4];
    char fmt[4]; uint32_t fmt_len; uint16_t type, ch; uint32_t rate, byte_rate; uint16_t align, bits;
    char data[4]; uint32_t data_len;
} wav_hdr_t;

static FILE *jf; static const int16_t *jsrc; static int jn, jdone, jpad; static bool jbusy;
static char jpath[32];

static void path(int pad, char *b, size_t n) { snprintf(b, n, "/storage/air%d.wav", pad + 1); }

bool store_init(void)
{
    diag_crumb("pads mount");
    esp_vfs_fat_mount_config_t cfg = { .max_files = 2, .format_if_mount_failed = true, .allocation_unit_size = 4096 };
    esp_err_t e = esp_vfs_fat_spiflash_mount_rw_wl("/storage", "storage", &cfg, &wl);
    diag_crumb(NULL);
    if (e != ESP_OK) { ESP_LOGW(TAG, "no storage partition (%s): samples are not kept", esp_err_to_name(e)); return false; }
    ready = true;
    unlink("/storage/air.tmp");                       // a save that lost power
    return true;
}

int store_load(int pad, int16_t *dst, int max)
{
    if (!ready || !dst) return 0;
    char p[32]; path(pad, p, sizeof p);
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    wav_hdr_t h; int n = 0;
    if (fread(&h, sizeof h, 1, f) == 1 && memcmp(h.riff, "RIFF", 4) == 0 && h.ch == 1 && h.bits == 16) {
        n = (int)(h.data_len / 2); if (n > max) n = max;
        n = (int)fread(dst, 2, (size_t)n, f);
    }
    fclose(f);
    ESP_LOGI(TAG, "pad %d: %.2f s from flash", pad + 1, (double)n / AUDIO_SR);
    return n;
}

static void finish(bool ok)
{
    if (jf) { fclose(jf); jf = NULL; }
    if (ok) { unlink(jpath); ok = rename("/storage/air.tmp", jpath) == 0; }
    if (!ok) unlink("/storage/air.tmp");
    ESP_LOGI(TAG, "%s pad %d: %.2f s", ok ? "saved" : "FAILED to save", jpad + 1, (double)jn / AUDIO_SR);
    jbusy = false;
    diag_crumb(NULL);
}

bool store_save_begin(int pad, const int16_t *src, int n)
{
    if (!ready || jbusy || !src || n <= 0) return false;
    jpad = pad; jsrc = src; jn = n; jdone = 0; jbusy = true;
    path(pad, jpath, sizeof jpath);
    diag_crumb("pad save");
    audio_flash_hint();
    jf = fopen("/storage/air.tmp", "wb");
    if (!jf) { ESP_LOGE(TAG, "cannot write the sample"); finish(false); return false; }
    wav_hdr_t h = { "RIFF", 36 + (uint32_t)n * 2, "WAVE", "fmt ", 16, 1, 1, AUDIO_SR, AUDIO_SR * 2, 2, 16, "data", (uint32_t)n * 2 };
    if (fwrite(&h, sizeof h, 1, jf) != 1) { finish(false); return false; }
    return true;
}

bool store_save_step(void)
{
    if (!jbusy) return false;
    int c = jn - jdone < CHUNK ? jn - jdone : CHUNK;
    audio_flash_hint();
    if (fwrite(jsrc + jdone, 2, (size_t)c, jf) != (size_t)c) { finish(false); return false; }
    jdone += c;
    if (jdone >= jn) { finish(true); return false; }
    return true;
}

bool store_busy(void) { return jbusy; }
void store_mute(bool on) { audio_set_mute(on); }
bool store_silent(void) { return audio_silent(); }
void store_forget(int pad) { if (!ready) return; char p[32]; path(pad, p, sizeof p); unlink(p); }

#else   // the host: nothing is kept between runs
bool store_init(void) { return false; }
int  store_load(int pad, int16_t *dst, int max) { (void)pad; (void)dst; (void)max; return 0; }
bool store_save_begin(int pad, const int16_t *src, int n) { (void)pad; (void)src; (void)n; return false; }
bool store_save_step(void) { return false; }
bool store_busy(void) { return false; }
void store_forget(int pad) { (void)pad; }
void store_mute(bool on) { (void)on; }
bool store_silent(void) { return true; }
#endif
