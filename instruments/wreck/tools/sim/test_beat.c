// BEAT's own checks (inst_beat.c): the chopper, measured. Most run BEAT alone, offline: beat_process() over a grid
// and hands the test sets, on a break of the test's own (beat_test_break) built to be measured: a tone of its own
// pitch per slice, a DC level, a long tone, a chirp. Then the grid's editor through the controls, the sounds, the
// zones on the screen, and the cost.
#include "sim.h"
#include "wreck.h"
#include "params.h"
#include "front.h"
#include "ui.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// from inst_beat.c (tools/sim only)
extern const brk_t *beat_test_break;
typedef struct { uint32_t frame; int16_t slice; int8_t lane, rev; uint8_t n; float rate, lvl; } beat_trig_t;
extern beat_trig_t beat_tlog[16384];
extern int beat_tlog_n;
extern double beat_life_sum;
extern int beat_life_n;
extern int beat_steal_n; extern float beat_steal_env;
int  beat_test_cursor(void);
uint32_t beat_test_cell(int k);
void beat_test_cell_put(int k, uint32_t c);
bool beat_test_stopped(void);
void front_midi(uint8_t st, uint8_t d1, uint8_t d2);

static int tag(const char *t) { int p = param_by_tag(t); if (p < 0) note("no param '%s'", t); return p; }
static void set(const char *t, float v) { int p = tag(t); if (p >= 0) param_set(p, v); }
static float get(const char *t) { int p = tag(t); return p >= 0 ? pv[p] : 0.0f; }
static void quiet(void) { SI.mic = NULL; SI.mic_u = NULL; SI.jx = SI.jy = 0; SI.joy = true; SI.imu = true; sim_tilt(0, 0); }

// ---------------------------------------------------------------- test breaks
#define TB_SIX  4000                                  // 120 BPM: a sixteenth is 4000 frames
#define TB_LEN  (16 * TB_SIX)
static int16_t tb_pcm[TB_LEN];
static brk_t TB;
static uint32_t tb_serial = 1000;
enum { TB_TONES, TB_DC, TB_LONG, TB_CHIRP, TB_NOISE };
static float tone_hz(int j) { return 1000.0f * powf(1.09f, (float)j); }
static void tb_make(int what)
{
    memset(tb_pcm, 0, sizeof tb_pcm);
    uint32_t r = 12345;
    for (int j = 0; j < 16; j++) {
        for (int i = 0; i < TB_SIX; i++) {
            float t = (float)i / AUDIO_SR, x = 0;
            switch (what) {
            case TB_TONES: x = 0.5f * sinf(6.2831853f * tone_hz(j) * t) * expf(-t / 0.005f); break;
            case TB_DC:    x = 0.5f; break;
            case TB_LONG:  x = 0.5f * sinf(6.2831853f * 440.0f * t); break;
            case TB_CHIRP: { float f0 = 300.0f, k = 2500.0f / 0.1f; x = 0.5f * sinf(6.2831853f * (f0 * t + 0.5f * k * t * t)) * (i < 3600 ? (float)i / 3600.0f : (float)(TB_SIX - i) / 400.0f); break; }
            case TB_NOISE: { r = r * 1664525u + 1013904223u; x = 0.5f * ((float)(int32_t)r / 2147483648.0f) * expf(-t / 0.04f); break; }
            }
            tb_pcm[j * TB_SIX + i] = (int16_t)(x * 32767.0f);
        }
        TB.slice[j] = j * TB_SIX; TB.step[j] = (uint8_t)j;
        TB.kind[j] = (uint8_t)(j % 4 == 0 ? KS_KICK : j % 4 == 2 ? KS_SNARE : KS_HAT);
        TB.peak[j] = (uint8_t)(j % 2 == 0 ? 200 : 120);
    }
    TB.pcm = tb_pcm; TB.len = TB_LEN; TB.steps = 16; TB.bpm = 120.0f; TB.nslices = 16; TB.serial = ++tb_serial; TB.name = "TEST";
    beat_test_break = &TB;
}

// ---------------------------------------------------------------- BEAT alone, offline
static float *R;                                     // what BEAT played (dry)
static int Rn, Rcap;
static uint32_t rf = 0x10000000u;                    // the offline frame count (far from the sim's)
static grid_t keep_grid;
static perf_t keep_perf;
static void grid_q(int q)
{
    __atomic_add_fetch(&wr_grid_seq, 1u, __ATOMIC_ACQ_REL);
    wr_grid.feel_q = q; wr_grid.fpb = 19200; wr_grid.origin = 0;
    __atomic_add_fetch(&wr_grid_seq, 1u, __ATOMIC_ACQ_REL);
}
static void offline_begin(void)
{
    wr_grid_get(&keep_grid); memcpy(&keep_perf, (const void *)&perf, sizeof keep_perf);
    perf.chaos = perf.drag = perf.smear = perf.stutter = perf.roll = perf.dive = perf.crush = perf.drive = 0;
    perf.running = true; perf.axis = 0.0f; perf.zone = Z_BREAKCORE;
}
static void offline_end(void)
{
    __atomic_add_fetch(&wr_grid_seq, 1u, __ATOMIC_ACQ_REL);
    wr_grid.feel_q = keep_grid.feel_q; wr_grid.fpb = keep_grid.fpb; wr_grid.origin = keep_grid.origin;
    __atomic_add_fetch(&wr_grid_seq, 1u, __ATOMIC_ACQ_REL);
    memcpy((void *)&perf, &keep_perf, sizeof keep_perf);
    beat_test_break = NULL;
}
static void zone_to(float axis)
{
    perf.axis = axis; perf.zone = axis < 0.25f ? Z_BREAKCORE : axis < 0.5f ? Z_HARDCORE : axis < 0.75f ? Z_SLUDGE : Z_DRONE;
    grid_q(1 - perf.zone);
}
static void render(float secs)
{
    int n = (int)(secs * AUDIO_SR) / AUDIO_BLOCK * AUDIO_BLOCK;
    if (Rn + n > Rcap) { Rcap = Rn + n + AUDIO_SR; R = realloc(R, (size_t)Rcap * sizeof(float)); }
    for (int b = 0; b < n; b += AUDIO_BLOCK) {
        float o[AUDIO_BLOCK] = { 0 }, s[AUDIO_BLOCK] = { 0 };
        beat_process(o, s, AUDIO_BLOCK, rf);
        memcpy(R + Rn, o, sizeof o);
        Rn += AUDIO_BLOCK; rf += AUDIO_BLOCK;
    }
}
// a fresh start on a bar line: what plays fades, the log and the buffer are emptied, the frame moves to the next bar
static void fresh(void)
{
    bool run = perf.running;
    perf.running = false; render(0.05f); perf.running = run;
    grid_t g; wr_grid_get(&g);
    int32_t st = wr_step_at(&g, rf);
    int32_t next = (st / 64 + 1) * 64;                 // a 4/4 phrase's start: every pattern starts there too
    rf = (uint32_t)wr_step_start(&g, next);
    Rn = 0; beat_tlog_n = 0; beat_life_sum = 0; beat_life_n = 0;
}
static uint32_t r_frame(int i) { return rf - (uint32_t)Rn + (uint32_t)i; }   // the frame of R[i]
static int r_index(uint32_t f) { return (int)(f - (rf - (uint32_t)Rn)); }

static void wav_write(const char *name, const float *x, int n)
{
    char fn[256]; snprintf(fn, sizeof fn, "out/%s-%s.wav", sim_name, name);
    FILE *f = fopen(fn, "wb"); if (!f) return;
    uint32_t sr = AUDIO_SR, br = sr * 2, ds = (uint32_t)n * 2, rs = 36 + ds, fl = 16; uint16_t one = 1, bits = 16, ba = 2;
    fwrite("RIFF", 1, 4, f); fwrite(&rs, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fl, 4, 1, f); fwrite(&one, 2, 1, f);
    fwrite(&one, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&ds, 4, 1, f);
    for (int i = 0; i < n; i++) { float v = x[i]; v = v > 1 ? 1 : v < -1 ? -1 : v; int16_t q = (int16_t)(v * 32767.0f); fwrite(&q, 2, 1, f); }
    fclose(f);
}

// onsets: where the signal comes up out of at least `gap` frames of near silence
static int onsets(const float *x, int n, float thr, int gap, int *at, int max)
{
    int k = 0, quiet = gap;
    for (int i = 0; i < n && k < max; i++) {
        float a = fabsf(x[i]);
        if (a > thr) { if (quiet >= gap) at[k++] = i; quiet = 0; }
        else if (a < thr * 0.25f) quiet++;
    }
    return k;
}
// the pitch of a short burst from its zero crossings (interpolated), Hz
static float burst_hz(const float *x, int n)
{
    float first = -1, last = -1; int cross = 0;
    for (int i = 1; i < n; i++) {
        if ((x[i - 1] < 0) != (x[i] < 0) && (fabsf(x[i]) > 1e-6f || fabsf(x[i - 1]) > 1e-6f)) {
            float t = (float)(i - 1) + x[i - 1] / (x[i - 1] - x[i]);
            if (first < 0) first = t;
            last = t; cross++;
        }
    }
    return cross >= 3 && last > first ? (float)(cross - 1) * 0.5f * AUDIO_SR / (last - first) : 0.0f;
}
static int which_tone(float hz)                       // the test break's slice with that pitch, or -1
{
    int best = -1; float be = 1e9f;
    for (int j = 0; j < 16; j++) { float d = fabsf(logf(hz / tone_hz(j))); if (d < be) { be = d; best = j; } }
    return be < 0.035f ? best : -1;
}
static void set_plain(void)                           // AUTO, nothing added: the break as it is
{
    set("bpat", 0); set("bedt", 0); set("brol", 0); set("brev", 0); set("bdri", 0); set("bbla", 0); set("bgab", 0);
    set("bodd", 0); set("bswg", 0); set("bpit", 0); set("bspd", 0); set("blvl", 80);
}

// ---------------------------------------------------------------- 1. the break played straight, at every feel
// Each onset heard is on a step's start (within 1.5 ms: the fade in) and is that step's slice (told by its pitch).
static void straight(void)
{
    section("beat: the break played straight");
    offline_begin(); tb_make(TB_TONES); set_plain();
    static const char *const qn[4] = { "x2", "x1", "x1/2", "x1/4" };
    int bad_total = 0;
    for (int speed = 0; speed < 2; speed++) {
        set("bspd", (float)speed);
        for (int q = 1; q >= -2; q--) {
            grid_q(q); fresh();
            grid_t g; wr_grid_get(&g);
            float step_s = wr_step_frames(&g) / AUDIO_SR;
            render(step_s * 20.0f);
            float rate = speed ? (float)TB_SIX / wr_step_frames(&g) : 1.0f;
            static int at[512];
            int no = onsets(R, Rn, 0.01f, 160, at, 512), off = 0, wrong = 0, steps = 0;
            for (int k = 0; k < no; k++) {
                uint32_t f = r_frame(at[k]);
                int32_t st = wr_step_at(&g, f + 48);              // the step it falls in (it may lead by a sample or two)
                int32_t d = (int32_t)(f - (uint32_t)wr_step_start(&g, st));
                if (d < -2 || d > 48) off++;
                int w = (int)(0.006f / rate * AUDIO_SR);
                if (at[k] + w > Rn) w = Rn - at[k];
                int j = which_tone(burst_hz(R + at[k] + 2, w) / rate);
                if (j != (int)(st % 16)) wrong++;
            }
            steps = (int)(wr_step_at(&g, rf - 1) - wr_step_at(&g, r_frame(0))) + 1;
            bool ok = off == 0 && wrong == 0 && no >= steps - 1 && no <= steps;
            if (!ok) { bad_total++; note("%s %s: %d onsets for %d steps, %d off the grid, %d the wrong slice", speed ? "TIED" : "SLICE", qn[1 - q], no, steps, off, wrong); }
        }
    }
    check(bad_total == 0, "each slice starts on its own step, SLICE and TIED, at x2, x1, x1/2 and x1/4 (%d of 8 wrong)", bad_total);
    offline_end();
}

// ---------------------------------------------------------------- 2. rolls: N onsets in the step
static void rolls(void)
{
    section("beat: rolls");
    offline_begin(); tb_make(TB_TONES); set_plain();
    set("bpat", 1);
    static const int tr[7] = { 2, 3, 4, 5, 6, 7, 0 }, want[7] = { 2, 3, 4, 8, 16, 64, 1 };   // T_X2.. T_X64, then plain
    for (int k = 0; k < 16; k++) beat_test_cell_put(k, k < 7 ? (uint32_t)(2 | (tr[k] << 6)) : 1u);
    grid_q(-2); fresh();
    grid_t g; wr_grid_get(&g);
    render(wr_step_frames(&g) * 16.0f / AUDIO_SR);
    int bad = 0;
    char got[96]; int w = 0;
    for (int k = 0; k < 7; k++) {
        uint32_t s0 = (uint32_t)wr_step_start(&g, wr_step_at(&g, r_frame(0)) + k), s1 = s0 + (uint32_t)wr_step_frames(&g);
        int a = r_index(s0), b = r_index(s1), n = 0;
        if (want[k] <= 16) { static int at[128]; n = onsets(R + a, b - a, 0.01f, 120, at, 128); }
        else for (int i = 0; i < beat_tlog_n; i++) if (beat_tlog[i].lane == 0 && beat_tlog[i].frame >= s0 && beat_tlog[i].frame < s1) n++;
        if (n != want[k]) bad++;
        w += snprintf(got + w, sizeof got - (size_t)w, "%s%d", k ? " " : "", n);
    }
    check(bad == 0, "a roll of 2, 3, 4, 8, 16 is that many onsets in its step, 64 that many hits; a plain step one (%s)", got);
    for (int k = 0; k < 16; k++) beat_test_cell_put(k, 0);
    set("bpat", 0);
    offline_end();
}

// ---------------------------------------------------------------- 3. reversed: the slice backwards
static void reverse(void)
{
    section("beat: reversed");
    offline_begin(); tb_make(TB_CHIRP); set_plain();
    set("bpat", 1);
    for (int k = 0; k < 16; k++) beat_test_cell_put(k, 1u);       // all off ...
    beat_test_cell_put(0, (uint32_t)(2 + 3) | (1u << 6));       // ... but step 1: slice 4, reversed
    grid_q(-2); fresh();
    render(1.0f);
    int a = beat_tlog_n ? r_index(beat_tlog[0].frame) : 0;      // where the hit started
    double xr = 0, xf = 0, e0 = 0, er = 0;
    const int16_t *s = tb_pcm + 3 * TB_SIX;
    for (int i = 64; i < TB_SIX - 160 && a + i < Rn; i++) {
        double y = R[a + i], r = s[TB_SIX - 1 - i] / 32768.0, f = s[i] / 32768.0;
        xr += y * r; xf += y * f; e0 += y * y; er += r * r;
    }
    double cr = xr / sqrt(e0 * er + 1e-12), cf = fabs(xf) / sqrt(e0 * er + 1e-12);
    check(cr > 0.97 && cf < 0.3, "a reversed slice is the slice backwards (correlation %.3f; forwards %.3f)", cr, cf);
    for (int k = 0; k < 16; k++) beat_test_cell_put(k, 0);
    set("bpat", 0);
    offline_end();
}

// ---------------------------------------------------------------- 4. pitch: +12 doubles a tone, -12 halves it
static void pitch(void)
{
    section("beat: pitch");
    offline_begin(); tb_make(TB_LONG); set_plain();
    float hz[3]; static const float semi[3] = { 0, 12, -12 };
    for (int i = 0; i < 3; i++) {
        set("bpit", semi[i]); grid_q(-2); fresh();
        render(0.12f);
        hz[i] = m_pitch(R + 800, 1800, 100.0f, 2000.0f);
    }
    check(fabsf(hz[0] - 440.0f) < 3.0f && fabsf(hz[1] / hz[0] - 2.0f) < 0.01f && fabsf(hz[2] / hz[0] - 0.5f) < 0.005f,
          "PITCH +12 doubles a tone, -12 halves it (%.1f, %.1f, %.1f Hz)", hz[0], hz[1], hz[2]);
    set("bpit", 0);
    offline_end();
}

static int brk_step_test(int k) { return k % 16; }              // the test break: one bar, a slice on every sixteenth
static int M_at_test(int s) { return s; }
// ---------------------------------------------------------------- 5. 7/8: the pattern repeats every 14 steps
static void odd_bars(void)
{
    section("beat: odd bars");
    offline_begin(); tb_make(TB_TONES); set_plain();
    static const int lens[3] = { 14, 10, 18 };
    int ok = 0;
    for (int o = 1; o <= 3; o++) {
        set("bodd", (float)o); grid_q(1); fresh();
        grid_t g; wr_grid_get(&g);
        render(wr_step_frames(&g) * 72.0f / AUDIO_SR);
        int32_t st0 = wr_step_at(&g, r_frame(0));
        static int id[96];
        for (int k = 0; k < 96; k++) id[k] = -1;
        for (int i = 0; i < beat_tlog_n; i++) {
            int32_t st = wr_step_at(&g, beat_tlog[i].frame) - st0;
            if (st >= 0 && st < 96 && beat_tlog[i].lane == 0) id[st] = beat_tlog[i].slice;
        }
        int L = lens[o - 1], same = 0, diff16 = 0, n = 0;
        for (int k = 0; k + L < 70; k++) { n++; if (id[k] == id[k + L] && id[k] >= 0) same++; if (id[k] != id[k + 16]) diff16++; }
        beat_view_t v; beat_view(&v);
        if (same == n && diff16 > 0 && v.steps == L) ok++;
        else note("%s: %d of %d steps repeat after %d, %d differ after 16, the view says %d steps", o == 1 ? "7/8" : o == 2 ? "5/8" : "9/8", same, n, L, diff16, v.steps);
    }
    check(ok == 3, "ODD BARS 7/8 repeats every 14 steps, 5/8 every 10, 9/8 every 18, against the 4/4 (%d of 3)", ok);
    // AUTO: some phrases odd at BREAKCORE (more with EDITS), none towards doom; each phrase starts on the flock's bar
    {   // beat_kicks() in 7/8: bit (step & 31) for the kicks of the bar now
        set("bodd", 1); grid_q(1); fresh();
        grid_t g; wr_grid_get(&g);
        int bad = 0;
        for (int b = 0; b < 6; b++) {
            render(wr_step_frames(&g) * 14.0f / AUDIO_SR);
            int32_t b0 = beat_bar_start();
            for (int k = 0; k < beat_bar_len(); k++) {
                int j = M_at_test(brk_step_test(k));
                bool kick = j >= 0 && TB.kind[j] == KS_KICK;
                if (((beat_kicks() >> ((b0 + k) & 31)) & 1u) != (kick ? 1u : 0u)) bad++;
            }
        }
        check(bad == 0 && beat_bar_len() == 14, "in 7/8 the kicks are mapped onto the machine's step numbers (%d wrong)", bad);
    }
    set("bodd", 4); set("bedt", 100);
    int odd_bc = 0, odd_sl = 0, off_bar = 0;
    for (int z = 0; z < 2; z++) {
        zone_to(z ? 0.62f : 0.05f); fresh();
        grid_t g; wr_grid_get(&g);
        for (int ph = 0; ph < 24; ph++) {
            render(wr_step_frames(&g) * 64.0f / AUDIO_SR);
            if (beat_bar_len() != 16 && beat_bar_len() != 64 - (64 / 16) * 16) { if (z) odd_sl++; else odd_bc++; }
            if ((beat_bar_start() + beat_bar_len()) % 64 != 0) off_bar++;     // the phrase's last bar ends on the flock's
        }
    }
    check(odd_bc >= 3 && odd_bc <= 18 && odd_sl == 0 && off_bar == 0, "ODD BARS AUTO: %d of 24 phrases end in an odd bar at BREAKCORE, %d at SLUDGE; "
          "every phrase lands on the flock's bar (%d not)", odd_bc, odd_sl, off_bar);
    set("bedt", 0);
    set("bodd", 4);
    offline_end();
}

// ---------------------------------------------------------------- 6. the stutter repeats, shorter the further
static float autocorr(const float *x, int n, int lag)
{
    double c = 0, e0 = 0, e1 = 0;
    for (int i = 0; i + lag < n; i++) { c += (double)x[i] * x[i + lag]; e0 += (double)x[i] * x[i]; e1 += (double)x[i + lag] * x[i + lag]; }
    return (float)(c / sqrt(e0 * e1 + 1e-12));
}
static void stutter(void)
{
    section("beat: the stutter");
    offline_begin(); tb_make(TB_NOISE); set_plain();
    grid_q(1); fresh();
    grid_t g; wr_grid_get(&g);
    float sf = wr_step_frames(&g);
    render(0.3f);
    float c_none = autocorr(R + Rn - 8000, 8000, (int)(sf / 4));
    int ok = 0; char got[64]; int w = 0;
    static const float amt[2] = { 0.5f, 0.95f };
    static const int div[2] = { 4, 16 };
    for (int k = 0; k < 2; k++) {
        perf.stutter = amt[k];
        render(0.3f);
        int lag = (int)(sf / (float)div[k] + 0.5f);
        float c = autocorr(R + Rn - 6000, 6000, lag);
        w += snprintf(got + w, sizeof got - (size_t)w, "%s%.2f", k ? ", " : "", c);
        if (c > 0.9f) ok++;
        perf.stutter = 0; render(0.2f);
    }
    check(ok == 2 && c_none < 0.5f, "the stutter repeats the last bit: a quarter step, then a sixteenth at full (correlation %s; without it %.2f)", got, c_none);
    offline_end();
}

// ---------------------------------------------------------------- 7. a knock's fill: denser, of the zone's kind
static int hits_between(uint32_t a, uint32_t b) { int n = 0; for (int i = 0; i < beat_tlog_n; i++) if (beat_tlog[i].frame >= a && beat_tlog[i].frame < b) n++; return n; }
static void fills(void)
{
    section("beat: fills");
    offline_begin(); set_plain();
    static const float ax[4] = { 0.1f, 0.37f, 0.62f, 0.88f };
    int ok = 0; char got[96]; int w = 0;
    for (int z = 0; z < 4; z++) {
        zone_to(ax[z]); fresh();
        grid_t g; wr_grid_get(&g);
        float sf = wr_step_frames(&g);
        render(sf * 16.0f / AUDIO_SR);                          // a bar without
        uint32_t a0 = r_frame(0);
        beat_fill(1.0f);
        render(sf * 16.0f / AUDIO_SR);                          // a bar with the fill
        uint32_t len = (uint32_t)(sf * 6.0f);
        int without = hits_between(a0, a0 + len), with = hits_between(a0 + (uint32_t)(sf * 16.0f), a0 + (uint32_t)(sf * 16.0f) + len);
        w += snprintf(got + w, sizeof got - (size_t)w, "%s%s %d>%d", z ? ", " : "", host_zone_name(z), with, without);
        if (with > without * 1.4f && with > without + 1) ok++;
    }
    check(ok == 4, "a knock is a fill, denser than the bar before it, in every zone (hits in 6 steps: %s)", got);
    // harder is longer
    zone_to(0.1f); fresh();
    grid_t g; wr_grid_get(&g);
    float sf = wr_step_frames(&g);
    beat_fill(0.0f); render(sf * 16.0f / AUDIO_SR);
    int soft = 0; for (int i = 0; i < beat_tlog_n; i++) if (beat_tlog[i].n > 1) soft++;
    fresh(); beat_fill(1.0f); render(sf * 16.0f / AUDIO_SR);
    int hard = 0; for (int i = 0; i < beat_tlog_n; i++) if (beat_tlog[i].n > 1) hard++;
    check(hard > soft * 2, "a harder knock is a longer fill (rolled hits: %d soft, %d hard)", soft, hard);
    offline_end();
}

// ---------------------------------------------------------------- 8. the gabber kick on every beat at HARDCORE
static float lowband_energy(const float *x, int n)
{
    biquad_t q; memset(&q, 0, sizeof q); biquad_set(&q, BQ_LP, 150.0f, 0.707f, 0.0f);
    double e = 0;
    for (int i = 0; i < n; i++) { float y = biquad_run(&q, x[i]); e += (double)y * y; }
    return (float)(e / (n > 0 ? n : 1));
}
static void gabber(void)
{
    section("beat: the gabber kick");
    offline_begin(); tb_make(TB_TONES); set_plain();
    set("bgab", 2); set("bedt", 50);
    zone_to(0.37f); fresh();
    grid_t g; wr_grid_get(&g);
    float beat = wr_step_frames(&g) * 4.0f;
    render(beat * 8.0f / AUDIO_SR);
    // the low band (the test break has none of its own) in the first fifth of each beat, and in its last fifth
    int32_t st0 = wr_step_at(&g, r_frame(0));
    float on_min = 1e9f, on_sum = 0, off_max = 0;
    for (int b = 0; b < 8; b++) {
        int a = r_index((uint32_t)wr_step_start(&g, st0 + b * 4));
        float on = lowband_energy(R + a, (int)(beat * 0.2f)), off = lowband_energy(R + a + (int)(beat * 0.86f), (int)(beat * 0.12f));
        if (on < on_min) on_min = on;
        on_sum += on;
        if (off > off_max) off_max = off;
    }
    // beat_kicks(): the bar's kicks for the AMP, as they were played (the gabber's beats and the break's kick slices)
    const brk_t *bk = beat_test_break;
    int32_t b0 = beat_bar_start();
    uint32_t want = 0;
    uint32_t mask = 0;
    for (int k = 0; k < beat_bar_len(); k++) mask |= 1u << ((b0 + k) & 31);
    for (int i = 0; i < beat_tlog_n; i++) {
        int32_t st = wr_step_at(&g, beat_tlog[i].frame);
        if (st >= b0 && st < b0 + beat_bar_len() && beat_tlog[i].slice >= 0 && bk->kind[beat_tlog[i].slice] == KS_KICK) want |= 1u << (st & 31);
    }
    for (int k = 0; k < beat_bar_len(); k++) if ((b0 + k) % 4 == 0) want |= 1u << ((b0 + k) & 31);
    uint32_t got = beat_kicks() & mask;
    bool kicks_ok = got == want && beat_bar_len() == 16;
    // and AUTO is only at HARDCORE: none at BREAKCORE
    zone_to(0.1f); fresh();
    wr_grid_get(&g);
    render(1.0f);
    float bc = lowband_energy(R, Rn);
    check(on_min > 0.3f * on_sum / 8 && on_min > 20.0f * off_max && on_min > 1e-3f && bc < 1e-3f * on_min,
          "GABBER KICK AUTO: a kick on every beat at HARDCORE (low band on the beats %.2g at least, %.2g at the beats' ends); none at BREAKCORE (%.2g)",
          on_min, off_max, bc);
    check(kicks_ok, "beat_kicks(): bit (step & 31) for each kick of the bar now: every beat and the break's kicks (%08x, played %08x)", (unsigned)got, (unsigned)want);
    wav_write("beat-gabber", R, Rn);
    set("bgab", 2);
    offline_end();
}

// ---------------------------------------------------------------- 9. towards doom: fewer, longer hits
static void doom(void)
{
    section("beat: towards doom");
    offline_begin(); set("bpat", 0); set("bedt", 50); set("brol", 50); set("bodd", 4); set("bspd", 0); set("bgab", 0);
    static const float ax[4] = { 0.1f, 0.37f, 0.62f, 0.88f };
    float rate[4], life[4];
    char got[160]; int w = 0;
    for (int z = 0; z < 4; z++) {
        zone_to(ax[z]); fresh();
        render(24.0f);
        int nh = 0; for (int i = 0; i < beat_tlog_n; i++) if (beat_tlog[i].lane == 0) nh++;
        rate[z] = (float)nh / 24.0f;
        life[z] = beat_life_n ? (float)(beat_life_sum / beat_life_n) / AUDIO_SR * 1000.0f : 0.0f;
        w += snprintf(got + w, sizeof got - (size_t)w, "%s%s %.1f/s %.0f ms", z ? ", " : "", host_zone_name(z), rate[z], life[z]);
        char nm[32]; snprintf(nm, sizeof nm, "beat-dry-%s", host_zone_name(z));
        for (char *c = nm; *c; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c - 'A' + 'a');
        wav_write(nm, R, Rn > AUDIO_SR * 12 ? AUDIO_SR * 12 : Rn);
    }
    check(rate[0] > rate[1] && rate[1] > rate[2] && rate[2] > rate[3] && life[2] > 2.0f * life[0] && life[3] > life[2] && rate[3] > 0.1f,
          "towards DRONE fewer hits, each longer (%s)", got);
    offline_end();
}

// ---------------------------------------------------------------- 10. no clicks: every start and cut is a fade
// On a break of DC slices what plays is only the envelopes: a cut with no fade would jump by the whole level (0.4),
// a 1 ms fade moves 1/32 of it a sample.
static void clicks(void)
{
    section("beat: no clicks at starts and cuts");
    offline_begin(); tb_make(TB_DC); set_plain(); set("blvl", 100);
    set("bpat", 1);
    static const int tr[16] = { 0, 1, 2, 4, 13, 7, 14, 11, 0, 15, 6, 12, 3, 5, 8, 9 };
    for (int k = 0; k < 16; k++) beat_test_cell_put(k, k == 8 ? 1u : (uint32_t)((2 + (k * 5) % 16) | (tr[k] << 6)));
    float worst = 0; int nh = 0;
    beat_steal_n = 0; beat_steal_env = 0;
    for (int pass = 0; pass < 4; pass++) {
        set("bspd", (float)(pass & 1)); set("bpat", pass < 2 ? 1.0f : 0.0f); set("bedt", 100); set("brol", 100); set("brev", 100); set("bdri", 100);
        grid_q(pass < 2 ? 1 : -1); fresh();
        render(1.0f);
        perf.roll = 0.9f; render(0.4f); perf.roll = 0;
        perf.stutter = 0.95f; render(0.4f); perf.stutter = 0.4f; render(0.3f); perf.stutter = 0;
        perf.smear = 0.8f; perf.chaos = 1.0f; render(0.6f); perf.smear = 0; perf.chaos = 0;
        beat_fill(1.0f); render(1.0f);
        perf.running = false; render(0.1f); perf.running = true; render(0.3f);
        for (int i = 1; i < Rn; i++) { float d = fabsf(R[i] - R[i - 1]); if (d > worst) worst = d; }
        nh += beat_tlog_n;
    }
    float lvl = 0.5f;                                              // the DC's level at LEVEL 100
    check(worst < lvl * 2.0f / 32.0f, "%d hits (rolls, cuts, reversed, stops, rests, stutters, fills, a stop): the biggest step "
          "between two samples is %.4f, 1/%.0f of a hit (a cut with no fade: 1/1)", nh, worst, lvl / worst);
    note("voices stolen while still sounding: %d (the loudest at %.4f of full scale)", beat_steal_n, (double)(beat_steal_env * 32768.0f));
    for (int k = 0; k < 16; k++) beat_test_cell_put(k, 0);
    set_plain(); set("bedt", 50); set("brol", 50); set("brev", 30); set("bdri", 20); set("bodd", 4); set("bgab", 2); set("bbla", 30);
    offline_end();
}

// ---------------------------------------------------------------- 11. the grid's editor, through the controls
static void editor(void)
{
    section("beat: the step grid");
    quiet(); set("bpat", 0); set("bodd", 0); set("xaxs", 10); sim_run(1.0f);
    sim_gif_begin("beat-grid");
    float ax0 = get("xaxs");
    sim_hold_knob(); sim_run(0.4f);
    check(beat_editing() && get("bpat") == 1.0f && (meta.learned >> (LEARN_INST + 8)) & 1u,
          "the knob held opens the grid, PATTERN goes to MINE, the hint is marked learned");
    sim_shot("beat_grid_open");
    sim_run(1.0f);
    sim_shot("beat_grid_hint");
    sim_turn(1); sim_run(0.2f); sim_turn(1); sim_run(0.3f);
    check(beat_test_cursor() == 2 && get("xaxs") == ax0, "turning moves the step (%d), not the axis", beat_test_cursor());
    uint32_t c0 = beat_test_cell(2);
    SI.pressed = true; sim_tick(); sim_turn(1); sim_run(0.1f); sim_turn(1); sim_run(0.1f); sim_turn(1); sim_run(0.2f); SI.pressed = false; sim_run(0.2f);
    uint32_t c1 = beat_test_cell(2);
    check((c0 & 63) == 0 && (c1 & 63) == 4 && beat_editing(), "press and turn: the step's slice (the break's own, then slice 1, 2, 3: %u)", (unsigned)(c1 & 63) - 1);
    sim_tap(); sim_run(0.3f); sim_tap(); sim_run(0.3f); sim_tap(); sim_run(0.3f);
    check(((beat_test_cell(2) >> 6) & 15) == 3, "a tap is the next trick (three taps: ROLL X3)");
    sim_shot("beat_grid_edit");
    SI.jx = 1.0f; sim_tick(); SI.jx = 0; sim_run(0.2f);
    SI.jy = 1.0f; sim_tick(); SI.jy = 0; sim_run(0.2f);
    check(beat_test_cursor() == 3 && ((beat_test_cell(3) >> 6) & 15) == 1, "the stick: right, the next step; up, its trick");
    // what was drawn plays: step 3 of every bar is slice 3, rolled three times
    beat_tlog_n = 0; sim_run(3.0f);
    grid_t g; wr_grid_get(&g);
    int good = 0, bad = 0;
    for (int i = 0; i < beat_tlog_n; i++) {
        if (beat_tlog[i].lane) continue;
        int32_t st = wr_step_at(&g, beat_tlog[i].frame) % 16;
        if (st == 2) { if (beat_tlog[i].slice == 2 && beat_tlog[i].n == 3) good++; else bad++; }
    }
    check(good >= 3 && bad == 0, "the grid plays what was drawn (step 3: slice 3 rolled three times, %d hits, %d others)", good, bad);
    sim_rec(0.1f); sim_run(0.1f);
    bool st1 = beat_test_stopped();
    beat_tlog_n = 0; sim_run(1.0f);
    int while_stopped = beat_tlog_n;
    SI.pressed = true; sim_tick(); sim_turn(1); sim_run(0.2f); SI.pressed = false; sim_run(0.3f);   // stopped: what is picked is heard
    int heard = beat_tlog_n;
    SI.pressed = true; sim_tick(); sim_turn(-1); sim_run(0.1f); SI.pressed = false; sim_run(0.2f);
    sim_rec(0.1f); beat_tlog_n = 0; sim_run(1.0f);
    check(st1 && !beat_test_stopped() && while_stopped == 0 && heard >= 1 && beat_tlog_n > 4,
          "REC tapped stops the grid (%d hits in a second; a slice picked is heard: %d), again runs it (%d)", while_stopped, heard, beat_tlog_n);
    // a sound saved and loaded keeps the grid
    uint32_t want[16]; for (int k = 0; k < 16; k++) want[k] = beat_test_cell(k);
    static uint8_t blob[4096];
    int n = params_pack(blob, sizeof blob, true);
    params_defaults(true);
    int cleared = 0; for (int k = 0; k < 16; k++) if (beat_test_cell(k) == 0) cleared++;
    params_unpack(blob, n, true);
    int same = 0; for (int k = 0; k < 16; k++) if (beat_test_cell(k) == want[k]) same++;
    check(cleared == 16 && same == 16, "the grid is in the sound: saved, cleared, loaded, all 16 steps back (%d)", same);
    if (!beat_editing()) { sim_hold_knob(); sim_run(0.3f); }
    SI.rec = true; sim_run(1.1f); SI.rec = false; sim_run(0.3f);
    int brk = 0; for (int k = 0; k < 16; k++) if (beat_test_cell(k) == 0) brk++;
    check(brk == 16, "REC held 1 s: every step back to the break's own (%d of 16)", brk);
    sim_shot("beat_grid_cleared");
    sim_hold_knob(); sim_run(0.3f);
    sim_turn(1); sim_run(0.2f);
    check(!beat_editing() && get("xaxs") != ax0, "held again it closes, and the knob is the axis again (%.0f -> %.0f)", ax0, get("xaxs"));
    sim_gif_end();
    set("xaxs", 10); set("bpat", 0); set("bodd", 4); sim_run(0.5f);
    // the hint fits
    int w1 = oled_ftext_w(F_TINY, "TURN: STEP   PRESS+TURN: SLICE", 0), w2 = oled_ftext_w(F_TINY, "TAP: TRICK   HOLD: DONE", 0);
    check(w1 <= 126 && w2 <= 126, "the grid's hint lines fit (%d, %d px)", w1, w2);
}

// ---------------------------------------------------------------- 12. the zones on the screen and by ear
static void zones_out(void)
{
    section("beat: the zones");
    quiet(); front_midi(0xC0, 0, 0); set("xchg", 1); sim_run(0.3f);
    static const float ax[4] = { 10, 37, 62, 88 };
    static const char *const nm[4] = { "breakcore", "hardcore", "sludge", "drone" };
    for (int z = 0; z < 4; z++) {
        set("xaxs", ax[z]); sim_run(2.0f);
        char b[48]; snprintf(b, sizeof b, "beat-%s", nm[z]);
        sim_gif_begin(b);
        float t0 = sim_t;
        sim_run(z < 2 ? 6.0f : 10.0f);
        sim_gif_end();
        snprintf(b, sizeof b, "-beat-%s", nm[z]);
        sim_wav(b, t0, sim_t);
        snprintf(b, sizeof b, "beat_%s", nm[z]);
        sim_shot(b);
    }
    // each sound's breakcore end
    int ok = 0;
    for (int k = 0; k < INST.npresets; k++) {
        front_midi(0xC0, (uint8_t)k, 0);
        set("xaxs", 10); sim_run(1.0f);
        float t0 = sim_t;
        sim_run(8.0f);
        char b[48]; snprintf(b, sizeof b, "-beat-sound-%s", INST.presets[k].name);
        for (char *c = b; *c; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c - 'A' + 'a');
        sim_wav(b, t0, sim_t);
        if (m_rms(t0, sim_t) > 0.02f) ok++;
    }
    check(ok == INST.npresets, "each sound's breakcore end plays (%d of %d; WAVs out/wreck-beat-sound-*.wav)", ok, INST.npresets);
    front_midi(0xC0, 0, 0); set("xaxs", 10); set("xchg", 0); sim_run(0.5f);
}

// ---------------------------------------------------------------- 13. the cost
static void cost(void)
{
    section("beat: cost");
    offline_begin(); set("bpat", 0); set("bedt", 100); set("brol", 100); set("bdri", 100); set("bgab", 1);
    static const float ax[4] = { 0.1f, 0.37f, 0.62f, 0.88f };
    static const char *const cfg[3] = { "rolls", "stutter", "chaos" };
    float worst = 0; char wz[48] = "";
    for (int z = 0; z < 4; z++) for (int c = 0; c < 3; c++) {
        zone_to(ax[z]); fresh();
        perf.roll = c == 0 ? 1.0f : 0; perf.stutter = c == 1 ? 1.0f : 0; perf.chaos = c == 2 ? 1.0f : 0; perf.smear = c == 2 ? 0.6f : 0;
        perf.dive = 0.5f;
        render(0.5f);
        double best = 1e9;
        for (int run = 0; run < 3; run++) {
            struct timespec a, b;
            float o[AUDIO_BLOCK], s[AUDIO_BLOCK];
            clock_gettime(CLOCK_MONOTONIC, &a);
            for (int k = 0; k < 2000; k++) { memset(o, 0, sizeof o); memset(s, 0, sizeof s); beat_process(o, s, AUDIO_BLOCK, rf); rf += AUDIO_BLOCK; }
            clock_gettime(CLOCK_MONOTONIC, &b);
            double us = ((double)(b.tv_sec - a.tv_sec) * 1e6 + (double)(b.tv_nsec - a.tv_nsec) * 1e-3) / 2000.0;
            if (us < best) best = us;
        }
        if (best > worst) { worst = (float)best; snprintf(wz, sizeof wz, "%s, %s", host_zone_name(z), cfg[c]); }
    }
    metric("beat_us_per_block", worst);
    check(worst < 5.0f, "BEAT's busiest block (%s, the gabber kick on, the dive) takes %.2f us on the host (budget 5)", wz, worst);
    int voices = 12;
    note("memory: no buffers of its own; the voices, the bar's plan and the break's map are about %d bytes of internal RAM",
         (int)(voices * 72 + 32 * 10 + 64 * 22 + 200));
    set("bedt", 50); set("brol", 50); set("bdri", 20); set("bgab", 2);
    offline_end();
}

void test_mod_beat(void)
{
    straight();
    rolls();
    reverse();
    pitch();
    odd_bars();
    stutter();
    fills();
    gabber();
    doom();
    clicks();
    editor();
    zones_out();
    cost();
    quiet(); set("xaxs", 10); sim_run(0.5f);
}
