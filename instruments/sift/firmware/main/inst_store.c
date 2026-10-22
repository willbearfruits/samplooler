// SIFT's corpus in flash: four banks of its own (sift1.wav .. sift4.wav) on the wear-levelled FAT partition the
// Samplooler keeps its slots on (the same mount, so the firmwares keep each other's files), and the Samplooler's
// slots themselves (slot1.wav .. slot8.wav), which SIFT reads but never writes: record on the Samplooler, flash SIFT,
// load the slot, sift it. Mono 16-bit WAV at 32 kHz. Loads and saves are chunked jobs (16 KB a step) driven from
// tick(); a save mutes the output meanwhile (the flash write stalls the audio task's reads from PSRAM).
// On the host the banks live in memory for the run, so the tests can save and load.
#include "inst_sift.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHUNK 8192                                    // frames a step: 16 KB

typedef struct __attribute__((packed)) {
    char riff[4]; uint32_t size; char wave[4];
    char fmt[4]; uint32_t fmt_len; uint16_t type, ch; uint32_t rate, byte_rate; uint16_t align, bits;
    char data[4]; uint32_t data_len;
} wav_hdr_t;

static bool jbusy, jfailed;
static int  jn, jdone, jbank;
static int16_t *jdst;

#ifdef ESP_PLATFORM
#include <unistd.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"
#include "audio.h"
#include "diag.h"

static const char *TAG = "sift";
static wl_handle_t wl = WL_INVALID_HANDLE;
static bool ready;
static FILE *jf;
static char jpath[32];
static int  len_cache[NBANK];
static UI_RAM int16_t conv[1024];

static void path(int bank, char *b, size_t n)
{
    if (bank < NBANK_OWN) snprintf(b, n, "/storage/sift%d.wav", bank + 1);
    else snprintf(b, n, "/storage/slot%d.wav", bank - NBANK_OWN + 1);
}

bool store_init(void)
{
    for (int k = 0; k < NBANK; k++) len_cache[k] = -1;
    diag_crumb("sift mount");
    esp_vfs_fat_mount_config_t cfg = { .max_files = 2, .format_if_mount_failed = true, .allocation_unit_size = 4096 };
    esp_err_t e = esp_vfs_fat_spiflash_mount_rw_wl("/storage", "storage", &cfg, &wl);
    diag_crumb(NULL);
    if (e != ESP_OK) { ESP_LOGW(TAG, "no storage partition (%s): the corpus is not kept", esp_err_to_name(e)); return false; }
    ready = true;
    unlink("/storage/sift.tmp");                       // a save that lost power
    return true;
}

static int read_header(FILE *f)
{
    wav_hdr_t h;
    if (fread(&h, sizeof h, 1, f) != 1 || memcmp(h.riff, "RIFF", 4) != 0 || h.ch != 1 || h.bits != 16) return -1;
    return (int)(h.data_len / 2);
}

int store_frames(int bank)
{
    if (!ready || bank < 0 || bank >= NBANK) return 0;
    if (len_cache[bank] >= 0) return len_cache[bank];
    char p[32]; path(bank, p, sizeof p);
    FILE *f = fopen(p, "rb");
    if (!f) return len_cache[bank] = 0;
    int n = read_header(f);
    fclose(f);
    return len_cache[bank] = n > 0 ? n : 0;
}
bool store_has(int bank) { return store_frames(bank) > 0; }

bool store_load_begin(int bank, int16_t *dst, int max)
{
    if (!ready || jbusy || !dst || bank < 0 || bank >= NBANK) return false;
    path(bank, jpath, sizeof jpath);
    jf = fopen(jpath, "rb");
    if (!jf) return false;
    int n = read_header(jf);
    if (n <= 0) { fclose(jf); jf = NULL; return false; }
    jn = n < max ? n : max; jdone = 0; jdst = dst; jbank = bank; jbusy = true; jfailed = false;
    return true;
}

int store_load_step(void)
{
    if (!jbusy || !jdst) return jdone;
    int c = jn - jdone < CHUNK ? jn - jdone : CHUNK;
    int got = (int)fread(jdst + jdone, 2, (size_t)c, jf);
    jdone += got;
    if (got < c || jdone >= jn) {
        if (got < c) jfailed = jdone == 0;
        fclose(jf); jf = NULL; jbusy = false; jdst = NULL;
        ESP_LOGI(TAG, "bank %d: %.2f s loaded", jbank + 1, (double)jdone / AUDIO_SR);
    }
    return jdone;
}

bool store_save_begin(int bank)
{
    if (!ready || jbusy || bank < 0 || bank >= NBANK_OWN) return false;
    jbank = bank; jn = 0; jbusy = true; jfailed = false; jdst = NULL;
    path(bank, jpath, sizeof jpath);
    diag_crumb("sift save");
    audio_flash_hint();
    jf = fopen("/storage/sift.tmp", "wb");
    if (!jf) { ESP_LOGE(TAG, "cannot write the corpus"); jbusy = false; jfailed = true; diag_crumb(NULL); return false; }
    wav_hdr_t h = { "RIFF", 36, "WAVE", "fmt ", 16, 1, 1, AUDIO_SR, AUDIO_SR * 2, 2, 16, "data", 0 };
    if (fwrite(&h, sizeof h, 1, jf) != 1) { store_save_end(false); return false; }
    return true;
}

bool store_save_put(const int16_t *src, int n, float gain)
{
    if (!jbusy || !jf) return false;
    audio_flash_hint();
    while (n > 0) {
        int c = n < 1024 ? n : 1024;
        for (int i = 0; i < c; i++) { float v = (float)src[i] * gain; conv[i] = (int16_t)(v > 32767.0f ? 32767.0f : v < -32767.0f ? -32767.0f : v); }
        if (fwrite(conv, 2, (size_t)c, jf) != (size_t)c) { store_save_end(false); return false; }
        jn += c; src += c; n -= c;
    }
    return true;
}

bool store_save_end(bool ok)
{
    if (!jbusy) return false;
    if (jf && ok) {
        wav_hdr_t h = { "RIFF", 36 + (uint32_t)jn * 2, "WAVE", "fmt ", 16, 1, 1, AUDIO_SR, AUDIO_SR * 2, 2, 16, "data", (uint32_t)jn * 2 };
        ok = fseek(jf, 0, SEEK_SET) == 0 && fwrite(&h, sizeof h, 1, jf) == 1;
    }
    if (jf) { fclose(jf); jf = NULL; }
    if (ok) { unlink(jpath); ok = rename("/storage/sift.tmp", jpath) == 0; }
    if (!ok) unlink("/storage/sift.tmp");
    len_cache[jbank] = ok ? jn : -1;
    ESP_LOGI(TAG, "%s bank %d: %.2f s", ok ? "saved" : "FAILED to save", jbank + 1, (double)jn / AUDIO_SR);
    jbusy = false; jfailed = !ok;
    diag_crumb(NULL);
    return ok;
}

void store_mute(bool on) { audio_set_mute(on); }
bool store_silent(void) { return audio_silent(); }

#else   // the host: the banks live in memory for the run
static int16_t *hb[NBANK];
static int hn[NBANK];
static int16_t *hsave;
static int hcap;

void store_sim_put(int bank, const int16_t *x, int n)       // the tests plant a Samplooler slot
{
    if (bank < 0 || bank >= NBANK) return;
    free(hb[bank]); hb[bank] = malloc((size_t)n * 2); memcpy(hb[bank], x, (size_t)n * 2); hn[bank] = n;
}
bool store_init(void) { return true; }
int  store_frames(int bank) { return bank >= 0 && bank < NBANK ? hn[bank] : 0; }
bool store_has(int bank) { return store_frames(bank) > 0; }
bool store_load_begin(int bank, int16_t *dst, int max)
{
    if (jbusy || !dst || bank < 0 || bank >= NBANK || !hn[bank]) return false;
    jbank = bank; jdst = dst; jn = hn[bank] < max ? hn[bank] : max; jdone = 0; jbusy = true; jfailed = false;
    return true;
}
int store_load_step(void)
{
    if (!jbusy) return jdone;
    int c = jn - jdone < CHUNK ? jn - jdone : CHUNK;
    memcpy(jdst + jdone, hb[jbank] + jdone, (size_t)c * 2);
    jdone += c;
    if (jdone >= jn) jbusy = false;
    return jdone;
}
bool store_save_begin(int bank)
{
    if (jbusy || bank < 0 || bank >= NBANK_OWN) return false;
    jbank = bank; jn = 0; jbusy = true; jfailed = false;
    return true;
}
bool store_save_put(const int16_t *src, int n, float gain)
{
    if (!jbusy) return false;
    if (jn + n > hcap) { hcap = (jn + n) * 2; hsave = realloc(hsave, (size_t)hcap * 2); }
    for (int i = 0; i < n; i++) { float v = (float)src[i] * gain; hsave[jn + i] = (int16_t)(v > 32767.0f ? 32767.0f : v < -32767.0f ? -32767.0f : v); }
    jn += n;
    return true;
}
bool store_save_end(bool ok)
{
    if (!jbusy) return false;
    if (ok) store_sim_put(jbank, hsave, jn);
    jbusy = false; jfailed = !ok;
    return ok;
}
void store_mute(bool on) { (void)on; }
bool store_silent(void) { return true; }
#endif

bool store_busy(void) { return jbusy; }
bool store_failed(void) { return jfailed; }
