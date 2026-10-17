// SING's voice engine (firmware/main/inst_voice.c) on its own, against a synthetic singer: a band-limited
// glottal pulse train (an LF-like flow derivative, Fourier series per period) through vowel formants, with
// vibrato, glides, note changes, breath and consonant noise. Every check prints its numbers; WAVs for listening
// go to out-voice/. run_voice.sh builds and runs it and fails if any check fails.
//
// The API is sing.h's as written (no change). Test hooks (inst_voice.c built with -DVOICE_TEST, not part of
// sing.h): voice_test_solo (only that extra voice into extra[]) and voice_test_leff() (the latency now, samples).
#include "sing.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

extern int voice_test_solo;
float voice_test_leff(void);

#define SR AUDIO_SR
#define BLK AUDIO_BLOCK
#define CAP (SR * 40)

// ---------------------------------------------------------------- stand-ins
void *app_alloc(size_t n) { return calloc(1, n); }
void *app_alloc_fast(size_t n) { return calloc(1, n); }
void app_free(void *p) { free(p); }
void app_log(const char *fmt, ...) { va_list ap; va_start(ap, fmt); printf("    [log] "); vprintf(fmt, ap); printf("\n"); va_end(ap); }
float pv[PARAMS_MAX];

// ---------------------------------------------------------------- checks
static int nrun, nfail;
static FILE *summary;
static void check(int ok, const char *fmt, ...)
{
    char b[400]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", b);
    if (summary) fprintf(summary, "%s  %s\n", ok ? "ok  " : "FAIL", b);
    nrun++; if (!ok) nfail++;
}
static void section(const char *s) { printf("\n== %s\n", s); if (summary) fprintf(summary, "\n== %s\n", s); }

// ---------------------------------------------------------------- WAV
static void wav(const char *name, const float *x, int n)
{
    char fn[256]; snprintf(fn, sizeof fn, "out-voice/%s.wav", name);
    FILE *f = fopen(fn, "wb"); if (!f) return;
    int bytes = n * 2;
    unsigned char h[44] = { 'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,0,0,0,0,0,0,0,0,2,0,16,0,'d','a','t','a',0,0,0,0 };
    int v; v = 36 + bytes; memcpy(h + 4, &v, 4); v = SR; memcpy(h + 24, &v, 4); v = SR * 2; memcpy(h + 28, &v, 4); memcpy(h + 40, &bytes, 4);
    fwrite(h, 1, 44, f);
    for (int i = 0; i < n; i++) { float s = x[i] * 32767.0f; if (s > 32767) s = 32767; if (s < -32767) s = -32767; short q = (short)s; fwrite(&q, 2, 1, f); }
    fclose(f);
}

// ---------------------------------------------------------------- the singer
enum { K_VOICED, K_NOISE, K_SILENT };
enum { V_A, V_I, V_U, V_O, V_E, NVOW };
static const double formF[NVOW][4] = { { 730, 1090, 2440, 3300 }, { 270, 2290, 3010, 3500 }, { 300, 870, 2240, 3200 }, { 570, 840, 2410, 3300 }, { 530, 1840, 2480, 3500 } };
static const double formB[NVOW][4] = { { 90, 110, 170, 250 }, { 60, 100, 170, 250 }, { 60, 100, 170, 250 }, { 70, 100, 170, 250 }, { 70, 110, 170, 250 } };
typedef struct { double dur, f0, f1; int vowel, kind; double vib_c, vib_hz, breath, amp; } note_t;

static double gl_re[80], gl_im[80];             // the glottal pulse's Fourier series (per period)
static void glottal_init(void)
{
    // LF-like flow derivative on one period: open 0..0.45 (rising flow), return 0.45..0.6, closed after
    const int N = 4096;
    static double g[4096];
    for (int i = 0; i < N; i++) {
        double t = (double)i / N, u;
        if (t < 0.45) u = 0.5 * (1 - cos(M_PI * t / 0.45));
        else if (t < 0.6) u = cos(0.5 * M_PI * (t - 0.45) / 0.15);
        else u = 0;
        g[i] = u;
    }
    for (int h = 1; h < 80; h++) {
        double re = 0, im = 0;
        for (int i = 0; i < N; i++) { double a = 2 * M_PI * h * i / N; re += g[i] * cos(a); im += g[i] * sin(a); }
        // the derivative of the flow: times j h (the lips radiate the derivative)
        double dre = -h * im * 2.0 / N, dim = h * re * 2.0 / N;
        gl_re[h] = dre; gl_im[h] = dim;
    }
}
typedef struct { double a1, a2, b0, y1, y2; } res_t;
static void res_set(res_t *r, double f, double bw)   // unity gain at DC, as a cascade formant synthesiser has them
{
    double R = exp(-M_PI * bw / SR), w = 2 * M_PI * f / SR;
    r->a1 = 2 * R * cos(w); r->a2 = -R * R; r->b0 = 1 - r->a1 - r->a2;
}
static double res_run(res_t *r, double x) { double y = r->b0 * x + r->a1 * r->y1 + r->a2 * r->y2; r->y2 = r->y1; r->y1 = y; return y; }
static uint32_t nz = 0x1234567;
static double noise(void) { nz ^= nz << 13; nz ^= nz >> 17; nz ^= nz << 5; return (double)(int32_t)nz / 2147483648.0; }

// renders a score; f0_true[i] gets the true pitch per sample (0 where unvoiced)
static int render(const note_t *s, int ns, float *out, float *f0_true, int cap)
{
    static res_t vr[NVOW][4];
    for (int v = 0; v < NVOW; v++) for (int k = 0; k < 4; k++) { res_set(&vr[v][k], formF[v][k], formB[v][k]); vr[v][k].y1 = vr[v][k].y2 = 0; }
    double ph = 0, vw[NVOW] = { 1, 0, 0, 0, 0 }, env = 0, hp1 = 0, hp_x1 = 0, nenv = 0;
    int n = 0;
    double vib_ph = 0;
    for (int k = 0; k < ns && n < cap; k++) {
        const note_t *q = &s[k];
        int len = (int)(q->dur * SR);
        for (int i = 0; i < len && n < cap; i++, n++) {
            double fr = (double)i / len;
            double f = q->f0 + (q->f1 - q->f0) * (fr < 0.3 ? 0 : fr > 0.7 ? 1 : (fr - 0.3) / 0.4);   // glides in the middle
            vib_ph += q->vib_hz / SR;
            f *= pow(2.0, q->vib_c / 1200.0 * sin(2 * M_PI * vib_ph));
            double target_env = q->kind == K_VOICED ? q->amp : 0, target_n = q->kind == K_NOISE ? q->amp : 0;
            env += (target_env - env) * (1.0 / (0.012 * SR));
            nenv += (target_n - nenv) * (1.0 / (0.008 * SR));
            for (int v = 0; v < NVOW; v++) vw[v] += ((v == q->vowel ? 1.0 : 0.0) - vw[v]) * (1.0 / (0.02 * SR));
            ph += f / SR; if (ph >= 1) ph -= 1;
            double src = 0;
            if (env > 1e-5) for (int h = 1; h < 80 && h * f < 7500; h++) { double a = 2 * M_PI * h * ph; src += gl_re[h] * cos(a) - gl_im[h] * sin(a); }
            src = src * env + q->breath * env * noise() * 0.3;
            double y = 0;
            for (int v = 0; v < NVOW; v++) {
                double z = src;
                for (int kk = 0; kk < 4; kk++) z = res_run(&vr[v][kk], z);
                y += vw[v] * z;
            }
            double nx = noise() * nenv;                  // an s: noise over 4 kHz
            double hp = 0.7 * (hp1 + nx - hp_x1); hp1 = hp; hp_x1 = nx;
            out[n] = (float)(y * 0.2 + hp * 0.5);
            if (f0_true) f0_true[n] = (q->kind == K_VOICED && env > 0.3 * q->amp) ? (float)f : 0.0f;
        }
    }
    return n;
}

// ---------------------------------------------------------------- running the engine
typedef void (*hook_t)(int block, voice_params_t *p);
static voice_state_t *states;                    // one per block
static double us_total; static int us_blocks; static double us_max;
static float blk_us[CAP / BLK + 8];
static void run(const float *in, int n, voice_params_t *p, float *lead, float *extra, float *dryd, hook_t hook)
{
    voice_init();
    voice_set(p);
    us_total = 0; us_blocks = 0; us_max = 0;
    int nb = n / BLK;
    for (int b = 0; b < nb; b++) {
        if (hook) { hook(b, p); voice_set(p); }
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        voice_process(in + b * BLK, lead + b * BLK, extra + b * BLK, BLK);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double us = (t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_nsec - t0.tv_nsec) * 1e-3;
        us_total += us; if (us_blocks < CAP / BLK) blk_us[us_blocks] = (float)us; us_blocks++; if (us > us_max) us_max = us;
        if (dryd) memcpy(dryd + b * BLK, voice_dry_delayed(), BLK * sizeof(float));
        if (states) voice_state(&states[b]);
    }
}
static voice_params_t plain(void)
{
    voice_params_t p; memset(&p, 0, sizeof p);
    p.lead_gain = 1; p.tune = 0; p.speed_ms = 0; p.keep_vib = 0; p.scale_mask = 0;
    return p;
}
#define CMAJ 0xAB5u                               // C D E F G A B
static inline double cents(double a, double b) { return 1200.0 * log2(a / b); }

// ---------------------------------------------------------------- measuring
// YIN in double over [i0, i0 + len), with parabolic interpolation: Hz, 0 if no clear pitch
static double pitch(const float *x, int i0, int len, double fmin, double fmax)
{
    int tmin = (int)(SR / fmax), tmax = (int)(SR / fmin) + 2, W = len - tmax;
    if (W < 64 || i0 < 0) return 0;
    static double d[4096];
    double cum = 0; int best = -1;
    d[0] = 1;
    for (int t = 1; t <= tmax; t++) {
        double s = 0;
        for (int j = 0; j < W; j++) { double e = (double)x[i0 + j] - x[i0 + j + t]; s += e * e; }
        cum += s; d[t] = cum > 0 ? s * t / cum : 1;
    }
    for (int t = tmin; t < tmax; t++) if (d[t] < 0.2) { while (t + 1 < tmax && d[t + 1] < d[t]) t++; best = t; break; }
    if (best < 0) { best = tmin; for (int t = tmin; t < tmax; t++) if (d[t] < d[best]) best = t; if (d[best] > 0.4) return 0; }
    double tf = best;
    if (best > 1 && best < tmax) { double a = d[best - 1], b = d[best], c = d[best + 1], den = a - 2 * b + c; if (den > 0) tf += 0.5 * (a - c) / den; }
    return SR / tf;
}
static double rms(const float *x, int a, int b) { double e = 0; for (int i = a; i < b; i++) e += (double)x[i] * x[i]; return b > a ? sqrt(e / (b - a)) : 0; }
static double peakabs(const float *x, int n) { double p = 0; for (int i = 0; i < n; i++) if (fabs(x[i]) > p) p = fabs(x[i]); return p; }
static int bad(const float *x, int n) { int k = 0; for (int i = 0; i < n; i++) if (!isfinite(x[i])) k++; return k; }
// the core's click measure (tools/sim m_click), on any buffer: the biggest first difference in [tc, tc + w] over the
// biggest around it ([-100, -4] ms and [w + 34, w + 134] ms); > 3 is a click
static double click(const float *x, int n, double tc, double w)
{
    int a = (int)(tc * SR), b = (int)((tc + w) * SR);
    double sp = 0, cx = 0;
    for (int i = a > 0 ? a : 1; i < b && i < n; i++) { double d = fabs(x[i] - x[i - 1]); if (d > sp) sp = d; }
    int c0 = (int)((tc - 0.10) * SR), c1 = (int)((tc - 0.004) * SR), c2 = (int)((tc + w + 0.034) * SR), c3 = (int)((tc + w + 0.134) * SR);
    for (int i = c0 > 0 ? c0 : 1; i < c1; i++) { double d = fabs(x[i] - x[i - 1]); if (d > cx) cx = d; }
    for (int i = c2; i < c3 && i < n; i++) { double d = fabs(x[i] - x[i - 1]); if (d > cx) cx = d; }
    return sp / (cx + 0.004);
}
// the lag (samples) at which y best matches x, searched over [0, maxlag]
static int best_lag(const float *x, const float *y, int a, int len, int maxlag)
{
    int bl = 0; double bc = -1e30;
    for (int l = 0; l <= maxlag; l++) {
        double c = 0, ex = 0, ey = 0;
        for (int i = a; i < a + len; i++) { c += (double)x[i] * y[i + l]; ex += (double)x[i] * x[i]; ey += (double)y[i + l] * y[i + l]; }
        c /= sqrt(ex * ey + 1e-20);
        if (c > bc) { bc = c; bl = l; }
    }
    return bl;
}
// the first formant: LPC (order 14, autocorrelation, Levinson) on a Hann window, the first envelope peak over 200 Hz
static double first_formant(const float *x, int i0, int len)
{
    static double w[8192], r[16], a[16], t[16];
    const int ord = 14;
    double pre = 0;
    for (int i = 0; i < len; i++) { double s = x[i0 + i] - 0.9 * pre; pre = x[i0 + i]; w[i] = s * (0.5 - 0.5 * cos(2 * M_PI * i / (len - 1))); }
    for (int k = 0; k <= ord; k++) { double s = 0; for (int i = k; i < len; i++) s += w[i] * w[i - k]; r[k] = s; }
    r[0] *= 1.0001;
    for (int i = 0; i <= ord; i++) a[i] = 0;
    a[0] = 1; double e = r[0];
    for (int i = 1; i <= ord; i++) {
        double acc = r[i]; for (int j = 1; j < i; j++) acc += a[j] * r[i - j];
        double k = -acc / e;
        for (int j = 0; j <= i; j++) t[j] = a[j];
        for (int j = 1; j < i; j++) a[j] = t[j] + k * t[i - j];
        a[i] = k; e *= (1 - k * k);
    }
    double prev2 = 0, prev = 0;
    for (double f = 150; f < 2500; f += 5) {
        double re = 0, im = 0;
        for (int j = 0; j <= ord; j++) { double ang = -2 * M_PI * f * j / SR; re += a[j] * cos(ang); im += a[j] * sin(ang); }
        double m = -10 * log10(re * re + im * im);
        if (f > 160 && prev > prev2 && prev > m && f - 5 > 200) return f - 5;
        prev2 = prev; prev = m;
    }
    return 0;
}

// ---------------------------------------------------------------- the tests
static float *in, *ld, *ex, *dr, *f0t, *mix;

static void t_tracking(void)
{
    section("pitch tracking");
    const double fs[] = { 75, 110, 165, 260, 400, 540, 620, 880, 1080, 600, 1180 };
    const int vow[] = { V_A, V_O, V_A, V_I, V_A, V_I, V_U, V_A, V_I, V_A, V_O };
    double worst = 0;
    for (int k = 0; k < 11; k++) {
        note_t s[] = { { 0.2, fs[k], fs[k], vow[k], K_SILENT, 0, 5, 0, 0.3 }, { 1.0, fs[k], fs[k], vow[k], K_VOICED, 0, 5, 0.05, 0.3 } };
        int n = render(s, 2, in, f0t, CAP);
        states = calloc((size_t)(n / BLK + 1), sizeof *states);
        voice_params_t p = plain();
        run(in, n, &p, ld, ex, dr, NULL);
        double se = 0, me = 0; int c = 0, vc = 0, tot = 0;
        for (int b = (int)(0.45 * SR / BLK); b < n / BLK - 4; b++) {
            tot++;
            if (!states[b].voiced) continue;
            vc++;
            double e = fabs(cents(states[b].f0, fs[k])); se += e; c++; if (e > me) me = e;
        }
        double mean = c ? se / c : 999;
        if (mean > worst) worst = mean;
        check(c > 0 && mean < 5.0 && vc > 0.97 * tot, "%4.0f Hz: mean error %.2f cents (worst block %.1f), voiced %d%% of the steady part", fs[k], mean, me, tot ? 100 * vc / tot : 0);
        free(states); states = NULL;
    }
    // a sung note change: how soon the tracker has the new note
    note_t s[] = { { 0.2, 220, 220, V_A, K_SILENT, 0, 5, 0, 0.3 }, { 0.6, 220, 220, V_A, K_VOICED, 0, 5, 0.05, 0.3 }, { 0.6, 330, 330, V_A, K_VOICED, 0, 5, 0.05, 0.3 } };
    int n = render(s, 3, in, f0t, CAP);
    states = calloc((size_t)(n / BLK + 1), sizeof *states);
    voice_params_t p = plain();
    run(in, n, &p, ld, ex, dr, NULL);
    int bc = (int)(0.8 * SR / BLK), got = -1;
    for (int b = bc; b < n / BLK; b++) if (states[b].voiced && fabs(cents(states[b].f0, 330)) < 25) { got = b; break; }
    double lag_ms = got >= 0 ? (got - bc) * 2.0 : 999;
    check(got >= 0 && lag_ms <= 30, "a jump 220 -> 330 Hz: the tracker has it %.0f ms later (the latency is 16 ms)", lag_ms);
    free(states); states = NULL;
    // a noisy room (noise 30 dB under the voice) and a whistle (a plain sine)
    {
        note_t q[] = { { 0.2, 200, 200, V_A, K_SILENT, 0, 5, 0, 0.3 }, { 1.0, 200, 200, V_A, K_VOICED, 0, 5, 0.05, 0.3 } };
        n = render(q, 2, in, f0t, CAP);
        double r = rms(in, (int)(0.4 * SR), (int)(1.1 * SR));
        for (int i = 0; i < n; i++) in[i] += (float)(r * 0.0316 * 1.732 * noise());
        states = calloc((size_t)(n / BLK + 1), sizeof *states);
        run(in, n, &p, ld, ex, dr, NULL);
        double se = 0; int c = 0, vc = 0, tot = 0;
        for (int b = (int)(0.45 * SR / BLK); b < n / BLK - 4; b++) { tot++; if (!states[b].voiced) continue; vc++; se += fabs(cents(states[b].f0, 200)); c++; }
        check(c && se / c < 5 && vc > 0.95 * tot, "200 Hz with noise 30 dB under it: mean error %.2f cents, voiced %d%%", c ? se / c : 999.0, 100 * vc / tot);
        free(states); states = NULL;
        for (int i = 0; i < n; i++) in[i] = i > (int)(0.2 * SR) ? 0.2f * sinf(2.0f * 3.14159265f * 880.0f * (float)i / SR) : 0.0f;
        states = calloc((size_t)(n / BLK + 1), sizeof *states);
        run(in, n, &p, ld, ex, dr, NULL);
        se = 0; c = vc = tot = 0;
        for (int b = (int)(0.45 * SR / BLK); b < n / BLK - 4; b++) { tot++; if (!states[b].voiced) continue; vc++; se += fabs(cents(states[b].f0, 880)); c++; }
        check(c && se / c < 5 && vc > 0.95 * tot, "a whistle (a plain 880 Hz sine): mean error %.2f cents, voiced %d%%", c ? se / c : 999.0, 100 * vc / tot);
        free(states); states = NULL;
    }
    // octave leaps, both ways: no octave error holds on to the old note
    const double from[] = { 440, 220, 196, 392 }, to[] = { 220, 440, 392, 196 };
    const int vw[] = { V_A, V_A, V_I, V_O };
    for (int k = 0; k < 4; k++) {
        note_t o[] = { { 0.2, from[k], from[k], vw[k], K_SILENT, 0, 5, 0, 0.3 }, { 0.6, from[k], from[k], vw[k], K_VOICED, 15, 5, 0.05, 0.3 },
                       { 0.6, to[k], to[k], vw[k], K_VOICED, 15, 5, 0.05, 0.3 } };
        n = render(o, 3, in, f0t, CAP);
        states = calloc((size_t)(n / BLK + 1), sizeof *states);
        run(in, n, &p, ld, ex, dr, NULL);
        int ok = 0, tot = 0;
        for (int b = (int)(0.9 * SR / BLK); b < (int)(1.38 * SR / BLK); b++) { tot++; if (states[b].voiced && fabs(cents(states[b].f0, to[k])) < 50) ok++; }
        check(ok == tot, "a leap %.0f -> %.0f Hz: on the new note from 0.1 s after it (%d of %d blocks)", from[k], to[k], ok, tot);
        free(states); states = NULL;
    }
}

static void t_voicing(void)
{
    section("voiced and unvoiced");
    note_t s[] = {
        { 0.3, 200, 200, V_A, K_SILENT, 0, 5, 0, 0.3 },  { 0.5, 200, 200, V_A, K_VOICED, 0, 5, 0.05, 0.3 },
        { 0.25, 200, 200, V_A, K_NOISE, 0, 5, 0, 0.15 }, { 0.5, 240, 240, V_I, K_VOICED, 0, 5, 0.1, 0.3 },
        { 0.4, 240, 240, V_I, K_SILENT, 0, 5, 0, 0.3 },  { 0.4, 150, 150, V_O, K_VOICED, 0, 5, 0.3, 0.3 },   // breathy
        { 0.3, 150, 150, V_O, K_SILENT, 0, 5, 0, 0.3 } };
    int n = render(s, 7, in, f0t, CAP);
    states = calloc((size_t)(n / BLK + 1), sizeof *states);
    voice_params_t p = plain();
    run(in, n, &p, ld, ex, dr, NULL);
    double t = 0; int ok = 0, tot = 0, okv[7] = { 0 }, totv[7] = { 0 };
    for (int k = 0; k < 7; k++) {
        double a = t + 0.04, b = t + s[k].dur - 0.04;                // edges aside
        for (int bl = (int)(a * SR / BLK); bl < (int)(b * SR / BLK); bl++) {
            // the tracker's window is ~15 ms behind the block
            bool want = s[k].kind == K_VOICED;
            totv[k]++; tot++;
            if (states[bl].voiced == want) { okv[k]++; ok++; }
        }
        t += s[k].dur;
    }
    check(ok > 0.96 * tot, "right on %d%% of the blocks away from the edges (vowel %d%%, s %d%%, vowel %d%%, silence %d%%, breathy vowel %d%%)",
          100 * ok / tot, 100 * okv[1] / totv[1], 100 * okv[2] / totv[2], 100 * okv[3] / totv[3], 100 * okv[4] / totv[4], 100 * okv[5] / totv[5]);
    free(states); states = NULL;
}

static void t_latency(void)
{
    section("latency");
    const int L = VOICE_LATENCY_MS * SR / 1000;
    // a woman's voice, nothing changed: the lead is the input exactly one latency late, as is the dry copy
    note_t s[] = { { 0.2, 260, 260, V_A, K_SILENT, 0, 5, 0, 0.3 }, { 1.2, 260, 260 * 1.12, V_A, K_VOICED, 30, 5.5, 0.05, 0.3 } };
    int n = render(s, 2, in, f0t, CAP);
    voice_params_t p = plain();
    run(in, n, &p, ld, ex, dr, NULL);
    int a = (int)(0.75 * SR);
    int lag = best_lag(in, ld, a, 8000, 1600), lagd = best_lag(in, dr, a, 8000, 1600);
    double c = 0, e1 = 0, e2 = 0;
    for (int i = a; i < a + 8000; i++) { c += (double)in[i] * ld[i + L]; e1 += (double)in[i] * in[i]; e2 += (double)ld[i + L] * ld[i + L]; }
    check(lag == L && lagd == L && c / sqrt(e1 * e2) > 0.98, "260 Hz, nothing changed: the lead is the input %d samples (%.1f ms) late, the dry copy %d; alike %.4f",
          lag, lag * 1000.0 / SR, lagd, c / sqrt(e1 * e2));
    // a low man's voice: a note, a breath, a note. The latency grows (in the breath if not before); the lead and the
    // dry copy stay together
    note_t m[] = { { 0.2, 98, 98, V_O, K_SILENT, 0, 5, 0, 0.3 }, { 1.0, 98, 98, V_O, K_VOICED, 20, 5, 0.05, 0.3 },
                   { 0.2, 98, 98, V_O, K_NOISE, 0, 5, 0, 0.08 }, { 1.2, 92, 98, V_O, K_VOICED, 20, 5, 0.05, 0.3 } };
    n = render(m, 4, in, f0t, CAP);
    run(in, n, &p, ld, ex, dr, NULL);
    a = (int)(1.75 * SR);
    lag = best_lag(dr, ld, a, 8000, 64);
    c = e1 = e2 = 0;
    for (int i = a; i < a + 8000; i++) { c += (double)dr[i] * ld[i + lag]; e1 += (double)dr[i] * dr[i]; e2 += (double)ld[i + lag] * ld[i + lag]; }
    check(lag <= 1 && c / sqrt(e1 * e2) > 0.97 && voice_test_leff() <= 1280,
          "98 Hz: the latency grew to %.0f samples (%.1f ms); the lead against the dry copy: %d samples apart, alike %.4f",
          voice_test_leff(), voice_test_leff() * 1000.0 / SR, lag, c / sqrt(e1 * e2));
    wav("low-in", in, n); wav("low-lead", ld, n);
}

static void t_autotune(void)
{
    section("autotune");
    // hard: A3 sung 30 cents sharp, then C4 30 cents flat; C major; the step lands at 0.9 s
    note_t s[] = { { 0.2, 220, 220, V_A, K_SILENT, 0, 5, 0, 0.3 },
                   { 0.7, 220 * pow(2, 0.30 / 12), 220 * pow(2, 0.30 / 12), V_A, K_VOICED, 0, 5, 0.05, 0.3 },
                   { 0.8, 261.626 * pow(2, -0.30 / 12), 261.626 * pow(2, -0.30 / 12), V_A, K_VOICED, 0, 5, 0.05, 0.3 } };
    int n = render(s, 3, in, f0t, CAP);
    voice_params_t p = plain(); p.tune = 1; p.speed_ms = 0; p.scale_mask = CMAJ;
    run(in, n, &p, ld, ex, dr, NULL);
    double a1 = pitch(ld, (int)(0.6 * SR), 1600, 100, 600);
    check(fabs(cents(a1, 220)) < 3, "hard: A3 sung 30 cents sharp comes out %.2f cents from A3", cents(a1, 220));
    // the new note reaches the output at 0.9 s + the latency; it must be C4 within 10 ms of that
    double tc = 0.9 + VOICE_LATENCY_MS * 0.001;
    double landed = -1;
    // pitch over 8 ms windows starting each ms: the first that is C4 within 3 cents, and the 20 ms from there on too
    for (double t = tc; t < tc + 0.06; t += 0.001) {
        double f = pitch(ld, (int)(t * SR), 256, 200, 400);
        if (f > 0 && fabs(cents(f, 261.626)) < 3) {
            double f2 = pitch(ld, (int)(t * SR), 800, 200, 400);
            if (f2 > 0 && fabs(cents(f2, 261.626)) < 3) { landed = (t - tc) * 1000; break; }
        }
    }
    double c2 = pitch(ld, (int)(1.4 * SR), 1600, 100, 600);
    check(landed >= 0 && landed <= 10 && fabs(cents(c2, 261.626)) < 3,
          "hard: the step to C4 (sung 30 cents flat) lands %.1f ms after it reaches the output, then %.2f cents from C4", landed, cents(c2, 261.626));
    wav("autotune-hard-in", in, n); wav("autotune-hard-lead", ld, n);

    // gentle: a 5.5 Hz vibrato of +-40 cents around a note 25 cents sharp; speed 200 ms, keep_vib 1
    note_t g[] = { { 0.2, 196, 196, V_O, K_SILENT, 0, 5, 0, 0.3 }, { 2.5, 196 * pow(2, 0.25 / 12), 196 * pow(2, 0.25 / 12), V_O, K_VOICED, 40, 5.5, 0.05, 0.3 } };
    n = render(g, 2, in, f0t, CAP);
    p = plain(); p.tune = 1; p.speed_ms = 200; p.keep_vib = 1; p.scale_mask = CMAJ;
    run(in, n, &p, ld, ex, dr, NULL);
    double lo = 1e9, hi = -1e9, sum = 0; int c = 0, lo_in = 0;
    double ilo = 1e9, ihi = -1e9;
    for (double t = 1.4; t < 2.5; t += 0.005) {
        double f = pitch(ld, (int)(t * SR), 480, 100, 600), fi = pitch(in, (int)((t - 0.016) * SR), 480, 100, 600);
        if (f <= 0 || fi <= 0) continue;
        double ce = cents(f, 196), ci = cents(fi, 196);
        lo = fmin(lo, ce); hi = fmax(hi, ce); ilo = fmin(ilo, ci); ihi = fmax(ihi, ci);
        sum += ce; c++;
    }
    (void)lo_in;
    double mean = c ? sum / c : 999, ext = hi - lo, iext = ihi - ilo;
    check(c > 50 && fabs(mean) < 8 && ext > 0.7 * iext, "gentle: the vibrato kept (%.0f cents peak to peak of the %.0f sung), its centre %.1f cents from G3 (sung +25)", ext, iext, mean);
    wav("autotune-gentle-in", in, n); wav("autotune-gentle-lead", ld, n);
}

static void t_shift_formant(void)
{
    section("shift and formant");
    note_t s[] = { { 0.2, 130, 130, V_E, K_SILENT, 0, 5, 0, 0.3 }, { 1.4, 130, 130, V_E, K_VOICED, 0, 5, 0.03, 0.3 } };
    int n = render(s, 2, in, f0t, CAP);
    double f1in = first_formant(in, (int)(0.8 * SR), 1280);
    for (int k = -1; k <= 1; k += 2) {
        voice_params_t p = plain(); p.shift = 12.0f * k;
        run(in, n, &p, ld, ex, dr, NULL);
        double f = pitch(ld, (int)(1.0 * SR), 1600, 0.7 * 130 * pow(2, k), 1.4 * 130 * pow(2, k));
        double lv = rms(ld, (int)(0.8 * SR), (int)(1.5 * SR)) / rms(in, (int)(0.8 * SR), (int)(1.5 * SR));
        check(fabs(cents(f, 130 * pow(2, k))) < 5 && lv > 0.6 && lv < 1.6, "shift %+d: %.1f Hz out of 130 (%.2f cents from %.0f), level x%.2f of the input",
              12 * k, f, cents(f, 130 * pow(2, k)), 130 * pow(2, k), lv);
        wav(k > 0 ? "shift-up-lead" : "shift-down-lead", ld, n);
    }
    for (int k = -1; k <= 1; k += 2) {
        voice_params_t p = plain(); p.formant = 6.0f * k;
        run(in, n, &p, ld, ex, dr, NULL);
        double f = pitch(ld, (int)(1.0 * SR), 1600, 90, 180);
        double f1 = first_formant(ld, (int)(0.8 * SR), 1280), r = f1 / f1in;
        bool ok = k > 0 ? (r > 1.2 && r < 1.7) : (r > 0.58 && r < 0.84);
        check(ok && fabs(cents(f, 130)) < 5, "formant %+d: the first formant %.0f Hz from %.0f (x%.2f, a fifth would be x%.2f), the pitch %.2f cents off",
              6 * k, f1, f1in, r, pow(2, 0.5 * k), cents(f, 130));
        wav(k > 0 ? "formant-up-lead" : "formant-down-lead", ld, n);
    }
    wav("shift-in", in, n);
}

static void t_harmony(void)
{
    section("harmonies (each extra voice alone)");
    // E4 sung in tune, C major, the chord C E G
    note_t s[] = { { 0.2, 329.63, 329.63, V_A, K_SILENT, 0, 5, 0, 0.3 }, { 1.2, 329.63, 329.63, V_A, K_VOICED, 0, 5, 0.03, 0.3 } };
    int n = render(s, 2, in, f0t, CAP);
    struct { int mode; int iv; double want; const char *what; } cases[] = {
        { HARM_SCALE, 2, 392.00, "SCALE +2 (a third up in C major: G4)" }, { HARM_SCALE, -2, 261.63, "SCALE -2 (C4)" },
        { HARM_SCALE, 4, 493.88, "SCALE +4 (B4)" },
        { HARM_CHORD, 1, 392.00, "CHORD +1 (the next tone of C E G above E: G4)" }, { HARM_CHORD, -1, 261.63, "CHORD -1 (C4)" },
        { HARM_CHORD, 2, 523.25, "CHORD +2 (C5)" },
        { HARM_SEMIS, 7, 493.88, "SEMIS +7 (B4)" }, { HARM_SEMIS, -12, 164.81, "SEMIS -12 (E3)" },
    };
    for (unsigned c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        voice_params_t p = plain(); p.tune = 1; p.scale_mask = CMAJ; p.chord_mask = (1 << 0) | (1 << 4) | (1 << 7);
        p.harm_mode = cases[c].mode; p.harm[0] = (int8_t)cases[c].iv; p.harm_gain[0] = 1;
        voice_test_solo = 0;
        run(in, n, &p, ld, ex, dr, NULL);
        voice_test_solo = -1;
        double f = pitch(ex, (int)(0.9 * SR), 1600, 80, 900);
        check(fabs(cents(f, cases[c].want)) < 5, "%s: %.1f Hz (%.2f cents)", cases[c].what, f, cents(f, cases[c].want));
    }
    section("choir");
    for (int mode = 0; mode < 3; mode++) {
        voice_params_t p = plain(); p.tune = 1; p.scale_mask = CMAJ; p.chord_mask = (1 << 0) | (1 << 4) | (1 << 7);
        p.choir = 2.0f / 7.0f; p.choir_mode = mode; p.choir_spread = 20; p.choir_time_ms = 20; p.humanize = 0;
        double want[2];
        if (mode == CHOIR_UNISON) { want[0] = 329.63 * pow(2, 20.0 / 1200); want[1] = 329.63 * pow(2, -20.0 / 1200); }
        else if (mode == CHOIR_OCTAVES) { want[0] = 659.26 * pow(2, 20.0 / 1200); want[1] = 164.81 * pow(2, -20.0 / 1200); }
        else { want[0] = 392.00 * pow(2, 20.0 / 1200); want[1] = 261.63 * pow(2, -20.0 / 1200); }
        double got[2];
        for (int v = 0; v < 2; v++) {
            voice_test_solo = v;
            run(in, n, &p, ld, ex, dr, NULL);
            got[v] = pitch(ex, (int)(0.9 * SR), 1600, 80, 900);
        }
        voice_test_solo = -1;
        const char *nm[] = { "UNISON", "OCTAVES", "CHORD" };
        check(fabs(cents(got[0], want[0])) < 4 && fabs(cents(got[1], want[1])) < 4, "%s, spread 20 cents: %.1f and %.1f Hz (%.1f and %.1f cents from %.1f and %.1f)",
              nm[mode], got[0], got[1], cents(got[0], want[0]), cents(got[1], want[1]), want[0], want[1]);
    }
}

// mid-note jumps of every control: no clicks
static int jump_at; static voice_params_t jump_to;
static void jump_hook(int b, voice_params_t *p) { if (b == jump_at) *p = jump_to; }
static void t_clicks(void)
{
    section("clicks");
    note_t s[] = { { 0.2, 196, 196, V_A, K_SILENT, 0, 5, 0, 0.3 }, { 0.6, 196, 196, V_A, K_VOICED, 15, 5, 0.05, 0.3 },
                   { 0.2, 196, 196, V_A, K_NOISE, 0, 5, 0, 0.12 }, { 0.6, 247, 247, V_I, K_VOICED, 15, 5, 0.05, 0.3 },
                   { 0.3, 196, 196, V_A, K_SILENT, 0, 5, 0, 0.3 } };
    int n = render(s, 5, in, f0t, CAP);
    voice_params_t p = plain(); p.tune = 1; p.scale_mask = CMAJ; p.harm_mode = HARM_SCALE; p.harm[0] = 2; p.harm[1] = -3; p.harm_gain[0] = 0.8f; p.harm_gain[1] = 0.6f;
    p.choir = 0.6f; p.choir_spread = 15; p.choir_time_ms = 25; p.humanize = 0.5f;
    run(in, n, &p, ld, ex, dr, NULL);
    double worst = 0; const double edges[] = { 0.2, 0.8, 1.0, 1.6 };
    for (int k = 0; k < 4; k++) {
        double tc = edges[k] + VOICE_LATENCY_MS * 0.001 - 0.01;
        double c1 = click(ld, n, tc, 0.04), c2 = click(ex, n, tc, 0.04);
        worst = fmax(worst, fmax(c1, c2));
    }
    check(worst < 3, "voiced and unvoiced edges (vowel, s, vowel, silence): the worst spike is %.2f of its surroundings (lead and extra; 3 is a click)", worst);
    wav("edges-in", in, n); wav("edges-lead", ld, n); wav("edges-extra", ex, n);

    // a held vowel, then each control jumps end to end mid-note
    note_t h[] = { { 0.2, 220, 220, V_A, K_SILENT, 0, 5, 0, 0.3 }, { 1.6, 220, 220, V_A, K_VOICED, 20, 5.2, 0.05, 0.3 } };
    n = render(h, 2, in, f0t, CAP);
    struct { const char *what; void (*set)(voice_params_t *from, voice_params_t *to); } J[] = {
        { "shift 0 -> +12", NULL }, { "formant -12 -> +12", NULL }, { "tune 0 -> 1", NULL }, { "lead 0 -> 1", NULL },
        { "harmony 0 -> 1", NULL }, { "choir 0 -> 1", NULL }, { "harmony mode SCALE -> CHORD", NULL }, { "speed 400 -> 0", NULL },
    };
    for (int k = 0; k < 8; k++) {
        voice_params_t a = plain(), b;
        a.scale_mask = CMAJ; a.chord_mask = 0x091;
        switch (k) {
        case 0: b = a; b.shift = 12; break;
        case 1: a.formant = -12; b = a; b.formant = 12; break;
        case 2: b = a; b.tune = 1; break;
        case 3: a.lead_gain = 0; b = a; b.lead_gain = 1; break;
        case 4: a.harm_mode = HARM_SCALE; a.harm[0] = 2; b = a; b.harm_gain[0] = 1; break;
        case 5: a.choir_spread = 20; a.choir_time_ms = 30; b = a; b.choir = 1; break;
        case 6: a.harm_mode = HARM_SCALE; a.harm[0] = 3; a.harm_gain[0] = 1; b = a; b.harm_mode = HARM_CHORD; b.harm[0] = 1; break;
        default: a.tune = 1; a.speed_ms = 400; b = a; b.speed_ms = 0; break;
        }
        jump_at = (int)(1.0 * SR / BLK); jump_to = b;
        run(in, n, &a, ld, ex, dr, jump_hook);
        double tc = jump_at * BLK / (double)SR - 0.002;
        float *both = mix;
        for (int i = 0; i < n; i++) both[i] = ld[i] + ex[i];
        double c = click(both, n, tc, 0.05);
        check(c < 3 && !bad(both, n), "%s mid-note: spike %.2f of its surroundings", J[k].what, c);
    }
}

static void t_busy(void)
{
    section("cost and levels");
    note_t s[] = { { 0.1, 300, 300, V_A, K_SILENT, 0, 5, 0, 0.3 }, { 10.0, 300, 420, V_I, K_VOICED, 30, 5.5, 0.05, 0.4 } };
    int n = render(s, 2, in, f0t, CAP);
    voice_params_t p = plain(); p.tune = 1; p.speed_ms = 20; p.keep_vib = 0.5f; p.scale_mask = CMAJ; p.formant = 3; p.shift = 0;
    p.harm_mode = HARM_SCALE; p.harm[0] = 2; p.harm[1] = 4; p.harm[2] = -3; p.harm_gain[0] = p.harm_gain[1] = p.harm_gain[2] = 0.7f;
    p.choir = 1; p.choir_mode = CHOIR_CHORD; p.chord_mask = 0x091; p.choir_spread = 20; p.choir_time_ms = 40; p.humanize = 1;
    double best = 1e9, p99 = 1e9;
    static float sorted[CAP / BLK + 8];
    for (int rep = 0; rep < 5; rep++) {
        run(in, n, &p, ld, ex, dr, NULL);
        double us = us_total / us_blocks; if (us < best) best = us;
        int nb = us_blocks < CAP / BLK ? us_blocks : CAP / BLK;
        memcpy(sorted, blk_us, (size_t)nb * sizeof(float));
        for (int i = 1; i < nb; i++) { float x = sorted[i]; int j = i - 1; while (j >= 0 && sorted[j] > x) { sorted[j + 1] = sorted[j]; j--; } sorted[j + 1] = x; }
        double q = sorted[(int)(0.99 * nb)];
        if (q < p99) p99 = q;
    }
    voice_state_t st; voice_state(&st);
    check(best <= 6.0, "the busiest (lead + 3 harmonies + 4 choir voices, formant moved, all voiced): %.2f us per 64-frame block on this machine (best of five; budget 6)", best);
    check(st.nvoices == 7, "seven extra voices sounding (%d)", st.nvoices);
    double pi = peakabs(in, n), pl = peakabs(ld, n), pe = peakabs(ex, n);
    check(!bad(ld, n) && !bad(ex, n) && pl < 2.0 * pi && pe < 4.0 * pi, "no NaN; peaks: in %.2f, lead %.2f, all seven extra voices summed %.2f (under 4x the input)", pi, pl, pe);
    check(p99 <= 3.0 * best, "and the blocks are even: 99 %% of them under %.2f us (the work is spread: grains are played as the output reaches them, the pitch frame split over two blocks)", p99);
    printf("    cost: %.2f us/block (best), 99th percentile %.2f us, worst single block %.1f us (with the machine's own interruptions)\n", best, p99, us_max);
}

// three singers, one tune, everything on: for listening
static void demos(void)
{
    section("demos (out-voice/*.wav)");
    const char *names[] = { "male", "female", "child" };
    const double base[] = { 110, 247, 392 };
    for (int k = 0; k < 3; k++) {
        double b = base[k], st[] = { 0, 2, 4, 5, 4, 2, 0, 7, 5, 4 };
        note_t s[24]; int ns = 0;
        s[ns++] = (note_t){ 0.3, b, b, V_A, K_SILENT, 0, 5, 0, 0.3 };
        for (int i = 0; i < 10; i++) {
            double f = b * pow(2, (st[i] + 0.35 * sin(i * 1.7)) / 12.0);        // a little out of tune
            if (i == 3) s[ns++] = (note_t){ 0.12, f, f, V_A, K_NOISE, 0, 5, 0, 0.1 };
            if (i == 7) s[ns++] = (note_t){ 0.25, f, f, V_A, K_SILENT, 0, 5, 0, 0.3 };
            s[ns++] = (note_t){ i == 6 || i == 9 ? 0.9 : 0.42, f, f * (i == 2 ? 1.03 : 1.0), (int[]){ V_A, V_O, V_I, V_A, V_U, V_A, V_O, V_I, V_A, V_A }[i], K_VOICED, i >= 6 ? 35 : 10, 5.4, 0.08, 0.3 };
        }
        s[ns++] = (note_t){ 0.5, b, b, V_A, K_SILENT, 0, 5, 0, 0.3 };
        int n = render(s, ns, in, f0t, CAP);
        voice_params_t p = plain(); p.tune = 1; p.speed_ms = 10; p.keep_vib = 0.6f; p.scale_mask = CMAJ;
        p.harm_mode = HARM_SCALE; p.harm[0] = 2; p.harm[1] = -3; p.harm_gain[0] = 0.6f; p.harm_gain[1] = 0.5f;
        p.choir = 0.6f; p.choir_mode = CHOIR_UNISON; p.choir_spread = 14; p.choir_time_ms = 25; p.humanize = 0.6f;
        run(in, n, &p, ld, ex, dr, NULL);
        for (int i = 0; i < n; i++) mix[i] = 0.7f * ld[i] + 0.5f * ex[i];
        char nm[64];
        snprintf(nm, sizeof nm, "%s-in", names[k]); wav(nm, in, n);
        snprintf(nm, sizeof nm, "%s-lead", names[k]); wav(nm, ld, n);
        snprintf(nm, sizeof nm, "%s-extra", names[k]); wav(nm, ex, n);
        snprintf(nm, sizeof nm, "%s-mix", names[k]); wav(nm, mix, n);
        // a click anywhere: the biggest step in any 2 ms against the biggest in the 60 ms around it
        double worst = 0; double wt = 0;
        for (int a = (int)(0.35 * SR); a + 64 < n - (int)(0.06 * SR); a += 64) {
            double sp = 0, cx = 0;
            for (int i = a; i < a + 64; i++) sp = fmax(sp, fabs(mix[i] - mix[i - 1]));
            for (int i = a - (int)(0.03 * SR); i < a + (int)(0.03 * SR); i++) if (i < a || i >= a + 64) cx = fmax(cx, fabs(mix[i] - mix[i - 1]));
            double r = sp / (cx + 0.004);
            if (r > worst) { worst = r; wt = (double)a / SR; }
        }
        check(!bad(mix, n) && peakabs(mix, n) < 1.5 && worst < 3, "%s: written (%.1f s, peak %.2f); no click anywhere (the worst step is %.2f of its surroundings, at %.2f s)",
              names[k], (double)n / SR, peakabs(mix, n), worst, wt);
        // the robot: hard tune, no vibrato, a fifth below as the only harmony
        p = plain(); p.tune = 1; p.speed_ms = 0; p.scale_mask = CMAJ; p.harm_mode = HARM_SEMIS; p.harm[0] = -7; p.harm_gain[0] = 0.7f;
        run(in, n, &p, ld, ex, dr, NULL);
        for (int i = 0; i < n; i++) mix[i] = 0.7f * ld[i] + 0.5f * ex[i];
        snprintf(nm, sizeof nm, "%s-robot", names[k]); wav(nm, mix, n);
        worst = 0;
        for (int a = (int)(0.35 * SR); a + 64 < n - (int)(0.06 * SR); a += 64) {
            double sp = 0, cx = 0;
            for (int i = a; i < a + 64; i++) sp = fmax(sp, fabs(mix[i] - mix[i - 1]));
            for (int i = a - (int)(0.03 * SR); i < a + (int)(0.03 * SR); i++) if (i < a || i >= a + 64) cx = fmax(cx, fabs(mix[i] - mix[i - 1]));
            worst = fmax(worst, sp / (cx + 0.004));
        }
        (void)wt;
        check(!bad(mix, n) && worst < 3, "%s, the robot (hard tune, a fifth below): no click anywhere (worst step %.2f)", names[k], worst);
    }
}

int main(void)
{
    mkdir("out-voice", 0755);
    summary = fopen("out-voice/results.txt", "w");
    glottal_init();
    in = calloc(CAP, 4); ld = calloc(CAP, 4); ex = calloc(CAP, 4); dr = calloc(CAP, 4); f0t = calloc(CAP, 4); mix = calloc(CAP, 4);
    { note_t s[] = { { 0.5, 200, 200, V_A, K_VOICED, 0, 5, 0.05, 0.3 }, { 0.3, 200, 200, V_A, K_NOISE, 0, 5, 0, 0.15 } };
      int n = render(s, 2, in, f0t, CAP); printf("levels: vowel peak %.3f rms %.3f, s peak %.3f\n", peakabs(in, (int)(0.5 * SR)), rms(in, SR / 10, SR / 2), peakabs(in + (int)(0.55 * SR), n - (int)(0.55 * SR))); }
    t_tracking();
    t_voicing();
    t_latency();
    t_autotune();
    t_shift_formant();
    t_harmony();
    t_clicks();
    t_busy();
    demos();
    printf("\n%d checks, %d failed\n", nrun, nfail);
    if (summary) { fprintf(summary, "\n%d checks, %d failed\n", nrun, nfail); fclose(summary); }
    return nfail ? 1 : 0;
}
