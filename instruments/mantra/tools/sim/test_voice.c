// VOICE's checks (inst_voice.c). They drive the voices directly (their own clock and buffers, the host not ticking
// meanwhile), measure, and put everything back: the queues flushed, VOICE's params as they were, the clock the
// audio's again. WAVs to listen to: out/mantra-voice-*.wav.
//
//   vowels      every vowel sung at 98 Hz: F1 and F2 (LPC on the output) within 10 % of Peterson and Barney's
//   nasals      M, N, NG: the antiresonance (a dip in the harmonics at the nasal zero), in place order
//   fricatives  S high (4-8 kHz over 1-3 kHz), SH lower (2-4.5 kHz), F and TH flat and weak
//   stops       P T K: a closure (silent), then a burst; T's high, P's low, K's on the vowel's F2 region
//   whistle     harmonic k of a 120 Hz drone 15 dB over both neighbours for k = 6..12; a glide 6 -> 12 followed
//   undertone   energy at f0/2 (and the period doubled), the choir (cents and onsets), the 8-bit (band limited,
//               stepped, on semitones), events at their frame, HOLD, GLIDE, the queue, flush, the view
//   effect      a sung AH through the mouth comes out with EE's (and OO's) formants; the pitch tracked within 20 cents
//   clicks      none at phoneme changes (stops' bursts excepted: they are meant), flushes, jaw jumps, 8-bit params
//   six voices  everything loud at once: no NaN, under full scale; the cost (budget 8 us per 64-frame block)
#include "sim.h"
#include "mantra.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <complex.h>

extern uint32_t (*mv_clock)(void);
float    mv_test_hz(int v);
float    mv_test_env(int v);
uint32_t mv_test_t0(int v);
void     mv_test_formants(int v, float *f);
void     mv_test_reset(void);

#define SRD 32000.0
static uint32_t clk;
static uint32_t t_clock(void) { return clk; }
static mv_ctl_t C;
static float *buf, *mbuf;
static int bn, bcap;
static float (*vmic)(double t);
static mv_fx_t FX;
static bool with_fx;
static void (*per_tick)(void);

// ---------------------------------------------------------------- driving
static void run(double sec)
{
    int blocks = (int)(sec * SRD / AUDIO_BLOCK + 0.5);
    for (int b = 0; b < blocks; b++) {
        if (clk % 320 == 0) { if (per_tick) per_tick(); mv_set(&C); }
        float out[AUDIO_BLOCK] = { 0 }, send[AUDIO_BLOCK] = { 0 }, mic[AUDIO_BLOCK] = { 0 };
        mv_process(out, send, AUDIO_BLOCK);
        if (vmic) {
            for (int i = 0; i < AUDIO_BLOCK; i++) mic[i] = vmic((clk + (uint32_t)i) / SRD);
            if (with_fx) mv_effect(mic, out, send, AUDIO_BLOCK, &FX);
        }
        if (bn + AUDIO_BLOCK > bcap) {
            bcap = bcap * 2 + 64000;
            buf = realloc(buf, sizeof(float) * (size_t)bcap); mbuf = realloc(mbuf, sizeof(float) * (size_t)bcap);
        }
        memcpy(buf + bn, out, sizeof out); memcpy(mbuf + bn, mic, sizeof mic);
        bn += AUDIO_BLOCK; clk += AUDIO_BLOCK;
    }
}
static int ptag(const char *t) { int p = param_by_tag(t); if (p < 0) note("no param '%s'", t); return p; }
static void setp(const char *t, float v) { int p = ptag(t); if (p >= 0) param_set(p, v); }
static float keep_pv[MOD_PARAMS_MAX];
static void params_default(void)
{
    for (int i = 0; i < mod_voice.nparams; i++) param_set(mod_base[M_VOICE] + i, mod_voice.params[i].def);
}
static void sound(const char *s)                // VOICE's params as a sound sets them (the others at their defaults)
{
    params_default();
    char b[512]; snprintf(b, sizeof b, "%s", s);
    for (char *tok = strtok(b, " "); tok; tok = strtok(NULL, " ")) {
        char *eq = strchr(tok, '='); if (!eq) continue; *eq = 0;
        setp(tok, (float)atof(eq + 1));
    }
}
static void ctl_default(void)
{
    memset(&C, 0, sizeof C);
    C.jaw = 0.5f; C.vib_rate = 5.5f; C.breath = 0.15f; C.level = 0.8f; C.send = 0.6f;
}
static void fresh(const char *snd)             // silence, then a clean start
{
    mv_test_reset(); ctl_default(); sound(snd); per_tick = NULL; vmic = NULL; with_fx = false;
    run(1.0); bn = 0;                           // every smoother settled
}
static double hz2midi(double f) { return 69.0 + 12.0 * log2(f / 440.0); }
static void push(int v, uint32_t at, int ph, double dur, double note, double lvl, int flags)
{
    while (mv_queued(v) >= 30) run(0.02);
    mv_ev_t e = { at, (uint8_t)ph, (uint8_t)flags, (float)dur, (float)note, (float)lvl };
    mv_push(v, &e);
}
// a word: consonants at their spoken length, the vowels share the rest of `dur`
static void say(int v, uint32_t at, const char *phs, double dur, double note, double lvl)
{
    uint8_t ph[32]; int n = mv_parse(phs, ph, 32);
    double cons = 0; int nv = 0;
    for (int i = 0; i < n; i++) { if (mv_ph_vowel(ph[i])) nv++; else cons += mv_ph_len(ph[i]); }
    double vd = nv ? (dur - cons) / nv : 0; if (vd < 0.06) vd = 0.06;
    for (int i = 0; i < n; i++) push(v, i == 0 ? at : 0, ph[i], mv_ph_vowel(ph[i]) ? vd : mv_ph_len(ph[i]), note, lvl, 0);
}
static void wavout(const char *name, const float *x, int n, float gain)
{
    char fn[256]; snprintf(fn, sizeof fn, "out/%s-voice-%s.wav", sim_name, name);
    FILE *f = fopen(fn, "wb"); if (!f) return;
    uint32_t sr = AUDIO_SR, br = sr * 2, ds = (uint32_t)n * 2, rs = 36 + ds, fl = 16; uint16_t one = 1, bits = 16, ba = 2;
    fwrite("RIFF", 1, 4, f); fwrite(&rs, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fl, 4, 1, f);
    fwrite(&one, 2, 1, f); fwrite(&one, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&ds, 4, 1, f);
    for (int i = 0; i < n; i++) { float v = x[i] * gain; v = v > 1 ? 1 : v < -1 ? -1 : v; int16_t q = (int16_t)(v * 32767.0f); fwrite(&q, 2, 1, f); }
    fclose(f);
}
// the host's hall on a stretch (SING's FX bus, as MANTRA plays it), for listening
static void wav_hall(const char *name, int from, int to, float rev, float secs, float echo)
{
    fx_bus_params_t b = { echo, 0.75f * 26667.0f, 0.4f, 0.35f, 0.3f, rev, secs, 0.45f, 0.0f, 0.0f };
    fx_set_bus(&b);
    int n = to - from + (int)(secs * SRD * 0.5);
    float *o = calloc((size_t)n, sizeof(float));
    for (int i = 0; i < n; i += AUDIO_BLOCK) {
        float s[AUDIO_BLOCK], io[AUDIO_BLOCK];
        for (int j = 0; j < AUDIO_BLOCK; j++) { int k = from + i + j; float x = k < to ? buf[k] : 0.0f; s[j] = x * 0.6f; io[j] = x; }
        fx_bus(s, io, AUDIO_BLOCK);
        for (int j = 0; j < AUDIO_BLOCK && i + j < n; j++) o[i + j] = io[j];
    }
    float pk = 0; for (int i = 0; i < n; i++) if (fabsf(o[i]) > pk) pk = fabsf(o[i]);
    wavout(name, o, n, pk > 0.7f ? 0.7f / pk : 1.0f);
    free(o);
}

// ---------------------------------------------------------------- measuring
static double rmsd(const float *x, int n) { double e = 0; for (int i = 0; i < n; i++) e += (double)x[i] * x[i]; return n > 0 ? sqrt(e / n) : 0; }
static int nans(const float *x, int n)   // NaN or inf, by the bits: -ffast-math folds isfinite() and x == x to "finite"
{
    int bad = 0;
    for (int i = 0; i < n; i++) { uint32_t u; memcpy(&u, &x[i], 4); if ((u & 0x7f800000u) == 0x7f800000u) bad++; }
    return bad;
}
static double peakd(const float *x, int n) { double p = 0; for (int i = 0; i < n; i++) if (fabs(x[i]) > p) p = fabs(x[i]); return p; }
static double db(double p) { return 10.0 * log10(p + 1e-30); }
static double pw_at(const float *x, int n, double f)           // power at f (Hann-windowed DFT)
{
    double re = 0, im = 0, ws = 0, c = cos(2 * M_PI * f / SRD), s = sin(2 * M_PI * f / SRD), cr = 1, ci = 0;
    for (int i = 0; i < n; i++) {
        double w = 0.5 - 0.5 * cos(2 * M_PI * i / (n - 1));
        re += x[i] * w * cr; im -= x[i] * w * ci; ws += w;
        double t = cr * c - ci * s; ci = cr * s + ci * c; cr = t;
    }
    return (re * re + im * im) / (ws * ws);
}
static double pw_peak(const float *x, int n, double f, double w)   // the strongest within +-w Hz
{
    double best = 0;
    for (double g = f - w; g <= f + w; g += 1.0) { double p = pw_at(x, n, g); if (p > best) best = p; }
    return best;
}
static double band_db(const float *x, int n, double f0, double f1)  // power in a band (25 Hz steps)
{
    double p = 0; for (double f = f0; f < f1; f += 25.0) p += pw_at(x, n, f); return db(p);
}
// LPC formants: decimate by 3, pre-emphasis, Hamming, order 12, roots; the median of F1 F2 over 40 ms frames
static void lpc_roots(const double *a, int p, double complex *z)
{
    for (int i = 0; i < p; i++) z[i] = cpow(0.4 + 0.9 * I, i);
    for (int it = 0; it < 800; it++) {
        double md = 0;
        for (int i = 0; i < p; i++) {
            double complex num = 1.0, den = 1.0;
            for (int k = 1; k <= p; k++) num = num * z[i] + a[k];
            for (int j = 0; j < p; j++) if (j != i) den *= (z[i] - z[j]);
            double complex d = num / den; z[i] -= d;
            if (cabs(d) > md) md = cabs(d);
        }
        if (md < 1e-12) break;
    }
}
static int lpc_frame(const double *x, int n, double fs, double *F)
{
    const int p = 12;
    double *w = malloc(sizeof(double) * (size_t)n), r[16], a[16], t[16];
    for (int i = 0; i < n; i++) w[i] = (i ? x[i] - 0.95 * x[i - 1] : x[i]) * (0.54 - 0.46 * cos(2 * M_PI * i / (n - 1)));
    for (int k = 0; k <= p; k++) { r[k] = 0; for (int i = k; i < n; i++) r[k] += w[i] * w[i - k]; }
    free(w);
    if (r[0] <= 0) return 0;
    r[0] *= 1.0 + 1e-6;
    double e = r[0]; a[0] = 1; for (int k = 1; k <= p; k++) a[k] = 0;
    for (int i = 1; i <= p; i++) {
        double acc = r[i]; for (int j = 1; j < i; j++) acc += a[j] * r[i - j];
        double k = -acc / e; memcpy(t, a, sizeof t);
        for (int j = 1; j < i; j++) a[j] = t[j] + k * t[i - j];
        a[i] = k; e *= 1 - k * k;
    }
    double complex z[16]; lpc_roots(a, p, z);
    int nf = 0;
    for (int i = 0; i < p; i++) {
        if (cimag(z[i]) <= 0) continue;
        double f = carg(z[i]) * fs / (2 * M_PI), bw = -log(cabs(z[i])) * fs / M_PI;
        if (f < 150 || bw > 500) continue;
        F[nf++] = f;
    }
    for (int i = 0; i < nf; i++) for (int j = i + 1; j < nf; j++) if (F[j] < F[i]) { double q = F[i]; F[i] = F[j]; F[j] = q; }
    return nf;
}
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
static void formants(const float *x, int n, double *f12)
{
    const int dec = 3, taps = 64 * dec + 1, nd = n / dec;
    double *d = calloc((size_t)nd, sizeof(double)), fc = 0.45 / dec;
    for (int m = 0; m < nd; m++) {
        double s = 0; int c = m * dec;
        for (int k = -taps / 2; k <= taps / 2; k++) {
            int j = c + k; if (j < 0 || j >= n) continue;
            double t = k, h = t == 0 ? 2 * fc : sin(2 * M_PI * fc * t) / (M_PI * t);
            h *= 0.42 + 0.5 * cos(M_PI * t / (taps / 2)) + 0.08 * cos(2 * M_PI * t / (taps / 2));
            s += h * x[j];
        }
        d[m] = s;
    }
    double fsd = SRD / dec; int fl = (int)(0.04 * fsd), hop = (int)(0.01 * fsd);
    static double a1[400], a2[400]; int nfr = 0;
    for (int s = 0; s + fl <= nd && nfr < 400; s += hop) {
        double F[16]; int nf = lpc_frame(d + s, fl, fsd, F);
        if (nf >= 2) { a1[nfr] = F[0]; a2[nfr] = F[1]; nfr++; }
    }
    free(d);
    if (!nfr) { f12[0] = f12[1] = 0; return; }
    qsort(a1, (size_t)nfr, sizeof(double), cmpd); qsort(a2, (size_t)nfr, sizeof(double), cmpd);
    f12[0] = a1[nfr / 2]; f12[1] = a2[nfr / 2];
}
// a click: the first difference's jump right at i0 (6 ms) against the signal's own before and after
static double click_at(int i0)
{
    double sp = 0, cx = 0;
    int a = i0, b = i0 + (int)(0.006 * SRD);
    for (int i = a > 0 ? a : 1; i < b && i < bn; i++) { double d = fabs(buf[i] - buf[i - 1]); if (d > sp) sp = d; }
    int c0 = i0 - (int)(0.1 * SRD), c1 = i0 - (int)(0.004 * SRD), c2 = i0 + (int)(0.04 * SRD), c3 = i0 + (int)(0.14 * SRD);
    for (int i = c0 > 1 ? c0 : 1; i < c1; i++) { double d = fabs(buf[i] - buf[i - 1]); if (d > cx) cx = d; }
    for (int i = c2; i < c3 && i < bn; i++) { double d = fabs(buf[i] - buf[i - 1]); if (d > cx) cx = d; }
    return sp / (cx + 0.0005);
}

// ---------------------------------------------------------------- the checks
static const int vowels[10] = { PH_IY, PH_IH, PH_EH, PH_AE, PH_AA, PH_AO, PH_UH, PH_UW, PH_AH, PH_ER };
static const double tF[10][2] = { { 270, 2290 }, { 390, 1990 }, { 530, 1840 }, { 660, 1720 }, { 730, 1090 }, { 570, 840 }, { 440, 1020 }, { 300, 870 }, { 640, 1190 }, { 490, 1350 } };
static void c_vowels(void)
{
    fresh("vbre=50 vvib=25");
    int bad = 0; double worst = 0; int wk = 0;
    for (int k = 0; k < 10; k++) {
        int s0 = bn;
        push(0, 0, vowels[k], 0.9, hz2midi(98), 0.8, 0);
        run(1.05);
        double F[2]; formants(buf + s0 + 9600, 16000, F);
        double e = fmax(fabs(F[0] - tF[k][0]) / tF[k][0], fabs(F[1] - tF[k][1]) / tF[k][1]);
        if (e > worst) { worst = e; wk = k; }
        if (e > 0.10) { bad++; note("%s: F1 %.0f (%.0f) F2 %.0f (%.0f)", mv_ph_name(vowels[k]), F[0], tF[k][0], F[1], tF[k][1]); }
    }
    check(bad == 0, "every vowel's F1 and F2 within 10 %% of Peterson and Barney's (worst %s, %.1f %%)", mv_ph_name(vowels[wk]), worst * 100);
    wavout("vowels", buf, bn, 1.0f);
    // the jaw: a free voice from OO to EE
    fresh("vbre=50 vvib=25");
    C.v[0] = (mv_free_t){ true, (float)hz2midi(98), 0.8f };
    C.jaw = 0.0f; run(0.8); float fo[5]; mv_test_formants(0, fo);
    C.jaw = 1.0f; run(0.8); float fe[5]; mv_test_formants(0, fe);
    check(fabsf(fo[1] - 870) < 87 && fabsf(fe[1] - 2290) < 229 && fabsf(fo[0] - 300) < 30 && fabsf(fe[0] - 270) < 27,
          "a free voice's jaw: tipped forward OO (F1 %.0f F2 %.0f), back EE (F1 %.0f F2 %.0f)", fo[0], fo[1], fe[0], fe[1]);
    // the view
    mv_view_t vw; C.jaw = 0.5f; run(0.5); mv_view(&vw);
    float open_aa = vw.mouth;
    C.jaw = 0.0f; run(0.5); mv_view(&vw);
    check(open_aa > 0.7f && vw.round > 0.8f && vw.mouth < 0.3f && vw.level[0] > 0.2f && vw.ph[0] == PH_UW && fabsf(vw.f2 - fo[1]) < 50,
          "the view: AH's mouth open %.2f; OO's rounded %.2f, shut to %.2f; the level %.2f; F2 %.0f", open_aa, vw.round, vw.mouth, vw.level[0], vw.f2);
}

// the harmonics' levels near f, dB
static double harm_db(const float *x, int n, double f0, double f) { return db(pw_peak(x, n, round(f / f0) * f0, 3)); }
static void c_nasals(void)
{
    static const int nas[3] = { PH_M, PH_N, PH_NG };
    static const double fz[3] = { 1100, 1500, 2600 };
    double where[3]; int ok = 0;
    for (int k = 0; k < 3; k++) {
        fresh("vbre=0 vvib=0 vdri=0 vrou=0");
        push(0, 0, nas[k], 0.8, hz2midi(100), 0.8, 0); run(0.8);
        const float *x = buf + 9600; int n = 12800;
        // the deepest dip between the harmonics' local envelope: each harmonic against the mean of the two a third
        // of an octave away on either side
        double best = 0, bf = 0;
        for (double f = 600; f <= 3400; f += 100) {
            double lo = harm_db(x, n, 100, f / 1.26), hi = harm_db(x, n, 100, f * 1.26), h = harm_db(x, n, 100, f);
            double depth = 0.5 * (lo + hi) - h;
            if (depth > best) { best = depth; bf = f; }
        }
        where[k] = bf;
        bool good = best > 8.0 && fabs(bf - fz[k]) < 0.2 * fz[k];
        if (good) ok++; else note("%s: the deepest dip %.0f dB at %.0f Hz (its zero %.0f)", mv_ph_name(nas[k]), best, bf, fz[k]);
    }
    check(ok == 3 && where[0] < where[1] && where[1] < where[2], "the nasals' antiresonance: M %.0f, N %.0f, NG %.0f Hz (zeros 1100, 1500, 2600)", where[0], where[1], where[2]);
}
static void c_fricatives(void)
{
    static const int fr[5] = { PH_S, PH_SH, PH_F, PH_TH, PH_AA };
    double b[5][6], lvl[5];
    const double e[7] = { 50, 1500, 3000, 4500, 6000, 8000, 11000 };
    for (int k = 0; k < 5; k++) {
        fresh("vbre=50");
        push(0, 0, fr[k], 0.5, 48, 0.8, 0); run(0.55);
        const float *x = buf + 4800; int n = 8192;
        for (int j = 0; j < 6; j++) b[k][j] = band_db(x, n, e[j], e[j + 1]);
        lvl[k] = 20 * log10(rmsd(x, n) + 1e-12);
    }
    double s_hi = fmax(b[0][3], b[0][4]) - fmax(b[0][0], b[0][1]);
    double sh_mid = fmax(b[1][1], b[1][2]) - b[1][0];
    double f_flat = fmax(fmax(b[2][1], b[2][2]), fmax(b[2][3], b[2][4])) - fmin(fmin(b[2][1], b[2][2]), fmin(b[2][3], b[2][4]));
    double sh_vs_s = (b[1][1] - b[1][4]) - (b[0][1] - b[0][4]);
    check(s_hi > 15 && sh_mid > 8 && sh_vs_s > 10 && f_flat < 12 && lvl[2] < lvl[0] - 6 && lvl[3] < lvl[0] - 6 && lvl[0] < lvl[4] - 3,
          "fricatives: S's 4.5-8 kHz %.0f dB over its lows, SH's 1.5-4.5 kHz %.0f dB over its lows and %.0f dB lower than S, F flat (%.0f dB), F and TH weak (%.0f %.0f dB under S), S under the vowel",
          s_hi, sh_mid, sh_vs_s, f_flat, lvl[0] - lvl[2], lvl[0] - lvl[3]);
}
static void c_stops(void)
{
    static const int st[3] = { PH_P, PH_T, PH_K };
    int ok = 0; double cen[3] = { 0 };
    for (int k = 0; k < 3; k++) {
        double pf[100] = { 0 };                     // each stop six times: its burst's spectrum, power-averaged
        int good = 0;
        for (int rep = 0; rep < 6; rep++) {
            fresh("vbre=0");
            push(0, 0, PH_AA, 0.3, 48 + rep, 0.8, 0);
            push(0, 0, st[k], mv_ph_len(st[k]), 48 + rep, 0.8, 0);
            push(0, 0, PH_AA, 0.4, 48 + rep, 0.8, 0);
            run(0.9);
            double vowel = rmsd(buf + (int)(0.1 * SRD), 3200);
            // the closure: the quietest 10 ms after the vowel; the burst: the first sound after it
            int c = (int)(0.3 * SRD), q = c; double qmin = 1e9;
            for (int i = c; i < c + (int)(0.1 * SRD); i += 32) { double r = rmsd(buf + i, 320); if (r < qmin) { qmin = r; q = i; } }
            int r0 = q; while (r0 < bn && fabsf(buf[r0]) < 0.003f) r0++;
            const float *x = buf + r0; int n = 160;     // the burst's first 5 ms
            if (qmin < vowel * 0.03 && rmsd(x, n) > qmin * 30) good++;
            for (int j = 0; j < 100; j++) pf[j] += pw_at(x, n, 100.0 * (j + 1));
        }
        double num = 0, den = 0;
        for (int j = 0; j < 100; j++) { num += pf[j] * 100.0 * (j + 1); den += pf[j]; }
        cen[k] = num / (den + 1e-30);
        if (good == 6) ok++; else note("%s: a closure and a burst %d times of 6", mv_ph_name(st[k]), good);
    }
    check(ok == 3, "P T K: a closure (under -30 dB) and a burst after it (30 dB over the closure), every time");
    check(cen[0] < 3000 && cen[1] > 4000 && cen[2] > 1300 && cen[2] < 3600 && cen[0] < cen[2] && cen[2] < cen[1] - 1000,
          "the bursts' place: P's low (centre %.0f Hz), K's on the vowel's F2 region (%.0f), T's high (%.0f)", cen[0], cen[2], cen[1]);
}

static double ot_now;
static void ot_tick(void) { C.overtone = (float)ot_now; }
static void c_whistle(void)
{
    fresh("vtrc=0 vbre=35 vrou=5 vvib=0 vshp=25 vprs=80 vdri=10 vgld=80");
    C.v[0] = (mv_free_t){ true, (float)hz2midi(120), 0.8f }; C.jaw = 0.4f;
    per_tick = ot_tick; ot_now = 0; run(1.0);
    int bad = 0; double worst = 1e9; int wk = 0; mv_view_t vw = { 0 };
    for (int k = 6; k <= 12; k++) {
        ot_now = k; run(0.3); int s0 = bn; run(0.5);
        double f0 = mv_test_hz(0);
        double pk = pw_peak(buf + s0, 16000, k * f0, 4), lo = pw_peak(buf + s0, 16000, (k - 1) * f0, 4), hi = pw_peak(buf + s0, 16000, (k + 1) * f0, 4);
        double m = fmin(db(pk) - db(lo), db(pk) - db(hi));
        if (m < worst) { worst = m; wk = k; }
        if (m < 15) bad++;
        if (k == 9) mv_view(&vw);
    }
    check(bad == 0, "the whistle on harmonic k of a 120 Hz drone stands 15 dB over both neighbours, k = 6..12 (least %.1f dB, k = %d)", worst, wk);
    check(vw.harmonic == 9 && vw.whistle > 0.2f, "the view: the whistle on harmonic %d, %.2f loud", vw.harmonic, vw.whistle);
    // a melody, then a glide 6 -> 12 over 3 s: the strongest harmonic between 600 and 1700 Hz follows it
    static const double mel[] = { 6, 8, 9, 10, 12, 10, 9, 8, 9, 6, 8, 6 };
    for (int i = 0; i < 12; i++) { ot_now = mel[i]; run(0.45); }
    ot_now = 6; run(0.4);
    int g0 = bn, follow = 0, steps = 0, last = 0; bool mono = true;
    for (int i = 0; i < 300; i++) { ot_now = 6 + 6 * i / 300.0; run(0.01); }
    ot_now = 12; run(0.6);
    double f0 = mv_test_hz(0);
    for (int w = 0; w < 12; w++) {
        int s0 = g0 + w * 8000; double want = 6 + 6 * (w * 8000 + 4000) / (3.0 * SRD);
        int bk = 0; double bp = 0;
        for (int k = 5; k <= 14; k++) { double p = pw_peak(buf + s0, 8000, k * f0, 4); if (p > bp) { bp = p; bk = k; } }
        if (fabs(bk - want) <= 1.0) follow++;
        if (bk < last) mono = false;
        last = bk; steps++;
    }
    check(follow >= 11 && mono, "the whistle follows a glide 6 -> 12: %d of %d windows on the right harmonic, never back", follow, steps);
    ot_now = 0; run(1.0);
    wavout("khoomei", buf, bn, 1.0f);
    wav_hall("khoomei-hall", 0, bn, 0.4f, 3.0f, 0.25f);
    // sygyt: sharp and high, over a quieter drone (a second voice)
    fresh("vtrc=-10 vbre=30 vrou=5 vvib=0 vshp=90 vprs=90 vdri=10 vgld=60");
    C.v[0] = (mv_free_t){ true, (float)hz2midi(140), 0.8f }; C.v[1] = (mv_free_t){ true, (float)hz2midi(140), 0.25f }; C.jaw = 0.4f;
    per_tick = ot_tick; ot_now = 0; run(0.8);
    static const double mel2[] = { 10, 12, 13, 12, 10, 9, 10, 12, 16, 15, 13, 12, 10, 12 };
    for (int i = 0; i < 14; i++) { ot_now = mel2[i]; run(0.42); }
    ot_now = 0; run(0.8);
    wav_hall("sygyt", 0, bn, 0.45f, 4.0f, 0.2f);
}
static void c_undertone(void)
{
    fresh("vtrc=35 vbre=40 vrou=35 vvib=0 vund=0 vprs=85 vdri=15 vgld=150");
    C.v[0] = (mv_free_t){ true, (float)hz2midi(100), 0.8f }; C.jaw = 0.0f;
    run(1.0); int a0 = bn; run(1.0);
    setp("vund", 100); run(0.6); int b0 = bn; run(1.0);
    double f0 = mv_test_hz(0);
    double half0 = db(pw_peak(buf + a0, 32000, f0 / 2, 2)), half1 = db(pw_peak(buf + b0, 32000, f0 / 2, 2)), one1 = db(pw_peak(buf + b0, 32000, f0, 2));
    float per = m_pitch(buf + b0, 16000, 30, 300);
    check(half1 - half0 > 25 && half1 > one1 - 10 && per > 0.45f * f0 && per < 0.55f * f0,
          "the undertone: at f0/2 %+.0f dB with it (%+.0f dB against f0's own), the period doubled (%.1f Hz from %.1f)", half1 - half0, half1 - one1, per, f0);
    // kargyraa: the vowels moving slowly OO OH AH; then OM with its undertone
    for (int i = 0; i < 500; i++) { C.jaw = (float)(0.25 - 0.25 * cos(i / 500.0 * 2 * M_PI * 1.5)); run(0.01); }
    C.v[0].on = false; run(0.8);
    int k0 = bn;
    sound("vtrc=30 vbre=60 vrou=20 vvib=5 vund=45 vchr=25 vprs=55 vnas=15 vdri=25 vgld=120");
    for (int r = 0; r < 2; r++) { push(0, 0, PH_OW, 1.6, hz2midi(73), 0.8, 0); push(0, 0, PH_M, 1.4, hz2midi(73), 0.8, 0); push(0, 0, PH_SIL, 0.8, 0, 0, 0); }
    run(7.5);
    wavout("kargyraa", buf, k0, 1.0f);
    wav_hall("om", k0, bn, 0.55f, 6.0f, 0.2f);
}
static void c_choir(void)
{
    // measured without DRIFT and VIBRATO (they wander at random): what is left is the choir's own spread, fixed per
    // singer (0, +11, -9, +17 cents at CHOIR 100: 26 cents between the four; the onsets 0..35 ms) plus the choir's
    // own small wander, averaged out over 3.5 s of a held OW
    double spread_t[2], spread_c[2];
    for (int pass = 0; pass < 2; pass++) {
        fresh(pass ? "vchr=100 vdri=0 vvib=0" : "vchr=0 vdri=0 vvib=0");
        run(0.3);
        uint32_t at = clk + 3200;
        for (int v = 0; v < 5; v++) { push(v, at, PH_OW, 4.6, hz2midi(98) - (v == 4 ? 12 : 0), 0.7, 0); push(v, 0, PH_M, 1.2, 0, 0.7, 0); push(v, 0, PH_SIL, 0.6, 0, 0, 0); }
        run(0.6);
        double lo = 1e9, hi = -1e9, clo = 1e9, chi = -1e9, cents[4] = { 0 };
        for (int v = 0; v < 5; v++) {
            double t = (double)(int32_t)(mv_test_t0(v) - at) / SRD * 1000;
            if (t < lo) lo = t;
            if (t > hi) hi = t;
        }
        const int nb = 875;
        for (int b = 0; b < nb; b++) {              // each voice's pitch, the mean over 3.5 s
            run(0.004);
            for (int v = 0; v < 4; v++) cents[v] += 1200 * log2(mv_test_hz(v) / (440 * pow(2, (hz2midi(98) - 69) / 12))) / nb;
        }
        for (int v = 0; v < 4; v++) { if (cents[v] < clo) clo = cents[v]; if (cents[v] > chi) chi = cents[v]; }
        spread_t[pass] = hi - lo; spread_c[pass] = chi - clo;
        run(2.5);
    }
    check(spread_t[0] < 0.5 && spread_c[0] < 3 && spread_t[1] > 30 && spread_t[1] < 40 && spread_c[1] > 18 && spread_c[1] < 34,
          "the choir: onsets %.1f ms and pitches %.1f cents apart without it, %.0f ms (35 by its table) and %.0f cents (26) with it",
          spread_t[0], spread_c[0], spread_t[1], spread_c[1]);
    // the monks, to listen to (MONKS' voice: the drift and the vibrato back)
    fresh("vtrc=25 vbre=60 vrou=25 vvib=10 vund=20 vchr=70 vprs=50 vnas=15 vdri=35 vgld=120");
    run(0.3);
    uint32_t at = clk + 3200;
    for (int v = 0; v < 5; v++) { push(v, at, PH_OW, 2.6, hz2midi(98) - (v == 4 ? 12 : 0), 0.7, 0); push(v, 0, PH_M, 1.6, 0, 0.7, 0); push(v, 0, PH_SIL, 0.6, 0, 0, 0); }
    run(5.1);
    wav_hall("monks", 0, bn, 0.65f, 8.0f, 0.15f);
}

// Daisy Bell's chorus (MANTRA_NOTES.md), a syllable at a time, over a hummed oom-pah-pah
typedef struct { double beat, len; const char *note, *ph; } syl_t;
static const syl_t daisy[] = {
    { 0, 3, "D5", "D EY" }, { 3, 3, "B4", "Z IY" }, { 6, 3, "G4", "D EY" }, { 9, 3, "D4", "Z IY" },
    { 12, 1, "E4", "G IH V" }, { 13, 1, "F#4", "M IY" }, { 14, 1, "G4", "Y AO R" }, { 15, 2, "E4", "AE N" }, { 17, 1, "G4", "S ER" },
    { 18, 5, "D4", "D UW" }, { 24, 3, "A4", "AY M" }, { 27, 3, "D5", "HH AE F" }, { 30, 3, "B4", "K R EY" }, { 33, 3, "G4", "Z IY" },
    { 36, 1, "E4", "AO L" }, { 37, 1, "F#4", "F AO R" }, { 38, 1, "G4", "DH AH" }, { 39, 2, "A4", "L AH V" }, { 41, 1, "B4", "AH V" },
    { 42, 5, "A4", "Y UW" }, { 47, 1, "B4", "IH T" }, { 48, 1, "C5", "W OW N T" }, { 49, 1, "B4", "B IY" }, { 50, 1, "A4", "AH" },
    { 51, 2, "D5", "S T AY" }, { 53, 1, "B4", "L IH SH" }, { 54, 1, "A4", "M EH" }, { 55, 4, "G4", "R IH JH" }, { 59, 1, "A4", "AY" },
    { 60, 2, "B4", "K AE N T" }, { 62, 1, "G4", "AH" }, { 63, 2, "E4", "F AO R D" }, { 65, 1, "G4", "AH" }, { 66, 1, "E4", "K EH" },
    { 67, 4, "D4", "R IH JH" }, { 71, 1, "D4", "B AH T" }, { 72, 2, "G4", "Y UW L" }, { 74, 1, "B4", "L UH K" }, { 75, 3, "A4", "S W IY T" },
    { 78, 2, "G4", "AH P" }, { 80, 0.5, "B4", "AA N" }, { 80.5, 0.5, "B4", "DH AH" }, { 81, 2, "A4", "S IY T" }, { 83, 0.5, "B4", "AH V" },
    { 83.5, 0.5, "C5", "AH" }, { 84, 1, "D5", "B AY" }, { 85, 1, "B4", "S IH" }, { 86, 1, "G4", "K AH L" }, { 87, 2, "A4", "B IH L T" },
    { 89, 1, "D4", "F AO R" }, { 90, 6, "G4", "T UW" },
};
static double note_of(const char *s)
{
    static const int pc[7] = { 9, 11, 0, 2, 4, 5, 7 };
    int n = pc[s[0] - 'A'], i = 1;
    if (s[i] == '#') { n++; i++; }
    return 12 * (atoi(s + i) + 1) + n;
}
static void sing_daisy(double beat, double transpose)
{
    uint32_t t0 = clk + 3200;
    size_t ns = sizeof daisy / sizeof daisy[0], next = 0;
    // the waltz under it, bar by bar (G, G, D7, D7 ... roughly as the tune goes): the root on 1, a hummed chord on 2 3
    static const char roots[32] = { 'G', 'G', 'G', 'G', 'C', 'G', 'D', 'D', 'D', 'D', 'G', 'G', 'C', 'G', 'D', 'D',
                                    'G', 'G', 'G', 'G', 'C', 'G', 'D', 'D', 'D', 'G', 'G', 'C', 'G', 'D', 'G', 'G' };
    for (int bar = 0; bar < 33; bar++) {
        double b0 = bar * 3.0;
        while (next < ns && daisy[next].beat < b0 + 3.0) {
            const syl_t *s = &daisy[next++];
            uint8_t ph[16]; int n = mv_parse(s->ph, ph, 16);
            int vi = 0; while (vi < n && !mv_ph_vowel(ph[vi])) vi++;
            double pre = 0, post = 0;
            for (int i = 0; i < vi; i++) pre += mv_ph_len(ph[i]) * 0.8;
            for (int i = vi + 1; i < n; i++) post += mv_ph_len(ph[i]) * 0.8;
            uint32_t at = t0 + (uint32_t)(s->beat * beat * SRD) - (uint32_t)(pre * SRD);
            double len = s->len * beat * 0.97, note = note_of(s->note) + transpose;
            for (int i = 0; i < n; i++)
                push(0, i == 0 ? at : 0, ph[i], i == vi ? fmax(0.08, len - post) : mv_ph_len(ph[i]) * 0.8, note, 0.85, 0);
        }
        if (bar < 32) {                          // the bass on 1, a hummed third and fifth on 2 and 3
            int r = roots[bar] == 'G' ? 43 : roots[bar] == 'C' ? 48 : 38;
            uint32_t at = t0 + (uint32_t)(b0 * beat * SRD);
            push(1, at, PH_M, beat * 0.9, r, 0.55, MVF_ACCENT);
            for (int k = 1; k <= 2; k++) {
                uint32_t a2 = at + (uint32_t)(k * beat * SRD);
                push(2, a2, PH_M, beat * 0.6, r + 19, 0.3, 0);
                push(3, a2, PH_M, beat * 0.6, r + 16, 0.27, 0);
            }
        }
        while (clk < t0 + (uint32_t)((b0 - 3.0) * beat * SRD)) run(0.05);
    }
    run(beat * 9);
}
static void c_words(void)
{
    fresh("vtrc=5 vbre=35 vrou=0 vvib=15 vchr=10 vprs=45 vdri=8 vgld=40");
    double n = 50;
    say(0, 0, "D EY Z IY", 0.7, n + 7, 0.8); push(0, 0, PH_SIL, 0.3, 0, 0, 0);
    say(0, 0, "D EY Z IY", 0.7, n + 4, 0.8); push(0, 0, PH_SIL, 0.5, 0, 0, 0);
    push(0, 0, PH_OW, 0.9, n - 5, 0.8, 0); push(0, 0, PH_M, 0.7, n - 5, 0.8, 0); push(0, 0, PH_SIL, 0.5, 0, 0, 0);
    say(0, 0, "B AY", 0.3, n + 7, 0.8); say(0, 0, "S IH", 0.3, n + 4, 0.8); say(0, 0, "K AH L", 0.3, n, 0.8);
    say(0, 0, "B IH L T", 0.6, n + 2, 0.8); say(0, 0, "F AO R", 0.3, n - 5, 0.8); say(0, 0, "T UW", 1.4, n, 0.8);
    push(0, 0, PH_SIL, 0.6, 0, 0, 0);
    run(8.5);
    wavout("words", buf, bn, 1.0f);
    fresh("vtrc=5 vbre=35 vrou=0 vvib=15 vchr=10 vprs=45 vdri=8 vgld=40");
    sing_daisy(0.36, -12);
    wav_hall("daisy", 0, bn, 0.3f, 1.2f, 0.0f);
    fresh("v8bt=100 vbre=0 vrou=0 vvib=0 vdri=0 vchr=0 vbit=5 vrat=8000 vgld=0");
    sing_daisy(0.36, -12);
    wavout("daisy-8bit", buf, bn, 1.0f);
}
static void c_8bit(void)
{
    fresh("v8bt=100 vbre=0 vrou=0 vvib=0 vdri=0 vchr=0 vbit=5 vrat=8000 vgld=0");
    say(0, 0, "D EY Z IY D EY Z IY", 2.0, 52.3, 0.8);
    float last = -1; int changes = 0, short_hold = 0, held = 1000, off_grid = 0;
    double f2s[1200]; int nf = 0;
    for (int b = 0; b < 1000; b++) {
        run(0.002);
        float f[5]; mv_test_formants(0, f);
        if (nf < 1200) f2s[nf++] = f[1];
        if (last >= 0 && fabsf(f[1] - last) > 5.0f) { changes++; if (held < 3) short_hold++; held = 0; last = f[1]; } else held++;
        if (last < 0) last = f[1];
        double note = hz2midi(mv_test_hz(0));
        if (mv_test_env(0) > 0.01f && fabs(note - round(note)) > 0.02) off_grid++;
    }
    (void)f2s;
    double hi = band_db(buf + 16000, 32000, 5000, 15000), all = band_db(buf + 16000, 32000, 50, 15000);
    check(hi - all < -30 && changes > 4 && short_hold == 0 && off_grid == 0,
          "the 8-bit: %.0f dB above 5 kHz (band limited), F2 steps %d times, each held 8 ms or more, the pitch on semitones",
          hi - all, changes);
}
static void c_events(void)
{
    fresh("vchr=0 vbre=0 vvib=0 vdri=0");
    run(0.2);
    uint32_t at = clk + 1000;
    push(0, at, PH_AA, 0.3, 48, 0.8, 0);
    push(0, 0, PH_IY, 0.3, 52, 0.8, 0);
    int b0 = bn; run(0.1);
    int first = -1; for (int i = b0; i < bn; i++) if (fabsf(buf[i]) > 1e-7f) { first = i; break; }
    uint32_t t_first = first >= 0 ? clk - (uint32_t)(bn - first) : 0;
    run(0.25);
    uint32_t t1 = mv_test_t0(0);
    check(first >= 0 && t_first >= at && t_first <= at + 2 && t1 == at + (uint32_t)(0.3f * SRD),
          "events start at their frame (heard %d frames after it) and the next right after (at %d, %d frames after the first)", (int)(t_first - at), (int)(t1 - at), (int)(t1 - at));
    // HOLD: on past its length until the next is queued; GLIDE: the pitch glides; without, it steps
    fresh("vchr=0 vbre=0 vvib=0 vdri=0 vgld=300");
    push(0, 0, PH_AA, 0.2, 48, 0.8, MVF_HOLD); run(0.8);
    float held = mv_test_env(0);
    push(0, 0, PH_AA, 0.6, 55, 0.8, MVF_GLIDE); run(0.1);
    double mid = hz2midi(mv_test_hz(0));
    run(0.5);
    double end = hz2midi(mv_test_hz(0));
    push(0, 0, PH_AA, 0.6, 48, 0.8, 0); run(0.03);
    double step = hz2midi(mv_test_hz(0));
    check(held > 0.01f && mid > 49 && mid < 54 && fabs(end - 55) < 0.2 && fabs(step - 48) < 0.3,
          "HOLD keeps the vowel on (%.3f); a GLIDE passes %.1f on its way to 55 (%.1f); without, 48 in 30 ms (%.1f)", held, mid, end, step);
    // the queue: 32, then full; a flush empties it and the voice falls silent
    mv_test_reset(); run(0.1);
    int pushed = 0; for (int i = 0; i < 40; i++) { mv_ev_t e = { 0, PH_AA, 0, 0.5f, 48, 0.8f }; if (mv_push(1, &e)) pushed++; }
    int q = mv_queued(1);
    run(0.6);
    mv_flush(1); run(0.4);
    check(pushed == 32 && q == 32 && mv_queued(1) == 0 && mv_test_env(1) < 1e-4f, "the queue takes 32 (%d), a flush empties it (%d left) and the voice falls silent", pushed, mv_queued(1));
}
static void c_clicks(void)
{
    fresh("vchr=0 vdri=10");
    // changes at known frames: vowel to vowel, nasal, liquid, fricative, silence, H and back
    static const int seq[] = { PH_AA, PH_IY, PH_UW, PH_M, PH_AA, PH_L, PH_EH, PH_S, PH_AA, PH_N, PH_OW, PH_SH, PH_IY, PH_HH, PH_AA, PH_R, PH_ER, PH_W, PH_AY, PH_SIL, PH_EY, PH_SIL };
    const int ns = (int)(sizeof seq / sizeof seq[0]);
    uint32_t at0 = clk + 3200;
    double worst = 0; int wi = 0;
    for (int i = 0; i < ns; i++) push(0, at0 + (uint32_t)(i * 0.25 * SRD), seq[i], 0.25, 48 + (i % 3), 0.8, i == 8 ? MVF_GLIDE : i == 12 ? MVF_ACCENT : 0);
    run(ns * 0.25 + 0.6);
    for (int i = 1; i < ns; i++) {
        int i0 = (int)(at0 - (clk - (uint32_t)bn)) + (int)(i * 0.25 * SRD);
        double r = click_at(i0);
        if (r > worst) { worst = r; wi = i; }
    }
    // a flush mid-vowel; the jaw and the hum jumping on a free voice
    push(0, 0, PH_AA, 2.0, 50, 0.8, 0); run(0.5); mv_flush(0); int f0 = bn; run(0.5);
    double rf = click_at(f0);
    C.v[0] = (mv_free_t){ true, 50, 0.8f }; run(0.6);
    C.jaw = 0.0f; int j0 = bn; run(0.4); double rj = click_at(j0);
    C.jaw = 1.0f; j0 = bn; run(0.4); rj = fmax(rj, click_at(j0));
    C.hum = 1.0f; j0 = bn; run(0.4); rj = fmax(rj, click_at(j0));
    C.hum = 0.0f; C.overtone = 9; j0 = bn; run(0.4); rj = fmax(rj, click_at(j0));
    C.overtone = 0; j0 = bn; run(0.4); rj = fmax(rj, click_at(j0));
    C.bend = 7.0f; j0 = bn; run(0.4); rj = fmax(rj, click_at(j0));     // the stick's bend, a fifth at once
    C.bend = 0.0f; j0 = bn; run(0.4); rj = fmax(rj, click_at(j0));
    check(worst < 3 && rf < 3 && rj < 3, "no click at phoneme changes (worst %.1fx, into %s), at a flush (%.1fx), the jaw, the hum, the whistle, the bend jumping (%.1fx)",
          worst, mv_ph_name(seq[wi]), rf, rj);
    // the 8-bit's own params jumping while it sings
    fresh("v8bt=100 vbre=0 vvib=0");
    C.v[0] = (mv_free_t){ true, 50, 0.8f }; run(0.6);
    double r8 = 0;
    const char *tg[2] = { "vbit", "vrat" }; const float lo[2] = { 3, 4000 }, hi[2] = { 8, 16000 };
    for (int k = 0; k < 2; k++) for (int d = 0; d < 2; d++) { setp(tg[k], d ? lo[k] : hi[k]); run(0.35); int c0 = bn; setp(tg[k], d ? hi[k] : lo[k]); run(0.2); r8 = fmax(r8, click_at(c0)); }
    check(r8 < 3, "no click when BITS or RATE jump end to end while the 8-bit sings (%.1fx)", r8);
}
static void c_six(void)
{
    fresh("vlvl=100 vbak=100 vchr=100 vund=100 vshp=100 vrou=100 vbre=100 vvib=100 vprs=100");
    C.level = 1.0f; C.breath = 0.65f; C.overtone = 10; C.vib_depth = 0.6f;
    for (int v = 0; v < MV_N; v++) C.v[v] = (mv_free_t){ true, (float)(43 + 5 * v), 1.0f };
    run(1.5);
    for (int v = 0; v < MV_N; v++) for (int r = 0; r < 3; r++) say(v, 0, "AA S P IY CH K AE T OW M", 1.5, 45 + 4 * v, 1.0);
    run(5.0);
    int nan = nans(buf, bn);
    double pk = peakd(buf, bn);
    check(nan == 0 && pk < 1.0, "six voices, everything up (undertone, whistle, choir, rough, breath): no NaN, peak %.2f, under full scale", pk);
    // the cost: six voices singing words, the worst of it
    fresh("vbre=50 vrou=40 vund=50 vchr=60 vshp=50");
    C.overtone = 8;
    for (int v = 0; v < MV_N; v++) for (int r = 0; r < 4; r++) say(v, 0, "S P IY CH K AE T S", 1.2, 48 + 3 * v, 0.6);
    double sum = 0; int nb = 0;
    for (int b = 0; b < 2400; b++) {
        if (b % 5 == 0) mv_set(&C);
        float out[AUDIO_BLOCK] = { 0 }, send[AUDIO_BLOCK] = { 0 };
        struct timespec a, c; clock_gettime(CLOCK_MONOTONIC, &a);
        mv_process(out, send, AUDIO_BLOCK);
        clock_gettime(CLOCK_MONOTONIC, &c);
        sum += (double)(c.tv_sec - a.tv_sec) * 1e6 + (double)(c.tv_nsec - a.tv_nsec) * 1e-3; nb++;
        clk += AUDIO_BLOCK;
    }
    double us = sum / nb;
    // ... and the six singing vowels
    fresh("vbre=50 vrou=40 vund=50 vchr=60 vshp=50");
    C.overtone = 8; for (int v = 0; v < MV_N; v++) C.v[v] = (mv_free_t){ true, (float)(48 + 3 * v), 0.6f };
    run(0.5);
    double sum2 = 0;
    for (int b = 0; b < 2400; b++) {
        if (b % 5 == 0) { C.jaw = (float)(0.5 + 0.5 * sin(b * 0.01)); mv_set(&C); }
        float out[AUDIO_BLOCK] = { 0 }, send[AUDIO_BLOCK] = { 0 };
        struct timespec a, c; clock_gettime(CLOCK_MONOTONIC, &a);
        mv_process(out, send, AUDIO_BLOCK);
        clock_gettime(CLOCK_MONOTONIC, &c);
        sum2 += (double)(c.tv_sec - a.tv_sec) * 1e6 + (double)(c.tv_nsec - a.tv_nsec) * 1e-3;
        clk += AUDIO_BLOCK;
    }
    metric("voice_us_per_block", us);
    metric("voice_us_per_block_vowels", sum2 / 2400);
    check(us < 8.0, "the cost: six voices saying words %.1f us per 64-frame block, six singing vowels %.1f (budget 8)", us, sum2 / 2400);
    // ... and six silent voices (the mix's top and the bus still run on an empty block)
    fresh("");
    run(1.0);
    double sum3 = 0;
    for (int b = 0; b < 2400; b++) {
        if (b % 5 == 0) mv_set(&C);
        float out[AUDIO_BLOCK] = { 0 }, send[AUDIO_BLOCK] = { 0 };
        struct timespec a, c; clock_gettime(CLOCK_MONOTONIC, &a);
        mv_process(out, send, AUDIO_BLOCK);
        clock_gettime(CLOCK_MONOTONIC, &c);
        sum3 += (double)(c.tv_sec - a.tv_sec) * 1e6 + (double)(c.tv_nsec - a.tv_nsec) * 1e-3;
        clk += AUDIO_BLOCK;
    }
    metric("voice_us_per_block_silent", sum3 / 2400);
    check(sum3 / 2400 < 0.5, "six silent voices: %.2f us per block (under 0.5)", sum3 / 2400);
}

// the effect: VOICE itself sings AH (that is the singer), then it goes through the mouth
static float *sung; static int sung_n, sung_i;
static uint32_t room = 7;
static float sung_mic(double t) { (void)t; room ^= room << 13; room ^= room >> 17; room ^= room << 5; return (sung_n ? sung[sung_i++ % sung_n] * 0.6f : 0.0f) + (float)((int32_t)room / 2147483648.0 * 0.0003); }
static void c_effect(void)
{
    // the singer: a little tune on AH, and a held AH at 98 Hz (for the measurements)
    fresh("vbre=50 vvib=30");
    push(0, 0, PH_AA, 5.0, hz2midi(98), 0.8, 0); run(5.2);
    sung_n = bn - 9600; sung = malloc(sizeof(float) * (size_t)sung_n); memcpy(sung, buf + 9600, sizeof(float) * (size_t)sung_n); sung_i = 0;
    fresh("");
    vmic = sung_mic; with_fx = true;
    FX = (mv_fx_t){ 1.0f, 1.0f, 0, 0, 0, 0, 0 };
    run(1.5); int s0 = bn; run(1.0);
    double fe[2]; formants(buf + s0, 32000, fe);
    double tr = mv_track(), want = hz2midi(98);
    FX.jaw = 0.0f; run(1.0); s0 = bn; run(1.0);
    double fo[2]; formants(buf + s0, 32000, fo);
    check(fabs(fe[0] - 270) < 27 && fabs(fe[1] - 2290) < 229 && fabs(fo[0] - 300) < 30 && fabs(fo[1] - 870) < 87,
          "the mic's AH through the mouth: as EE (F1 %.0f, F2 %.0f), as OO (F1 %.0f, F2 %.0f)", fe[0], fe[1], fo[0], fo[1]);
    check(fabs(tr - want) < 0.2, "the mic's pitch tracked: %.2f for %.2f (within 20 cents)", tr, want);
    free(sung);
    // to listen to: a tune sung on AH (dry first), then through the mouth, the jaw moving, the undertone, the whistle
    fresh("vbre=50 vvib=30");
    static const double tune[] = { 48, 50, 52, 55, 52, 50, 48, 43, 45, 48 };
    for (int i = 0; i < 10; i++) push(0, 0, PH_AA, 0.6, tune[i], 0.8, i ? MVF_GLIDE : 0);
    push(0, 0, PH_AA, 1.5, 48, 0.8, MVF_GLIDE);
    run(8.0);
    sung_n = bn; sung = malloc(sizeof(float) * (size_t)sung_n); memcpy(sung, buf, sizeof(float) * (size_t)sung_n); sung_i = 0;
    fresh("");
    vmic = sung_mic; with_fx = false; run(7.5);                  // the singer as heard
    with_fx = true; FX = (mv_fx_t){ 1.0f, 0.5f, 0, 0, 0, 0, 0 };
    for (int i = 0; i < 750; i++) { FX.jaw = (float)(0.5 + 0.5 * sin(i * 0.02)); run(0.01); }
    FX.jaw = 0.25f; FX.sub = 1.0f; run(7.5);
    FX.sub = 0; FX.overtone = 8; FX.sharp = 0.5f; run(7.5);
    FX.overtone = 0; FX.amount = 0; run(0.5);
    wavout("effect", buf, bn, 1.0f);
    free(sung); sung = NULL; sung_n = 0;
}

void test_mod_voice(void)
{
    for (int i = 0; i < mod_voice.nparams; i++) keep_pv[i] = pv[mod_base[M_VOICE] + i];
    mv_test_reset();
    mv_clock = t_clock; clk = 0x40000000u;
    c_vowels();
    c_nasals();
    c_fricatives();
    c_stops();
    c_events();
    c_whistle();
    c_undertone();
    c_choir();
    c_8bit();
    c_clicks();
    c_effect();
    c_words();
    c_six();
    // as it was
    mv_test_reset(); ctl_default(); per_tick = NULL; vmic = NULL; run(0.2);
    mv_clock = NULL;
    for (int i = 0; i < mod_voice.nparams; i++) param_set(mod_base[M_VOICE] + i, keep_pv[i]);
    free(buf); free(mbuf); buf = mbuf = NULL; bn = bcap = 0;
}
