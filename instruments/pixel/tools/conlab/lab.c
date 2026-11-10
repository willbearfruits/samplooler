// conlab: the consoles on a bench, without the rest of the instrument. It plays a fixed tune through the real player
// (inst_player.c) into each console (inst_con_*.c), writes what comes out as WAVs and checks what a console owes the
// contract in pixel.h: levels, no DC, silence after the notes, pitch, every part and drum audible on its own, the
// cost per block, nothing bad under a full glitch, the same output twice.
//
//   ./run.sh                all eight consoles
//   ./run.sh NES C64        only those (names as in px_console_t.name, case does not matter; "GAME BOY" is GAMEBOY)
//
// out/<console>-tune.wav    the bench tune, 8 bars
// out/<console>-parts.wav   each part alone, two bars each: drums, bass, chords, lead
// out/<console>-sounds.wav  every SOUND of the lead, the bass and the chords, every drum KIT, a bar each
// out/<console>-glitch.wav  the tune with the cartridge tilted all the way
// out/tour.wav              two bars on each console in the knob's order
// out/results.json          every number and check
#include "pixel.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

// ---------------------------------------------------------------- what the firmware's platform gives (core.h)
void *app_alloc(size_t n) { return calloc(1, n); }
void *app_alloc_fast(size_t n) { return calloc(1, n); }
void  app_free(void *p) { free(p); }
uint32_t audio_frames(void) { return 0; }
void  app_log(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); printf("\n"); }

// ---------------------------------------------------------------- the bench tune: A minor, 150 BPM
static const char TUNE[] =
    "AL: 5 . 1' . 3' 2' 1' . 5 . 1' . 7 - - .\n"
    "AB: 1 . 1' . 1 . 1' . 1 . 1' . 1 . 1' 1\n"
    "AC: I . . . . . . . . . . . . . . .\n"
    "AK: x...x...x...x...\n"
    "AS: ....x.......x...\n"
    "AH: x.x.x.x.x.x.x.x.\n"
    "BL: 6 . 1' . 3' 2' 1' . 6 . 4 . 6! - - .\n"
    "BB: 6, . 6 . 6, . 6 . 6, . 6 . 6, . 6 6\n"
    "BC: VI . . . . . . . . . . . . . . .\n"
    "BK: x...x...x...x...\n"
    "BS: ....x.......x...\n"
    "BH: x.x.x.x.x.x.x.x.\n"
    "CL: 5 . 7 . 3' 2' 7 . 5 . 3 . 5 - ~7 -\n"
    "CB: 3 . 3' . 3 . 3' . 3 . 3' . 3 . 3' 3\n"
    "CC: III . . . . . . . . . . . . . . .\n"
    "CK: x...x...x...x...\n"
    "CS: ....x.......x...\n"
    "CH: x.x.x.x.x.x.x.x.\n"
    "CO: ..............x.\n"
    "DL: 7 . 2' . 4' 3' 2' . 7 . 5 . 7! - 2' -\n"
    "DB: 7, . 7 . 7, . 7 . 7, . 7 . 5 . 7 2'\n"
    "DC: VII . . . . . . . V7 . . . . . . .\n"
    "DK: x...x...x...x.x.\n"
    "DS: ....x.......x.xx\n"
    "DH: x.x.x.x.x.x.....\n"
    "DT: ............X.x.\n"
    "ORDER: A B C D A B C D\n";

#define BPM       150.0f
#define TPS       6
#define TICK_HZ   (BPM / 60.0f * 4.0f * (float)TPS)
#define BAR_TICKS (PX_STEPS * TPS)

static px_song_t   song;
static px_player_t *player;
static px_ctl_t    ctl;

static void ctl_defaults(void)
{
    memset(&ctl, 0, sizeof ctl);
    ctl.key = 9; ctl.scale = PX_SC_MINOR;
    for (int i = 0; i < PART_N; i++) { ctl.level[i] = 1.0f; ctl.live[i] = -1; }
    ctl.len_bass = 80; ctl.len_lead = 85;
    ctl.arp = ARP_FAST; ctl.shape = SHAPE_UP; ctl.rhythm = RHY_HELD;
    ctl.vibrato = 40; ctl.echo = 2; ctl.slide = 60;
}

// ---------------------------------------------------------------- running a console
#define OUT_MAX (AUDIO_SR * 120)
static float  *obuf;
static int     on;
static double  cost_render_ns, cost_render_max, cost_tick_ns, cost_tick_max;
static long    cost_blocks, cost_ticks;
static double  tick_acc;

static double now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec * 1e9 + (double)t.tv_nsec; }

// one tick of the player and the console, then that tick's audio (in blocks of at most AUDIO_BLOCK, as the host does)
static void run_tick(const px_console_t *c, const px_pattern_t *pat, const px_pattern_t *next, int step, int tis)
{
    px_frame_t f;
    px_player_tick(player, pat, next, step, tis, TPS, TICK_HZ, &ctl, &f);
    double t0 = now_ns();
    c->tick(&f);
    double dt = now_ns() - t0;
    cost_tick_ns += dt; cost_ticks++; if (dt > cost_tick_max) cost_tick_max = dt;
    tick_acc += (double)AUDIO_SR / (double)TICK_HZ;
    int n = (int)tick_acc; tick_acc -= n;
    while (n > 0 && on < OUT_MAX) {
        int k = n > AUDIO_BLOCK ? AUDIO_BLOCK : n;
        if (on + k > OUT_MAX) k = OUT_MAX - on;
        t0 = now_ns();
        c->render(obuf + on, k);
        dt = (now_ns() - t0) * (double)AUDIO_BLOCK / (double)k;       // as if it were a whole block
        if (k >= 32) { cost_render_ns += dt; cost_blocks++; if (dt > cost_render_max) cost_render_max = dt; }
        on += k; n -= k;
    }
}

// bars of the song from order slot `from`; pat = NULL bars are silence (the song stopped)
static void run_bars(const px_console_t *c, int from, int bars, bool stopped)
{
    for (int b = 0; b < bars; b++) {
        const px_pattern_t *pat = stopped ? NULL : &song.pat[song.order[(from + b) % song.order_n]];
        const px_pattern_t *nx = stopped ? NULL : &song.pat[song.order[(from + b + 1) % song.order_n]];
        for (int s = 0; s < PX_STEPS; s++) for (int t = 0; t < TPS; t++) run_tick(c, pat, nx, s, t);
    }
}
static void run_ticks(const px_console_t *c, int ticks) { for (int i = 0; i < ticks; i++) run_tick(c, NULL, NULL, 0, i % TPS); }

static void start(const px_console_t *c)
{
    on = 0; tick_acc = 0;
    cost_render_ns = cost_render_max = cost_tick_ns = cost_tick_max = 0; cost_blocks = cost_ticks = 0;
    ctl_defaults();
    px_player_reset(player);
    c->reset();
    px_player_restart(player);
}

// ---------------------------------------------------------------- measuring
static float m_rms(int a, int b) { double s = 0; for (int i = a; i < b; i++) s += (double)obuf[i] * obuf[i]; return b > a ? (float)sqrt(s / (b - a)) : 0; }
static float m_peak(int a, int b) { float p = 0; for (int i = a; i < b; i++) { float x = fabsf(obuf[i]); if (x > p) p = x; } return p; }
static float m_dc(int a, int b) { double s = 0; for (int i = a; i < b; i++) s += obuf[i]; return b > a ? (float)(s / (b - a)) : 0; }
static int   m_bad(int a, int b) { int n = 0; for (int i = a; i < b; i++) { uint32_t u; memcpy(&u, &obuf[i], 4); if ((u & 0x7f800000u) == 0x7f800000u) n++; } return n; }
static uint32_t m_sum(int a, int b) { uint32_t h = 2166136261u; for (int i = a; i < b; i++) { uint32_t u; memcpy(&u, &obuf[i], 4); h = (h ^ u) * 16777619u; } return h; }
// the pitch between fmin and fmax by autocorrelation (the first strong peak), Hz; 0: none
static float m_pitch(int a, int b, float fmin, float fmax)
{
    int n = b - a, lmin = (int)((float)AUDIO_SR / fmax), lmax = (int)((float)AUDIO_SR / fmin);
    if (n < 2 * lmax + 16) return 0;
    const float *x = obuf + a;
    double e0 = 0; for (int i = 0; i < n - lmax; i++) e0 += (double)x[i] * x[i];
    if (e0 < 1e-9) return 0;
    double best = 0; int bl = 0;
    static double r[8192];
    if (lmax >= 8190) lmax = 8189;
    for (int l = lmin - 1; l <= lmax + 1; l++) { double s = 0; for (int i = 0; i < n - lmax - 1; i++) s += (double)x[i] * x[i + l]; r[l] = s / e0; }
    for (int l = lmin; l <= lmax; l++) if (r[l] > r[l - 1] && r[l] >= r[l + 1] && r[l] > best + 0.02) { best = r[l]; bl = l; if (best > 0.9) break; }
    if (!bl || best < 0.3) return 0;
    double y0 = r[bl - 1], y1 = r[bl], y2 = r[bl + 1], d = (y0 - y2) / (2.0 * (y0 - 2.0 * y1 + y2));
    return (float)((double)AUDIO_SR / ((double)bl + d));
}

static void wav(const char *console, const char *what, int a, int b)
{
    char path[128], nm[32];
    int k = 0; for (const char *s = console; *s && k < 30; s++) if (*s != ' ') nm[k++] = (char)(*s >= 'A' && *s <= 'Z' ? *s + 32 : *s);
    nm[k] = 0;
    if (what[0]) snprintf(path, sizeof path, "out/%s-%s.wav", nm, what); else snprintf(path, sizeof path, "out/%s.wav", nm);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t n = (uint32_t)(b - a), sr = AUDIO_SR, bytes = n * 2, v;
    uint16_t w;
    fwrite("RIFF", 1, 4, f); v = 36 + bytes; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    v = 16; fwrite(&v, 4, 1, f); w = 1; fwrite(&w, 2, 1, f); w = 1; fwrite(&w, 2, 1, f);
    fwrite(&sr, 4, 1, f); v = sr * 2; fwrite(&v, 4, 1, f); w = 2; fwrite(&w, 2, 1, f); w = 16; fwrite(&w, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    for (int i = a; i < b; i++) { float x = obuf[i] * 0.9f; if (x > 1) x = 1; if (x < -1) x = -1; int16_t s = (int16_t)lrintf(x * 32767.0f); fwrite(&s, 2, 1, f); }
    fclose(f);
}

static int   n_checks, n_failed;
static FILE *json;
static const char *cur_console = "";
static void check(bool ok, const char *fmt, ...)
{
    char b[256]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    n_checks++; if (!ok) n_failed++;
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", b);
    if (json) fprintf(json, "    {\"console\": \"%s\", \"ok\": %s, \"what\": \"%s\"},\n", cur_console, ok ? "true" : "false", b);
}
static void metric(const char *key, double v)
{
    printf("        %-22s %.4g\n", key, v);
    if (json) fprintf(json, "    {\"console\": \"%s\", \"metric\": \"%s\", \"value\": %.6g},\n", cur_console, key, v);
}

// the host's budgets for render(), microseconds per 64-frame block on this machine (the board is about 70 times slower)
static float budget_us(const char *name)
{
    static const struct { const char *n; float us; } B[] = { { "BEEPER", 3.0f }, { "ATARI", 2.0f }, { "AY", 3.0f }, { "NES", 3.5f },
        { "GAME BOY", 3.5f }, { "C64", 6.0f }, { "AMIGA", 3.0f }, { "MEGA", 9.0f } };
    for (int i = 0; i < 8; i++) if (!strcmp(B[i].n, name)) return B[i].us;
    return 4.0f;
}

static void bench(const px_console_t *c)
{
    cur_console = c->name;
    printf("\n%s (%s, %s)\n", c->name, c->chip, c->year);
    bool atari = !strcmp(c->name, "ATARI"), beeper = !strcmp(c->name, "BEEPER");

    // the tune, twice: the same both times, its levels and its cost
    start(c); run_bars(c, 0, 8, false);
    int n1 = on; uint32_t h1 = m_sum(0, on);
    float peak = m_peak(0, on), rms = m_rms(0, on), dc = m_dc(0, on);
    double us = cost_render_ns / (double)(cost_blocks ? cost_blocks : 1) / 1000.0, us_max = cost_render_max / 1000.0;
    double tus = cost_tick_ns / (double)(cost_ticks ? cost_ticks : 1) / 1000.0;
    check(m_bad(0, on) == 0, "tune: no NaN or inf");
    check(peak <= 0.9f, "tune: peak %.3f <= 0.9", (double)peak);
    check(rms >= 0.10f && rms <= 0.16f, "tune: RMS %.3f in 0.10..0.16", (double)rms);
    check(fabsf(dc) < 0.003f, "tune: DC %.4f", (double)dc);
    metric("render_us_per_block", us); metric("render_us_max", us_max); metric("tick_us", tus);
    check(us <= (double)budget_us(c->name), "cost: render %.2f us per block <= %.1f", us, (double)budget_us(c->name));
    check(tus <= 2.0, "cost: tick %.2f us <= 2", tus);
    wav(c->name, "tune", 0, on);
    run_ticks(c, (int)(TICK_HZ * 1.5f));                         // then nothing: it must fall to exact silence
    check(m_peak(on - AUDIO_SR / 4, on) < 1e-6f, "silence: exact zeros 1.25 s after the last note (peak %.2g)", (double)m_peak(on - AUDIO_SR / 4, on));
    start(c); run_bars(c, 0, 8, false);
    check(on == n1 && m_sum(0, on) == h1, "the same output twice");

    // each part alone
    start(c);
    static const char *const pn[PART_N] = { "drums", "bass", "chords", "lead" };
    for (int p = 0; p < PART_N; p++) {
        ctl.mute = (uint8_t)(0x0F & ~(1u << p)); ctl.echo = 0;
        int a = on; run_bars(c, 0, 2, false);
        float r = m_rms(a, on);
        check(r > 0.015f, "%s alone: RMS %.3f", pn[p], (double)r);
        run_ticks(c, 20);
    }
    wav(c->name, "parts", 0, on);

    // every sound and kit, a bar each
    start(c);
    for (int p = PART_N - 1; p >= 0; p--) for (int s = 0; s < 4; s++) {
        ctl.mute = (uint8_t)(0x0F & ~(1u << p)); ctl.echo = 0; ctl.sound[p] = (uint8_t)s;
        int a = on; run_bars(c, 0, 1, false);
        float r = m_rms(a, on);
        check(r > 0.01f && m_bad(a, on) == 0, "%s sound %d: RMS %.3f", pn[p], s, (double)r);
        run_ticks(c, 12);
    }
    wav(c->name, "sounds", 0, on);

    // pitch: a held note on the lead and on the bass, the song stopped (played by hand), no vibrato
    static const struct { int part, note; float cents; } P[] = { { PART_LEAD, 69, 20 }, { PART_LEAD, 84, 30 }, { PART_LEAD, 57, 20 }, { PART_BASS, 45, 20 }, { PART_BASS, 33, 25 } };
    for (unsigned i = 0; i < sizeof P / sizeof P[0]; i++) {
        start(c); ctl.vibrato = 0; ctl.echo = 0;
        ctl.live[P[i].part] = (int8_t)P[i].note;
        run_ticks(c, (int)TICK_HZ);
        float want = 440.0f * powf(2.0f, (float)(P[i].note - 69) / 12.0f);
        float got = m_pitch(on - AUDIO_SR / 2, on, want * 0.45f, want * 2.2f);
        float cents = got > 0 ? 1200.0f * log2f(got / want) : 9999.0f;
        // the Atari's chip has 32 pitches per tone: it is out of tune by design, so only heard and noted
        if (atari) { check(got > 0, "pitch: %s note %d plays %.1f Hz (%.0f cents off %.1f; the chip's own tuning)", pn[P[i].part], P[i].note, (double)got, (double)cents, (double)want); }
        else check(fabsf(cents) <= P[i].cents, "pitch: %s note %d is %.1f Hz, %.0f cents off %.1f", pn[P[i].part], P[i].note, (double)got, (double)cents, (double)want);
        ctl.live[P[i].part] = -1;
    }

    // the lead's level: half the level is about 6 dB less (a 1-bit beeper has no volume: noted only)
    start(c); ctl.vibrato = 0; ctl.echo = 0; ctl.live[PART_LEAD] = 69;
    run_ticks(c, 60); float r1 = m_rms(on - AUDIO_SR / 2, on);
    ctl.level[PART_LEAD] = 0.5f;
    run_ticks(c, 60); float r2 = m_rms(on - AUDIO_SR / 2, on);
    float db = r2 > 0 && r1 > 0 ? 20.0f * log10f(r2 / r1) : -99.0f;
    if (beeper) check(r2 > 0.0f, "level: half the LEVEL changes the lead by %.1f dB (one bit: no volume as such)", (double)db);
    else check(db < -3.0f && db > -10.0f, "level: half the LEVEL is %.1f dB", (double)db);
    ctl.level[PART_LEAD] = 0.0f;
    run_ticks(c, 60);
    check(m_rms(on - AUDIO_SR / 2, on) < 0.004f, "level: LEVEL 0 is silent (RMS %.4f)", (double)m_rms(on - AUDIO_SR / 2, on));

    // each drum alone, by hand
    static const char *const dn[PX_DRUMS] = { "kick", "snare", "hat", "open hat", "tom" };
    for (int k = 0; k < 2; k++) for (int d = 0; d < PX_DRUMS; d++) {
        start(c); ctl.sound[PART_DRUMS] = (uint8_t)k;
        ctl.live_drums = (uint8_t)(1u << d); run_ticks(c, 1); ctl.live_drums = 0;
        run_ticks(c, 44);
        float e = m_rms(0, AUDIO_SR / 8), late = m_rms(on - AUDIO_SR / 16, on);
        check(e > 0.01f, "kit %d %s: sounds (RMS %.3f in its first 125 ms)", k, dn[d], (double)e);
        check(late < 0.02f, "kit %d %s: over after 0.7 s (RMS %.4f)", k, dn[d], (double)late);
    }

    // a reset in the middle of the tune, then the restart frame: it plays on
    start(c); run_bars(c, 0, 1, false);
    c->reset(); px_player_restart(player);
    int a = on; for (int s = 0; s < 4; s++) for (int t = 0; t < TPS; t++) run_tick(c, &song.pat[1], &song.pat[1], s, t);
    check(m_rms(a, on) > 0.02f && m_bad(a, on) == 0, "restart: plays on after reset() (RMS %.3f)", (double)m_rms(a, on));

    // the cartridge tilted all the way
    start(c); ctl.glitch = 1.0f; run_bars(c, 0, 4, false);
    check(m_bad(0, on) == 0 && m_peak(0, on) <= 1.0f, "glitch: nothing bad at full tilt (peak %.3f)", (double)m_peak(0, on));
    wav(c->name, "glitch", 0, on);
    ctl.glitch = 0; run_bars(c, 0, 1, true);
    check(m_peak(on - AUDIO_SR / 4, on) < 1e-6f, "glitch: silent again once stopped");

    // the picture
    px_vview_t v[PX_VOICES_MAX];
    start(c); run_bars(c, 0, 1, false);
    int nv = c->view(v, PX_VOICES_MAX), live = 0;
    for (int i = 0; i < nv; i++) if (v[i].kind != VW_OFF) live++;
    check(nv == c->voices && nv >= 1 && nv <= PX_VOICES_MAX && live >= 1, "view: %d voices, %d sounding at the end of a bar", nv, live);
}

int main(int argc, char **argv)
{
    obuf = malloc(sizeof(float) * OUT_MAX);
    player = px_player_new();
    char err[96];
    ctl_defaults();
    if (!px_song_parse(&song, TUNE, ctl.key, ctl.scale, err, sizeof err)) { printf("the bench tune: %s\n", err); return 2; }
    json = fopen("out/results.json", "w");
    if (json) fprintf(json, "{\n  \"results\": [\n");
    for (int i = 0; i < CON_N; i++) PX_CONSOLES[i]->init();
    int ran = 0;
    for (int i = 0; i < CON_N; i++) {
        const px_console_t *c = PX_CONSOLES[i];
        bool want = argc < 2;
        for (int a = 1; a < argc; a++) {
            char nm[16]; int k = 0;
            for (const char *s = c->name; *s && k < 15; s++) if (*s != ' ') nm[k++] = *s;
            nm[k] = 0;
            if (!strcasecmp(argv[a], nm) || !strcasecmp(argv[a], c->name)) want = true;
        }
        if (!want) continue;
        bench(c); ran++;
    }
    // the tour: two bars on each console, the song running on
    cur_console = "tour";
    on = 0; tick_acc = 0; ctl_defaults(); px_player_reset(player);
    for (int i = 0; i < CON_N; i++) {
        PX_CONSOLES[i]->reset(); px_player_restart(player);
        run_bars(PX_CONSOLES[i], i * 2, 2, false);
    }
    run_ticks(PX_CONSOLES[CON_N - 1], 30);
    wav("tour", "", 0, on);
    if (json) { fprintf(json, "    {\"checks\": %d, \"failed\": %d}\n  ]\n}\n", n_checks, n_failed); fclose(json); }
    printf("\n%d consoles, %d checks, %d failed\n", ran, n_checks, n_failed);
    return n_failed ? 1 : 0;
}
