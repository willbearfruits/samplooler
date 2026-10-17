// BAND's host (see band.h): one instrument made of six layers, DRUMS (KNOCK and the grid), BASS, LEAD, KEYS, BREATH
// (HUFF) and HOWL. The knob in play picks the live layer: the mic, the tilt, the stick and REC are its; the others play on.
//
//   the song      tempo, beat 0 and the loop's length on a whole number of frames per beat, shared on the link
//                 (link.h) with the boards around: TEMPO sets it, a tempo heard from another board too; the first loop
//                 anywhere fixes the loop's length (whole bars at the tempo, or, with FIRST LOOP: SETS TEMPO and nothing
//                 in time, the length held sets the tempo as JAM did); key, scale and chords go both ways, the chord a
//                 layer plays live (the bass's lean) too
//   the tracks    BREATH and HOWL are recorded into audio tracks in the song's loop: REC held records (the take is
//                 written from the press and records on to the loop's end) or adds, a tap mutes / unmutes, held on a
//                 muted track it records a new take over the old one; DRUMS, BASS, LEAD and KEYS loop their own events
//                 (the same: a tap stops, held while stopped is a new take)
//   the mixer     a level and a mute per layer; one FX bus fed by the layers' sends: an echo in time with the
//                 song and a small reverb (a four-line feedback delay network). The knob turned past HOWL shows them
//                 as six faders (MIX): no layer is live there; the stick, REC and the knob (held) set the levels and
//                 mutes and erase loops
//   the screen    the header band: the layer bar (six icons, the live one lit, each one's loop under it), the chord,
//                 the beat, the link, the meter; the loop's playhead on row 15; a slide between layers' scenes
#include "band.h"
#include "ui.h"
#include "params.h"
#include "front.h"
#include "spsc.h"
#include "link.h"
#include <stdio.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_cpu.h"
#define CYCLES() esp_cpu_get_cycle_count()
#else
#include <time.h>
static inline uint32_t CYCLES(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint32_t)(t.tv_nsec / 4); }
#endif
// What each part of the audio costs, in CPU cycles summed over blocks (the log shows it every 5 s): the four
// layers, then the tracks and mixer, then the FX bus. A block is 2 ms: 480000 cycles at 240 MHz.
static volatile uint32_t prof_cyc[L_N + 2], prof_blocks;

// ---------------------------------------------------------------- the param table
// Each layer has LAYER_PARAMS_MAX slots (unused ones are hidden placeholders), then the host's own.
enum {
    H_LAYER, H_TEMPO, H_KEY, H_SCALE, H_PROG, H_CHORD, H_BARS, H_FIRST,
    H_LINK, H_GROUP, H_RANGE, H_PEERS,
    H_LVL_D, H_LVL_B, H_LVL_L, H_LVL_K, H_LVL_H, H_LVL_W, H_MUTE_D, H_MUTE_B, H_MUTE_L, H_MUTE_K, H_MUTE_H, H_MUTE_W,
    H_ECHO, H_ETIME, H_EFB, H_REVERB, H_RSIZE, H_RDAMP,
    H_CLR_D, H_CLR_B, H_CLR_L, H_CLR_K, H_CLR_H, H_CLR_W, H_CLR_ALL,
    H_N
};
#define HB (L_N * LAYER_PARAMS_MAX)
#define NPARAMS (HB + H_N)
#define H(i) pv[HB + (i)]
enum { G_SONG, G_LAYER0, G_MIX = G_LAYER0 + 2 * L_N, G_FX, G_LOOPS, G_LINK, G_N };

static const layer_t *const layers[L_N] = { &layer_drums, &layer_bass, &layer_lead, &layer_keys, &layer_breath, &layer_howl };
int layer_base[L_N] = { 0, LAYER_PARAMS_MAX, 2 * LAYER_PARAMS_MAX, 3 * LAYER_PARAMS_MAX, 4 * LAYER_PARAMS_MAX, 5 * LAYER_PARAMS_MAX };

#define L_MIX L_N                          // the knob's seventh place, past HOWL: the mixer (no layer is live there)
static const char *const layer_names[] = { "DRUMS", "BASS", "LEAD", "KEYS", "BREATH", "HOWL", "MIX" };
static const char *const key_names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const char *const scale_names[] = { "MAJOR", "MINOR", "DORIAN", "MIXOLYDIAN", "PHRYGIAN", "HARM MINOR" };
static const int8_t scales[][7] = { { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 9, 10 },
                                    { 0, 2, 4, 5, 7, 9, 10 }, { 0, 1, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 8, 11 } };
#define NSCALES 6
// progressions: scale degrees, one per bar (0 = I)
typedef struct { const char *name; int n; int8_t d[12]; } prog_t;
static const prog_t progs[] = {
    { "HOLD", 1, { 0 } },
    { "I V vi IV", 4, { 0, 4, 5, 3 } },
    { "vi IV I V", 4, { 5, 3, 0, 4 } },
    { "I vi IV V", 4, { 0, 5, 3, 4 } },
    { "I IV", 2, { 0, 3 } },
    { "ii V I", 4, { 1, 4, 0, 0 } },
    { "i VII VI V", 4, { 0, 6, 5, 4 } },
    { "i iv", 2, { 0, 3 } },
    { "12 BAR BLUES", 12, { 0, 0, 0, 0, 3, 3, 0, 0, 4, 3, 0, 4 } },
};
#define NPROGS ((int)(sizeof progs / sizeof progs[0]))
static const char *const prog_names[NPROGS] = { "HOLD", "I V vi IV", "vi IV I V", "I vi IV V", "I IV", "ii V I", "i VII VI V", "i iv", "12 BAR BLUES" };
static const char *const chord_names[] = { "I", "II", "III", "IV", "V", "VI", "VII" };
static const char *const bars_names[] = { "1 BAR", "2 BARS", "HALF BAR" };
static const char *const mute_names[] = { "PLAY", "MUTE" };
static const char *const etime_names[] = { "1/8", "1/8 DOT", "1/4", "1/4 DOT", "1/2" };
static const char *const first_names[] = { "FITS TEMPO", "SETS TEMPO" };
static const char *const link_names[] = { "OFF", "ON" };
static const char *const range_names[] = { "NEAR", "ROOM", "FAR" };

#define SNG (PF_SYS)
static const param_t host_params[H_N] = {
    [H_LAYER]  = { "LAYER", "slay", G_SONG, PK_ENUM, PF_SYS, 0, L_MIX, 1, 0, "", layer_names, "THE LAYER YOU PLAY, OR MIX" },
    [H_TEMPO]  = { "TEMPO", "stmp", G_SONG, PK_NUM, PF_INT, 60, 180, 1, 100, "bpm", NULL, "TILL THE FIRST LOOP SETS IT" },
    [H_KEY]    = { "KEY", "skey", G_SONG, PK_ENUM, 0, 0, 11, 1, 0, "", key_names, "THE KEY OF EVERY LAYER" },
    [H_SCALE]  = { "SCALE", "sscl", G_SONG, PK_ENUM, 0, 0, NSCALES - 1, 1, 0, "", scale_names, "THE NOTES EVERY LAYER USES" },
    [H_PROG]   = { "CHORDS", "sprg", G_SONG, PK_ENUM, 0, 0, NPROGS - 1, 1, 1, "", prog_names, "HOW THE CHORDS MOVE BY BAR" },
    [H_CHORD]  = { "HOLD CHORD", "schd", G_SONG, PK_ENUM, 0, 0, 6, 1, 0, "", chord_names, "THE CHORD FOR CHORDS: HOLD" },
    [H_BARS]   = { "CHORD BARS", "sbar", G_SONG, PK_ENUM, 0, 0, 2, 1, 0, "", bars_names, "HOW LONG EACH CHORD LASTS" },
    [H_FIRST]  = { "FIRST LOOP", "sfst", G_SONG, PK_ENUM, PF_SYS, 0, 1, 1, 0, "", first_names, "BARS, OR IT SETS THE TEMPO" },
    [H_LINK]   = { "LINK", "nlnk", G_LINK, PK_ENUM, PF_SYS | PF_NOCC, 0, 1, 1, 1, "", link_names, "SYNC WITH BOARDS NEARBY" },
    [H_GROUP]  = { "GROUP", "ngrp", G_LINK, PK_NUM, PF_SYS | PF_NOCC | PF_INT, 1, 8, 1, 1, "", NULL, "SAME GROUP: THEY SYNC" },
    [H_RANGE]  = { "RANGE", "npow", G_LINK, PK_ENUM, PF_SYS | PF_NOCC, 0, 2, 1, 1, "", range_names, "HOW FAR THE RADIO REACHES" },
    [H_PEERS]  = { "WHO IS THERE", "nwho", G_LINK, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "THE BOARDS LINKED NOW" },
    [H_LVL_D]  = { "DRUMS", "mlvd", G_MIX, PK_NUM, PF_INT, 0, 100, 5, 80, "%", NULL, "DRUMS LEVEL" },
    [H_LVL_B]  = { "BASS", "mlvb", G_MIX, PK_NUM, PF_INT, 0, 100, 5, 80, "%", NULL, "BASS LEVEL" },
    [H_LVL_L]  = { "LEAD", "mlvl", G_MIX, PK_NUM, PF_INT, 0, 100, 5, 70, "%", NULL, "LEAD LEVEL" },
    [H_LVL_K]  = { "KEYS", "mlvk", G_MIX, PK_NUM, PF_INT, 0, 100, 5, 70, "%", NULL, "KEYS LEVEL" },
    [H_LVL_H]  = { "BREATH", "mlvh", G_MIX, PK_NUM, PF_INT, 0, 100, 5, 80, "%", NULL, "BREATH LEVEL" },
    [H_LVL_W]  = { "HOWL", "mlvw", G_MIX, PK_NUM, PF_INT, 0, 100, 5, 80, "%", NULL, "HOWL LEVEL" },
    [H_MUTE_D] = { "MUTE DRUMS", "mmud", G_MIX, PK_ENUM, 0, 0, 1, 1, 0, "", mute_names, "SILENCE THE DRUMS" },
    [H_MUTE_B] = { "MUTE BASS", "mmub", G_MIX, PK_ENUM, 0, 0, 1, 1, 0, "", mute_names, "SILENCE THE BASS" },
    [H_MUTE_L] = { "MUTE LEAD", "mmul", G_MIX, PK_ENUM, 0, 0, 1, 1, 0, "", mute_names, "SILENCE THE LEAD" },
    [H_MUTE_K] = { "MUTE KEYS", "mmuk", G_MIX, PK_ENUM, 0, 0, 1, 1, 0, "", mute_names, "SILENCE THE KEYS" },
    [H_MUTE_H] = { "MUTE BREATH", "mmuh", G_MIX, PK_ENUM, 0, 0, 1, 1, 0, "", mute_names, "SILENCE THE BREATH" },
    [H_MUTE_W] = { "MUTE HOWL", "mmuw", G_MIX, PK_ENUM, 0, 0, 1, 1, 0, "", mute_names, "SILENCE THE HOWL" },
    [H_ECHO]   = { "ECHO", "fech", G_FX, PK_NUM, PF_INT, 0, 100, 5, 35, "%", NULL, "HOW LOUD THE ECHO IS" },
    [H_ETIME]  = { "ECHO TIME", "fetm", G_FX, PK_ENUM, 0, 0, 4, 1, 1, "", etime_names, "IN TIME WITH THE SONG" },
    [H_EFB]    = { "REPEATS", "fefb", G_FX, PK_NUM, PF_INT, 0, 90, 5, 40, "%", NULL, "HOW LONG THE ECHO GOES ON" },
    [H_REVERB] = { "REVERB", "frev", G_FX, PK_NUM, PF_INT, 0, 100, 5, 35, "%", NULL, "HOW LOUD THE REVERB IS" },
    [H_RSIZE]  = { "REVERB TIME", "frsz", G_FX, PK_NUM, PF_LOG, 0.3f, 6.0f, 1.12f, 1.6f, "s", NULL, "HOW LONG THE ROOM RINGS" },
    [H_RDAMP]  = { "REVERB TONE", "frdm", G_FX, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "DARK TO BRIGHT" },
    [H_CLR_D]  = { "CLEAR DRUMS", "xcld", G_LOOPS, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "ERASE THE DRUMS' LOOP" },
    [H_CLR_B]  = { "CLEAR BASS", "xclb", G_LOOPS, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "ERASE THE BASS'S LOOP" },
    [H_CLR_L]  = { "CLEAR LEAD", "xcll", G_LOOPS, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "ERASE THE LEAD'S LOOP" },
    [H_CLR_K]  = { "CLEAR KEYS", "xclk", G_LOOPS, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "ERASE THE KEYS' LOOP" },
    [H_CLR_H]  = { "CLEAR BREATH", "xclh", G_LOOPS, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "ERASE THE BREATH TRACK" },
    [H_CLR_W]  = { "CLEAR HOWL", "xclw", G_LOOPS, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "ERASE THE HOWL TRACK" },
    [H_CLR_ALL]= { "CLEAR ALL", "xcla", G_LOOPS, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "EVERY LOOP; THE TEMPO FREE" },
};
static UI_RAM param_t table[NPARAMS];
static UI_RAM char placeholder_tag[L_N * LAYER_PARAMS_MAX][5];
static const char *groups[G_N];

__attribute__((constructor)) static void build_table(void)
{
    groups[G_SONG] = "SONG"; groups[G_MIX] = "MIX"; groups[G_FX] = "FX"; groups[G_LOOPS] = "LOOPS"; groups[G_LINK] = "LINK";
    for (int l = 0; l < L_N; l++) {
        const layer_t *L = layers[l];
        groups[G_LAYER0 + 2 * l] = L->pages[0];
        groups[G_LAYER0 + 2 * l + 1] = L->npages > 1 ? L->pages[1] : L->pages[0];
        for (int i = 0; i < LAYER_PARAMS_MAX; i++) {
            param_t *d = &table[l * LAYER_PARAMS_MAX + i];
            if (i < L->nparams) { *d = L->params[i]; d->group = (uint8_t)(G_LAYER0 + 2 * l + (L->params[i].group ? 1 : 0)); }
            else {
                snprintf(placeholder_tag[l * LAYER_PARAMS_MAX + i], 5, "~%c%02d", L->letter, i);
                *d = (param_t){ "-", placeholder_tag[l * LAYER_PARAMS_MAX + i], 0, PK_NUM, PF_HIDE | PF_NOCC | PF_SYS, 0, 1, 1, 0, "", NULL, NULL };
            }
        }
    }
    for (int i = 0; i < H_N; i++) table[HB + i] = host_params[i];
}

// ---------------------------------------------------------------- the song
song_t song = { .bpm = 100, .fpb = 19200, .bar_beats = 4, .scale = scales[0], .scale_n = 7, .chord = { 36, 40, 43 }, .chord_n = 3 };
volatile float host_out_env;
static float last_out[AUDIO_BLOCK];
const float *host_last_out(void) { return last_out; }
static volatile int chord_ovr = -1;
static int last_src = -1;

int host_live(void) { return (int)H(H_LAYER); }
void host_chord_override(int degree) { chord_ovr = degree < 0 ? -1 : degree % 7; }   // any task; the tick shares it

// The loop's length, from the first take made on this board (band.h). With a loop somewhere in the session already
// (a linked board's), it is that loop. Else whole bars at the tempo, the take's length rounded (FITS TEMPO); or, with
// SETS TEMPO and nothing on this board or the link playing in time, the length held sets the tempo as JAM did.
// The loop's position 0 is on the session's beat 0, so every board's loops line up.
static bool any_playing(void);
static void loop_made(uint32_t now);
uint32_t host_first_loop(uint32_t start, uint32_t held)
{
    if (song.loop_len) return song.loop_len;
    uint32_t max = (uint32_t)(JAM_LOOP_MAX_S * AUDIO_SR), now = start + held;
    if (held > max) held = max;
    if (held < AUDIO_SR / 4) held = AUDIO_SR / 4;
    const link_time_t *tl = link_time();
    int beats;
    if (tl->beats) beats = tl->beats;                               // the session has a loop: the same length
    else if (H(H_FIRST) >= 1.0f && !any_playing() && !(link_flags_any() & LINKF_LOOP)) {
        // JAM's way: the whole number of beats nearest the tempo so far, then halved or doubled into 75..165 BPM
        float fpbf = song_frames_per_beat();
        beats = (int)((float)held / fpbf + 0.5f);
        if (beats < 1) beats = 1;
        float bpm = (float)beats * 60.0f * (float)AUDIO_SR / (float)held;
        while (bpm > 165.0f && beats > 1) { beats = beats & 1 ? beats - 1 : beats / 2; bpm = (float)beats * 60.0f * (float)AUDIO_SR / (float)held; }
        while (bpm < 75.0f) { beats *= 2; bpm *= 2.0f; }
        uint32_t fpb = (held + (uint32_t)beats / 2) / (uint32_t)beats;
        song.fpb = fpb; song.origin = start;                        // beat 0 is where the take began
        link_set_time(fpb, start, song.bar_beats, beats);
    } else {
        uint32_t bar = song.fpb * (uint32_t)song.bar_beats;
        int bars = (int)((held + bar / 2) / bar);
        int most = (int)(max / bar);
        if (bars < 1) bars = 1;
        if (bars > most) bars = most > 0 ? most : 1;
        beats = bars * song.bar_beats;
    }
    if (beats > 255) beats = 255;
    song.bpm = 60.0f * (float)AUDIO_SR / (float)song.fpb;
    song.beats = beats;
    uint32_t len = song.fpb * (uint32_t)beats;
    song.loop_start = now - (uint32_t)song_mod((int32_t)(now - song.origin), (int32_t)len);   // the last beat 0 of the loop
    __atomic_store_n(&song.loop_len, len, __ATOMIC_RELEASE);
    loop_made(now);
    app_log("band: the loop is %.2f s, %d beats at %.1f BPM", (double)((float)len / AUDIO_SR), beats, (double)song.bpm);
    return len;
}

// ---------------------------------------------------------------- the link: the tempo, the bar, the loop and the harmony
// What the board starts with is the session's version 0 (a board already playing wins); every change made here goes
// to the others; a change from another board sets the params here (without being sent back).
static bool link_up, applying;
static int  peers_was;
static uint32_t tempo_fpb(float bpm) { return (uint32_t)(60.0f * (float)AUDIO_SR / (bpm < 20.0f ? 20.0f : bpm) + 0.5f); }

static void link_start(void)
{
    link_init("BAND", 2);                                          // the clock of a flock: BAND's drums keep time best
    link_boot_time(tempo_fpb(H(H_TEMPO)), song.origin, song.bar_beats);
    link_boot_harm((int)H(H_KEY), (int)H(H_SCALE), (int)H(H_PROG), (int)H(H_CHORD), (int)H(H_BARS));
    link_enable(H(H_LINK) >= 1.0f, (int)H(H_GROUP), (int)H(H_RANGE));
    link_up = true;
}

static void loop_made(uint32_t now)
{
    (void)now;
    const link_time_t *tl = link_time();
    if (!tl->beats) link_set_time(song.fpb, song.origin, song.bar_beats, song.beats);   // the first loop of the session
    link_set_flags(LINKF_LOOP);
}

static bool loops_here(void) { return song.loop_len != 0; }

// a tempo set here: beat continuous through the change (the beat now stays the beat now)
static void tempo_set_here(void)
{
    uint32_t fpb = tempo_fpb(H(H_TEMPO));
    if (fpb == song.fpb) return;
    if (loops_here() || (link_flags_any() & LINKF_LOOP)) {          // a loop keeps its tempo
        applying = true; param_set(HB + H_TEMPO, (float)(int)(song.bpm + 0.5f)); applying = false;
        toast("A LOOP KEEPS THE TEMPO", 1200);
        return;
    }
    uint32_t now = audio_frames();
    float beat = song_beat(now);
    uint32_t origin = now - (uint32_t)(beat * (float)fpb);
    song.fpb = fpb; song.origin = origin;
    song.bpm = 60.0f * (float)AUDIO_SR / (float)fpb;
    link_set_time(fpb, origin, song.bar_beats, 0);
}

static void harm_set_here(void)
{
    link_set_harm((int)H(H_KEY), (int)H(H_SCALE), (int)H(H_PROG), (int)H(H_CHORD), (int)H(H_BARS), chord_ovr);
}

static void link_follow(void)
{
    // the session's time: adopt the tempo and beat 0; a loop's start moves with a small slew, re-placed on a jump
    if (link_synced()) {
        uint32_t o; float f; link_origin(&o, &f);
        uint32_t fpb = link_fpb();
        if (fpb && (fpb != song.fpb || o != song.origin)) {
            int32_t d = (int32_t)(o - song.origin);
            bool tempo = fpb != song.fpb;
            song.fpb = fpb; song.origin = o;
            song.bpm = 60.0f * (float)AUDIO_SR / (float)fpb;
            if (song.loop_len) {
                if (tempo) {                                        // a new tempo under a loop: only when ours was cleared elsewhere
                    song.loop_len = fpb * (uint32_t)song.beats;
                    uint32_t now = audio_frames();
                    song.loop_start = now - (uint32_t)song_mod((int32_t)(now - o), (int32_t)song.loop_len);
                } else if (d > -64 && d < 64) song.loop_start += (uint32_t)d;   // the clocks' drift, frame by frame
                else { uint32_t now = audio_frames(); song.loop_start = now - (uint32_t)song_mod((int32_t)(now - o), (int32_t)song.loop_len); }
            }
            if (tempo && (int)(song.bpm + 0.5f) != (int)H(H_TEMPO)) { applying = true; param_set(HB + H_TEMPO, (float)(int)(song.bpm + 0.5f)); applying = false; }
        }
    }
    if (link_time_news()) { /* applied above */ }
    if (link_harm_news()) {
        const link_harm_t *h = link_harm();
        applying = true;
        param_set(HB + H_KEY, h->key); param_set(HB + H_SCALE, h->scale); param_set(HB + H_PROG, h->prog);
        param_set(HB + H_CHORD, h->hold); param_set(HB + H_BARS, h->bars);
        applying = false;
        chord_ovr = h->live;
    }
    // who is there: a toast when a board joins or leaves
    link_peer_t pr[LINK_PEERS_MAX];
    int np = link_peers(pr, LINK_PEERS_MAX);
    if (np != peers_was) {
        char b[40];
        if (np > peers_was && np > 0) snprintf(b, sizeof b, "LINKED: %s", pr[np - 1].name);
        else snprintf(b, sizeof b, np ? "A BOARD LEFT" : "LINK: ALONE");
        toast(b, 1500);
        peers_was = np;
    }
}

// the bar a frame falls in, as the chords count it: within the session's loop (this board's, or a linked board's),
// so loops and chords line up; within SONG_WRAP_BEATS without a loop
static uint32_t bar_at(uint32_t frame)
{
    const link_time_t *tl = link_time();
    float beat = song_beat(frame);
    if (!song.loop_len && tl->beats) beat = (float)song_mod((int32_t)(frame - song.origin), (int32_t)(song.fpb * tl->beats)) / (float)song.fpb;
    static const float bar_beats[] = { 4.0f, 8.0f, 2.0f };
    return (uint32_t)(beat / bar_beats[(int)H(H_BARS)]);
}

// the chord the progression (or a chord played live, or HOLD) gives at a frame: a layer playing a note right on a bar
// line asks this instead of song.degree, which the tick moves up to 10 ms late
int host_degree_at(uint32_t frame)
{
    int ovr = chord_ovr;
    if (ovr >= 0) return ovr;
    if ((int)H(H_PROG) == 0) return (int)H(H_CHORD);
    const prog_t *p = &progs[(int)H(H_PROG)];
    return p->d[bar_at(frame) % (uint32_t)p->n];
}

static void set_song(uint32_t frame)
{
    int s = (int)H(H_SCALE);
    song.key = (int)H(H_KEY); song.scale = scales[s]; song.scale_n = 7;
    song.bar = bar_at(frame);
    // the chord: written when its source moves (a new bar, a new override, a new setting), so a direct write to
    // song.degree holds until then
    int src = host_degree_at(frame);
    if (src != last_src) { last_src = src; song.degree = src; }
    int d = song.degree;
    song.chord_n = 3;
    int root = scale_note(d, 2), pc = ((root % 12) + 12) % 12;
    song.chord[0] = 36 + pc;                                       // the root in C2..B2, the third and fifth above it
    song.chord[1] = song.chord[0] + ((scale_note(d + 2, 2) - root) % 12 + 12) % 12;
    song.chord[2] = song.chord[0] + ((scale_note(d + 4, 2) - root) % 12 + 12) % 12;
}

void host_midi_note(int layer, int note, int vel)
{
    static const int off[L_N] = { 0, 1, 3, 4, 0, 2 };
    int base = (int)pv[CP(CP_MIDI_CH)];
    midi_note_ch(layer == L_DRUMS ? 10 : ((base - 1 + off[layer]) % 16) + 1, note, vel);
}
void host_midi_cc(int layer, int cc, int val)
{
    static const int off[L_N] = { 0, 1, 3, 4, 0, 2 };
    int base = (int)pv[CP(CP_MIDI_CH)];
    int ch = layer == L_DRUMS ? 10 : ((base - 1 + off[layer]) % 16) + 1;
    if (cc < 0 || cc > 119) return;
    plat_midi3((uint8_t)(0xB0 | ((ch - 1) & 15)), (uint8_t)cc, (uint8_t)(val < 0 ? 0 : val > 127 ? 127 : val));
}

// ---------------------------------------------------------------- the audio tracks (BREATH, HOWL)
#define TRACK_N   (JAM_LOOP_MAX_S * AUDIO_SR + 192)   // the longest loop and the seam's tail after it
#define XF        192                      // 6 ms: every ramp and the seam
#define RING      16384u                   // 0.5 s of the live layer, so a hold loses nothing to the tap test
#define RMASK     (RING - 1u)
#define HOLD_MS   300
enum { T_PRESS, T_CONFIRM, T_RELEASE, T_TAP, T_CLEAR, T_CLEAR_ALL };
typedef struct { uint8_t cmd, layer; uint32_t a; } tcmd_t;     // T_RELEASE: a = where the first take's sound starts
SPSC(tcmdq, tcmd_t, 16)
static tcmdq_t tq;
typedef struct {
    int16_t *buf;
    volatile int has, muted, rec;          // rec: 0 no, 1 the band's first take, 2 adding
    float g, dub_g, dub_t, send_ratio, acc_dry, acc_send;
    int wpos, wipe;                        // first take: written so far; wiping: samples left to clear
    uint32_t press, off;                   // the first take's start; where position 0 is from the loop's start
    int16_t *base;                         // the loop's position 0 in buf: past the silence before a first take's first sound
    bool closing;                          // REC let go on a first take: it records on to the loop's end and the seam
    int fresh;                             // a new take over a muted track: samples left of the pass that writes over
                                           // the old one (it is not heard; what is not played now becomes silence)
} track_t;
static track_t trk[L_N];                   // only BREATH and HOWL have buffers
static int16_t *ring;
static uint32_t press_frame;
static bool pend;
static int pend_layer;

static inline int16_t s16(float x) { x *= 32767.0f; return (int16_t)(x > 32767.0f ? 32767.0f : x < -32767.0f ? -32767.0f : x); }
static inline float f32(int16_t v) { return (float)v * (1.0f / 32767.0f); }
static bool has_track(int l) { return trk[l].buf != NULL; }

static void track_cmd2(int cmd, int layer, uint32_t a) { tcmd_t c = { (uint8_t)cmd, (uint8_t)layer, a }; tcmdq_push(&tq, &c); }
static void track_cmd(int cmd, int layer) { track_cmd2(cmd, layer, 0); }

// ---------------------------------------------------------------- the FX bus
#define ECHO_N 65536                       // 2 s (int16, PSRAM, a power of two): a dotted quarter at 45 BPM
static int16_t *echo_buf;
static uint32_t echo_w;
static float echo_d_s, echo_lp;
#define FDN 4
#define FDN_N 2048                         // a power of two over the longest line
static float *fdn_buf;                     // FDN x FDN_N (PSRAM)
static uint32_t fdn_w;
static const int fdn_len[FDN] = { 1031, 1327, 1523, 1801 };
static float fdn_lp[FDN], fdn_g[FDN], fdn_damp = 0.5f;
static float last_rt = -1, last_dmp = -1;
static smooth_t lvl_s[L_N], mute_s[L_N], echo_s, rev_s, efb_s;
static float blk_k_lvl, blk_k_mute;                // the level and mute smoothers' step over a whole block

// ---------------------------------------------------------------- the instrument
static float dry[L_N][AUDIO_BLOCK], send[L_N][AUDIO_BLOCK];
static volatile float layer_peak[L_N];

static void init(void)
{
    for (int l = 0; l < L_N; l++) layers[l]->init();
    trk[L_BREATH].buf = app_alloc(TRACK_N * sizeof(int16_t));
    trk[L_HOWL].buf = app_alloc(TRACK_N * sizeof(int16_t));
    trk[L_BREATH].base = trk[L_BREATH].buf; trk[L_HOWL].base = trk[L_HOWL].buf;
    ring = app_alloc(RING * sizeof(int16_t));
    echo_buf = app_alloc(ECHO_N * sizeof(int16_t));
    fdn_buf = app_alloc_fast(FDN * FDN_N * sizeof(float));      // four lines 8 KB apart: in PSRAM their cache slots collide
    if (!trk[L_BREATH].buf || !trk[L_HOWL].buf || !ring || !echo_buf || !fdn_buf) app_log("band: short of memory for the tracks or the FX");
    for (int l = 0; l < L_N; l++) { smooth_init(&lvl_s[l], 0.8f, 0.02f); smooth_init(&mute_s[l], 1.0f, 0.006f); }
    blk_k_lvl = 1.0f - powf(1.0f - lvl_s[0].k, (float)AUDIO_BLOCK);
    blk_k_mute = 1.0f - powf(1.0f - mute_s[0].k, (float)AUDIO_BLOCK);
    smooth_init(&echo_s, 0.35f, 0.03f); smooth_init(&rev_s, 0.35f, 0.03f); smooth_init(&efb_s, 0.4f, 0.03f);
    tcmdq_clear(&tq);
}

static void track_command(const tcmd_t *c, uint32_t f0)
{
    uint32_t len = song.loop_len;
    track_t *t = has_track(c->layer) ? &trk[c->layer] : NULL;
    switch (c->cmd) {
    case T_PRESS: pend = true; press_frame = f0; pend_layer = c->layer; break;
    case T_CONFIRM: {
        if (!pend || !t || t->wipe) { pend = false; break; }
        pend = false;
        uint32_t k = f0 - press_frame; if (k > RING - 1) k = RING - 1;
        if (len && t->has && t->muted) {                     // stopped, then held: a new take in place of the old one
            for (uint32_t j = 0; j < k; j++) {
                uint32_t at = (uint32_t)song_mod((int32_t)(press_frame + j - song.loop_start - t->off), (int32_t)len);
                float r = j < XF ? (float)j / (float)XF : 1.0f;
                t->base[at] = s16(r * f32(ring[(press_frame + j) & RMASK]));
            }
            t->fresh = (int)(len - k); t->muted = 0; t->g = 1.0f;
            t->rec = 2; t->dub_g = t->dub_t = 1;
            t->acc_dry = t->acc_send = 0;
            break;
        }
        if (!len) {                                          // the band's first take: written as it comes, placed at the release
            for (uint32_t j = 0; j < k; j++) t->buf[j] = ring[(press_frame + j) & RMASK];
            t->wpos = (int)k; t->rec = 1; t->has = 0; t->closing = false; t->press = press_frame; t->base = t->buf;
        } else {                                             // adding: what was played since the press, where it was played
            if (!t->has) { t->off = 0; t->base = t->buf; }   // a first take over a loop that exists: the rest of it is silence (wiped)
            for (uint32_t j = 0; j < k; j++) {
                uint32_t at = (uint32_t)song_mod((int32_t)(press_frame + j - song.loop_start - t->off), (int32_t)len);
                float r = j < XF ? (float)j / (float)XF : 1.0f;
                t->base[at] = s16((t->has ? f32(t->base[at]) : 0.0f) + r * f32(ring[(press_frame + j) & RMASK]));
            }
            if (!t->has) { t->has = 1; t->g = 0; }
            t->rec = 2; t->dub_g = t->dub_t = 1;
        }
        t->acc_dry = t->acc_send = 0;
        break;
    }
    case T_RELEASE:
        if (!t) break;
        if (t->rec == 1) {                                   // song.loop_len is set by now (host_first_loop)
            if (!len) { t->rec = 0; t->wpos = 0; break; }
            // position 0 is the first sound (or the press); the take records on to the loop's end (whole bars) and 6 ms
            // past it, then that tail is blended into its start (the audio path does it when it gets there)
            {
                uint32_t skip = c->a;
                if (skip + len + XF > TRACK_N) skip = TRACK_N > len + XF ? TRACK_N - len - XF : 0;
                t->base = t->buf + skip; t->press += skip; t->wpos -= (int)skip;
                if (t->wpos < 0) t->wpos = 0;
            }
            t->off = (uint32_t)song_mod((int32_t)(t->press - song.loop_start), (int32_t)len);
            t->closing = true;
        } else if (t->rec == 2) t->dub_t = 0;
        break;
    case T_TAP: pend = false; if (t && t->has) t->muted = !t->muted; break;
    case T_CLEAR: if (t) { t->rec = 0; t->has = 0; t->muted = 0; t->closing = false; t->fresh = 0; t->wipe = TRACK_N; t->base = t->buf; } break;
    case T_CLEAR_ALL:
        for (int l = 0; l < L_N; l++) if (has_track(l)) { trk[l].rec = 0; trk[l].has = 0; trk[l].muted = 0; trk[l].closing = false; trk[l].fresh = 0; trk[l].wipe = TRACK_N; trk[l].base = trk[l].buf; }
        pend = false;
        break;
    }
}

static void fx_coeffs(void)
{
    float rt = H(H_RSIZE), dm = H(H_RDAMP);
    if (rt == last_rt && dm == last_dmp) return;
    last_rt = rt; last_dmp = dm;
    for (int i = 0; i < FDN; i++) fdn_g[i] = powf(10.0f, -3.0f * (float)fdn_len[i] / (rt * (float)AUDIO_SR));
    fdn_damp = 0.15f + 0.8f * dm * 0.01f;
}

static void audio(const float *in, float *out, int n)
{
    uint32_t f0 = audio_frames();
    int live = (int)H(H_LAYER);
    tcmd_t c;
    while (tcmdq_pop(&tq, &c)) track_command(&c, f0);
    fx_coeffs();
    memset(out, 0, (size_t)n * sizeof(float));
    static float sbus[AUDIO_BLOCK];
    memset(sbus, 0, sizeof sbus);
    uint32_t len = song.loop_len;
    for (int l = 0; l < L_N; l++) {
        memset(dry[l], 0, sizeof dry[l]); memset(send[l], 0, sizeof send[l]);
        uint32_t c0 = CYCLES();
        layers[l]->audio(in, dry[l], send[l], n, l == live);
        uint32_t c1 = CYCLES();
        prof_cyc[l] += c1 - c0;
        track_t *t = has_track(l) ? &trk[l] : NULL;
        float pk = 0;
        // the layer's level and mute: the smoothers step once a block (the same curve: (1 - k)^64 a block), the gain
        // ramps straight across it
        float g0 = lvl_s[l].y * mute_s[l].y;
        lvl_s[l].y += blk_k_lvl * (H(H_LVL_D + l) * 0.01f - lvl_s[l].y);
        mute_s[l].y += blk_k_mute * ((H(H_MUTE_D + l) >= 1.0f ? 0.0f : 1.0f) - mute_s[l].y);
        float g1 = lvl_s[l].y * mute_s[l].y, gd = (g1 - g0) / (float)n;
        if (!t) {                                                    // an events layer: just its sound, if it made any
            bool any = false;
            for (int i = 0; i < n && !any; i++) any = dry[l][i] != 0.0f || send[l][i] != 0.0f;
            if (any)
                for (int i = 0; i < n; i++) {
                    float g = g0 + gd * (float)i, o = dry[l][i] * g;
                    out[i] += o; sbus[i] += send[l][i] * g;
                    float a = fabsf(o); if (a > pk) pk = a;
                }
            layer_peak[l] = pk;
            prof_cyc[L_N] += CYCLES() - c1;
            continue;
        }
        for (int i = 0; i < n; i++) {
            float d = dry[l][i], s = send[l][i];
            {
                if (l == live && ring) ring[(f0 + (uint32_t)i) & RMASK] = s16(d);
                if (t->rec == 1) {
                    if (t->base + t->wpos < t->buf + TRACK_N) t->base[t->wpos++] = s16(d);
                    if (t->closing && len && (t->wpos >= (int)len + XF || t->base + t->wpos >= t->buf + TRACK_N)) {   // the loop's end and the seam
                        if (t->base + len + XF <= t->buf + TRACK_N)
                            for (int j = 0; j < XF; j++) { float r = (float)j / (float)XF; t->base[j] = s16(f32(t->base[j]) * r + f32(t->base[len + (uint32_t)j]) * (1.0f - r)); }
                        t->rec = 0; t->closing = false; t->has = 1; t->g = 0;
                    }
                } else if (len && t->has) {
                    uint32_t pos = (uint32_t)song_mod((int32_t)(f0 + (uint32_t)i - song.loop_start - t->off), (int32_t)len);
                    float y = f32(t->base[pos]);
                    if (t->fresh > 0) { t->base[pos] = s16(d * t->dub_g); y = 0.0f; t->fresh--; }   // over the old take
                    else if (t->rec == 2 || t->dub_g > 0.0f) t->base[pos] = s16(y + d * t->dub_g);
                    if (t->muted) { t->g -= 1.0f / (float)XF; if (t->g < 0.0f) t->g = 0.0f; }   // 6 ms ramps
                    else { t->g += 1.0f / (float)XF; if (t->g > 1.0f) t->g = 1.0f; }
                    d += y * t->g;
                    s += y * t->g * t->send_ratio;
                }
                if (t->dub_g < t->dub_t) { t->dub_g += 1.0f / (float)XF; if (t->dub_g > t->dub_t) t->dub_g = t->dub_t; }
                else if (t->dub_g > t->dub_t) { t->dub_g -= 1.0f / (float)XF; if (t->dub_g < 0.0f) t->dub_g = 0.0f; }
                if (t->rec == 2 && t->dub_t == 0.0f && t->dub_g == 0.0f) t->rec = 0;
                if (t->rec) { t->acc_dry += fabsf(dry[l][i]); t->acc_send += fabsf(send[l][i]); }
            }
            float g = g0 + gd * (float)i;
            float o = d * g;
            out[i] += o; sbus[i] += s * g;
            float a = fabsf(o); if (a > pk) pk = a;
        }
        if (t) {
            if (t->wipe > 0) { int k = t->wipe > 4096 ? 4096 : t->wipe; memset(t->buf + (TRACK_N - t->wipe), 0, (size_t)k * sizeof(int16_t)); t->wipe -= k; }
            if (t->rec && t->acc_dry > 1e-3f) t->send_ratio = clampf(t->acc_send / t->acc_dry, 0.0f, 2.0f);
        }
        layer_peak[l] = pk;
        prof_cyc[L_N] += CYCLES() - c1;
    }
    uint32_t cfx = CYCLES();
    // the FX bus: an echo in time with the song, then a reverb, both fed by the sends
    float spb = 60.0f / (song.bpm > 1.0f ? song.bpm : 1.0f);
    static const float div[] = { 0.5f, 0.75f, 1.0f, 1.5f, 2.0f };
    float want_d = clampf(spb * div[(int)H(H_ETIME)] * (float)AUDIO_SR, 64.0f, (float)ECHO_N - 8.0f);
    float pk = 0;
    for (int i = 0; i < n; i++) {
        float x = sbus[i], y = out[i];
        if (echo_buf) {
            echo_d_s += 0.0005f * (want_d - echo_d_s);
            if (echo_d_s < 64.0f) echo_d_s = want_d;
            int di = (int)echo_d_s; float fr = echo_d_s - (float)di;
            float e0 = f32(echo_buf[(echo_w - (uint32_t)di) & (ECHO_N - 1u)]), e1 = f32(echo_buf[(echo_w - (uint32_t)di - 1) & (ECHO_N - 1u)]);
            float e = e0 + (e1 - e0) * fr;
            echo_lp += 0.4f * (e - echo_lp);
            echo_buf[echo_w & (ECHO_N - 1u)] = s16(soft_tanh(x + echo_lp * smooth_to(&efb_s, H(H_EFB) * 0.01f)));
            echo_w++;
            y += echo_lp * smooth_to(&echo_s, H(H_ECHO) * 0.01f);
            x += 0.3f * echo_lp;
        }
        if (fdn_buf) {                                           // four lines, a Householder mix, damped
            float o[FDN], sum = 0;
            for (int k = 0; k < FDN; k++) {
                float v = fdn_buf[k * FDN_N + ((fdn_w - (uint32_t)fdn_len[k]) & (FDN_N - 1u))];
                fdn_lp[k] += fdn_damp * (v - fdn_lp[k]);
                o[k] = fdn_lp[k] * fdn_g[k];
                sum += o[k];
            }
            sum *= 0.5f;
            for (int k = 0; k < FDN; k++) fdn_buf[k * FDN_N + (fdn_w & (FDN_N - 1u))] = flush_denormal(o[k] - sum + 0.35f * x);
            fdn_w++;
            y += smooth_to(&rev_s, H(H_REVERB) * 0.01f) * 0.6f * (o[0] - o[1] + o[2] - o[3]);
        }
        out[i] = y;
        float a = fabsf(y); if (a > pk) pk = a;
    }
    prof_cyc[L_N + 1] += CYCLES() - cfx;
    prof_blocks++;
    host_out_env = pk > host_out_env ? pk : host_out_env * 0.96f;
    memcpy(last_out, out, (size_t)(n < AUDIO_BLOCK ? n : AUDIO_BLOCK) * sizeof(float));
}

// ---------------------------------------------------------------- the UI side
static void mixer_enter(void), mixer_leave(void), mixer_tick(const ctl_t *c), mixer_draw(const view_t *v);
static bool mixer_hold(void);
static int  mx_cur;
static void clear_layer(int l), clear_all(void);
static int  slide_from = -1, slide_dir;
static float slide_t = 1.0f;
static int  prev_live;
static bool rec_was, confirmed;
static int  rec_ms, rec_layer;
static uint32_t first_press;
static int  beat_flash;
static uint32_t last_beat = 0xFFFFFFFFu;

static int layer_state(int l)
{
    if (layers[l]->events) return layers[l]->state ? layers[l]->state() : LOOP_NONE;
    const track_t *t = &trk[l];
    if (t->rec) return LOOP_REC;
    if (!t->has) return LOOP_NONE;
    return t->muted ? LOOP_OFF : LOOP_ON;
}

static void clear_layer(int l)
{
    if (layers[l]->events) { if (layers[l]->clear) layers[l]->clear(); }
    else track_cmd(T_CLEAR, l);
}

static void clear_all(void)
{
    for (int l = 0; l < L_N; l++) if (layers[l]->events && layers[l]->clear) layers[l]->clear();
    track_cmd(T_CLEAR_ALL, 0);
    __atomic_store_n(&song.loop_len, 0, __ATOMIC_RELEASE);
    song.beats = 0; song.kicks = 0;
    link_set_flags(0);
    toast("ALL LOOPS CLEARED", 1200);
}

// REC for BREATH and HOWL: the host records them. Hold = record (the jam's first loop) or add; tap = mute.
static void track_rec(const ctl_t *c, bool rec, int live)
{
    bool press = rec && !rec_was, release = !rec && rec_was;
    if (press) { rec_layer = live; confirmed = false; rec_ms = 0; first_press = c->frames; track_cmd(T_PRESS, live); }
    if (rec_layer < 0) { rec_was = rec; return; }                // a take closed at the longest loop: wait for the release
    if (rec) rec_ms += 10;
    if (rec && !confirmed && rec_ms >= HOLD_MS) {
        confirmed = true; track_cmd(T_CONFIRM, rec_layer);
        if (song.loop_len && has_track(rec_layer) && trk[rec_layer].has && trk[rec_layer].muted) { toast("NEW TAKE", 900); coach_done(LEARN_INST + 11); }
        coach_done(LEARN_REC);
    }
    bool too_long = rec && confirmed && !song.loop_len && c->frames - first_press >= (uint32_t)(JAM_LOOP_MAX_S * AUDIO_SR) - 64;
    if ((release || too_long) && confirmed) {
        uint32_t skip = 0;
        if (!song.loop_len) {
            // the silence before the take's first sound is cut (and REC's click, the first 0.12 s): it starts there
            track_t *t = has_track(rec_layer) ? &trk[rec_layer] : NULL;
            if (t && t->rec == 1) {
                int w = t->wpos, g = AUDIO_SR * 12 / 100;
                for (int i = g; i < w; i++) { int v = t->buf[i]; if (v > 492 || v < -492) { skip = (uint32_t)(i - AUDIO_SR * 15 / 1000); break; } }
                if (skip && skip < (uint32_t)g) skip = (uint32_t)g;
            }
            host_first_loop(first_press + skip, c->frames - first_press - skip);
        }
        track_cmd2(T_RELEASE, rec_layer, skip);
        confirmed = false;
        if (too_long) { rec_was = rec; rec_layer = -1; return; }
    } else if (release && !confirmed && rec_layer >= 0) {
        track_cmd(T_TAP, rec_layer);
        if (has_track(rec_layer) && trk[rec_layer].has) { toast(trk[rec_layer].muted ? "TRACK ON" : "TRACK OFF", 700); coach_done(LEARN_INST + 1); }
    }
    rec_was = rec;
}

static void tick(const ctl_t *c)
{
    if ((c->ticks % 1000 == 0 || (c->ticks < 3000 && c->ticks % 100 == 0)) && prof_blocks) {   // the cost of each part, us per
                                                             // block: every second for the first 30 s, then every 10 s
        uint32_t b = prof_blocks;
        app_log("cpu us/block: drums %lu bass %lu lead %lu keys %lu breath %lu howl %lu mix %lu fx %lu (of 2000)",
                (unsigned long)(prof_cyc[0] / b / 240), (unsigned long)(prof_cyc[1] / b / 240), (unsigned long)(prof_cyc[2] / b / 240),
                (unsigned long)(prof_cyc[3] / b / 240), (unsigned long)(prof_cyc[4] / b / 240), (unsigned long)(prof_cyc[5] / b / 240),
                (unsigned long)(prof_cyc[6] / b / 240), (unsigned long)(prof_cyc[7] / b / 240));
        for (int i = 0; i < L_N + 2; i++) prof_cyc[i] = 0;
        prof_blocks = 0;
    }
    // the knob in play always picks the layer: editing a setting in the menu does not take it over (the core
    // would make it turn the last edited setting)
    if (meta.quick[0] && memcmp(meta.quick, "slay", 4)) memset(meta.quick, 0, sizeof meta.quick);
    if (!link_up) link_start();
    link_tick(c->dt);
    link_follow();
    static int ovr_sent = -1;
    if (chord_ovr != ovr_sent) { ovr_sent = chord_ovr; harm_set_here(); }   // a chord played live goes to the other boards
    set_song(c->frames);
    int live = (int)H(H_LAYER);
    if (live != prev_live) {                                   // the knob moved: slide, and close a take on the old layer
        slide_from = prev_live; slide_dir = live > prev_live ? 1 : -1; slide_t = 0;
        if (rec_was && prev_live < L_N && !layers[prev_live]->events) { ctl_t r = *c; track_rec(&r, false, prev_live); }
        rec_layer = -1; confirmed = false; rec_was = c->rec;       // REC still held from there is not a press here
        if (prev_live == L_MIX) mixer_leave();
        if (live == L_MIX) mixer_enter();
        prev_live = live;
    }
    ctl_t idle = *c;
    idle.rec = idle.rec_press = idle.rec_release = false; idle.rec_ms = 0;
    idle.has_imu = false; idle.has_joy = false; idle.jx = idle.jy = 0; idle.nburst = 0;
    idle.lean_x = idle.lean_y = 0; idle.gyro = (vec3_t){ 0, 0, 0 }; idle.gyro_mag = 0;
    idle.knob = 0; idle.knob_tap = idle.knob_hold = idle.knob_down = idle.knob_turned = false;
    for (int l = 0; l < L_N; l++) {
        if (l != live) { layers[l]->tick(&idle, false); continue; }
        if (layers[l]->events) layers[l]->tick(c, true);
        else {
            ctl_t lc = *c;                                       // BREATH and HOWL: REC is the host's track
            lc.rec = lc.rec_press = lc.rec_release = false; lc.rec_ms = 0;
            layers[l]->tick(&lc, true);
            track_rec(c, c->rec, l);
        }
    }
    if (live == L_MIX) mixer_tick(c);
    if (live == L_MIX || layers[live]->events) rec_was = c->rec;
    // the beat, for the header
    uint32_t b = (uint32_t)song_beat(c->frames);
    if (b != last_beat) { last_beat = b; beat_flash = (b % 4 == 0) ? 3 : 2; }
    else if (beat_flash > 0 && c->ticks % 4 == 0) beat_flash--;
    // the LED: the live layer's colour, pulsing with the beat; red while recording
    static const uint8_t col[L_N][3] = { { 255, 60, 20 }, { 160, 0, 255 }, { 255, 200, 0 }, { 40, 255, 90 }, { 0, 200, 255 }, { 120, 140, 255 } };
    if (live == L_MIX) { uint8_t w = (uint8_t)(beat_flash ? 90 : 25); led_rgb(w, w, w); return; }
    float lv = clampf(layer_peak[live] * 2.0f, 0.0f, 1.0f) * 0.7f + (beat_flash ? 0.3f : 0.08f);
    if (layer_state(live) == LOOP_REC) led_rgb((uint8_t)(255.0f * (beat_flash ? 1.0f : 0.4f)), 0, 0);
    else led_rgb((uint8_t)(col[live][0] * lv), (uint8_t)(col[live][1] * lv), (uint8_t)(col[live][2] * lv));
}

static void changed(int p)
{
    if (p < 0) {
        for (int l = 0; l < L_N; l++) layers[l]->changed(-1);
        if (link_up) { tempo_set_here(); harm_set_here(); link_enable(H(H_LINK) >= 1.0f, (int)H(H_GROUP), (int)H(H_RANGE)); }
        return;
    }
    if (p < HB) { int l = p / LAYER_PARAMS_MAX, i = p % LAYER_PARAMS_MAX; if (i < layers[l]->nparams) layers[l]->changed(i); return; }
    if (!link_up || applying) return;
    switch (p - HB) {
    case H_TEMPO: tempo_set_here(); break;
    case H_KEY: case H_SCALE: case H_PROG: case H_CHORD: case H_BARS: harm_set_here(); break;
    case H_LINK: case H_GROUP: case H_RANGE:
        link_enable(false, 1, 1);                                  // a new group or range: start the radio again
        link_enable(H(H_LINK) >= 1.0f, (int)H(H_GROUP), (int)H(H_RANGE));
        peers_was = 0;
        break;
    }
}

static void action(int p)
{
    if (p < HB) { int l = p / LAYER_PARAMS_MAX; if (layers[l]->action) layers[l]->action(p % LAYER_PARAMS_MAX); return; }
    switch (p - HB) {
    case H_CLR_D: case H_CLR_B: case H_CLR_L: case H_CLR_K: case H_CLR_H: case H_CLR_W: {
        int l = p - HB - H_CLR_D;
        clear_layer(l);
        char b[24]; snprintf(b, sizeof b, "%s CLEARED", layer_names[l]); toast(b, 1000);
        break;
    }
    case H_CLR_ALL: clear_all(); break;
    case H_PEERS: {
        link_peer_t pr[LINK_PEERS_MAX];
        int np = link_peers(pr, LINK_PEERS_MAX);
        char b[48];
        if (!link_radio()) snprintf(b, sizeof b, "LINK IS OFF");
        else if (!np) snprintf(b, sizeof b, "NO BOARD IN GROUP %d YET", (int)H(H_GROUP));
        else {
            int k = snprintf(b, sizeof b, "%s", pr[0].name);
            for (int i = 1; i < np && k < (int)sizeof b - 10; i++) k += snprintf(b + k, sizeof b - (size_t)k, " + %s", pr[i].name);
        }
        toast(b, 2500);
        break;
    }
    }
}

// the knob held in play: the live layer's (the drums' grid editor), else the core's motion hold
static bool hold(void)
{
    if ((int)H(H_LAYER) == L_MIX) return mixer_hold();
    const layer_t *L = layers[(int)H(H_LAYER)];
    return L->hold ? L->hold() : false;
}

static bool any_playing(void)
{
    for (int l = 0; l < L_N; l++) if (layers[l]->playing && layers[l]->playing()) return true;
    return false;
}

static void midi_in(uint8_t st, uint8_t d1, uint8_t d2)
{
    if ((int)H(H_LAYER) == L_MIX) return;
    const layer_t *L = layers[(int)H(H_LAYER)];
    if (L->midi_in) L->midi_in(st, d1, d2);
}

// ---------------------------------------------------------------- the mixer (the knob's seventh place, past HOWL)
// Six strips: each layer's level and mute (the MIX page's settings), its meter, and its loop. With the stick: left and
// right pick a strip, up and down set its level. REC: a tap mutes the strip, held 1 s erases its loop, 2 s every loop.
// The knob held takes the knob: turn = pick a strip, press and turn = its level, tap = mute, hold = done.
#define MX_W   21                          // a strip's width: six fill the screen
#define MX_TOP 29                          // the fader's travel, rows
#define MX_BOT 51
static int   mx_rec_stage, mx_jx_ms, mx_jy_ms, mx_hint_ms, mx_rec_ms, mx_hint_n;
static bool  mx_grab, mx_seen, mx_seen_grab, mx_joy;
static float mx_show[L_N], mx_pk[L_N], mx_peak[L_N], mx_flash[L_N], mx_cx = -1;
static UI_RAM parts_t mx_parts;
static uint32_t mx_seed = 0x5eed1u;

static void mixer_enter(void)
{
    mx_rec_stage = 2; mx_jx_ms = mx_jy_ms = 0; mx_rec_ms = 0;     // (REC held from the last layer does nothing here)
    mx_hint_ms = mx_seen ? 0 : 6000; mx_seen = true; mx_hint_n = 0;
    for (int l = 0; l < L_N; l++) mx_show[l] = H(H_LVL_D + l) * 0.01f;
    coach_done(LEARN_INST + 10);
}
static void mixer_leave(void) { if (mx_grab) { mx_grab = false; knob_grab(false); } }
static bool mixer_hold(void)
{
    if (!mx_grab) {
        mx_grab = true; knob_grab(true);
        if (!mx_seen_grab) { mx_hint_ms = 6000; mx_hint_n = 0; mx_seen_grab = true; }
    }
    return true;
}
static void mixer_level(int l, int d)
{
    int p = HB + H_LVL_D + l;
    param_set(p, clampf(pv[p] + 5.0f * (float)d, 0.0f, 100.0f));
    if (H(H_MUTE_D + l) >= 1.0f && d > 0) param_set(HB + H_MUTE_D + l, 0);   // turned up: on again
}
static void mixer_mute(int l)
{
    bool m = H(H_MUTE_D + l) < 1.0f;
    param_set(HB + H_MUTE_D + l, m ? 1.0f : 0.0f);
    char b[24]; snprintf(b, sizeof b, "%s %s", layer_names[l], m ? "MUTED" : "ON"); toast(b, 700);
}
static void mixer_erase(int l)
{
    float x = (float)(1 + l * MX_W + 10), y = 58.0f;
    if (layer_state(l) == LOOP_NONE) { char b[24]; snprintf(b, sizeof b, "%s: NO LOOP", layer_names[l]); toast(b, 800); return; }
    clear_layer(l);
    mx_flash[l] = 1.0f;
    parts_burst(&mx_parts, x, y, 10, 55.0f, 0.5f, PART_SPARK, &mx_seed);
    char b[24]; snprintf(b, sizeof b, "%s ERASED", layer_names[l]); toast(b, 900);
}

static void mixer_tick(const ctl_t *c)
{
    mx_joy = c->has_joy;
    bool touched = false;
    if (mx_grab) {
        if (c->knob_hold) { mx_grab = false; knob_grab(false); }
        else {
            if (c->knob_tap) { mixer_mute(mx_cur); touched = true; }
            if (c->knob) {
                if (c->knob_down) mixer_level(mx_cur, c->knob);
                else mx_cur = clampi(mx_cur + c->knob, 0, L_N - 1);
                touched = true;
            }
        }
    }
    if (c->has_joy) {                                            // the stick: strips, and the level (repeating while held)
        int dx = c->jx > 0.6f ? 1 : c->jx < -0.6f ? -1 : 0, dy = c->jy > 0.6f ? 1 : c->jy < -0.6f ? -1 : 0;
        if (dx) { if (mx_jx_ms == 0 || (mx_jx_ms >= 350 && (mx_jx_ms - 350) % 150 < 10)) mx_cur = clampi(mx_cur + dx, 0, L_N - 1); mx_jx_ms += 10; touched = true; }
        else mx_jx_ms = 0;
        if (dy) { if (mx_jy_ms == 0 || (mx_jy_ms >= 300 && (mx_jy_ms - 300) % 80 < 10)) mixer_level(mx_cur, dy); mx_jy_ms += 10; touched = true; }
        else mx_jy_ms = 0;
    }
    // REC: a tap mutes the strip; held, it erases its loop (1 s), then every loop (2 s)
    if (c->rec_press) { mx_rec_stage = 0; touched = true; }
    mx_rec_ms = c->rec && mx_rec_stage < 2 ? c->rec_ms : 0;
    if (c->rec && c->rec_ms >= 1000 && mx_rec_stage == 0) { mixer_erase(mx_cur); mx_rec_stage = 1; }
    if (c->rec && c->rec_ms >= 2000 && mx_rec_stage == 1) {
        clear_all();
        for (int l = 0; l < L_N; l++) { mx_flash[l] = 1.0f; parts_burst(&mx_parts, (float)(1 + l * MX_W + 10), 58.0f, 5, 50.0f, 0.5f, PART_SPARK, &mx_seed); }
        mx_rec_stage = 2;
    }
    if (c->rec_release && c->rec_ms < 300 && mx_rec_stage == 0) mixer_mute(mx_cur);
    if (c->rec_release) mx_rec_stage = 2;
    if (touched && mx_hint_ms > 1500) mx_hint_ms = 1500;
    if (mx_hint_ms > 0) mx_hint_ms -= 10;
}

static void mixer_draw(const view_t *v)
{
    const int h = MX_BOT - MX_TOP;
    for (int l = 0; l < L_N; l++) {
        int x0 = 1 + l * MX_W, fx = x0 + 7;
        bool mute = H(H_MUTE_D + l) >= 1.0f;
        mx_show[l] += 0.3f * (H(H_LVL_D + l) * 0.01f - mx_show[l]);   // the fader slides to its place
        layers[l]->icon(x0 + 4, 17, false);                          // the layer's icon, as in the header
        // the fader: a dotted track, the level filled below the cap (grey when muted)
        for (int y = MX_TOP; y <= MX_BOT; y += 2) oled_pixel(fx, y, true);
        int yc = MX_BOT - (int)(mx_show[l] * (float)h + 0.5f);
        if (yc < MX_BOT) { if (mute) oled_dither(fx - 1, yc + 1, 3, MX_BOT - yc, 2, true); else oled_rect(fx - 1, yc + 1, 3, MX_BOT - yc, true); }
        if (mute) { oled_rect(fx - 3, yc - 1, 7, 3, false); oled_frame(fx - 3, yc - 1, 7, 3); }
        else oled_rrect(fx - 3, yc - 1, 7, 3, 1, true);
        // the meter: what the layer plays now (after its fader), -42..0 dB in 2-row segments, a peak that falls
        float pk = layer_peak[l], m = pk > 1e-4f ? clampf((20.0f * log10f(pk) + 42.0f) / 42.0f, 0.0f, 1.0f) : 0.0f;
        mx_pk[l] = m > mx_pk[l] ? m : fmaxf(0.0f, mx_pk[l] - 0.06f);
        mx_peak[l] = m > mx_peak[l] ? m : fmaxf(0.0f, mx_peak[l] - 0.012f);
        int mh = (int)(mx_pk[l] * (float)h);
        for (int y = 0; y < mh; y += 2) oled_rect(x0 + 12, MX_BOT - y, 3, 1, true);
        if (mx_peak[l] > 0.02f) oled_rect(x0 + 12, MX_BOT - (int)(mx_peak[l] * (float)h), 3, 1, true);
        // the level, or MUTE
        char b[8];
        if (mute) { oled_ftext_c(F_TINY, x0 + 10, 54, "MUTE", 1); oled_invert(x0 + 2, 53, 17, 7); }
        else { snprintf(b, sizeof b, "%d", (int)H(H_LVL_D + l)); oled_ftext_c(F_TINY, x0 + 10, 54, b, 1); }
        // its loop: plays (a bar), stopped (dotted), recording (blinking), plays by itself (a dot), nothing
        int st = layer_state(l);
        if (st == LOOP_ON) oled_rect(x0 + 4, 61, 13, 2, true);
        else if (st == LOOP_OFF) oled_hline_pat(x0 + 4, 62, 13, 0x55, true);
        else if (st == LOOP_REC) { if ((v->frame / 4) % 2 == 0) oled_rect(x0 + 4, 61, 13, 2, true); }
        else if (layers[l]->playing && layers[l]->playing()) oled_rect(x0 + 9, 61, 3, 2, true);
        // erased: the strip flashes
        if (mx_flash[l] > 0.0f) { oled_dither(x0, 17, MX_W - 1, 46, (int)(mx_flash[l] * 4.0f), true); mx_flash[l] -= 0.08f; }
    }
    // the strip picked: a frame sliding to it (solid while the knob is the mixer's, corners otherwise)
    float tx = (float)(mx_cur * MX_W);
    mx_cx = mx_cx < 0 ? tx : mx_cx + 0.4f * (tx - mx_cx);
    int cx = (int)(mx_cx + 0.5f);
    if (mx_grab) { oled_rframe(cx, 16, MX_W + 1, 48, 3); oled_invert(cx + 1, 17, MX_W - 1, 10); }
    else {
        static const int8_t cn[4][2] = { { 0, 16 }, { MX_W, 16 }, { 0, 63 }, { MX_W, 63 } };
        for (int k = 0; k < 4; k++) {
            int x = cx + cn[k][0], y = cn[k][1], dx = k & 1 ? -1 : 1, dy = k < 2 ? 1 : -1;
            oled_line(x, y, x + 3 * dx, y, true); oled_line(x, y, x, y + 3 * dy, true);
        }
    }
    // REC held: erasing, a bar filling under the strip's name (then across every strip)
    if (mx_rec_ms > 300) {
        float f = clampf((float)(mx_rec_ms - 300) / 700.0f, 0.0f, 1.0f);
        if (mx_rec_stage == 0) oled_rect(cx + 2, 27, (int)(f * (float)(MX_W - 3)), 1, true);
        else { float g = clampf((float)(mx_rec_ms - 1000) / 1000.0f, 0.0f, 1.0f); oled_rect(0, 27, (int)(g * (float)OLED_W), 1, true); }
    }
    parts_step(&mx_parts, 1.0f / 25.0f, 60.0f, 0.9f);
    parts_draw(&mx_parts);
    // the first visits: how it works, a line at a time
    if (mx_hint_ms > 0) {
        static const char *const free_l[] = { "HOLD THE KNOB TO MIX", "TAP REC: MUTE", "HOLD REC: ERASE ITS LOOP", "STICK: PICK AND LEVEL" };
        static const char *const grab_l[] = { "TURN: PICK A LAYER", "PRESS AND TURN: LEVEL", "TAP: MUTE", "HOLD THE KNOB: DONE" };
        int n = mx_grab || mx_joy ? 4 : 3;
        int k = ((6000 - mx_hint_ms) / 1500) % n;
        ui_box(40, mx_grab ? grab_l[k] : free_l[k]);
    }
}
int  mixer_cursor(void) { return mx_cur; }                       // for tools/sim
bool mixer_grabbed(void) { return mx_grab; }
int  host_layer_state(int l) { return layer_state(l); }

// ---------------------------------------------------------------- drawing: the header band and the slide
static void chord_name(char *b, int n)
{
    // the chord's name from its three notes: major, minor, diminished or augmented
    int r = song.chord[0] % 12, t3 = (song.chord[1] - song.chord[0] + 120) % 12, t5 = (song.chord[2] - song.chord[0] + 120) % 12;
    const char *q = t3 == 4 && t5 == 7 ? "" : t3 == 3 && t5 == 7 ? "m" : t3 == 3 && t5 == 6 ? "o" : t3 == 4 && t5 == 8 ? "+" : "";
    snprintf(b, (size_t)n, "%s%s", key_names[r], q);
}

static void link_glyph(int x, int y, const view_t *v);

static void header(const view_t *v)
{
    int live = (int)H(H_LAYER);
    if (live == L_MIX) {                                         // the mixer: its name and the strip it is on
        int x = 1 + oled_pill(F_SMALL, 1, 3, "MIX", true) + 3;
        char b[20];
        if (H(H_MUTE_D + mx_cur) >= 1.0f) snprintf(b, sizeof b, "%s OFF", layer_names[mx_cur]);
        else snprintf(b, sizeof b, "%s %d", layer_names[mx_cur], (int)H(H_LVL_D + mx_cur));
        oled_ftext(F_SMALL, x, 5, b);
    } else
    for (int l = 0; l < L_N; l++) {                              // the layer bar: six icons, the live one lit
        int x = 1 + l * 13;
        layers[l]->icon(x, 3, l == live);
        if (l == live) oled_invert(x - 1, 2, 14, 11);
        int st = layer_state(l);                                 // its loop under it: playing, muted, recording
        if (st == LOOP_ON) oled_hline_pat(x + 1, 13, 10, 0xFF, true);
        else if (st == LOOP_OFF) oled_hline_pat(x + 1, 13, 10, 0x55, true);
        else if (st == LOOP_REC && (v->frame / 4) % 2 == 0) oled_rect(x + 1, 12, 10, 2, true);
        else if (layers[l]->playing && layers[l]->playing()) oled_rect(x + 5, 13, 2, 1, true);   // it plays by itself
    }
    char b[8];
    chord_name(b, sizeof b);
    oled_ftext_ex(F_SMALL, 80, 5, b, 1);
    // the bar's four beats, the one now filled (bigger on the downbeat)
    int beat = (int)song_beat(audio_frames()) % 4;
    for (int k = 0; k < 4; k++) {
        int bx2 = 97 + k * 3;
        if (k == beat) { bool big = beat_flash > 1; oled_rect(bx2 - (big ? 1 : 0), big ? 5 : 6, big ? 3 : 2, big ? 4 : 3, true); }
        else oled_pixel(bx2, 8, true);
    }
    link_glyph(110, 4, v);
    ui_meter(OLED_W - 11, 3, 11, 9, v->out_level, false);
    // the loop's playhead along row 15 (beats as dots); while the first take runs, a bar that grows
    uint32_t len = song.loop_len;
    if (len) {
        for (int k = 0; k < song.beats; k++) oled_pixel(k * (OLED_W - 1) / song.beats, 15, true);
        int px = (int)((float)song_pos(audio_frames()) / (float)len * (float)(OLED_W - 1));
        oled_rect(px - 1, 14, 3, 2, true);
    } else {
        int rl = -1;
        for (int l = 0; l < L_N; l++) if (layer_state(l) == LOOP_REC) rl = l;
        if (rl >= 0) oled_rect(0, 15, (int)fminf((float)OLED_W, (float)rec_ms * 0.001f / (float)JAM_LOOP_MAX_S * (float)OLED_W), 1, true);
    }
}

// the link in the header: nothing with the radio off; a slow blink looking for others; two boards side by side when
// linked, their lights flashing on the beat (hollow while the clocks are still settling)
static void link_glyph(int x, int y, const view_t *v)
{
    if (!link_radio()) return;
    link_peer_t pr[LINK_PEERS_MAX];
    int np = link_peers(pr, LINK_PEERS_MAX);
    if (!np) { if ((v->frame / 12) % 2 == 0) { oled_pixel(x + 2, y + 3, true); ui_arc(x + 2, y + 3, 3, -60, 60); } return; }
    bool on = beat_flash > 1 || (beat_flash > 0 && (v->frame & 1));
    oled_frame(x, y + 1, 3, 6); oled_frame(x + 4, y + 1, 3, 6);
    if (link_synced() && link_error_ms() < 2.0f) { if (on) { oled_rect(x, y + 1, 3, 6, true); oled_rect(x + 4, y + 1, 3, 6, true); } }
    else if ((v->frame / 3) % 2 == 0) oled_pixel(x + 3, y + 3, true);
    if (link_tempo_clash() && (v->frame / 6) % 2 == 0) oled_pixel(x + 3, y, true);
}

#define SCENE_Y 16
#define SCENE_H (OLED_H - SCENE_Y)
static UI_RAM uint8_t snap_a[SCENE_H][OLED_W / 8];

static void grab(uint8_t buf[][OLED_W / 8])
{
    for (int y = 0; y < SCENE_H; y++) for (int xb = 0; xb < OLED_W / 8; xb++) {
        uint8_t v = 0;
        for (int k = 0; k < 8; k++) if (oled_get(xb * 8 + k, y + SCENE_Y)) v |= (uint8_t)(1 << k);
        buf[y][xb] = v;
    }
}

static void scene(int l, const view_t *v) { if (l == L_MIX) mixer_draw(v); else layers[l]->draw(v); }

static void draw(const view_t *v)
{
    int live = (int)H(H_LAYER);
    if (slide_t < 1.0f && slide_from >= 0) {
        // the old scene and the new, side by side, sliding
        scene(slide_from, v);
        grab(snap_a);
        oled_rect(0, SCENE_Y, OLED_W, SCENE_H, false);
        scene(live, v);
        float e = ease_out(slide_t);
        int off = (int)((1.0f - e) * (float)OLED_W) * slide_dir;    // the new scene comes in from that side
        static UI_RAM uint8_t snap_b[SCENE_H][OLED_W / 8];
        grab(snap_b);
        oled_rect(0, SCENE_Y, OLED_W, SCENE_H, false);
        for (int y = 0; y < SCENE_H; y++) for (int x = 0; x < OLED_W; x++) {
            int xn = x - off, xo = x - off + OLED_W * slide_dir;
            bool on = (xn >= 0 && xn < OLED_W && (snap_b[y][xn >> 3] >> (xn & 7) & 1)) || (xo >= 0 && xo < OLED_W && (snap_a[y][xo >> 3] >> (xo & 7) & 1));
            if (on) oled_pixel(x, y + SCENE_Y, true);
        }
        oled_line(off > 0 ? off - 1 : OLED_W + off, SCENE_Y, off > 0 ? off - 1 : OLED_W + off, OLED_H - 1, true);
        slide_t += 0.2f;
    } else scene(live, v);
    oled_rect(0, 0, OLED_W, SCENE_Y, false);                     // the header band is the host's
    header(v);
}

// ---------------------------------------------------------------- splash: the four players, one by one
// The box knocked by a fist, the woofer, the ocarina player, the wolf: each pops up in turn, then they all play
// together on a beat while the title drops in.
static const char *const wolf_up[] = {
    "#...................",
    "##..................",
    ".##.................",
    ".###.#..............",
    "..######............",
    "..#######...........",
    "...#######..........",
    "....######..........",
    "....#######.........",
    ".....#######........",
    ".....########.......",
    "....##########......",
    "....###########.....",
    "....############....",
    "....#############...",
    "....##############..",
    ".....#############.#",
    ".....###.....#######",
    "....####....####....",
    "...#####...#####....",
};
static const char *const wolf_down[] = {
    "....................",
    "....................",
    "......#.............",
    ".##..##.............",
    "########............",
    ".#######............",
    "..######............",
    "....######..........",
    "....#######.........",
    ".....#######........",
    ".....########.......",
    "....##########......",
    "....###########.....",
    "....############....",
    "....#############...",
    "....##############..",
    ".....#############.#",
    ".....###.....#######",
    "....####....####....",
    "...#####...#####....",
};

static void sp_box(int cx, int base, int f, bool hit)
{
    int sh = hit ? 1 : 0;
    oled_frame(cx - 4 + sh, base - 11, 14, 11);
    oled_line(cx - 4 + sh, base - 11, cx + sh, base - 15, true); oled_line(cx + 9 + sh, base - 11, cx + 13 + sh, base - 15, true);
    oled_line(cx + sh, base - 15, cx + 13 + sh, base - 15, true); oled_line(cx + 13 + sh, base - 15, cx + 13 + sh, base - 5, true);
    oled_line(cx + 9 + sh, base - 1, cx + 13 + sh, base - 5, true);
    int k = f % 12, dx = k < 6 ? k : 12 - k;                    // the fist swings in and out
    int fx = cx - 18 + dx * 2;
    oled_rrect(fx, base - 9, 8, 7, 2, true);
    oled_line(fx - 6, base - 7, fx, base - 7, true); oled_line(fx - 6, base - 4, fx, base - 4, true);
    if (hit) { oled_line(cx - 6, base - 13, cx - 8, base - 15, true); oled_line(cx - 6, base - 1, cx - 8, base + 1, true); oled_line(cx - 7, base - 7, cx - 10, base - 7, true); }
}

static void sp_woofer(int cx, int base, int f, bool hit)
{
    int cy = base - 13;
    oled_rframe(cx - 10, cy - 12, 21, 25, 3);
    oled_circle(cx, cy - 7, 2, true);                           // the tweeter
    oled_circle(cx, cy + 3, 7, true);
    oled_circle(cx, cy + 3, hit ? 5 : 3, true);
    oled_disc(cx, cy + 3, 1, true);
    if (hit) { ui_arc(cx, cy + 3, 13, -35, 35); ui_arc(cx, cy + 3, 13, 145, 215); ui_arc(cx, cy + 3, 16, -25, 25); }
    (void)f;
}

static void sp_player(int cx, int base, int f)
{
    int y = base - 20;
    oled_circle(cx, y, 2, true);
    oled_line(cx, y + 3, cx, y + 12, true);
    oled_line(cx, y + 12, cx - 3, y + 19, true); oled_line(cx, y + 12, cx + 3, y + 19, true);
    oled_line(cx, y + 5, cx + 5, y + 3, true); oled_line(cx, y + 7, cx + 6, y + 4, true);
    oled_rframe(cx + 3, y - 1, 8, 5, 2);                        // the ocarina at the mouth
    oled_pixel(cx + 6, y + 1, true); oled_pixel(cx + 8, y + 1, true);
    for (int k = 0; k < 3; k++) {                               // notes rising from it
        int t = (f + k * 10) % 30;
        if (t >= 26) continue;
        int nx = cx + 12 + t / 4 + (k & 1) * 3, ny = y - 2 - t / 2;
        oled_disc(nx, ny, 1, true); oled_line(nx + 1, ny, nx + 1, ny - 4, true);
        if (k == 1) oled_line(nx + 1, ny - 4, nx + 3, ny - 3, true);
    }
}

static void sp_wolf(int cx, int base, int f, bool howl)
{
    oled_circle(cx + 10, base - 32, 4, true);                   // the moon
    ui_sprite(cx - 10, base - 20, howl ? wolf_up : wolf_down, 20, false, true);
    if (howl) { int t = f % 16; ui_arc(cx - 11, base - 20, 3 + t / 3, 200, 250); ui_arc(cx - 11, base - 20, 6 + t / 3, 200, 250); }
}

// the keytar player: a figure with a slanted keyboard across it, a note popping off on the beat
static void sp_keytar(int cx, int base, int f, bool hit)
{
    int y = base - 20;
    oled_circle(cx, y, 2, true);
    oled_line(cx, y + 3, cx, y + 12, true);
    oled_line(cx, y + 12, cx - 3, y + 19, true); oled_line(cx, y + 12, cx + 3, y + 19, true);
    int ky = y + 8;                                             // the keyboard, slanted up to the right
    oled_line(cx - 6, ky + 3, cx + 8, ky - 2, true); oled_line(cx - 6, ky + 5, cx + 8, ky, true);
    for (int k = 0; k < 5; k++) oled_pixel(cx - 4 + k * 3, ky + 4 - k, true);
    oled_line(cx, y + 5, cx + 5, ky - 1 + (hit ? 1 : 0), true);   // the playing hand
    oled_line(cx, y + 5, cx - 4, ky + 3, true);
    if (hit) { int t = f % 12; oled_disc(cx + 9 + t / 3, y - 2 - t / 2, 1, true); oled_line(cx + 10 + t / 3, y - 2 - t / 2, cx + 10 + t / 3, y - 6 - t / 2, true); }
}

// the piano: an upright with its keys, two hands coming down on the beat
static void sp_piano(int cx, int base, int f, bool hit)
{
    int x = cx - 9, y = base - 16;
    oled_rframe(x, y, 19, 16, 1);
    oled_rect(x + 1, y + 7, 17, 1, true);
    for (int k = 0; k < 6; k++) oled_pixel(x + 2 + k * 3, y + 9, true);
    oled_rect(x + 3, y + 2, 13, 3, false); oled_frame(x + 3, y + 2, 13, 3);   // the music stand
    int dy = hit ? 0 : -3;
    oled_disc(x + 5, y + 5 + dy, 2, true); oled_disc(x + 13, y + 5 + (hit ? -3 : 0), 2, true);
    if (hit) { ui_arc(cx, y - 2, 6 + (f % 12) / 3, 230, 310); }
}

static bool splash(int f)
{
    const int ground = 60, beat = 12;
    const int at[L_N] = { 2, 8, 14, 20, 26, 32 };              // when each one comes in
    const int cx[L_N] = { 19, 44, 62, 81, 96, 117 };
    oled_hline_pat(0, ground + 1, OLED_W, 0x55, true);
    bool together = f >= 44;
    bool hit = together && (f % beat) < 3;
    for (int l = 0; l < L_N; l++) {
        if (f < at[l]) continue;
        int k = f - at[l];
        int rise = k < 8 ? (int)((1.0f - ease_back((float)k / 8.0f)) * 30.0f) : 0;   // pops up from below the ground
        int base = ground + rise;
        bool mine = together ? hit : (k % beat) < 3;
        switch (l) {
        case L_DRUMS: sp_box(cx[l], base, together ? f : k, mine); break;
        case L_BASS: sp_woofer(cx[l], base, f, mine); break;
        case L_LEAD: sp_keytar(cx[l], base, f, mine); break;
        case L_KEYS: sp_piano(cx[l], base, f, mine); break;
        case L_BREATH: sp_player(cx[l] - 4, base, f); break;
        default: sp_wolf(cx[l], base, f, together || (k / 8) % 2 == 0); break;
        }
    }
    oled_rect(0, ground + 2, OLED_W, OLED_H - ground - 2, false);
    if (f >= 48) {                                              // the title drops into the band, the six under it
        float t = ease_bounce(clampf((float)(f - 48) / 14.0f, 0.0f, 1.0f));
        int y = (int)(-14.0f + 16.0f * t);
        oled_rect(0, 0, OLED_W, 15, false);
        oled_ftext_c(F_MED, 64, y, "BAND", 4);
        if (f >= 60) { oled_rect(0, 16, OLED_W, 7, false); oled_ftext_c(F_TINY, 64, 17, "DRUMS BASS LEAD KEYS BREATH HOWL", 0); }
    }
    return f < 100;
}

// ---------------------------------------------------------------- screensaver: the four, drifting round
static void saver(uint32_t f)
{
    for (int l = 0; l < L_N; l++) {
        float a = (float)f * 0.01f + (float)l * 1.5708f;
        int x = 58 + (int)(40.0f * cosf(a)), y = 28 + (int)(18.0f * sinf(a * 1.3f));
        layers[l]->icon(x, y, false);
    }
}

// ---------------------------------------------------------------- sounds and hints
// Whole-band sounds: the song, the mix, the FX and every layer. Names are the params' tags; enums by index.
//   drums  dkit: 0 STREET 1 808 2 BOOM BAP 3 TECHNO 4 HAND DRUM 5 MARIMBA 6 BELLS 7 GLASS 8 TABLA 9 STEEL PAN 10 SCRAP 11 ZAPS 12 COWBELLS 13 SCRATCH
//          the grid: dgon 0/1; dgbt 0 MINE 1 ROCK 2 FUNK 3 HIP HOP 4 FOUR FLOOR 5 ONE DROP 6 HALF TIME 7 TRAP; dgst 0 1/8 1 1/16 2 1/4;
//          the rows dgr0..dgr3 (low byte: steps on, high byte: accents) must be given with the beat (a sound does not load a beat)
//   bass   bmod: 0 KNOCK 1 HUM 2 FOLLOW; bpat: 0 ROOT 1 ROOT+5 2 OCTAVES 3 WALK 4 ARP 5 ACID; brat: 0 1/4 1 1/8 2 1/16;
//          bsnd: 0 SUB 1 ACID 2 808 3 FINGER 4 REESE 5 FM
//   lead   lmod: 0 HUM 1 TILT 2 ARP; larp: 0 UP 1 DOWN 2 UP-DOWN 3 RANDOM 4 OCTAVES 5 RIFF 1 6 RIFF 2; lrat: 0 1/8 1 1/16 2 1/8T;
//          lsnd: 0 SAW 1 SQUARE 2 SYNC 3 SUPERSAW 4 FM BELL 5 CHIP 6 WHISTLE 7 BRASS
//   keys   kmod: 0 COMP 1 KNOCK; kpat: 0 WHOLE 1 HALF 2 QUARTERS 3 OFFBEAT 4 STABS 5 ARPEGGIO 6 GOSPEL 7 BOSSA;
//          kvoi: 0 CLOSE 1 OPEN 2 SEVENTHS 3 SPREAD; ksnd: 0 PIANO 1 E.PIANO 2 ORGAN 3 PAD 4 CLAV 5 VIBES
//   breath hmdl: 0 OCARINA 1 FLUTE 2 PAN PIPE 3 SHAKUHACHI 4 CLARINET 5 BOTTLE; hhrm: 0 OFF 1 3RD UP 2 5TH UP 3 OCT UP 4 OCT DN 5 DRONE
//   howl   wmod: 0 INNER 1 AIR 2 BOTH; wcol: 0 SOFT 1 WARM 2 HARD 3 FOLD
//   song   skey 0..11 from C; sscl: 0 MAJOR 1 MINOR 2 DORIAN 3 MIXOLYDIAN 4 PHRYGIAN 5 HARM MINOR;
//          sprg: 0 HOLD 1 I V vi IV 2 vi IV I V 3 I vi IV V 4 I IV 5 ii V I 6 i VII VI V 7 i iv 8 12 BAR BLUES; fetm: 0 1/8 1 1/8 DOT 2 1/4 3 1/4 DOT 4 1/2
#define ROCK  "dgbt=1 dgr0=273 dgr1=17476 dgr2=22015 dgr3=0 dgst=0"
#define FUNK  "dgbt=2 dgr0=265 dgr1=4240 dgr2=22015 dgr3=0 dgst=1"
#define HIPHOP "dgbt=3 dgr0=297 dgr1=17476 dgr2=22015 dgr3=0 dgst=0"
#define FLOOR "dgbt=4 dgr0=21845 dgr1=68 dgr2=43690 dgr3=0 dgst=0"
#define DROP  "dgbt=5 dgr0=4112 dgr1=4112 dgr2=43775 dgr3=0 dgst=0"
#define HALFT "dgbt=6 dgr0=265 dgr1=4112 dgr2=22015 dgr3=128 dgst=0"
#define TRAP  "dgbt=7 dgr0=321 dgr1=4112 dgr2=4607 dgr3=0 dgst=1"
static const preset_t presets[] = {
    { "FIRST BAND",   ROCK " dgon=0" },
    { "ROCK BAND",    ROCK " dgon=1 stmp=112 skey=4 sscl=0 sprg=4 dkit=0 bmod=2 bpat=1 brat=1 bsnd=3 kpat=2 ksnd=0 lsnd=7 lmod=2 larp=2 mlvl=55 fech=20 frev=25" },
    { "FUNK BAND",    FUNK " dgon=1 stmp=106 skey=4 sscl=2 sprg=4 dkit=2 dswg=35 dpch=55 bmod=2 bpat=2 brat=2 bkik=1 bfil=40 bsnd=3 bcut=1400 "
                      "kpat=3 ksnd=4 kton=70 lmod=2 larp=5 lrat=1 lsnd=2 mlvl=50 hmdl=4 hsnd=25 wgai=40 mlvw=45 fetm=0 fech=30 frev=20" },
    { "HIP HOP",      HIPHOP " dgon=1 stmp=88 skey=9 sscl=1 sprg=7 dkit=2 dswg=45 bmod=2 bpat=0 brat=1 bsnd=3 bkik=1 kpat=0 kvoi=2 ksnd=1 kmot=45 "
                      "lmod=1 lsnd=6 mlvl=45 fech=25 fetm=1 frev=25" },
    { "DUB",          DROP " dgon=1 stmp=76 skey=7 sscl=1 sprg=7 dkit=0 ddcy=1.4 dsnd=60 bmod=2 bpat=0 brat=0 bsnd=0 bcut=300 bdec=1.2 "
                      "kpat=3 ksnd=2 kmot=60 kfx=50 lmod=1 lsnd=6 lfx=70 hmdl=1 hsnd=70 wlvl=-24 mlvw=50 fech=75 fetm=1 fefb=70 frev=40" },
    { "LULLABY",      "dgon=0 stmp=72 skey=5 sscl=0 sprg=3 dkit=5 dbdy=80 dsnd=40 bmod=2 bpat=0 brat=0 bsnd=0 bdec=1.2 kpat=5 ksnd=0 kped=1 "
                      "lmod=2 larp=0 lrat=0 lsnd=6 mlvl=35 hmdl=0 hvib=25 hsnd=50 wgai=30 wlvl=-24 wswl=4 mlvw=55 frev=60 frsz=3.5 fech=25 fetm=3" },
    { "ACID SET",     FLOOR " dgon=1 stmp=124 skey=9 sscl=1 sprg=0 dkit=3 dgrd=4 dstr=100 bmod=2 bpat=5 brat=2 bsnd=1 bcut=400 bres=80 benv=80 bdrv=40 brif=7 "
                      "kpat=0 ksnd=3 mlvk=45 lmod=2 larp=2 lrat=1 lsnd=0 lres=70 mlvl=50 hmdl=4 mlvh=55 wshf=3 wcol=2 mlvw=40 fech=40 fetm=1 frev=20" },
    { "RITUAL DRONE", "dgon=0 stmp=66 skey=2 sscl=4 sprg=0 dkit=4 dsnd=40 bmod=2 bpat=0 brat=0 bsnd=4 bdec=2 kpat=0 ksnd=3 mlvk=40 "
                      "lmod=1 lsnd=6 mlvl=30 hmdl=3 hhrm=5 hhld=1 hsnd=60 wfrz=1 wbnd=8 wdrf=20 wswl=6 wlvl=-20 frev=70 frsz=5 fech=30 fetm=4" },
    { "BLUES BAR",    HALFT " dgon=1 dswg=50 stmp=96 skey=4 sscl=3 sprg=8 dkit=2 bmod=2 bpat=3 brat=0 bsnd=3 kpat=6 kvoi=2 ksnd=0 "
                      "lmod=1 lsnd=7 hmdl=4 hsnd=30 wcol=1 mlvw=45 fech=20 frev=25" },
    { "STEEL ISLAND", "dgon=0 stmp=112 skey=0 sscl=0 sprg=4 dkit=9 dsnd=30 bmod=2 bpat=2 brat=1 bsnd=3 kpat=3 ksnd=5 lmod=2 larp=4 lsnd=5 mlvl=40 "
                      "hmdl=2 hsnd=40 mlvw=40 fech=30 fetm=1 frev=30" },
    { "TECHNO WOLF",  FLOOR " dgon=1 stmp=128 skey=6 sscl=1 sprg=0 dkit=3 dgrd=4 dstr=90 bmod=2 bpat=0 brat=1 bkik=1 bsnd=2 kpat=0 ksnd=3 mlvk=35 "
                      "lmod=2 larp=4 lrat=2 lsnd=3 mlvl=45 wfrz=1 wcol=3 wshf=-3 wlvl=-20 fech=50 fetm=0 fefb=55 frev=30" },
    { "TRAP",         TRAP " dgon=1 stmp=140 skey=1 sscl=1 sprg=7 dkit=1 bmod=2 bpat=0 brat=0 bsnd=2 bdec=1.6 kpat=0 ksnd=3 mlvk=40 "
                      "lmod=2 larp=5 lrat=2 lsnd=4 mlvl=45 fech=30 fetm=0 frev=30" },
    { "GLASS GARDEN", "dgon=0 stmp=88 skey=11 sscl=0 sprg=2 dkit=7 dsnd=50 bmod=2 bpat=4 brat=1 bsnd=5 kpat=5 ksnd=5 lmod=2 larp=3 lsnd=4 mlvl=40 "
                      "hmdl=5 hsnd=50 wgld=2 wlvl=-22 frev=60 frsz=3 fech=35 fetm=3" },
    { "INDUSTRY",     ROCK " dgon=1 stmp=118 skey=1 sscl=1 sprg=6 dkit=10 ddrv=40 bmod=2 bpat=2 brat=2 bsnd=4 bdrv=60 kpat=4 ksnd=4 kdrv=60 "
                      "lmod=2 larp=6 lsnd=1 ldrv=60 wcol=2 whea=4 mlvw=50 fech=20 frev=20" },
    { "ANDALUSIA",    "dgon=0 stmp=100 skey=9 sscl=5 sprg=6 dkit=4 bmod=2 bpat=3 brat=1 bsnd=3 kpat=7 ksnd=0 lmod=1 lsnd=7 mlvl=45 "
                      "hmdl=1 hhrm=1 hsnd=40 wlvl=-24 mlvw=45 frev=45 fech=25" },
    { "ZAP ARCADE",   "dgon=0 stmp=140 skey=0 sscl=0 sprg=1 dkit=11 dsnd=40 bmod=2 bpat=4 brat=2 bsnd=5 kpat=4 ksnd=4 lmod=2 larp=0 lrat=1 lsnd=5 "
                      "hmdl=5 mlvw=40 fech=50 fetm=0 fefb=45 frev=15" },
};
// LEARN_INST + 0: a knock heard by the drums (layer_drums.c), + 1: a tap on REC muted or unmuted a loop
static const hint_t hints[] = {
    { "HOLD THE KNOB TO DRAW THE DRUMS", LEARN_INST + 2 },
    { "ON BASS, HOLD THE KNOB TO WRITE A LINE", LEARN_INST + 12 },
    { "KNOCK ON THE BOX TO DRUM", LEARN_INST + 0 },
    { "TURN THE KNOB TO PICK A LAYER", LEARN_TURN },
    { "HOLD REC TO LOOP THIS LAYER", LEARN_REC },
    { "LOOPS ARE WHOLE BARS, ON THE BEAT", LEARN_REC },
    { "TAP REC TO MUTE ITS LOOP", LEARN_INST + 1 },
    { "TURN PAST HOWL FOR THE MIXER", LEARN_INST + 10 },
    { "MUTED, HOLD REC FOR A NEW TAKE", LEARN_INST + 11 },
    { "PRESS THE KNOB FOR THE MENU", LEARN_MENU },
};

const instrument_t INST = {
    .name = "BAND", .tagline = "SIX PLAYERS, ONE BOARD",
    .params = table, .nparams = NPARAMS, .groups = groups, .ngroups = G_N,
    .presets = presets, .npresets = (int)(sizeof presets / sizeof presets[0]),
    .hints = hints, .nhints = (int)(sizeof hints / sizeof hints[0]),
    .burst = true, .looper = false, .knob = "slay",
    .init = init, .audio = audio, .tick = tick, .draw = draw, .splash = splash,
    .changed = changed, .action = action, .midi_in = midi_in, .saver = saver, .hold = hold,
};
