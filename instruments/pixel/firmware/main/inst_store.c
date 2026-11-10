// The notes' store (pixel.h): small blobs by a short name, in the same NVS namespace as the core's settings and user
// sounds (settings.c: the instrument's name in lower case), so RESET ALL clears them with the rest. A song's notes
// are a few hundred bytes packed (inst_lib.c), nine of them at most: eight user songs and the working notes.
// On the host (tools/sim, tools/conlab) the store is memory.
#include "pixel.h"
#include <string.h>

#ifdef ESP_PLATFORM
#include "nvs.h"
#include "audio.h"
#include <ctype.h>

static bool open_ns(nvs_open_mode_t mode, nvs_handle_t *h)
{
    char ns[16]; int i = 0;
    for (; INST.name[i] && i < 15; i++) ns[i] = (char)tolower((unsigned char)INST.name[i]);
    ns[i] = 0;
    return nvs_open(ns, mode, h) == ESP_OK;
}
bool px_store_write(const char *name, const void *data, int n)
{
    nvs_handle_t h;
    if (!open_ns(NVS_READWRITE, &h)) return false;
    audio_flash_hint();                                    // a flash write may stall the sound: file it as that
    bool ok = nvs_set_blob(h, name, data, (size_t)n) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}
int px_store_read(const char *name, void *data, int cap)
{
    nvs_handle_t h;
    if (!open_ns(NVS_READONLY, &h)) return 0;
    size_t n = (size_t)cap;
    bool ok = nvs_get_blob(h, name, data, &n) == ESP_OK;
    nvs_close(h);
    return ok ? (int)n : 0;
}
void px_store_erase(void)
{
    nvs_handle_t h;
    if (!open_ns(NVS_READWRITE, &h)) return;
    static const char *const names[] = { "n0", "n1", "n2", "n3", "n4", "n5", "n6", "n7", "nw" };
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++) nvs_erase_key(h, names[i]);
    nvs_commit(h);
    nvs_close(h);
}
#else
#define SLOTS 12
static struct { char name[8]; uint8_t data[2048]; int n; } slot[SLOTS];
bool px_store_fail;                                        // tools/sim: the next writes fail (the store is full)
bool px_store_write(const char *name, const void *data, int n)
{
    if (px_store_fail || n > (int)sizeof slot[0].data) return false;
    int w = -1;
    for (int i = 0; i < SLOTS; i++) if (slot[i].n && !strcmp(slot[i].name, name)) w = i;
    for (int i = 0; i < SLOTS && w < 0; i++) if (!slot[i].n) w = i;
    if (w < 0) return false;
    strncpy(slot[w].name, name, sizeof slot[w].name - 1);
    memcpy(slot[w].data, data, (size_t)n); slot[w].n = n;
    return true;
}
int px_store_read(const char *name, void *data, int cap)
{
    for (int i = 0; i < SLOTS; i++) if (slot[i].n && !strcmp(slot[i].name, name)) {
        int n = slot[i].n < cap ? slot[i].n : cap;
        memcpy(data, slot[i].data, (size_t)n);
        return n;
    }
    return 0;
}
void px_store_erase(void) { memset(slot, 0, sizeof slot); }
#endif
