// Which notes go with the sound (pixel.h). For the core a sound is a set of params; for PIXEL a sound is a song, and
// a song also has notes, which the core knows nothing about. This file keeps px_song in step with whatever the core
// has loaded, so the core's own ways all work for songs: the factory list, SAVE AS, MY EDIT, REVERT, power off and on.
//
//   a factory sound    its notes are text in the firmware (inst_demos.c)
//   a user sound k     its notes are in the store as "n<k>", written when the core saves the sound (the core gives no
//                      sign of a save, so lib_tick() watches for one: the sound became USER k, or stopped being edited,
//                      without a load)
//   edited notes       the hidden param NOTES (nrev) is a stamp: the editor's first change after a load or a save
//                      gives it a new random value, so to the core the sound is edited (the * in the list, MY EDIT when
//                      another sound is chosen, the stamp saved with the sound). The notes themselves go to the store
//                      as "nw" (the working notes) in a quiet moment, with their stamp and the sound they came from, so
//                      a board switched off and on comes back with them
//   MY EDIT            the core keeps an edited sound's params in RAM when another is chosen; the notes that went with
//                      them are kept here the same way (a few, by stamp)
//
// UI task only.
#include "pixel.h"
#include "params.h"
#include "front.h"
#include <stdio.h>
#include <string.h>

#define SOUND_ID_USER 100                  // front.c's numbering: 0.. factory, 100 + k user k, -1 your last edit
#define RAW_N   ((int)sizeof(px_song_t))
#define BLOB_N  (RAW_N + RAW_N / 2 + 16)
#define NSTASH  3

typedef struct { uint32_t rev; bool used; uint32_t age; px_song_t s; } stash_t;
static UI_RAM stash_t stash[NSTASH];
static uint32_t stash_clock;
static uint32_t ram_rev;                   // the stamp of the notes in px_song
static bool     booted, loaded, was_edited, notes_dirty, work_dirty;
static int      work_ms, last_sound = -999;
static uint32_t rng = 0x1234ABCDu;

bool lib_notes_edited(void) { return notes_dirty; }

// ---------------------------------------------------------------- a song as a blob: a header, then the bytes with
// runs of zeros packed (0, count). Most of a song is empty steps.
static int pack(const px_song_t *s, uint32_t rev, int base, uint8_t *b)
{
    const uint8_t *r = (const uint8_t *)s;
    int k = 0;
    b[k++] = 'P'; b[k++] = 'X'; b[k++] = 1;
    b[k++] = (uint8_t)rev; b[k++] = (uint8_t)(rev >> 8); b[k++] = (uint8_t)(rev >> 16);
    b[k++] = (uint8_t)(base + 1);          // -1 (MY EDIT) .. 107
    uint16_t sum = 0;
    for (int i = 0; i < RAW_N; i++) sum = (uint16_t)(sum * 31u + r[i] + 7u);
    b[k++] = (uint8_t)sum; b[k++] = (uint8_t)(sum >> 8);
    for (int i = 0; i < RAW_N; ) {
        if (r[i]) { b[k++] = r[i++]; continue; }
        int n = 0;
        while (i < RAW_N && !r[i] && n < 255) { i++; n++; }
        b[k++] = 0; b[k++] = (uint8_t)n;
    }
    return k;
}
static bool unpack(const uint8_t *b, int n, px_song_t *s, uint32_t *rev, int *base)
{
    if (n < 9 || b[0] != 'P' || b[1] != 'X' || b[2] != 1) return false;
    static UI_RAM px_song_t t;
    uint8_t *r = (uint8_t *)&t;
    int i = 0, k = 9;
    while (k < n && i < RAW_N) {
        if (b[k]) { r[i++] = b[k++]; continue; }
        if (k + 1 >= n) return false;
        int z = b[k + 1]; k += 2;
        if (!z || i + z > RAW_N) return false;
        memset(r + i, 0, (size_t)z); i += z;
    }
    if (i != RAW_N || k != n) return false;
    uint16_t sum = 0;
    for (int j = 0; j < RAW_N; j++) sum = (uint16_t)(sum * 31u + r[j] + 7u);
    if (sum != (uint16_t)(b[7] | (b[8] << 8))) return false;
    if (t.order_n < 1 || t.order_n > PX_ORDER_MAX) return false;
    for (int j = 0; j < PX_ORDER_MAX; j++) t.order[j] &= PX_PATTERNS - 1;
    *s = t;
    if (rev) *rev = (uint32_t)b[3] | ((uint32_t)b[4] << 8) | ((uint32_t)b[5] << 16);
    if (base) *base = (int)b[6] - 1;
    return true;
}
static bool file_write(const char *name, const px_song_t *s, uint32_t rev, int base)
{
    static UI_RAM uint8_t b[BLOB_N];
    return px_store_write(name, b, pack(s, rev, base, b));
}
static bool file_read(const char *name, px_song_t *s, uint32_t *rev, int *base)
{
    static UI_RAM uint8_t b[BLOB_N];
    int n = px_store_read(name, b, sizeof b);
    return n > 0 && unpack(b, n, s, rev, base);
}
static const char *user_file(int k) { static char nm[4]; snprintf(nm, sizeof nm, "n%d", k & 7); return nm; }

// ---------------------------------------------------------------- the notes kept for MY EDIT
static void stash_put(uint32_t rev, const px_song_t *s)
{
    int w = 0;
    for (int i = 0; i < NSTASH; i++) {
        if (stash[i].used && stash[i].rev == rev) { w = i; break; }
        if (!stash[i].used) { w = i; break; }
        if (stash[i].age < stash[w].age) w = i;
    }
    stash[w].used = true; stash[w].rev = rev; stash[w].age = ++stash_clock; stash[w].s = *s;
}
static const px_song_t *stash_get(uint32_t rev)
{
    for (int i = 0; i < NSTASH; i++) if (stash[i].used && stash[i].rev == rev) return &stash[i].s;
    return NULL;
}

static void factory_notes(int id)
{
    char err[64];
    int key = (int)pv[param_by_tag("key ")], scale = (int)pv[param_by_tag("scal")];
    if (id < 0 || id >= PX_NSONGS) id = 0;
    if (!px_song_parse(&px_song, PX_SONG_TEXT[id], key, scale, err, sizeof err)) {
        app_log("song %s: %s", PX_PRESETS[id].name, err);
        px_song_clear(&px_song);
    }
}

void lib_init(void)
{
    memset(stash, 0, sizeof stash);
    booted = loaded = was_edited = notes_dirty = work_dirty = false;
    ram_rev = 0; last_sound = -999; work_ms = 0;
}

// The core loaded a sound (INST.changed(-1)): which notes are its?
void lib_loaded(void)
{
    int s = meta.sound, P = px_param_nrev();
    uint32_t r = (uint32_t)pv[P];
    if (booted && was_edited) stash_put(ram_rev, &px_song);       // the state being left, for MY EDIT
    loaded = true;
    bool got = false;
    uint32_t frev = 0;
    if (!booted) {
        // Starting up. The core loads the sound as it was saved, then puts back the params the flash held and calls
        // again: so each time take the saved notes, unless the working notes are this very state.
        uint32_t wrev; int wbase;
        static UI_RAM px_song_t w;
        bool have_work = file_read("nw", &w, &wrev, &wbase);
        if (s >= SOUND_ID_USER) got = file_read(user_file(s - SOUND_ID_USER), &px_song, &frev, NULL);
        else if (s >= 0) { factory_notes(s); got = true; }
        if (have_work && wbase == s && wrev == r && (s < 0 || !got || wrev != frev)) { px_song = w; got = true; notes_dirty = s >= 0; }
        else notes_dirty = false;
        if (!got) { factory_notes(0); app_log("notes: none for sound %d, the first song's instead", s); }
    } else if (s < 0) {
        const px_song_t *k = stash_get(r);
        if (k) px_song = *k;                                      // (none: the notes stay as they are)
        notes_dirty = false; work_dirty = true; work_ms = 0;      // MY EDIT lives in the working notes
    } else {
        if (s >= SOUND_ID_USER) {
            got = file_read(user_file(s - SOUND_ID_USER), &px_song, &frev, NULL);
            if (!got) { px_song_clear(&px_song); toast("THIS SONG'S NOTES ARE GONE", 1800); }
        } else factory_notes(s);
        notes_dirty = false; work_dirty = false;
    }
    ram_rev = r;
}

// The editor changed a note. The first change after a load or a save stamps the notes anew.
void px_notes_edited(void)
{
    if (!notes_dirty) {
        notes_dirty = true;
        uint32_t r;
        do { r = rng_next(&rng) & 0xFFFFFFu; } while (r == 0 || r == ram_rev || stash_get(r));
        ram_rev = r;
        param_set(px_param_nrev(), (float)r);
    }
    work_dirty = true; work_ms = 0;
}

void lib_tick(const ctl_t *c)
{
    rng ^= c->frames * 2654435761u + c->ticks; if (!rng) rng = 1;
    int s = meta.sound;
    bool edited = params_edited();
    if (!booted) { booted = true; loaded = false; last_sound = s; was_edited = edited; return; }
    // a save: the sound is a user's and either it has just become that one, or it stopped being edited, with no load
    if (!loaded && s >= SOUND_ID_USER && (s != last_sound || (was_edited && !edited))) {
        ram_rev = (uint32_t)pv[px_param_nrev()];
        if (!file_write(user_file(s - SOUND_ID_USER), &px_song, ram_rev, s)) toast("NO ROOM FOR THE NOTES", 2000);
        notes_dirty = false; work_dirty = false;
    }
    loaded = false; last_sound = s; was_edited = edited;
    // the working notes: 3 s after the last change once it is quiet, 30 s at most (as the core saves its settings)
    if (work_dirty) {
        work_ms += 10;
        if ((work_ms >= 3000 && (front_quiet() || !px_pos.running)) || work_ms >= 30000) {
            file_write("nw", &px_song, ram_rev, s);
            work_dirty = false; work_ms = 0;
        }
    }
}
#ifndef ESP_PLATFORM
void lib_test_flush(void) { if (work_dirty) { file_write("nw", &px_song, ram_rev, meta.sound); work_dirty = false; } }   // tools/sim
uint32_t lib_test_rev(void) { return ram_rev; }
#endif
