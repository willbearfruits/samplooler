// WRECK's host (wreck.h): the param table, the sounds, the song and the link, the axis, the hands, REC, the mix.
//
//   the knob      turn: the AXIS, from BREAKCORE to DRONE (it is always the knob's: editing a setting in the menu
//                 does not take it); press: the menu; hold: BEAT's step grid
//   REC           hold: sample a break (SAMPLE FROM: the mic, the USB sound card, or WRECK itself); tap: stop the
//                 machine (the tape brakes to a halt) or start it again (it spins up). SYSTEM > REC BUTTON starts
//                 OFF here (a board has its REC pin stuck low): BOOT is REC until it is turned on
//   the tilt      forward: chaos (more edits); back: the tape drags slower, flat again it catches up
//   the roll      left: reverse and smear; right: the stutter
//   the stick     up: a rising roll; down: the dive (an octave down); left: crush; right: drive
//   a knock       a fill (BEAT), a chord hit (AMP, towards DRONE)
//
// The axis's zones set the FEEL: the machine runs at the song's tempo x2, x1, x1/2, x1/4 (TEMPO STEPS), always on
// the flock's beat; a change waits for the song's next beat, with the tape braking over it (CHANGE: TAPE STOP) or a
// clean cut (CUT). The song (tempo, key, scale, chords) is SING's and BAND's, shared both ways on the link.
#include "wreck.h"
#include "ui.h"
#include "params.h"
#include "front.h"
#include "link.h"
#include "usb.h"
#include <stdio.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_cpu.h"
#include "driver/gpio.h"
#include "pins.h"
#define CYCLES() esp_cpu_get_cycle_count()
#else
#include <time.h>
static inline uint32_t CYCLES(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint32_t)((uint64_t)t.tv_sec * 250000000ULL + (uint64_t)t.tv_nsec / 4); }   // wraps consistently
#endif
#ifndef PIN_REC
#define PIN_REC 3
#endif
#ifndef PIN_BOOT
#define PIN_BOOT 0
#endif
// What each part of the audio costs, CPU cycles summed over blocks (the log shows it): KIT, BEAT, AMP, FX, the mix.
// A block is 2 ms: 480000 cycles at 240 MHz (on the host the unit is 4 ns: the same arithmetic gives us x 1.04).
#define PROF_N (M_N + 1)
static volatile uint32_t prof_cyc[PROF_N], prof_blocks;
void wr_prof(float *us, int n)
{
    uint32_t b = prof_blocks ? prof_blocks : 1;
    for (int i = 0; i < n && i < PROF_N; i++) us[i] = (float)prof_cyc[i] / (float)b / 240.0f;
    for (int i = 0; i < PROF_N; i++) prof_cyc[i] = 0;
    prof_blocks = 0;
}

// ---------------------------------------------------------------- the param table
// Each module has MOD_PARAMS_MAX slots (unused ones are hidden placeholders), then the host's own.
enum {
    H_AXIS, H_STEPS, H_CHANGE, H_KNOCK, H_KSENSE,
    H_SOURCE, H_USBMON,
    H_DRUMS, H_AMP,
    H_TEMPO, H_KEY, H_SCALE, H_PROG, H_HOLD, H_BARS,
    H_LINK, H_GROUP, H_RANGE, H_PEERS,
    H_N
};
#define HB (M_N * MOD_PARAMS_MAX)
#define NPARAMS (HB + H_N)
#define H(i) pv[HB + (i)]
// the menu's pages: WRECK, then BEAT, AMP, FX, KIT (two each), SAMPLE, MIX, SONG, LINK
enum { G_WRECK, G_MOD0, G_SAMPLE = G_MOD0 + 2 * M_N, G_MIX, G_SONG, G_LINK, G_N };
static const module_t *const mods[M_N] = { &mod_kit, &mod_beat, &mod_amp, &mod_fx };
static const int mod_page_order[M_N] = { 3, 0, 1, 2 };            // KIT's pages come after BEAT, AMP and FX
int mod_base[M_N] = { 0, MOD_PARAMS_MAX, 2 * MOD_PARAMS_MAX, 3 * MOD_PARAMS_MAX };

static const char *const zone_names[Z_N] = { "BREAKCORE", "HARDCORE", "SLUDGE", "DRONE" };
const char *host_zone_name(int z) { return zone_names[z < 0 ? 0 : z >= Z_N ? Z_N - 1 : z]; }
static const char *const steps_l[] = { "x2-x1/4", "x1-x1/4", "x2-x1/2", "STAY x1" };
static const int8_t steps_q[4][Z_N] = { { 1, 0, -1, -2 }, { 0, 0, -1, -2 }, { 1, 0, -1, -1 }, { 0, 0, 0, 0 } };
static const char *const change_l[] = { "TAPE STOP", "CUT" };
static const char *const source_l[] = { "MIC", "USB", "RESAMPLE" };
static const char *const off_on[] = { "OFF", "ON" };
static const char *const key_l[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const char *const scale_l[] = { "MAJOR", "MINOR", "DORIAN", "MIXOLYDIAN", "PHRYGIAN", "HARM MINOR" };
static const int8_t scales[][7] = { { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 9, 10 },
                                    { 0, 2, 4, 5, 7, 9, 10 }, { 0, 1, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 8, 11 } };
#define NSCALES 6
// progressions: scale degrees, one per bar (0 = I); the same list as BAND's and SING's, so the link's numbers agree
typedef struct { int n; int8_t d[12]; } prog_t;
static const prog_t progs[] = {
    { 1, { 0 } }, { 4, { 0, 4, 5, 3 } }, { 4, { 5, 3, 0, 4 } }, { 4, { 0, 5, 3, 4 } }, { 2, { 0, 3 } },
    { 4, { 1, 4, 0, 0 } }, { 4, { 0, 6, 5, 4 } }, { 2, { 0, 3 } }, { 12, { 0, 0, 0, 0, 3, 3, 0, 0, 4, 3, 0, 4 } },
};
#define NPROGS ((int)(sizeof progs / sizeof progs[0]))
static const char *const prog_l[NPROGS] = { "HOLD", "I V vi IV", "vi IV I V", "I vi IV V", "I IV", "ii V I", "i VII VI V", "i iv", "12 BAR BLUES" };
static const char *const chord_l[] = { "I", "II", "III", "IV", "V", "VI", "VII" };
static const char *const bars_l[] = { "1 BAR", "2 BARS", "HALF BAR" };
static const char *const range_l[] = { "NEAR", "ROOM", "FAR" };

#define SYS PF_SYS
static const param_t host_params[H_N] = {
    [H_AXIS]   = { "AXIS", "xaxs", G_WRECK, PK_NUM, PF_INT | SYS, 0, 100, 2, 12, "", NULL, "0 BREAKCORE, 100 DRONE" },
    [H_STEPS]  = { "TEMPO STEPS", "xstp", G_WRECK, PK_ENUM, 0, 0, 3, 1, 0, "", steps_l, "THE TEMPO AT EACH END" },
    [H_CHANGE] = { "CHANGE", "xchg", G_WRECK, PK_ENUM, 0, 0, 1, 1, 0, "", change_l, "HOW A NEW TEMPO COMES IN" },
    [H_KNOCK]  = { "KNOCKS", "xkno", G_WRECK, PK_ENUM, SYS, 0, 1, 1, 1, "", off_on, "A KNOCK IS A FILL" },
    [H_KSENSE] = { "KNOCK SENSE", "xkse", G_WRECK, PK_NUM, PF_INT | SYS, 0, 100, 5, 50, "%", NULL, "MORE: SOFTER KNOCKS COUNT" },
    [H_SOURCE] = { "SAMPLE FROM", "xsrc", G_SAMPLE, PK_ENUM, SYS, 0, 2, 1, 0, "", source_l, "WHAT HOLDING REC RECORDS" },
    [H_USBMON] = { "USB THRU", "xusb", G_SAMPLE, PK_ENUM, SYS, 0, 1, 1, 1, "", off_on, "HEAR THE COMPUTER (USB)" },
    [H_DRUMS]  = { "DRUMS", "mdrm", G_MIX, PK_NUM, PF_INT, 0, 100, 5, 85, "%", NULL, "THE BREAK'S LEVEL" },
    [H_AMP]    = { "AMP", "mamp", G_MIX, PK_NUM, PF_INT, 0, 100, 5, 70, "%", NULL, "THE AMP'S LEVEL" },
    [H_TEMPO]  = { "TEMPO", "stmp", G_SONG, PK_NUM, PF_INT | SYS, 60, 180, 1, 100, "bpm", NULL, "SHARED WITH THE FLOCK" },
    [H_KEY]    = { "KEY", "skey", G_SONG, PK_ENUM, SYS, 0, 11, 1, 9, "", key_l, "THE KEY THE AMP PLAYS IN" },
    [H_SCALE]  = { "SCALE", "sscl", G_SONG, PK_ENUM, SYS, 0, NSCALES - 1, 1, 4, "", scale_l, "THE NOTES OF THE KEY" },
    [H_PROG]   = { "CHORDS", "sprg", G_SONG, PK_ENUM, SYS, 0, NPROGS - 1, 1, 0, "", prog_l, "HOW THE CHORDS MOVE BY BAR" },
    [H_HOLD]   = { "HOLD CHORD", "schd", G_SONG, PK_ENUM, SYS, 0, 6, 1, 0, "", chord_l, "THE CHORD FOR CHORDS: HOLD" },
    [H_BARS]   = { "CHORD BARS", "sbar", G_SONG, PK_ENUM, SYS, 0, 2, 1, 0, "", bars_l, "HOW LONG EACH CHORD LASTS" },
    [H_LINK]   = { "LINK", "nlnk", G_LINK, PK_ENUM, SYS | PF_NOCC, 0, 1, 1, 1, "", off_on, "SYNC WITH BOARDS NEARBY" },
    [H_GROUP]  = { "GROUP", "ngrp", G_LINK, PK_NUM, SYS | PF_NOCC | PF_INT, 1, 8, 1, 1, "", NULL, "SAME GROUP: THEY SYNC" },
    [H_RANGE]  = { "RANGE", "npow", G_LINK, PK_ENUM, SYS | PF_NOCC, 0, 2, 1, 1, "", range_l, "HOW FAR THE RADIO REACHES" },
    [H_PEERS]  = { "WHO IS THERE", "nwho", G_LINK, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "THE BOARDS LINKED NOW" },
};
static UI_RAM param_t table[NPARAMS];
static UI_RAM char placeholder_tag[M_N * MOD_PARAMS_MAX][5];
static const char *groups[G_N];

// ---------------------------------------------------------------- the sounds: the host's part and each module's
static const char *const sound_names[WR_NSOUNDS] = { "WRECK", "SNARES", "DRILL", "GABBA", "GRIND", "BAROQUE", "WIZARD", "EARTH", "DRONE", "THUNDER" };
static const char *const host_sounds[WR_NSOUNDS] = {
    "",                                    // WRECK: the whole way, x2 to x1/4
    "",                                    // SNARES
    "xstp=2",                              // DRILL: x2 to x1/2
    "xstp=1",                              // GABBA: x1 to x1/4
    "xstp=1",                              // GRIND
    "",                                    // BAROQUE
    "xstp=1",                              // WIZARD
    "xstp=1 mdrm=70 mamp=80",              // EARTH
    "xstp=1 mdrm=60 mamp=85",              // DRONE
    "xchg=1",                              // THUNDER
};
#define SOUND_TEXT 1024
static UI_RAM char sound_text[WR_NSOUNDS][SOUND_TEXT];
static preset_t presets[WR_NSOUNDS];

__attribute__((constructor)) static void build_table(void)
{
    groups[G_WRECK] = "WRECK"; groups[G_SAMPLE] = "SAMPLE"; groups[G_MIX] = "MIX"; groups[G_SONG] = "SONG"; groups[G_LINK] = "LINK";
    for (int m = 0; m < M_N; m++) {
        const module_t *M = mods[m];
        int g0 = G_MOD0 + 2 * mod_page_order[m];
        groups[g0] = M->pages[0];
        groups[g0 + 1] = M->npages > 1 ? M->pages[1] : M->pages[0];
        for (int i = 0; i < MOD_PARAMS_MAX; i++) {
            param_t *d = &table[m * MOD_PARAMS_MAX + i];
            if (i < M->nparams) { *d = M->params[i]; d->group = (uint8_t)(g0 + (M->params[i].group ? 1 : 0)); }
            else {
                snprintf(placeholder_tag[m * MOD_PARAMS_MAX + i], 5, "~%c%02d", M->letter, i);
                *d = (param_t){ "-", placeholder_tag[m * MOD_PARAMS_MAX + i], 0, PK_NUM, PF_HIDE | PF_NOCC | PF_SYS, 0, 1, 1, 0, "", NULL, NULL };
            }
        }
    }
    for (int i = 0; i < H_N; i++) table[HB + i] = host_params[i];
    for (int k = 0; k < WR_NSOUNDS; k++) {
        int w = snprintf(sound_text[k], SOUND_TEXT, "%s", host_sounds[k]);
        for (int m = 0; m < M_N; m++) {
            const char *s = mods[m]->sounds ? mods[m]->sounds[k] : "";
            if (s && s[0] && w < SOUND_TEXT - 2) w += snprintf(sound_text[k] + w, (size_t)(SOUND_TEXT - w), "%s%s", w ? " " : "", s);
        }
        presets[k].name = sound_names[k]; presets[k].set = sound_text[k];
    }
}

// ---------------------------------------------------------------- the song and the link (as SING's)
song_t song = { .bpm = 100, .fpb = 19200, .bar_beats = 4, .scale = scales[4], .scale_n = 7, .chord = { 57, 58, 64 }, .chord_n = 3 };
volatile perf_t perf = { .running = true };
volatile grid_t wr_grid = { .feel_q = 1, .fpb = 19200, .origin = 0 };
volatile uint32_t wr_grid_seq;
static int chord_live = -1;
static bool link_up, applying;
static int  peers_was;
static volatile float out_env_v;
float host_out_env(void) { return out_env_v; }

static uint32_t tempo_fpb(float bpm) { return (uint32_t)(60.0f * (float)AUDIO_SR / (bpm < 20.0f ? 20.0f : bpm) + 0.5f); }
static uint16_t pc_mask_of(const int *notes, int n) { uint16_t m = 0; for (int i = 0; i < n; i++) m |= (uint16_t)(1u << (((notes[i] % 12) + 12) % 12)); return m; }
static uint16_t rot(uint16_t m, int k) { k = ((k % 12) + 12) % 12; return (uint16_t)(((m << k) | (m >> (12 - k))) & 0x0FFF); }

static const float bar_beats[3] = { 4.0f, 8.0f, 2.0f };            // CHORD BARS: 1 BAR, 2 BARS, HALF BAR (file scope:
                                                                   // the IRAM tool then keeps it in DRAM for the audio task)
// the chord's degree at a frame (the progression, HOLD, or a chord played live on a linked board): exact on a bar
// line, where song.degree may still be the last bar's for up to a tick. UI or audio task.
int host_degree_at(uint32_t frame)
{
    int live = chord_live;
    if (live >= 0) return live;
    if ((int)H(H_PROG) == 0) return (int)H(H_HOLD);
    uint32_t fpb = song.fpb ? song.fpb : 19200;
    int32_t span = song.loop_beats ? (int32_t)(fpb * (uint32_t)song.loop_beats) : (int32_t)(fpb * SONG_WRAP_BEATS);
    float beat = (float)wr_mod((int32_t)(frame - song.origin), span) / (float)fpb;
    uint32_t bar = (uint32_t)(beat / bar_beats[(int)H(H_BARS)] + 1e-4f);
    const prog_t *p = &progs[(int)H(H_PROG)];
    return p->d[bar % (uint32_t)p->n];
}

static void set_song(uint32_t frame)
{
    int s = (int)H(H_SCALE);
    song.key = (int)H(H_KEY); song.scale = scales[s]; song.scale_n = 7;
    uint16_t m = 0;
    for (int i = 0; i < 7; i++) m |= (uint16_t)(1u << song.scale[i]);
    song.scale_mask = rot(m, song.key);
    uint32_t fpb = song.fpb ? song.fpb : 19200;
    int32_t span = song.loop_beats ? (int32_t)(fpb * (uint32_t)song.loop_beats) : (int32_t)(fpb * SONG_WRAP_BEATS);
    float beat = (float)wr_mod((int32_t)(frame - song.origin), span) / (float)fpb;
    uint32_t bar = (uint32_t)(beat / bar_beats[(int)H(H_BARS)]);
    const prog_t *p = &progs[(int)H(H_PROG)];
    int d = chord_live >= 0 ? chord_live : (int)H(H_PROG) == 0 ? (int)H(H_HOLD) : p->d[bar % (uint32_t)p->n];
    song.degree = d;
    int root = scale_note(d, 3), pc = ((root % 12) + 12) % 12;
    song.chord_n = 3;
    song.chord[0] = 48 + pc;
    song.chord[1] = song.chord[0] + ((scale_note(d + 2, 3) - root) % 12 + 12) % 12;
    song.chord[2] = song.chord[0] + ((scale_note(d + 4, 3) - root) % 12 + 12) % 12;
    song.chord_mask = pc_mask_of(song.chord, song.chord_n);
}

static void link_start(void)
{
    link_init("WRECK", 1);
    link_boot_time(tempo_fpb(H(H_TEMPO)), song.origin, song.bar_beats);
    link_boot_harm((int)H(H_KEY), (int)H(H_SCALE), (int)H(H_PROG), (int)H(H_HOLD), (int)H(H_BARS));
    link_enable(H(H_LINK) >= 1.0f, (int)H(H_GROUP), (int)H(H_RANGE));
    link_up = true;
}
static void tempo_set_here(void)
{
    uint32_t fpb = tempo_fpb(H(H_TEMPO));
    if (fpb == song.fpb) return;
    if (link_flags_any() & LINKF_LOOP) {                          // a loop on another board keeps the tempo
        applying = true; param_set(HB + H_TEMPO, (float)(int)(song.bpm + 0.5f)); applying = false;
        toast("A LOOP KEEPS THE TEMPO", 1200);
        return;
    }
    uint32_t now = audio_frames();
    int32_t span = (int32_t)(song.fpb * SONG_WRAP_BEATS);
    float beat = (float)wr_mod((int32_t)(now - song.origin), span) / (float)song.fpb;
    uint32_t origin = now - (uint32_t)(beat * (float)fpb);
    song.fpb = fpb; song.origin = origin; song.origin_frac = 0;
    song.bpm = 60.0f * (float)AUDIO_SR / (float)fpb;
    link_set_time(fpb, origin, song.bar_beats, 0);
}
// (a chord another board plays live stays as it is: only its own change ends it)
static void harm_set_here(void) { link_set_harm((int)H(H_KEY), (int)H(H_SCALE), (int)H(H_PROG), (int)H(H_HOLD), (int)H(H_BARS), chord_live); }

static void link_follow(void)
{
    if (link_synced()) {
        uint32_t o; float f; link_origin(&o, &f);
        uint32_t fpb = link_fpb();
        if (fpb) {
            bool tempo = fpb != song.fpb;
            song.fpb = fpb; song.origin = o; song.origin_frac = f;
            song.bpm = 60.0f * (float)AUDIO_SR / (float)fpb;
            if (tempo && (int)(song.bpm + 0.5f) != (int)H(H_TEMPO)) { applying = true; param_set(HB + H_TEMPO, (float)(int)(song.bpm + 0.5f)); applying = false; }
        }
    }
    song.loop_beats = link_time()->beats;
    link_time_news();
    if (link_harm_news()) {
        const link_harm_t *h = link_harm();
        applying = true;
        param_set(HB + H_KEY, h->key); param_set(HB + H_SCALE, h->scale); param_set(HB + H_PROG, h->prog);
        param_set(HB + H_HOLD, h->hold); param_set(HB + H_BARS, h->bars);
        applying = false;
        chord_live = h->live;
    }
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

// ---------------------------------------------------------------- audio
#ifndef ESP_PLATFORM
// tools/sim has no USB: a test plays into the "sound card" through this (frames delivered; it fills mono[])
int (*wr_sim_usb_in)(int16_t *mono, int n);
int usb_audio_in_pull(int16_t *mono, int n)   // as the device's: what is not delivered is 0
{
    int got = wr_sim_usb_in ? wr_sim_usb_in(mono, n) : 0;
    if (got < 0) got = 0;
    if (got < n) memset(mono + got, 0, (size_t)(n - got) * sizeof *mono);
    return got;
}
#endif
static smooth_t g_drums, g_amp, g_usb;

static void init(void)
{
    for (int m = 0; m < M_N; m++) mods[m]->init();
    smooth_init(&g_drums, 0.85f, 0.01f); smooth_init(&g_amp, 0.7f, 0.01f); smooth_init(&g_usb, 0.0f, 0.01f);
}

static void audio(const float *in, float *out, int n)
{
    static float drums[AUDIO_BLOCK], dsend[AUDIO_BLOCK], amp[AUDIO_BLOCK], asend[AUDIO_BLOCK], send[AUDIO_BLOCK], usb[AUDIO_BLOCK];
    if (n > AUDIO_BLOCK) n = AUDIO_BLOCK;
    uint32_t f0 = audio_frames();
    memset(drums, 0, sizeof drums); memset(dsend, 0, sizeof dsend); memset(amp, 0, sizeof amp); memset(asend, 0, sizeof asend);
    uint32_t c0 = CYCLES();
    beat_process(drums, dsend, n, f0);
    fx_drums(drums, n);
    uint32_t c1 = CYCLES();
    amp_process(amp, asend, n, f0);
    uint32_t c2 = CYCLES();
    int src = (int)H(H_SOURCE);
    bool usb_on = src == KSRC_USB;
    float tu = usb_on && H(H_USBMON) >= 1.0f ? 1.0f : 0.0f, td = H(H_DRUMS) * 0.01f, ta = H(H_AMP) * 0.01f;
    if (usb_on || g_usb.y > 0.001f) {                               // (and while USB THRU fades out: no cut)
        int16_t raw[AUDIO_BLOCK];
        usb_audio_in_pull(raw, n);                                  // fills what it has not got with 0
        for (int i = 0; i < n; i++) usb[i] = (float)raw[i] * (1.0f / 32768.0f);
    } else memset(usb, 0, sizeof usb);
    for (int i = 0; i < n; i++) {
        float gd = smooth_to(&g_drums, td), ga = smooth_to(&g_amp, ta);
        out[i] = drums[i] * gd + amp[i] * ga + usb[i] * smooth_to(&g_usb, tu);
        send[i] = dsend[i] * gd + asend[i] * ga;
    }
    uint32_t c3 = CYCLES();
    fx_master(out, send, n);
    uint32_t c4 = CYCLES();
    kit_feed(src == KSRC_MIC ? in : src == KSRC_USB ? usb : out, n, f0);
    uint32_t c5 = CYCLES();
    prof_cyc[M_BEAT] += c1 - c0; prof_cyc[M_AMP] += c2 - c1; prof_cyc[M_N] += c3 - c2; prof_cyc[M_FX] += c4 - c3; prof_cyc[M_KIT] += c5 - c4;
    prof_blocks++;
    float pk = 0;
    for (int i = 0; i < n; i++) { float a = fabsf(out[i]); if (a > pk) pk = a; }
    out_env_v = pk > out_env_v ? pk : out_env_v * 0.95f;
}

// ---------------------------------------------------------------- the UI side
static int  zone = Z_BREAKCORE, feel_want = 1, change_st, change_ms;
static bool rec_was, rec_confirmed, stopping, rec_seen;
static int  rec_ms, beat_flash;
static uint32_t last_beat = 0xFFFFFFFFu;
static float knock_prev_mag = 1.0f, knock_ref;
static int  knock_quiet_ms, knock_count;
int  wr_knocks(void) { return knock_count; }                     // for tools/sim

static void grid_set(int q)
{
    __atomic_add_fetch(&wr_grid_seq, 1u, __ATOMIC_ACQ_REL);
    wr_grid.feel_q = q; wr_grid.fpb = song.fpb; wr_grid.origin = song.origin;
    __atomic_add_fetch(&wr_grid_seq, 1u, __ATOMIC_ACQ_REL);
}

// the hands: the tilt, the roll, the stick
static float lean_amount(float deg, float dead, float full)
{
    float a = fabsf(deg);
    return a < dead ? 0.0f : clampf((a - dead) / (full - dead), 0.0f, 1.0f);
}
static void hands(const ctl_t *c)
{
    float ly = c->has_imu ? c->lean_y : 0.0f, lx = c->has_imu ? c->lean_x : 0.0f;
    perf.chaos = ly > 0.0f ? lean_amount(ly, 6.0f, 40.0f) : 0.0f;
    perf.drag = ly < 0.0f ? lean_amount(ly, 6.0f, 40.0f) : 0.0f;
    perf.smear = lx < 0.0f ? lean_amount(lx, 15.0f, 45.0f) : 0.0f;
    perf.stutter = lx > 0.0f ? lean_amount(lx, 15.0f, 45.0f) : 0.0f;
    bool stick = c->has_joy && !beat_editing();                  // while the grid is open the stick is the grid's
    float jx = stick ? c->jx : 0.0f, jy = stick ? c->jy : 0.0f;
    perf.roll = jy > 0.0f ? jy : 0.0f;
    perf.dive = jy < 0.0f ? -jy : 0.0f;
    perf.crush = jx < 0.0f ? -jx : 0.0f;
    perf.drive = jx > 0.0f ? jx : 0.0f;
    if (perf.chaos > 0.3f || perf.drag > 0.3f) coach_done(LEARN_TILT);
    if (c->has_joy && (fabsf(jx) > 0.5f || fabsf(jy) > 0.5f)) coach_done(LEARN_JOY);
}

// a knock on the box: a jolt in the 1 kHz accelerometer stream, well over what the hands do
static void knocks(const ctl_t *c)
{
    if (H(H_KNOCK) < 1.0f || !c->burst || c->nburst <= 0) return;
    float pk = 0;
    for (int i = 0; i < c->nburst; i++) {
        const vec3_t *a = &c->burst[i];
        float mag = sqrtf(a->x * a->x + a->y * a->y + a->z * a->z);
        float j = fabsf(mag - knock_prev_mag);
        knock_prev_mag = mag;
        if (j > pk) pk = j;
    }
    knock_ref += 0.02f * (fminf(pk, 0.5f) - knock_ref);         // how much the box moves anyway (held, carried)
    float thr = 0.35f + (1.0f - H(H_KSENSE) * 0.01f) * 1.2f + 2.0f * knock_ref;
    if (c->knob_down || c->rec_press || c->rec_release) knock_quiet_ms = 150;   // pressing a control jolts it
    if (knock_quiet_ms > 0) { knock_quiet_ms -= 10; return; }
    if (pk > thr) {
        float s = clampf((pk - thr) / (thr * 2.0f), 0.0f, 1.0f);
        beat_fill(s);
        amp_hit(s);
        knock_count++;
        knock_quiet_ms = 120;
        coach_done(LEARN_INST + 0);
        ui_awake();
    }
}

// the axis: its zone (with a little hysteresis) and the feel it asks for
static void axis_tick(const ctl_t *c)
{
    float a = H(H_AXIS) * 0.01f;
    perf.axis += (a - perf.axis) * 0.067f;                      // about 0.15 s
    if (fabsf(a - perf.axis) < 0.001f) perf.axis = a;
    int z = zone;
    while (z < Z_N - 1 && perf.axis > (float)(z + 1) * 0.25f + 0.02f) z++;
    while (z > 0 && perf.axis < (float)z * 0.25f - 0.02f) z--;
    if (z != zone) { zone = z; char b[24]; snprintf(b, sizeof b, "%s", zone_names[z]); toast(b, 700); }
    perf.zone = zone;
    feel_want = steps_q[(int)H(H_STEPS)][zone];
    // a new feel: wait for the song's next beat (CUT), or brake the tape over half a beat first (TAPE STOP)
    grid_t g; wr_grid_get(&g);
    bool tempo_moved = g.fpb != song.fpb || g.origin != song.origin;
    if (feel_want == g.feel_q && change_st == 0) { if (tempo_moved) grid_set(g.feel_q); return; }
    uint32_t fpb = song.fpb ? song.fpb : 19200;
    int32_t pos = wr_mod((int32_t)(c->frames - song.origin), (int32_t)(fpb * SONG_WRAP_BEATS));
    int32_t to_beat = (int32_t)fpb - pos % (int32_t)fpb;            // frames to the song's next beat
    bool tape = H(H_CHANGE) < 1.0f && perf.running && !stopping;    // (a stop under way keeps its own brake)
    if (change_st == 0) {
        if (tape) { fx_brake(0.5f); change_st = 1; change_ms = 0; }
        else change_st = 2;
    }
    if (change_st == 1) {                                          // braking: then the next beat
        change_ms += 10;
        if (fx_stopped() || change_ms > 2000) change_st = 2;
    }
    if (change_st == 2 && to_beat <= (int32_t)(AUDIO_SR / 100 + AUDIO_BLOCK)) {   // the beat falls in this tick
        grid_set(feel_want);
        if (tape) fx_spin(0.25f);
        change_st = 0;
    } else if (change_st == 2 && tempo_moved) grid_set(g.feel_q);
}

// REC (the REC pin when SYSTEM > REC BUTTON is on, BOOT always, MIDI CC 64 or note 60): held 0.3 s = sample a
// break (KIT), let go sooner = a tap: stop the machine, or start it. REC already down when WRECK starts, or still
// down after a take filled up (8 s, KIT's buffer), counts for nothing until it has been up 0.1 s (a bounce is not a
// release). Boot never samples: KIT prints the first break in init(), and nothing here runs before the first tick.
#define REC_HOLD_MS   300
#define REC_FULL_S    8.0f
#define REC_LOG_MAX   16                                          // REC's presses in the log, then quiet
static bool rec_ignore = true;
static int  rec_logs, rec_up_ms;
static void rec_logic(const ctl_t *c)
{
    if (c->rec != rec_seen && rec_logs < REC_LOG_MAX) {            // what the button does, in the log
        rec_logs++;
        app_log("REC %s at %.2f s%s", c->rec ? "down" : "up", (double)((float)c->frames / (float)AUDIO_SR), rec_ignore ? " (ignored until let go)" : "");
    }
    rec_seen = c->rec;
    if (rec_ignore) {
        rec_up_ms = c->rec ? 0 : rec_up_ms + 10;
        if (rec_up_ms >= 100) rec_ignore = false;
        rec_was = false; return;
    }
    bool rec = c->rec;
    if (kit_recording() && kit_rec_seconds() >= REC_FULL_S) {     // the take is full: it ends here
        kit_rec_release(true); rec_confirmed = false; rec_was = false; rec_ignore = true;
        return;
    }
    if (beat_editing()) {                                          // the grid has REC while it is open
        if (rec_was) { kit_rec_release(rec_confirmed); rec_confirmed = false; }   // (a hold that reached it is kept)
        rec_was = false; return;
    }
    if (rec && !rec_was) { kit_rec_press((int)H(H_SOURCE)); rec_confirmed = false; rec_ms = 0; }
    if (rec) rec_ms += 10;
    if (rec && !rec_confirmed && rec_ms >= REC_HOLD_MS) { kit_rec_confirm(); rec_confirmed = true; coach_done(LEARN_INST + 3); }
    if (!rec && rec_was) {
        kit_rec_release(rec_confirmed);
        if (!rec_confirmed) {                                      // a tap: stop the machine, or start it
            if (perf.running && !stopping) { fx_brake(1.0f); stopping = true; toast("STOP", 600); }
            else { stopping = false; perf.running = true; fx_spin(0.5f); toast("GO", 600); }
            coach_done(LEARN_INST + 2);
        }
    }
    if (stopping && fx_stopped()) { perf.running = false; stopping = false; }
    rec_was = rec;
}
#ifndef ESP_PLATFORM
void wr_test_rec_as_at_boot(void)                                  // tools/sim: REC's state as at power-on
{
    rec_ignore = true; rec_up_ms = 0; rec_was = rec_seen = rec_confirmed = false; rec_ms = 0;
}
#endif

static void tick(const ctl_t *c)
{
    if (c->ticks == 1 || c->ticks == 100) {                         // the REC pins at boot, once (a stuck pin shows here)
        int pin_rec = 1, pin_boot = 1;
#ifdef ESP_PLATFORM
        pin_rec = gpio_get_level(PIN_REC); pin_boot = gpio_get_level(PIN_BOOT);
#endif
        app_log("REC pin (GPIO %d) %s, BOOT %s, REC BUTTON %s; REC %s", PIN_REC, pin_rec ? "up" : "DOWN", pin_boot ? "up" : "DOWN",
                pv[CP(CP_REC_BTN)] >= 1.0f ? "on" : "off (BOOT samples)", c->rec ? "down (ignored until let go)" : "up");
    }
    if ((c->ticks % 1000 == 0 || (c->ticks < 3000 && c->ticks % 100 == 0)) && prof_blocks) {   // every second for the
        float us[PROF_N]; wr_prof(us, PROF_N);                                                  // first 30 s, then 10 s
        app_log("cpu us/block: beat %.0f amp %.0f fx %.0f kit %.0f mix %.0f (of 2000)",
                (double)us[M_BEAT], (double)us[M_AMP], (double)us[M_FX], (double)us[M_KIT], (double)us[M_N]);
    }
    if (meta.quick[0] && memcmp(meta.quick, "xaxs", 4)) memset(meta.quick, 0, sizeof meta.quick);   // the knob stays the axis
    if (!link_up) link_start();
    link_tick(c->dt);
    link_follow();
    set_song(c->frames);
    hands(c);
    knocks(c);
    axis_tick(c);
    rec_logic(c);
    for (int m = 0; m < M_N; m++) if (mods[m]->tick) mods[m]->tick(c);
    // the beat, for the header and the LED
    uint32_t fpb = song.fpb ? song.fpb : 19200;
    uint32_t b = (uint32_t)wr_mod((int32_t)(c->frames - song.origin), (int32_t)(fpb * SONG_WRAP_BEATS)) / fpb;
    if (b != last_beat) { last_beat = b; beat_flash = (b % 4 == 0) ? 3 : 2; }
    else if (beat_flash > 0 && c->ticks % 4 == 0) beat_flash--;
    // the LED: from hot pink (BREAKCORE) to a dim blood red (DRONE), on the beat; red while sampling
    float x = perf.axis, lv = clampf(out_env_v * 2.0f, 0.0f, 1.0f) * 0.6f + (beat_flash ? 0.4f : 0.1f);
    if (kit_recording()) led_rgb((uint8_t)(beat_flash ? 255 : 90), 0, 0);
    else if (!perf.running) led_rgb(8, 0, 0);
    else led_rgb((uint8_t)(255.0f * lv), (uint8_t)(40.0f * (1.0f - x) * lv), (uint8_t)(200.0f * (1.0f - x) * (1.0f - x) * lv));
}

int  wr_zone(void) { return zone; }                               // for the screen
int  wr_beat_flash(void) { return beat_flash; }
bool wr_stopping(void) { return stopping; }
int  wr_peers(void) { return peers_was; }

static void changed(int p)
{
    if (p < 0) {                                                   // a sound loaded, or everything reset
        for (int m = 0; m < M_N; m++) mods[m]->changed(-1);
        if (link_up && !applying) {                                // the song's settings are PF_SYS: a sound leaves them;
            if (tempo_fpb(H(H_TEMPO)) != tempo_fpb((float)(int)(song.bpm + 0.5f))) tempo_set_here();   // a reset moves them
            harm_set_here();
            link_enable(H(H_LINK) >= 1.0f, (int)H(H_GROUP), (int)H(H_RANGE));
        }
        return;
    }
    if (p < HB) { int m = p / MOD_PARAMS_MAX, i = p % MOD_PARAMS_MAX; if (i < mods[m]->nparams) mods[m]->changed(i); return; }
    if (!link_up || applying) return;
    switch (p - HB) {
    case H_TEMPO: tempo_set_here(); break;
    case H_KEY: case H_SCALE: case H_PROG: case H_HOLD: case H_BARS: harm_set_here(); break;
    case H_LINK: case H_GROUP: case H_RANGE:
        link_enable(false, 1, 1);
        link_enable(H(H_LINK) >= 1.0f, (int)H(H_GROUP), (int)H(H_RANGE));
        peers_was = 0;
        break;
    }
}

static void action(int p)
{
    if (p < HB) { int m = p / MOD_PARAMS_MAX; if (mods[m]->action) mods[m]->action(p % MOD_PARAMS_MAX); return; }
    if (p - HB == H_PEERS) {
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
    }
}

static bool hold(void)
{
    if (beat_hold()) { coach_done(LEARN_INST + 1); return true; }
    return false;
}

// LEARN_INST + 0: knocked; + 1: opened the grid; + 2: stopped or started with a tap; + 3: held REC long enough to
// sample (the core's LEARN_REC is marked by any press, a tap too). Modules use + 8 and up. BOOT is named, not REC:
// REC BUTTON starts OFF on WRECK (INST.rec_button_off) and BOOT is a REC on every board either way.
static const hint_t hints[] = {
    { "TURN THE KNOB: FROM BREAKCORE TO DOOM", LEARN_TURN },
    { "TIP IT FORWARD FOR CHAOS, BACK TO DRAG", LEARN_TILT },
    { "KNOCK ON THE BOX FOR A FILL", LEARN_INST + 0 },
    { "HOLD THE KNOB TO CHOP IT YOURSELF", LEARN_INST + 1 },
    { "HOLD BOOT TO SAMPLE A NEW BREAK", LEARN_INST + 3 },
    { "TAP BOOT TO STOP, TAP AGAIN TO GO", LEARN_INST + 2 },
    { "STICK UP: A ROLL. DOWN: A DIVE", LEARN_JOY },
    { "PRESS THE KNOB FOR THE MENU", LEARN_MENU },
};

static void draw(const view_t *v) { scene_draw(v); }
static bool splash(int f) { return scene_splash(f); }
static void saver(uint32_t f) { scene_saver(f); }

const instrument_t INST = {
    .name = "WRECK", .tagline = "BREAKCORE TO DOOM",
    .params = table, .nparams = NPARAMS, .groups = groups, .ngroups = G_N,
    .presets = presets, .npresets = WR_NSOUNDS,
    .hints = hints, .nhints = (int)(sizeof hints / sizeof hints[0]),
    .burst = true, .looper = false, .knob = "xaxs", .midi_notes = false,
    .rec_button_off = true,    // the REC pin is ignored until SYSTEM > REC BUTTON is on (a board here has it stuck low:
                               // its joystick's click); BOOT and MIDI sample meanwhile. The hints say BOOT
    .init = init, .audio = audio, .tick = tick, .draw = draw, .splash = splash,
    .changed = changed, .action = action, .saver = saver, .hold = hold,
};
