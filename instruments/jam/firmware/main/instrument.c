// JAM's host (see jam.h): one instrument made of four layers, DRUMS (KNOCK), BASS, BREATH (HUFF) and HOWL.
// The knob in play picks the live layer: the mic, the tilt, the stick and REC are its; the others play on.
//
//   the song      tempo (TEMPO until the first loop, then from it: whole beats, 75..165 BPM), key and scale,
//                 the chord: a PROGRESSION stepping per bar, or one held chord, or the BASS's own (live)
//   the tracks    BREATH and HOWL are recorded into audio tracks in the song's loop: REC held records (the
//                 first take of the jam sets the loop) or adds, a tap mutes / unmutes the live layer's track;
//                 DRUMS and BASS loop their own events
//   the mixer     a level and a mute per layer; one FX bus fed by the layers' sends: an echo in time with the
//                 song and a small reverb (a four-line feedback delay network)
//   the screen    the header band: the layer bar (the live one lit, a dot per layer: hollow muted, filled
//                 playing, blinking recording), the chord, the beat, the meter; the loop's playhead on row 15;
//                 a slide between layers' scenes when the knob turns
#include "jam.h"
#include "ui.h"
#include "params.h"
#include "front.h"
#include "spsc.h"
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
    H_LAYER, H_TEMPO, H_KEY, H_SCALE, H_PROG, H_CHORD, H_BARS,
    H_LVL_D, H_LVL_B, H_LVL_H, H_LVL_W, H_MUTE_D, H_MUTE_B, H_MUTE_H, H_MUTE_W,
    H_ECHO, H_ETIME, H_EFB, H_REVERB, H_RSIZE, H_RDAMP,
    H_CLR_D, H_CLR_B, H_CLR_H, H_CLR_W, H_CLR_ALL,
    H_N
};
#define HB (L_N * LAYER_PARAMS_MAX)
#define NPARAMS (HB + H_N)
#define H(i) pv[HB + (i)]
enum { G_SONG, G_LAYER0, G_MIX = G_LAYER0 + 2 * L_N, G_FX, G_LOOPS, G_N };

static const layer_t *const layers[L_N] = { &layer_drums, &layer_bass, &layer_breath, &layer_howl };
int layer_base[L_N] = { 0, LAYER_PARAMS_MAX, 2 * LAYER_PARAMS_MAX, 3 * LAYER_PARAMS_MAX };

static const char *const layer_names[] = { "DRUMS", "BASS", "BREATH", "HOWL" };
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

#define SNG (PF_SYS)
static const param_t host_params[H_N] = {
    [H_LAYER]  = { "LAYER", "slay", G_SONG, PK_ENUM, PF_SYS, 0, L_N - 1, 1, 0, "", layer_names, "THE LAYER YOU PLAY" },
    [H_TEMPO]  = { "TEMPO", "stmp", G_SONG, PK_NUM, PF_INT, 60, 180, 1, 100, "bpm", NULL, "TILL THE FIRST LOOP SETS IT" },
    [H_KEY]    = { "KEY", "skey", G_SONG, PK_ENUM, 0, 0, 11, 1, 0, "", key_names, "THE KEY OF EVERY LAYER" },
    [H_SCALE]  = { "SCALE", "sscl", G_SONG, PK_ENUM, 0, 0, NSCALES - 1, 1, 0, "", scale_names, "THE NOTES EVERY LAYER USES" },
    [H_PROG]   = { "CHORDS", "sprg", G_SONG, PK_ENUM, 0, 0, NPROGS - 1, 1, 1, "", prog_names, "HOW THE CHORDS MOVE BY BAR" },
    [H_CHORD]  = { "HOLD CHORD", "schd", G_SONG, PK_ENUM, 0, 0, 6, 1, 0, "", chord_names, "THE CHORD FOR CHORDS: HOLD" },
    [H_BARS]   = { "CHORD BARS", "sbar", G_SONG, PK_ENUM, 0, 0, 2, 1, 0, "", bars_names, "HOW LONG EACH CHORD LASTS" },
    [H_LVL_D]  = { "DRUMS", "mlvd", G_MIX, PK_NUM, PF_INT, 0, 100, 5, 80, "%", NULL, "DRUMS LEVEL" },
    [H_LVL_B]  = { "BASS", "mlvb", G_MIX, PK_NUM, PF_INT, 0, 100, 5, 80, "%", NULL, "BASS LEVEL" },
    [H_LVL_H]  = { "BREATH", "mlvh", G_MIX, PK_NUM, PF_INT, 0, 100, 5, 80, "%", NULL, "BREATH LEVEL" },
    [H_LVL_W]  = { "HOWL", "mlvw", G_MIX, PK_NUM, PF_INT, 0, 100, 5, 80, "%", NULL, "HOWL LEVEL" },
    [H_MUTE_D] = { "MUTE DRUMS", "mmud", G_MIX, PK_ENUM, 0, 0, 1, 1, 0, "", mute_names, "SILENCE THE DRUMS" },
    [H_MUTE_B] = { "MUTE BASS", "mmub", G_MIX, PK_ENUM, 0, 0, 1, 1, 0, "", mute_names, "SILENCE THE BASS" },
    [H_MUTE_H] = { "MUTE BREATH", "mmuh", G_MIX, PK_ENUM, 0, 0, 1, 1, 0, "", mute_names, "SILENCE THE BREATH" },
    [H_MUTE_W] = { "MUTE HOWL", "mmuw", G_MIX, PK_ENUM, 0, 0, 1, 1, 0, "", mute_names, "SILENCE THE HOWL" },
    [H_ECHO]   = { "ECHO", "fech", G_FX, PK_NUM, PF_INT, 0, 100, 5, 35, "%", NULL, "HOW LOUD THE ECHO IS" },
    [H_ETIME]  = { "ECHO TIME", "fetm", G_FX, PK_ENUM, 0, 0, 4, 1, 1, "", etime_names, "IN TIME WITH THE SONG" },
    [H_EFB]    = { "REPEATS", "fefb", G_FX, PK_NUM, PF_INT, 0, 90, 5, 40, "%", NULL, "HOW LONG THE ECHO GOES ON" },
    [H_REVERB] = { "REVERB", "frev", G_FX, PK_NUM, PF_INT, 0, 100, 5, 35, "%", NULL, "HOW LOUD THE REVERB IS" },
    [H_RSIZE]  = { "REVERB TIME", "frsz", G_FX, PK_NUM, PF_LOG, 0.3f, 6.0f, 1.12f, 1.6f, "s", NULL, "HOW LONG THE ROOM RINGS" },
    [H_RDAMP]  = { "REVERB TONE", "frdm", G_FX, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "DARK TO BRIGHT" },
    [H_CLR_D]  = { "CLEAR DRUMS", "lcld", G_LOOPS, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "ERASE THE DRUMS' LOOP" },
    [H_CLR_B]  = { "CLEAR BASS", "lclb", G_LOOPS, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "ERASE THE BASS'S LOOP" },
    [H_CLR_H]  = { "CLEAR BREATH", "lclh", G_LOOPS, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "ERASE THE BREATH TRACK" },
    [H_CLR_W]  = { "CLEAR HOWL", "lclw", G_LOOPS, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "ERASE THE HOWL TRACK" },
    [H_CLR_ALL]= { "CLEAR ALL", "lcla", G_LOOPS, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "EVERY LOOP; A NEW TEMPO" },
};
static param_t table[NPARAMS];
static char placeholder_tag[L_N * LAYER_PARAMS_MAX][5];
static const char *groups[G_N];

__attribute__((constructor)) static void build_table(void)
{
    groups[G_SONG] = "SONG"; groups[G_MIX] = "MIX"; groups[G_FX] = "FX"; groups[G_LOOPS] = "LOOPS";
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
song_t song = { .bpm = 100, .scale = scales[0], .scale_n = 7, .chord = { 36, 40, 43 }, .chord_n = 3 };
volatile float host_out_env;
static float last_out[AUDIO_BLOCK];
const float *host_last_out(void) { return last_out; }
static volatile int chord_ovr = -1;
static int last_src = -1;

int host_live(void) { return (int)H(H_LAYER); }
void host_chord_override(int degree) { chord_ovr = degree < 0 ? -1 : degree % 7; }

uint32_t host_first_loop(uint32_t start, uint32_t held)
{
    if (song.loop_len) return song.loop_len;
    uint32_t max = (uint32_t)(JAM_LOOP_MAX_S * AUDIO_SR);
    if (held > max) held = max;
    if (held < AUDIO_SR / 4) held = AUDIO_SR / 4;
    // the whole number of beats nearest the tempo so far, then halved or doubled into 75..165 BPM
    float fpb = song_frames_per_beat();
    int beats = (int)((float)held / fpb + 0.5f);
    if (beats < 1) beats = 1;
    float bpm = (float)beats * 60.0f * (float)AUDIO_SR / (float)held;
    while (bpm > 165.0f && beats > 1) { beats = beats & 1 ? beats - 1 : beats / 2; bpm = (float)beats * 60.0f * (float)AUDIO_SR / (float)held; }
    while (bpm < 75.0f) { beats *= 2; bpm *= 2.0f; }
    song.bpm = bpm; song.beats = beats; song.loop_start = start;
    __atomic_store_n(&song.loop_len, held, __ATOMIC_RELEASE);
    app_log("jam: the loop is %.2f s, %d beats at %.1f BPM", (double)((float)held / AUDIO_SR), beats, (double)bpm);
    return held;
}

static void set_song(uint32_t frame)
{
    int s = (int)H(H_SCALE);
    song.key = (int)H(H_KEY); song.scale = scales[s]; song.scale_n = 7;
    if (!song.loop_len) song.bpm = H(H_TEMPO);
    float beat = song_beat(frame);
    static const float bar_beats[] = { 4.0f, 8.0f, 2.0f };
    song.bar = (uint32_t)(beat / bar_beats[(int)H(H_BARS)]);
    const prog_t *p = &progs[(int)H(H_PROG)];
    // the chord: written when its source moves (a new bar, a new override, a new setting), so a direct write to
    // song.degree holds until then
    int src = chord_ovr >= 0 ? chord_ovr : (int)H(H_PROG) == 0 ? (int)H(H_CHORD) : p->d[song.bar % (uint32_t)p->n];
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
    static const int off[L_N] = { 0, 1, 0, 2 };
    int base = (int)pv[CP(CP_MIDI_CH)];
    midi_note_ch(layer == L_DRUMS ? 10 : ((base - 1 + off[layer]) % 16) + 1, note, vel);
}
void host_midi_cc(int layer, int cc, int val)
{
    static const int off[L_N] = { 0, 1, 0, 2 };
    int base = (int)pv[CP(CP_MIDI_CH)];
    int ch = layer == L_DRUMS ? 10 : ((base - 1 + off[layer]) % 16) + 1;
    if (cc < 0 || cc > 119) return;
    plat_midi3((uint8_t)(0xB0 | ((ch - 1) & 15)), (uint8_t)cc, (uint8_t)(val < 0 ? 0 : val > 127 ? 127 : val));
}

// ---------------------------------------------------------------- the audio tracks (BREATH, HOWL)
#define TRACK_N   (JAM_LOOP_MAX_S * AUDIO_SR)
#define XF        192                      // 6 ms: every ramp and the seam
#define RING      16384u                   // 0.5 s of the live layer, so a hold loses nothing to the tap test
#define RMASK     (RING - 1u)
#define HOLD_MS   300
enum { T_PRESS, T_CONFIRM, T_RELEASE, T_TAP, T_CLEAR, T_CLEAR_ALL };
typedef struct { uint8_t cmd, layer; } tcmd_t;
SPSC(tcmdq, tcmd_t, 16)
static tcmdq_t tq;
typedef struct {
    int16_t *buf;
    volatile int has, muted, rec;          // rec: 0 no, 1 the jam's first take, 2 adding
    float g, dub_g, dub_t, send_ratio, acc_dry, acc_send;
    int wpos, tail, wipe;                  // first take: written so far; the tail after it; wiping: samples left to clear
} track_t;
static track_t trk[L_N];                   // only BREATH and HOWL have buffers
static int16_t *ring;
static uint32_t press_frame;
static bool pend;
static int pend_layer;

static inline int16_t s16(float x) { x *= 32767.0f; return (int16_t)(x > 32767.0f ? 32767.0f : x < -32767.0f ? -32767.0f : x); }
static inline float f32(int16_t v) { return (float)v * (1.0f / 32767.0f); }
static bool has_track(int l) { return trk[l].buf != NULL; }

static void track_cmd(int cmd, int layer) { tcmd_t c = { (uint8_t)cmd, (uint8_t)layer }; tcmdq_push(&tq, &c); }

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

// ---------------------------------------------------------------- the instrument
static float dry[L_N][AUDIO_BLOCK], send[L_N][AUDIO_BLOCK];
static volatile float layer_peak[L_N];

static void init(void)
{
    for (int l = 0; l < L_N; l++) layers[l]->init();
    trk[L_BREATH].buf = app_alloc(TRACK_N * sizeof(int16_t));
    trk[L_HOWL].buf = app_alloc(TRACK_N * sizeof(int16_t));
    ring = app_alloc(RING * sizeof(int16_t));
    echo_buf = app_alloc(ECHO_N * sizeof(int16_t));
    fdn_buf = app_alloc(FDN * FDN_N * sizeof(float));
    if (!trk[L_BREATH].buf || !trk[L_HOWL].buf || !ring || !echo_buf || !fdn_buf) app_log("jam: short of memory for the tracks or the FX");
    for (int l = 0; l < L_N; l++) { smooth_init(&lvl_s[l], 0.8f, 0.02f); smooth_init(&mute_s[l], 1.0f, 0.006f); }
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
        if (!len) {                                          // the jam's first take: the loop starts at the press
            for (uint32_t j = 0; j < k; j++) t->buf[j] = ring[(press_frame + j) & RMASK];
            t->wpos = (int)k; t->rec = 1; t->tail = XF; t->has = 0;
        } else {                                             // adding: what was played since the press, where it was played
            for (uint32_t j = 0; j < k; j++) {
                uint32_t at = (press_frame + j - song.loop_start) % len;
                float r = j < XF ? (float)j / (float)XF : 1.0f;
                t->buf[at] = s16((t->has ? f32(t->buf[at]) : 0.0f) + r * f32(ring[(press_frame + j) & RMASK]));
            }
            if (!t->has) { t->has = 1; t->g = 0; }           // a first take over a loop that exists: the rest of it is silence (wiped)
            t->rec = 2; t->dub_g = t->dub_t = 1;
        }
        t->acc_dry = t->acc_send = 0;
        break;
    }
    case T_RELEASE:
        if (!t) break;
        if (t->rec == 1) {                                   // song.loop_len is set by now (host_first_loop)
            t->rec = 0;
            if (!len) { t->wpos = 0; break; }
            // what was written past the loop's end already is the start of the tail, blended into the start
            t->tail = t->wpos > (int)len ? (t->wpos - (int)len > XF ? XF : t->wpos - (int)len) : 0;
            t->has = 1; t->g = 0;
            if (t->tail == XF && len + XF <= TRACK_N)
                for (int j = 0; j < XF; j++) { float r = (float)j / (float)XF; t->buf[j] = s16(f32(t->buf[j]) * r + f32(t->buf[len + (uint32_t)j]) * (1.0f - r)); }
        } else if (t->rec == 2) t->dub_t = 0;
        break;
    case T_TAP: pend = false; if (t && t->has) t->muted = !t->muted; break;
    case T_CLEAR: if (t) { t->rec = 0; t->has = 0; t->muted = 0; t->wipe = TRACK_N; } break;
    case T_CLEAR_ALL:
        for (int l = 0; l < L_N; l++) if (has_track(l)) { trk[l].rec = 0; trk[l].has = 0; trk[l].muted = 0; trk[l].wipe = TRACK_N; }
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
        for (int i = 0; i < n; i++) {
            float d = dry[l][i], s = send[l][i];
            if (t) {
                if (l == live && ring) ring[(f0 + (uint32_t)i) & RMASK] = s16(d);
                if (t->rec == 1) {
                    if (t->wpos < TRACK_N) t->buf[t->wpos++] = s16(d);
                } else if (len && t->has) {
                    uint32_t pos = (f0 + (uint32_t)i - song.loop_start) % len;
                    if (t->tail < XF) {                                          // the first take's tail, then the seam
                        if (len + XF <= TRACK_N) t->buf[len + (uint32_t)t->tail] = s16(d);
                        if (++t->tail == XF && len + XF <= TRACK_N)
                            for (int j = 0; j < XF; j++) { float r = (float)j / (float)XF; t->buf[j] = s16(f32(t->buf[j]) * r + f32(t->buf[len + (uint32_t)j]) * (1.0f - r)); }
                    }
                    float y = f32(t->buf[pos]);
                    if (t->rec == 2 || t->dub_g > 0.0f) t->buf[pos] = s16(y + d * t->dub_g);
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
            float g = smooth_to(&lvl_s[l], H(H_LVL_D + l) * 0.01f) * smooth_to(&mute_s[l], H(H_MUTE_D + l) >= 1.0f ? 0.0f : 1.0f);
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
        coach_done(LEARN_REC);
    }
    bool too_long = rec && confirmed && !song.loop_len && c->frames - first_press >= (uint32_t)(JAM_LOOP_MAX_S * AUDIO_SR) - 64;
    if ((release || too_long) && confirmed) {
        if (!song.loop_len) host_first_loop(first_press, c->frames - first_press);
        track_cmd(T_RELEASE, rec_layer);
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
    if (c->ticks % 1000 == 0 && prof_blocks) {               // every 10 s: the cost of each part, in us per block
        uint32_t b = prof_blocks;
        app_log("cpu us/block: drums %lu bass %lu breath %lu howl %lu mix %lu fx %lu (of 2000)",
                (unsigned long)(prof_cyc[0] / b / 240), (unsigned long)(prof_cyc[1] / b / 240), (unsigned long)(prof_cyc[2] / b / 240),
                (unsigned long)(prof_cyc[3] / b / 240), (unsigned long)(prof_cyc[4] / b / 240), (unsigned long)(prof_cyc[5] / b / 240));
        for (int i = 0; i < L_N + 2; i++) prof_cyc[i] = 0;
        prof_blocks = 0;
    }
    // the knob in play always picks the layer: editing a setting in the menu does not take it over (the core
    // would make it turn the last edited setting)
    if (meta.quick[0] && memcmp(meta.quick, "slay", 4)) memset(meta.quick, 0, sizeof meta.quick);
    set_song(c->frames);
    int live = (int)H(H_LAYER);
    if (live != prev_live) {                                   // the knob moved: slide, and close a take on the old layer
        slide_from = prev_live; slide_dir = live > prev_live ? 1 : -1; slide_t = 0;
        if (rec_was && !layers[prev_live]->events) { ctl_t r = *c; track_rec(&r, false, prev_live); }
        prev_live = live;
    }
    ctl_t idle = *c;
    idle.rec = idle.rec_press = idle.rec_release = false; idle.rec_ms = 0;
    idle.has_imu = false; idle.has_joy = false; idle.jx = idle.jy = 0; idle.nburst = 0;
    idle.lean_x = idle.lean_y = 0; idle.gyro = (vec3_t){ 0, 0, 0 }; idle.gyro_mag = 0;
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
    // the beat, for the header
    uint32_t b = (uint32_t)song_beat(c->frames);
    if (b != last_beat) { last_beat = b; beat_flash = (b % 4 == 0) ? 3 : 2; }
    else if (beat_flash > 0 && c->ticks % 4 == 0) beat_flash--;
    // the LED: the live layer's colour, pulsing with the beat; red while recording
    static const uint8_t col[L_N][3] = { { 255, 60, 20 }, { 160, 0, 255 }, { 0, 200, 255 }, { 120, 140, 255 } };
    float lv = clampf(layer_peak[live] * 2.0f, 0.0f, 1.0f) * 0.7f + (beat_flash ? 0.3f : 0.08f);
    if (layer_state(live) == LOOP_REC) led_rgb((uint8_t)(255.0f * (beat_flash ? 1.0f : 0.4f)), 0, 0);
    else led_rgb((uint8_t)(col[live][0] * lv), (uint8_t)(col[live][1] * lv), (uint8_t)(col[live][2] * lv));
}

static void changed(int p)
{
    if (p < 0) { for (int l = 0; l < L_N; l++) layers[l]->changed(-1); return; }
    if (p < HB) { int l = p / LAYER_PARAMS_MAX, i = p % LAYER_PARAMS_MAX; if (i < layers[l]->nparams) layers[l]->changed(i); }
}

static void action(int p)
{
    if (p < HB) { int l = p / LAYER_PARAMS_MAX; if (layers[l]->action) layers[l]->action(p % LAYER_PARAMS_MAX); return; }
    switch (p - HB) {
    case H_CLR_D: case H_CLR_B: case H_CLR_H: case H_CLR_W: {
        int l = p - HB - H_CLR_D;
        clear_layer(l);
        char b[24]; snprintf(b, sizeof b, "%s CLEARED", layer_names[l]); toast(b, 1000);
        break;
    }
    case H_CLR_ALL: clear_all(); break;
    }
}

static void midi_in(uint8_t st, uint8_t d1, uint8_t d2)
{
    const layer_t *L = layers[(int)H(H_LAYER)];
    if (L->midi_in) L->midi_in(st, d1, d2);
}

// ---------------------------------------------------------------- drawing: the header band and the slide
static void chord_name(char *b, int n)
{
    // the chord's name from its three notes: major, minor, diminished or augmented
    int r = song.chord[0] % 12, t3 = (song.chord[1] - song.chord[0] + 120) % 12, t5 = (song.chord[2] - song.chord[0] + 120) % 12;
    const char *q = t3 == 4 && t5 == 7 ? "" : t3 == 3 && t5 == 7 ? "m" : t3 == 3 && t5 == 6 ? "o" : t3 == 4 && t5 == 8 ? "+" : "";
    snprintf(b, (size_t)n, "%s%s", key_names[r], q);
}

static void header(const view_t *v)
{
    int live = (int)H(H_LAYER);
    for (int l = 0; l < L_N; l++) {                              // the layer bar
        int x = 1 + l * 18;
        layers[l]->icon(x, 3, l == live);
        if (l == live) oled_invert(x - 1, 2, 14, 11);
        int st = layer_state(l), dx = x + 14, dy = 6;               // its loop: playing, muted, recording
        if (st == LOOP_ON) oled_rect(dx, dy, 3, 3, true);
        else if (st == LOOP_OFF) oled_frame(dx, dy, 3, 3);
        else if (st == LOOP_REC && (v->frame / 4) % 2 == 0) oled_rect(dx, dy - 2, 3, 7, true);
    }
    char b[8];
    chord_name(b, sizeof b);
    oled_ftext_ex(F_SMALL, 74, 5, b, 1);
    // the bar's four beats, the one now filled (bigger on the downbeat)
    int beat = (int)song_beat(audio_frames()) % 4;
    for (int k = 0; k < 4; k++) {
        int bx2 = 88 + k * 4;
        if (k == beat) { bool big = beat_flash > 1; oled_rect(bx2 - (big ? 1 : 0), big ? 5 : 6, big ? 3 : 2, big ? 4 : 3, true); }
        else oled_pixel(bx2, 8, true);
    }
    ui_meter(OLED_W - 22, 3, 22, 9, v->out_level, false);
    // the loop's playhead along row 15 (beats as dots); while the first take runs, a bar that grows
    uint32_t len = song.loop_len;
    if (len) {
        for (int k = 0; k < song.beats; k++) oled_pixel(k * (OLED_W - 1) / song.beats, 15, true);
        int px = (int)((float)((audio_frames() - song.loop_start) % len) / (float)len * (float)(OLED_W - 1));
        oled_rect(px - 1, 14, 3, 2, true);
    } else {
        int rl = -1;
        for (int l = 0; l < L_N; l++) if (layer_state(l) == LOOP_REC) rl = l;
        if (rl >= 0) oled_rect(0, 15, (int)fminf((float)OLED_W, (float)rec_ms * 0.001f / (float)JAM_LOOP_MAX_S * (float)OLED_W), 1, true);
    }
}

#define SCENE_Y 16
#define SCENE_H (OLED_H - SCENE_Y)
static uint8_t snap_a[SCENE_H][OLED_W / 8];

static void grab(uint8_t buf[][OLED_W / 8])
{
    for (int y = 0; y < SCENE_H; y++) for (int xb = 0; xb < OLED_W / 8; xb++) {
        uint8_t v = 0;
        for (int k = 0; k < 8; k++) if (oled_get(xb * 8 + k, y + SCENE_Y)) v |= (uint8_t)(1 << k);
        buf[y][xb] = v;
    }
}

static void draw(const view_t *v)
{
    int live = (int)H(H_LAYER);
    if (slide_t < 1.0f && slide_from >= 0) {
        // the old scene and the new, side by side, sliding
        layers[slide_from]->draw(v);
        grab(snap_a);
        oled_rect(0, SCENE_Y, OLED_W, SCENE_H, false);
        layers[live]->draw(v);
        float e = ease_out(slide_t);
        int off = (int)((1.0f - e) * (float)OLED_W) * slide_dir;    // the new scene comes in from that side
        static uint8_t snap_b[SCENE_H][OLED_W / 8];
        grab(snap_b);
        oled_rect(0, SCENE_Y, OLED_W, SCENE_H, false);
        for (int y = 0; y < SCENE_H; y++) for (int x = 0; x < OLED_W; x++) {
            int xn = x - off, xo = x - off + OLED_W * slide_dir;
            bool on = (xn >= 0 && xn < OLED_W && (snap_b[y][xn >> 3] >> (xn & 7) & 1)) || (xo >= 0 && xo < OLED_W && (snap_a[y][xo >> 3] >> (xo & 7) & 1));
            if (on) oled_pixel(x, y + SCENE_Y, true);
        }
        oled_line(off > 0 ? off - 1 : OLED_W + off, SCENE_Y, off > 0 ? off - 1 : OLED_W + off, OLED_H - 1, true);
        slide_t += 0.2f;
    } else layers[live]->draw(v);
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

static bool splash(int f)
{
    const int ground = 60, beat = 12;
    const int at[L_N] = { 2, 12, 22, 32 };                      // when each one comes in
    const int cx[L_N] = { 22, 52, 80, 108 };
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
        case L_BREATH: sp_player(cx[l] - 6, base, f); break;
        default: sp_wolf(cx[l], base, f, together || (k / 8) % 2 == 0); break;
        }
    }
    oled_rect(0, ground + 2, OLED_W, OLED_H - ground - 2, false);
    if (f >= 48) {                                              // the title drops into the band, the four names under it
        float t = ease_bounce(clampf((float)(f - 48) / 14.0f, 0.0f, 1.0f));
        int y = (int)(-14.0f + 16.0f * t);
        oled_rect(0, 0, OLED_W, 15, false);
        oled_ftext_c(F_MED, 64, y, "JAM", 4);
        if (f >= 60) oled_ftext_c(F_TINY, 64, 17, "DRUMS  BASS  BREATH  HOWL", 1);
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
// Whole-jam sounds: the song, the mix, the FX and every layer. Names are the params' tags; enums by index.
//   drums  dkit: 0 STREET 1 808 2 BOOM BAP 3 TECHNO 4 HAND DRUM 5 MARIMBA 6 BELLS 7 GLASS 8 TABLA 9 STEEL PAN 10 SCRAP 11 ZAPS 12 COWBELLS 13 SCRATCH
//   bass   bmod: 0 KNOCK 1 HUM 2 FOLLOW; bpat: 0 ROOT 1 ROOT+5 2 OCTAVES 3 WALK 4 ARP 5 ACID; brat: 0 1/4 1 1/8 2 1/16;
//          bsnd: 0 SUB 1 ACID 2 808 3 FINGER 4 REESE 5 FM
//   breath hmdl: 0 OCARINA 1 FLUTE 2 PAN PIPE 3 SHAKUHACHI 4 CLARINET 5 BOTTLE; hhrm: 0 OFF 1 3RD UP 2 5TH UP 3 OCT UP 4 OCT DN 5 DRONE
//   howl   wmod: 0 INNER 1 AIR 2 BOTH; wcol: 0 SOFT 1 WARM 2 HARD 3 FOLD
//   song   skey 0..11 from C; sscl: 0 MAJOR 1 MINOR 2 DORIAN 3 MIXOLYDIAN 4 PHRYGIAN 5 HARM MINOR;
//          sprg: 0 HOLD 1 I V vi IV 2 vi IV I V 3 I vi IV V 4 I IV 5 ii V I 6 i VII VI V 7 i iv 8 12 BAR BLUES; fetm: 0 1/8 1 1/8 DOT 2 1/4 3 1/4 DOT 4 1/2
static const preset_t presets[] = {
    { "FIRST JAM",    "" },
    { "FUNK BAND",    "stmp=106 skey=4 sscl=2 sprg=4 dkit=2 dswg=35 dpch=55 bmod=2 bpat=2 brat=2 bkik=1 bfil=40 bsnd=3 bcut=1400 "
                      "hmdl=4 hsnd=25 wgai=40 mlvw=45 fetm=0 fech=30 frev=20" },
    { "DUB",          "stmp=76 skey=7 sscl=1 sprg=7 dkit=0 ddcy=1.4 dsnd=60 bmod=2 bpat=0 brat=0 bsnd=0 bcut=300 bdec=1.2 "
                      "hmdl=1 hsnd=70 wlvl=-24 mlvw=50 fech=75 fetm=1 fefb=70 frev=40" },
    { "LULLABY",      "stmp=72 skey=5 sscl=0 sprg=3 dkit=5 dbdy=80 dsnd=40 bmod=2 bpat=0 brat=0 bsnd=0 bdec=1.2 "
                      "hmdl=0 hvib=25 hsnd=50 wgai=30 wlvl=-24 wswl=4 mlvw=55 frev=60 frsz=3.5 fech=25 fetm=3" },
    { "ACID SET",     "stmp=124 skey=9 sscl=1 sprg=0 dkit=3 dgrd=4 dstr=100 bmod=2 bpat=5 brat=2 bsnd=1 bcut=400 bres=80 benv=80 bdrv=40 brif=7 "
                      "hmdl=4 mlvh=55 wshf=3 wcol=2 mlvw=40 fech=40 fetm=1 frev=20" },
    { "RITUAL DRONE", "stmp=66 skey=2 sscl=4 sprg=0 dkit=4 dsnd=40 bmod=2 bpat=0 brat=0 bsnd=4 bdec=2 "
                      "hmdl=3 hhrm=5 hhld=1 hsnd=60 wfrz=1 wbnd=8 wdrf=20 wswl=6 wlvl=-20 frev=70 frsz=5 fech=30 fetm=4" },
    { "BLUES BAR",    "stmp=96 skey=4 sscl=3 sprg=8 dkit=2 dswg=50 bmod=2 bpat=3 brat=0 bsnd=3 hmdl=4 hsnd=30 wcol=1 mlvw=45 fech=20 frev=25" },
    { "STEEL ISLAND", "stmp=112 skey=0 sscl=0 sprg=4 dkit=9 dsnd=30 bmod=2 bpat=2 brat=1 bsnd=3 hmdl=2 hsnd=40 mlvw=40 fech=30 fetm=1 frev=30" },
    { "TECHNO WOLF",  "stmp=128 skey=6 sscl=1 sprg=0 dkit=3 dgrd=4 dstr=90 bmod=2 bpat=0 brat=1 bkik=1 bsnd=2 wfrz=1 wcol=3 wshf=-3 wlvl=-20 fech=50 fetm=0 fefb=55 frev=30" },
    { "GLASS GARDEN", "stmp=88 skey=11 sscl=0 sprg=2 dkit=7 dsnd=50 bmod=2 bpat=4 brat=1 bsnd=5 hmdl=5 hsnd=50 wgld=2 wlvl=-22 frev=60 frsz=3 fech=35 fetm=3" },
    { "INDUSTRY",     "stmp=118 skey=1 sscl=1 sprg=6 dkit=10 ddrv=40 bmod=2 bpat=2 brat=2 bsnd=4 bdrv=60 wcol=2 whea=4 mlvw=50 fech=20 frev=20" },
    { "ANDALUSIA",    "stmp=100 skey=9 sscl=5 sprg=6 dkit=4 bmod=2 bpat=3 brat=1 bsnd=3 hmdl=1 hhrm=1 hsnd=40 wlvl=-24 mlvw=45 frev=45 fech=25" },
    { "ZAP ARCADE",   "stmp=140 skey=0 sscl=0 sprg=1 dkit=11 dsnd=40 bmod=2 bpat=4 brat=2 bsnd=5 hmdl=5 mlvw=40 fech=50 fetm=0 fefb=45 frev=15" },
};
// LEARN_INST + 0: a knock heard by the drums (layer_drums.c), + 1: a tap on REC muted or unmuted a loop
static const hint_t hints[] = {
    { "KNOCK ON THE BOX TO DRUM", LEARN_INST + 0 },
    { "TURN THE KNOB TO PICK A LAYER", LEARN_TURN },
    { "HOLD REC TO LOOP THIS LAYER", LEARN_REC },
    { "THE FIRST LOOP SETS THE TEMPO", LEARN_REC },
    { "TAP REC TO MUTE ITS LOOP", LEARN_INST + 1 },
    { "PRESS THE KNOB FOR THE MENU", LEARN_MENU },
};

const instrument_t INST = {
    .name = "JAM", .tagline = "DRUMS, BASS, BREATH, HOWL",
    .params = table, .nparams = NPARAMS, .groups = groups, .ngroups = G_N,
    .presets = presets, .npresets = (int)(sizeof presets / sizeof presets[0]),
    .hints = hints, .nhints = (int)(sizeof hints / sizeof hints[0]),
    .burst = true, .looper = false, .knob = "slay",
    .init = init, .audio = audio, .tick = tick, .draw = draw, .splash = splash,
    .changed = changed, .action = action, .midi_in = midi_in, .saver = saver,
};
