// FX's checks (test_inst.c calls test_mod_fx() with the axis at 10 and the controls quiet). Most drive a second FX
// engine directly (fx_engine_new: its own tape and reverb), so the measurements are exact and the instrument's own
// engine is left alone; then the same things through the whole instrument, and WAVs to listen to.
#include "sim.h"
#include "wreck.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SR AUDIO_SR
#define BLK AUDIO_BLOCK

static int ptag(const char *t) { int p = param_by_tag(t); if (p < 0) note("no param '%s'", t); return p; }
static void set(const char *t, float v) { int p = ptag(t); if (p >= 0) param_set(p, v); }

// ---------------------------------------------------------------- measuring
static double amp_at(const float *x, int n, double f)   // the amplitude of the component at f (n * f / SR whole: exact)
{
    double w = 2.0 * M_PI * f / SR, c = 2.0 * cos(w), s1 = 0, s2 = 0;
    for (int i = 0; i < n; i++) { double s0 = x[i] + c * s1 - s2; s2 = s1; s1 = s0; }
    double p = s1 * s1 + s2 * s2 - c * s1 * s2;
    return 2.0 * sqrt(p > 0 ? p : 0) / n;
}
static double rms(const float *x, int n) { double e = 0; for (int i = 0; i < n; i++) e += (double)x[i] * x[i]; return n ? sqrt(e / n) : 0; }
static int bad_n(const float *x, int n) { int b = 0; for (int i = 0; i < n; i++) if (!isfinite(x[i])) b++; return b; }
static double db(double r) { return 20.0 * log10(r > 1e-30 ? r : 1e-30); }
// frequency from the zero crossings (rising, linearly interpolated)
static double freq_zc(const float *x, int n)
{
    double first = -1, last = -1; int cnt = 0;
    for (int i = 1; i < n; i++) if (x[i - 1] < 0 && x[i] >= 0) {
        double t = (i - 1) + (double)(-x[i - 1]) / (double)(x[i] - x[i - 1]);
        if (first < 0) first = t; else cnt++;
        last = t;
    }
    return cnt > 0 ? cnt * (double)SR / (last - first) : 0;
}
// the largest jump from one sample to the next in [a, b)
static float max_step(const float *x, int a, int b) { float m = 0; for (int i = a > 0 ? a : 1; i < b; i++) { float d = fabsf(x[i] - x[i - 1]); if (d > m) m = d; } return m; }

// radix-2 FFT in place (re, im), n a power of two
static void fft(double *re, double *im, int n)
{
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { double t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double a = -2.0 * M_PI / len, wr = cos(a), wi = sin(a);
        for (int i = 0; i < n; i += len) {
            double cr = 1, ci = 0;
            for (int k = 0; k < len / 2; k++) {
                double ur = re[i + k], ui = im[i + k];
                double vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci, vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
                re[i + k] = ur + vr; im[i + k] = ui + vi; re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
                double t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
            }
        }
    }
}
// Welch power spectrum (Hann, half overlap) of x into psd[0 .. nfft/2]
static void welch(const float *x, int n, int nfft, double *psd)
{
    double *re = malloc(sizeof(double) * nfft), *im = malloc(sizeof(double) * nfft);
    for (int k = 0; k <= nfft / 2; k++) psd[k] = 0;
    int segs = 0;
    for (int s = 0; s + nfft <= n; s += nfft / 2) {
        for (int i = 0; i < nfft; i++) { double w = 0.5 - 0.5 * cos(2 * M_PI * i / nfft); re[i] = x[s + i] * w; im[i] = 0; }
        fft(re, im, nfft);
        for (int k = 0; k <= nfft / 2; k++) psd[k] += re[k] * re[k] + im[k] * im[k];
        segs++;
    }
    for (int k = 0; k <= nfft / 2; k++) psd[k] /= segs ? segs : 1;
    free(re); free(im);
}
// How flat a tail's fine structure is between f0 and f1: the spectrum divided by itself smoothed over +-40 Hz, then
// geometric mean over arithmetic mean (1: no peaks standing out; a ringing tail of separate modes: much lower)
static double fine_flatness(const float *x, int n, double f0, double f1)
{
    int nfft = 32768;
    double *psd = malloc(sizeof(double) * (nfft / 2 + 1));
    welch(x, n, nfft, psd);
    double hz = (double)SR / nfft;
    int k0 = (int)(f0 / hz), k1 = (int)(f1 / hz), hw = (int)(40.0 / hz);
    double lg = 0, ar = 0; int m = 0;
    for (int k = k0; k <= k1; k++) {
        double s = 0; int c = 0;
        for (int j = k - hw; j <= k + hw; j++) if (j > 0 && j < nfft / 2) { s += psd[j]; c++; }
        double r = psd[k] / (s / c + 1e-300);
        lg += log(r + 1e-300); ar += r; m++;
    }
    free(psd);
    return m ? exp(lg / m) / (ar / m) : 0;
}

// the decay time from an impulse response: the octave band around 1 kHz (two biquads), Schroeder's backward integral,
// a straight line through -5 .. -35 dB, extended to -60
static double rt60(const float *h, int n)
{
    double *y = malloc(sizeof(double) * n);
    double w = 2 * M_PI * 1000.0 / SR, al = sin(w) / (2 * 1.41), c = cos(w);
    double b0 = al / (1 + al), b2 = -al / (1 + al), a1 = -2 * c / (1 + al), a2 = (1 - al) / (1 + al);
    double z1 = 0, z2 = 0, q1 = 0, q2 = 0;
    for (int i = 0; i < n; i++) {
        double v = b0 * h[i] + z1; z1 = -a1 * v + z2; z2 = b2 * h[i] - a2 * v;
        double u = b0 * v + q1; q1 = -a1 * u + q2; q2 = b2 * v - a2 * u;
        y[i] = u;
    }
    double tot = 0;
    for (int i = n - 1; i >= 0; i--) { tot += y[i] * y[i]; y[i] = tot; }
    double e0 = y[0];
    double sx = 0, sy = 0, sxx = 0, sxy = 0; int m = 0;
    for (int i = 0; i < n; i++) {
        double L = 10 * log10(y[i] / e0 + 1e-300);
        if (L <= -5 && L >= -35) { double t = (double)i / SR; sx += t; sy += L; sxx += t * t; sxy += t * L; m++; }
    }
    free(y);
    if (m < 10) return 0;
    double slope = (m * sxy - sx * sy) / (m * sxx - sx * sx);
    return slope < 0 ? -60.0 / slope : 0;
}

// the delay (frames) of y against x, by cross-correlation over [a, b), searching 0 .. maxd
static int delay_of(const float *x, const float *y, int a, int b, int maxd)
{
    double best = -1e300; int bd = 0;
    for (int d = 0; d <= maxd; d++) {
        double c = 0;
        for (int i = a; i < b; i++) if (i - d >= 0) c += (double)x[i - d] * y[i];
        if (c > best) { best = c; bd = d; }
    }
    return bd;
}

static void wav_write(const char *fn, const float *x, int n)
{
    FILE *f = fopen(fn, "wb"); if (!f) return;
    uint32_t sr = SR, br = sr * 2, ds = (uint32_t)n * 2, rs = 36 + ds, fl = 16; uint16_t one = 1, bits = 16, ba = 2;
    fwrite("RIFF", 1, 4, f); fwrite(&rs, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fl, 4, 1, f); fwrite(&one, 2, 1, f);
    fwrite(&one, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&ds, 4, 1, f);
    for (int i = 0; i < n; i++) { float v = x[i]; if (!(v == v)) v = 0; v = v > 1 ? 1 : v < -1 ? -1 : v; int16_t q = (int16_t)(v * 32767.0f); fwrite(&q, 2, 1, f); }
    fclose(f);
}

// ---------------------------------------------------------------- driving an engine directly
static const char *const fx_tags[] = { "fcru", "frat", "fdrv", "fdty", "ffil", "fres", "fftp", "fdrk", "fdcr", "fddr",
                                       "froo", "fhal", "frev", "fdmp", "fwow", "fflu", "fdrg", "fbrk", "fglu", NULL };
static float saved[32];
static void save_params(void) { for (int i = 0; fx_tags[i]; i++) { int p = ptag(fx_tags[i]); saved[i] = p >= 0 ? pv[p] : 0; } }
static void restore_params(void) { for (int i = 0; fx_tags[i]; i++) { int p = ptag(fx_tags[i]); if (p >= 0) param_set(p, saved[i]); } }
static void neutral(void)   // everything off: the chain is a wire (the tape two frames late)
{
    set("fcru", 0); set("frat", 0); set("fdrv", 0); set("fdty", 0); set("ffil", 16000); set("fres", 0); set("fftp", 0);
    set("fdrk", 0); set("fdcr", 0); set("fddr", 0); set("frev", 0); set("fwow", 0); set("fflu", 0); set("fglu", 0);
    set("fdrg", 60); set("fbrk", 100); set("froo", 0.8f); set("fhal", 10); set("fdmp", 50);
}
static void perf_set(float axis, float drag, float crush, float drive) { perf.axis = axis; perf.drag = drag; perf.crush = crush; perf.drive = drive; }
static fx_engine_t *fresh(void) { fx_engine_t *e = fx_engine_new(); fx_engine_use(e); return e; }
static void done(fx_engine_t *e) { fx_engine_use(NULL); fx_engine_free(e); }
static void drums(float *x, int n) { for (int i = 0; i + BLK <= n; i += BLK) fx_drums(x + i, BLK); }
static void master(float *x, const float *s, int n) { for (int i = 0; i + BLK <= n; i += BLK) fx_master(x + i, s + i, BLK); }
static float *sine(int n, double f, float a) { float *x = malloc(sizeof(float) * n); for (int i = 0; i < n; i++) x[i] = a * (float)sin(2 * M_PI * f * i / SR); return x; }
static float *zeros(int n) { return calloc((size_t)n, sizeof(float)); }

// ---------------------------------------------------------------- crush
static void t_crush(void)
{
    section("fx: crush");
    neutral(); perf_set(0, 0, 0, 0);
    int n = SR;
    // 4 bits: every output sample is a multiple of 1/8, and a 0.9 sine takes 15 of them (997 Hz: it visits them all)
    set("fcru", 100);
    fx_engine_t *e = fresh();
    float *x = sine(n, 997, 0.9f);
    drums(x, n);
    int off = 0, lv[64] = { 0 }, nl = 0;
    for (int i = SR / 10; i < n; i++) {
        float k = x[i] / 0.125f; int r = (int)lrintf(k);
        if (fabsf(k - (float)r) > 1e-4f) off++;
        else if (r >= -32 && r < 32 && !lv[r + 32]) { lv[r + 32] = 1; nl++; }
    }
    float *ref = sine(n, 997, 0.9f);
    double err = 0; for (int i = SR / 10; i < n; i++) err += (double)(x[i] - ref[i]) * (x[i] - ref[i]);
    err = sqrt(err / (n - SR / 10));
    check(off == 0 && nl == 15, "CRUSH 100 %%: 4 bits, every sample on a step of 1/8 (%d off), a 0.9 sine on %d levels (15)", off, nl);
    check(fabs(err - 0.125 / sqrt(12.0)) < 0.2 * 0.125 / sqrt(12.0), "... the error is the steps' own: rms %.4f (q / sqrt 12 = %.4f)", err, 0.125 / sqrt(12.0));
    done(e); free(x); free(ref);
    // 50 %: 8 bits
    set("fcru", 50);
    // 50 %: 8 bits. The step, from the gaps between the levels the output takes
    e = fresh(); x = sine(n, 997, 0.9f); drums(x, n);
    {
        float lvls[512]; int nl8 = 0;
        for (int i = SR / 10; i < n && nl8 < 512; i++) { bool seen = false; for (int j = 0; j < nl8; j++) if (lvls[j] == x[i]) { seen = true; break; } if (!seen) lvls[nl8++] = x[i]; }
        float step = 1e9f;
        for (int a = 0; a < nl8; a++) for (int b = 0; b < nl8; b++) { float d = lvls[b] - lvls[a]; if (d > 1e-6f && d < step) step = d; }
        check(fabsf(step * 128.0f - 1.0f) < 0.01f && nl8 > 200 && nl8 < 240, "CRUSH 50 %%: 8 bits (the levels %.5f apart: 1/128 is %.5f; %d of them)", (double)step, 1.0 / 128, nl8);
    }
    done(e); free(x);
    // RATE 100 %: a 3 kHz sample and hold. A 1 kHz sine leaves images at 3 k - 1 k, 3 k + 1 k, 6 k - 1 k as a held
    // signal should (each down by sinc(f / 3 k)), and nothing in between
    set("fcru", 0); set("frat", 100);
    e = fresh(); x = sine(n, 1000, 0.5f); drums(x, n);
    const float *y = x + SR / 4; int m = n - SR / 4 - 8000;   // 24000 frames: 1 Hz bins land on every 1 kHz
    double a1 = amp_at(y, m, 1000), a2 = amp_at(y, m, 2000), a4 = amp_at(y, m, 4000), a5 = amp_at(y, m, 5000), an = amp_at(y, m, 1500), an2 = amp_at(y, m, 3500);
    double s1 = sin(M_PI / 3) / (M_PI / 3), s2 = sin(2 * M_PI / 3) / (2 * M_PI / 3), s4 = fabs(sin(4 * M_PI / 3)) / (4 * M_PI / 3), s5 = fabs(sin(5 * M_PI / 3)) / (5 * M_PI / 3);
    note("1 kHz at RATE 100 %%: 1 k %.3f, images 2 k %.3f 4 k %.3f 5 k %.3f (a hold predicts %.3f %.3f %.3f %.3f); 1.5 k %.4f 3.5 k %.4f",
         a1, a2, a4, a5, 0.5 * s1, 0.5 * s2, 0.5 * s4, 0.5 * s5, an, an2);
    bool img = fabs(db(a2 / a1) - db(s2 / s1)) < 1.5 && fabs(db(a4 / a1) - db(s4 / s1)) < 1.5 && fabs(db(a5 / a1) - db(s5 / s1)) < 1.5;
    check(img && db(an / a1) < -30 && db(an2 / a1) < -30, "RATE 100 %%: the images of a 3 kHz hold (2 k at %.1f dB, 4 k %.1f, 5 k %.1f; a hold: %.1f %.1f %.1f), between them < -30 dB (%.0f, %.0f)",
          db(a2 / a1), db(a4 / a1), db(a5 / a1), db(s2 / s1), db(s4 / s1), db(s5 / s1), db(an / a1), db(an2 / a1));
    done(e); free(x);
    // no zipper: CRUSH and RATE swept over 2 s. The error against the input jumps no more at the blocks' edges than
    // anywhere else (a crush stepped per block would: the same sweep stepped per block is shown for scale)
    set("frat", 0);
    for (int which = 0; which < 2; which++) {
        n = 2 * SR + SR / 2;
        e = fresh(); x = sine(n, 137, 0.5f); ref = sine(n, 137, 0.5f);
        set(which ? "frat" : "fcru", 0);
        drums(x, SR / 4);
        for (int b = 0; b < (n - SR / 4) / BLK; b++) {
            set(which ? "frat" : "fcru", 100.0f * (float)b / (float)((n - SR / 4) / BLK));
            fx_drums(x + SR / 4 + b * BLK, BLK);
        }
        double eb = 0, eo = 0; int nb = 0, no = 0;
        for (int i = SR / 4 + 1; i < n; i++) {
            double d = fabs((x[i] - ref[i]) - (x[i - 1] - ref[i - 1]));
            if (i % BLK == 0) { eb += d; nb++; } else { eo += d; no++; }
        }
        double r = (eb / nb) / (eo / no + 1e-12);
        // for scale: 4..16 bits stepped once per block, as a crush without per-sample ramps would
        double zb = 0, zo = 0; int zn = 0, zm = 0; float hold = 0, ph = 0, pe = 0;
        for (int i = SR / 4; i < n; i++) {
            float amt = (float)((i - SR / 4) / BLK) / (float)((n - SR / 4) / BLK);   // stepped once a block
            float v;
            if (which) { ph += exp2f(-3.415f * amt); if (ph >= 1) { ph -= 1; hold = ref[i]; } v = hold; }
            else { float q = exp2f(1.0f - (16.0f - 12.0f * powf(amt, 0.58f))); v = q * rintf(ref[i] / q); }
            float ee = v - ref[i]; double d = fabs(ee - pe); pe = ee;
            if (i == SR / 4) continue;
            if (i % BLK == 0) { zb += d; zn++; } else { zo += d; zm++; }
        }
        double rz = (zb / zn) / (zo / zm + 1e-12);
        check(r < 1.3 && bad_n(x, n) == 0, "%s swept 0 to 100 %% in 2 s: the error jumps %.2fx at block edges against elsewhere (1 = none; stepped per block: %.2fx)",
              which ? "RATE" : "CRUSH", r, rz);
        done(e); free(x); free(ref);
    }
}

// ---------------------------------------------------------------- drive
static const char *const tnames[] = { "WARM", "TORN", "FOLD" };
static float naive_curve(int t, float x)
{
    if (t == 0) return x > 1 ? 0.6666667f : x < -1 ? -0.6666667f : x - x * x * x / 3;
    if (t == 1) { float u = x + 0.18f; u = u < -0.62f ? -0.62f : u > 1 ? 1 : u; return u - 0.18f; }
    float u = (x + 1.0f) * 0.25f; u -= floorf(u); float p = u * 4.0f;
    return p < 2.0f ? p - 1.0f : 3.0f - p;
}
static void t_drive(void)
{
    section("fx: drive");
    neutral(); perf_set(0.375f, 0, 0, 0);                      // HARDCORE: the drive is whole
    int n = SR;
    for (int t = 0; t < 3; t++) {
        set("fdty", (float)t);
        double thd[5], cen[5]; float pk[5], rr[5];
        bool rising = true, brighter = true;
        for (int k = 0; k < 5; k++) {
            set("fdrv", 25.0f * (float)k);
            fx_engine_t *e = fresh();
            float *x = sine(n, 220, 0.25f);
            drums(x, n);
            const float *y = x + SR / 2; int m = SR / 2 - 6400;  // 16000 frames: 2 Hz bins, 220 on one
            double h1 = amp_at(y, m, 220), hs = 0, cw = h1 * h1;
            for (int h = 2; h <= 30; h++) { double a = amp_at(y, m, 220.0 * h); if (h <= 14) hs += a * a; cw += a * a; cen[k] = 0; }
            thd[k] = sqrt(hs) / (h1 + 1e-12);
            { double c = 0; for (int h = 1; h <= 30; h++) { double a = amp_at(y, m, 220.0 * h); c += h * a * a; } cen[k] = c / (cw + 1e-30); }
            pk[k] = 0; for (int i = 0; i < m; i++) if (fabsf(y[i]) > pk[k]) pk[k] = fabsf(y[i]);
            rr[k] = (float)rms(y, m);
            if (k && thd[k] <= thd[k - 1]) rising = false;
            if (k && cen[k] <= cen[k - 1]) brighter = false;
            done(e); free(x);
        }
        // a folder can fold its fundamental away, so for it the measure is where the harmonics' power sits (their centroid)
        bool lv = true; for (int k = 1; k < 5; k++) if (fabs(db(rr[k] / rr[0])) > 6.0) lv = false;
        check((t == 2 ? brighter : rising) && thd[0] < 0.003 && thd[4] > 0.25 && lv,
              "%s: the harmonics grow with DRIVE (0..100 %%: THD %.2f %.2f %.2f %.2f %.2f, centroid at harmonic %.1f %.1f %.1f %.1f %.1f), the level within 6 dB (rms %.3f %.3f %.3f %.3f %.3f)",
              tnames[t], thd[0], thd[1], thd[2], thd[3], thd[4], cen[0], cen[1], cen[2], cen[3], cen[4], rr[0], rr[1], rr[2], rr[3], rr[4]);
    }
    // anti-aliasing: a 2437 Hz sine at DRIVE 100 %: what is not one of its harmonics, against a naive curve
    set("fdrv", 100);
    for (int t = 0; t < 3; t++) {
        set("fdty", (float)t);
        fx_engine_t *e = fresh();
        float *x = sine(n, 2437, 0.25f);
        drums(x, n);
        const float *y = x + SR / 2; int m = SR / 2;           // 1 Hz bins
        double ph = 0, pt = 0; for (int i = 0; i < m; i++) pt += (double)y[i] * y[i];
        pt = pt / m;
        for (int h = 1; h * 2437 < SR / 2; h++) { double a = amp_at(y, m, 2437.0 * h); ph += a * a / 2; }
        double ali = db(sqrt((pt - ph > 0 ? pt - ph : 1e-30) / ph));
        // the same curve, no ADAA, same gain (45x)
        float *z = sine(n, 2437, 0.25f);
        for (int i = 0; i < n; i++) z[i] = naive_curve(t, z[i] * 45.25f);
        double zm = 0; for (int i = SR / 2; i < n; i++) zm += z[i]; zm /= m;
        double zt = 0, zh = 0; for (int i = SR / 2; i < n; i++) zt += (z[i] - zm) * (z[i] - zm);
        zt /= m;
        for (int h = 1; h * 2437 < SR / 2; h++) { double a = amp_at(z + SR / 2, m, 2437.0 * h); zh += a * a / 2; }
        double zali = db(sqrt((zt - zh > 0 ? zt - zh : 1e-30) / zh));
        check(ali < zali - 6.0, "%s at 100 %%, 2.4 kHz in: the aliases %.1f dB under the harmonics (the bare curve: %.1f dB)", tnames[t], -ali, -zali);
        done(e); free(x); free(z);
    }
    set("fdty", 0); set("fdrv", 0);
}

// ---------------------------------------------------------------- filter
static double svf_mag(double f, double fc, double q, bool hp)
{
    double w = tan(M_PI * f / SR) / tan(M_PI * fc / SR);
    double d = sqrt((1 - w * w) * (1 - w * w) + (w / q) * (w / q));
    return (hp ? w * w : 1.0) / d;
}
static void t_filter(void)
{
    section("fx: filter");
    neutral(); perf_set(0, 0, 0, 0);
    static const double fs[] = { 125, 250, 500, 1000, 2000, 4000, 8000 };
    for (int c = 0; c < 4; c++) {
        bool hp = c & 1; float res = c >= 2 ? 100.0f : 0.0f;
        double q = c >= 2 ? 0.707 * pow(2, 4.0857) : 0.707, cmp = 1.0 / (1.0 + 0.04 * (q - 0.707));
        set("fftp", hp ? 1 : 0); set("fres", res); set("ffil", 1000);
        double worst = 0, at_fc = 0;
        char line[200]; int w = 0;
        for (int k = 0; k < 7; k++) {
            fx_engine_t *e = fresh();
            int n = SR / 2;
            float *x = sine(n, fs[k], 0.1f);
            drums(x, n);
            double g = amp_at(x + SR / 4, SR / 4, fs[k]) / 0.1, want = svf_mag(fs[k], 1000, q, hp) * cmp;
            double dd = fabs(db(g) - db(want));
            if (dd > worst) worst = dd;
            if (fs[k] == 1000) at_fc = db(g);
            w += snprintf(line + w, sizeof line - w, " %.1f", db(g));
            done(e); free(x);
        }
        check(worst < 0.5, "%s at 1 kHz, RESONANCE %.0f %%: %.1f dB at the cutoff, within %.2f dB of the design from 125 Hz to 8 kHz (dB:%s)",
              hp ? "HIGH PASS" : "LOW PASS", res, at_fc, worst, line);
    }
    // the cutoff swept 40 Hz to 16 kHz and back in 2 s, resonant, on noise: no NaN, no blow-up, no jump
    set("fftp", 0); set("fres", 80);
    fx_engine_t *e = fresh();
    int n = 2 * SR;
    float *x = malloc(sizeof(float) * n);
    for (int i = 0; i < n; i++) x[i] = 0.2f * sim_noise();
    float pk = 0;
    for (int b = 0; b < n / BLK; b++) {
        float u = (float)b / (float)(n / BLK), f = 40.0f * powf(400.0f, u < 0.5f ? 2 * u : 2 - 2 * u);
        set("ffil", f);
        fx_drums(x + b * BLK, BLK);
    }
    for (int i = 0; i < n; i++) if (fabsf(x[i]) > pk) pk = fabsf(x[i]);
    check(bad_n(x, n) == 0 && pk < 1.5f, "the cutoff swept 40 Hz to 16 kHz and back in 2 s at RESONANCE 80 %%, on noise: finite, peak %.2f", pk);
    done(e); free(x);
    set("ffil", 16000); set("fres", 0);
}

// ---------------------------------------------------------------- reverb
static float *impulse_response(float axis, int n, bool nomod)
{
    perf_set(axis, 0, 0, 0);
    fx_engine_t *e = fresh();
    fx_test_nomod(nomod);
    float *io = zeros(n), *s = zeros(n);
    for (int i = 0; i < SR / 50; i++) s[i] = 0.3f * sim_noise();   // a 20 ms burst into the send
    master(io, s, n);
    free(s);
    done(e);
    return io;
}
static void t_reverb(void)
{
    section("fx: reverb");
    neutral(); set("frev", 100); set("fdmp", 50);
    static const float rooms[] = { 0.4f, 1.0f, 2.5f }, halls[] = { 4.0f, 8.0f, 15.0f };
    for (int end = 0; end < 2; end++) {
        char line[200]; int w = 0; double worst = 0;
        for (int k = 0; k < 3; k++) {
            float want = end ? halls[k] : rooms[k];
            set(end ? "fhal" : "froo", want);
            int n = (int)((1.5f * want + 0.5f) * SR) / BLK * BLK;
            float *h = impulse_response(end ? 1.0f : 0.0f, n, false);
            double rt = rt60(h, n), d = fabs(rt / want - 1);
            if (d > worst) worst = d;
            w += snprintf(line + w, sizeof line - w, "%s %.1f s: %.2f s", k ? "," : "", (double)want, rt);
            free(h);
        }
        check(worst < 0.2, "%s: the decay (1 kHz octave) against %s within %.0f %% (%s)", end ? "DRONE" : "BREAKCORE", end ? "HALL" : "ROOM", worst * 100, line);
    }
    // the level: REVERB 50 %, steady noise into the send: the room comes back at the send's level at BREAKCORE, louder at
    // DRONE (a longer time gathers more; its input comes down as rt^-0.3)
    {
        set("frev", 50);
        double lv[2];
        for (int z = 0; z < 2; z++) {
            perf_set(z ? 1.0f : 0.0f, 0, 0, 0);
            fx_engine_t *e = fresh();
            int n = 12 * SR;
            float *io = zeros(n), *s = malloc(sizeof(float) * n);
            for (int i = 0; i < n; i++) s[i] = 0.17f * sim_noise();
            master(io, s, n);
            lv[z] = db(rms(io + n / 2, n / 2) / rms(s + n / 2, n / 2));
            done(e); free(io); free(s);
        }
        check(fabs(lv[0]) < 2.0 && lv[1] > lv[0] + 4.0, "REVERB 50 %%: the room comes back %+.1f dB against the send at BREAKCORE, %+.1f dB at DRONE", lv[0], lv[1]);
        set("frev", 100);
    }
    // a 15 s cathedral dying out: no subnormal numbers left anywhere, it costs the same all the way down, and it ends
    // at exactly 0
    perf_set(1.0f, 0, 0, 0); set("fhal", 15);
    fx_engine_t *e = fresh();
    float io[BLK], s[BLK];
    double tmax = 0, tsum = 0; int sub = 0, chunks = 0; float last = 1;
    for (int sec = 0; sec < 120; sec++) {
        struct timespec a, b;
        clock_gettime(CLOCK_MONOTONIC, &a);
        for (int k = 0; k < SR / BLK; k++) {
            for (int i = 0; i < BLK; i++) { s[i] = sec == 0 && k < 10 ? 0.3f * sim_noise() : 0; io[i] = 0; }
            fx_master(io, s, BLK);
        }
        clock_gettime(CLOCK_MONOTONIC, &b);
        double us = ((b.tv_sec - a.tv_sec) * 1e6 + (b.tv_nsec - a.tv_nsec) * 1e-3) / (SR / BLK);
        if (sec >= 1) { if (us > tmax) tmax = us; tsum += us; chunks++; }
        if (sec % 10 == 9) sub += fx_subnormals();
        last = fabsf(io[BLK - 1]);
    }
    done(e);
    // (a subnormal slowdown is 10 to 100 times; the margin is for a busy machine)
    check(sub == 0 && last < 1e-12f && tmax < 3.0 * tsum / chunks, "a 15 s tail dying for 2 min: no subnormals (%d), it ends under -240 dB (%.0e), its slowest second %.2f us/block against %.2f on average",
          sub, (double)last, tmax, tsum / chunks);
    // no metallic ringing: the late tail's fine spectrum (0.98 Hz bins, 200 Hz to 4 kHz) has no modes standing out;
    // without the lines' drift it does
    double fl[2];
    for (int nm = 0; nm < 2; nm++) {
        int n = 7 * SR;
        float *h = impulse_response(1.0f, n, nm == 1);
        fl[nm] = fine_flatness(h + 2 * SR, 5 * SR, 200, 4000);
        if (nm == 0) wav_write("out/wreck-fx-cathedral-ir.wav", h, n);
        free(h);
    }
    check(fl[0] > 0.55 && fl[0] > 3.0 * fl[1], "a 15 s tail, 2 to 7 s in: fine flatness %.2f with the lines drifting (without: %.2f; separate ringing modes pull it down, a noise-like tail is near 0.9)", fl[0], fl[1]);
    set("fhal", 10); set("froo", 0.8f);
}

// ---------------------------------------------------------------- the tape
static void t_tape(void)
{
    section("fx: tape");
    neutral();
    // at rest the tape is a wire two frames late
    {
        perf_set(0.1f, 0, 0, 0);
        fx_engine_t *e = fresh();
        int n = SR / 2;
        float *x = malloc(sizeof(float) * n), *s = zeros(n), *in = malloc(sizeof(float) * n);
        for (int i = 0; i < n; i++) in[i] = x[i] = 0.4f * sim_noise();
        master(x, s, n);
        double err = 0; for (int i = 2; i < n; i++) { double d = fabs(x[i] - in[i - 2]); if (d > err) err = d; }
        check(err < 4e-5, "at rest the tape plays what it gets two frames late (62 us), to int16 (worst %.1e)", err);
        done(e); free(x); free(s); free(in);
    }
    // the drag: tipped half way back with DRAG 60 %, the speed is 1 - 0.75 x 0.6 x 0.5 = 0.775
    for (int c = 0; c < 2; c++) {
        float dg = c ? 1.0f : 0.5f, want = 1.0f - 0.75f * 0.6f * dg;
        perf_set(0.1f, 0, 0, 0);
        fx_engine_t *e = fresh();
        int n = 2 * SR;
        float *x = sine(n, 1000, 0.3f), *s = zeros(n);
        master(x, s, SR / 4);
        perf.drag = dg;
        master(x + SR / 4, s + SR / 4, n - SR / 4);
        double f = freq_zc(x + SR, SR / 2);
        check(fabs(f / (1000.0 * want) - 1) < 0.003, "tipped back %.0f %%: the pitch goes to %.1f Hz from 1 kHz (speed %.3f; %.3f wanted)", dg * 100, f, f / 1000, want);
        done(e); free(x); free(s);
    }
    // ... let go, it catches up and is back in time: a click every 100 ms goes in; after the drag the clicks come out
    // where they went in, two frames late
    {
        perf_set(0.1f, 0, 0, 0);
        fx_engine_t *e = fresh();
        int n = 8 * SR;
        float *x = zeros(n), *s = zeros(n), *in = zeros(n);
        for (int i = 0; i < n; i += SR / 10) in[i] = x[i] = 0.8f;
        double lag_max = 0, back = -1, smax = 0;
        for (int b = 0; b < n / BLK; b++) {
            float t = (float)(b * BLK) / SR;
            perf.drag = t >= 0.5f && t < 2.0f ? 0.7f : 0.0f;
            fx_master(x + b * BLK, s + b * BLK, BLK);
            fx_view_t v; fx_view(&v); if (v.speed > smax) smax = v.speed;
            double lg = fx_tape_lag();
            if (lg > lag_max) lag_max = lg;
            if (t >= 2.0f && back < 0 && fabs(lg) < 32) back = t - 2.0f;
        }
        int d = delay_of(in, x, 6 * SR, 8 * SR, 4000);
        check(back > 0 && back < 3.0 && d == 2 && fabs(fx_tape_lag()) < 0.01,
              "tipped back 1.5 s (%.0f ms behind), let go: back within 1 ms of time %.2f s later (at most %.2fx); 4 s on the clicks are %d frames late (2: in time)",
              lag_max * 1000 / SR, back, smax, d);
        wav_write("out/wreck-fx-drag-clicks.wav", x, n);
        done(e); free(x); free(s); free(in);
    }
    // the brake: 1 beat at 100 BPM (19200 frames), a 1 kHz sine in. Half way it plays at half speed; it stops on time,
    // then silence
    uint32_t fpb_was = song.fpb;
    song.fpb = 19200;
    {
        perf_set(0.1f, 0, 0, 0);
        fx_engine_t *e = fresh();
        int n = 3 * SR;
        float *x = sine(n, 1000, 0.3f), *s = zeros(n);
        master(x, s, SR / 2);
        fx_brake(1.0f);
        int at = -1; bool early = false;
        for (int b = SR / 2 / BLK; b < n / BLK; b++) {
            fx_master(x + b * BLK, s + b * BLK, BLK);
            int done_f = (b + 1) * BLK - SR / 2;
            if (fx_stopped() && at < 0) at = done_f;
            if (fx_stopped() && done_f < 19200 * 9 / 10) early = true;
        }
        double fq = freq_zc(x + SR / 2 + 19200 / 4 - 1600, 3200);       // a quarter of the way: 0.75 speed
        float after = 0; for (int i = SR / 2 + 19200 + BLK; i < n; i++) if (fabsf(x[i]) > after) after = fabsf(x[i]);
        float step = max_step(x, SR / 2 - 3200, SR / 2 + 19200), ctx = max_step(x, SR / 4, SR / 2 - 3200);
        check(!early && at >= 19200 && at <= 19200 + BLK && after == 0.0f,
              "BRAKE over 1 beat (19200 frames): fx_stopped() turns true at %d, then exactly silent", at);
        check(fabs(fq / 750.0 - 1) < 0.03 && step <= ctx * 1.05f, "... a quarter of the way down it plays %.0f Hz (750 wanted); no jump in it (%.3f, a 1 kHz sine's own %.3f)", fq, step, ctx);
        // the spin: half a beat, from the stop; it lands back in time at its end, exactly
        double smax = 0; float lag_end = 1e9f;
        fx_spin(0.5f);
        float *x2 = sine(n, 1000, 0.3f), *s2 = zeros(n), *in2 = sine(n, 1000, 0.3f);
        for (int b = 0; b < n / BLK; b++) {
            fx_master(x2 + b * BLK, s2 + b * BLK, BLK);
            fx_view_t v; fx_view(&v); if (v.speed > smax) smax = v.speed;
            if ((b + 1) * BLK == 9600) lag_end = fx_tape_lag();
        }
        int d = delay_of(in2, x2, SR, 2 * SR, 64);
        float st2 = max_step(x2, 0, 9600 + 640), cx2 = max_step(x2, SR, 2 * SR);
        check(!fx_stopped() && lag_end == 0.0f && d == 2 && smax > 1.2 && smax < 1.3 && st2 <= cx2 * (float)smax * 1.05f,
              "SPIN over half a beat: up to %.2fx and down, in time when it ends (%.2f frames behind), then two frames late as at rest (%d); no jump (%.3f; the sine's own at that speed %.3f)",
              smax, lag_end, d, st2, cx2 * smax);
        wav_write("out/wreck-fx-brake-sine.wav", x, n);
        done(e); free(x); free(s); free(x2); free(s2); free(in2);
    }
    // a tap again while it brakes: it spins back up from where it was, and still lands in time
    {
        perf_set(0.1f, 0, 0, 0);
        fx_engine_t *e = fresh();
        int n = 2 * SR;
        float *x = sine(n, 440, 0.3f), *s = zeros(n);
        master(x, s, SR / 4);
        fx_brake(1.0f);
        master(x + SR / 4, s + SR / 4, 9600);
        fx_spin(0.5f);
        master(x + SR / 4 + 9600, s + SR / 4 + 9600, 9600);
        float lag = fx_tape_lag();
        master(x + SR / 4 + 19200, s + SR / 4 + 19200, n - SR / 4 - 19200);
        check(fabsf(lag) < 0.5f && !fx_stopped() && bad_n(x, n) == 0, "braking half way, then a spin: back in time at its end (%.2f frames behind)", lag);
        done(e); free(x); free(s);
    }
    // BRAKE 50 % halves it
    {
        set("fbrk", 50);
        fx_engine_t *e = fresh();
        float x[BLK] = { 0 }, s[BLK] = { 0 };
        fx_brake(1.0f);
        int f = 0; while (!fx_stopped() && f < 40000) { fx_master(x, s, BLK); f += BLK; }
        check(f >= 9600 && f <= 9600 + BLK, "BRAKE 50 %%: the stop takes half as long (%d frames)", f);
        done(e); set("fbrk", 100);
    }
    song.fpb = fpb_was;
    // wow and flutter at DRONE (WOW 100 %%): the pitch wanders by about 1 %% and the tape stays within 7 ms of time
    {
        set("fwow", 100); set("fflu", 100);
        perf_set(1.0f, 0, 0, 0);
        fx_engine_t *e = fresh();
        int n = 8 * SR;
        float *x = sine(n, 1000, 0.3f), *s = zeros(n);
        master(x, s, n);
        double lo = 1e9, hi = 0;
        for (int k = 2 * SR; k + 1600 <= n; k += 800) { double f = freq_zc(x + k, 1600); if (f < lo) lo = f; if (f > hi) hi = f; }
        check(hi / lo > 1.01 && hi / lo < 1.06 && bad_n(x, n) == 0, "WOW and FLUTTER 100 %% at DRONE: the pitch wanders %.0f..%.0f Hz around 1 kHz", lo, hi);
        done(e); free(x); free(s);
        set("fwow", 0); set("fflu", 0);
    }
}

// ---------------------------------------------------------------- the cost
static void t_cost(void)
{
    section("fx: cost");
    neutral();
    set("fcru", 100); set("frat", 100); set("fdcr", 100); set("fdrv", 100); set("fddr", 100); set("fdty", 2);
    set("ffil", 800); set("fres", 80); set("fdrk", 100); set("fhal", 15); set("frev", 100); set("fwow", 100); set("fflu", 100); set("fglu", 100);
    perf_set(1.0f, 0.3f, 0.5f, 0.5f);
    double best = 1e9, bd = 1e9;
    for (int run = 0; run < 9; run++) {                                       // the best of 9: this machine is shared
        fx_engine_t *e = fresh();
        float d[BLK], m[BLK], s[BLK];
        double tt = 0, td = 0;
        for (int k = 0; k < 1000; k++) {
            for (int i = 0; i < BLK; i++) { d[i] = 0.3f * sim_noise(); m[i] = 0.3f * sim_noise(); s[i] = 0.2f * sim_noise(); }
            struct timespec a, b, c;
            clock_gettime(CLOCK_MONOTONIC, &a);
            fx_drums(d, BLK);
            clock_gettime(CLOCK_MONOTONIC, &b);
            for (int i = 0; i < BLK; i++) m[i] += d[i];
            fx_master(m, s, BLK);
            clock_gettime(CLOCK_MONOTONIC, &c);
            tt += (c.tv_sec - a.tv_sec) * 1e6 + (c.tv_nsec - a.tv_nsec) * 1e-3;
            td += (b.tv_sec - a.tv_sec) * 1e6 + (b.tv_nsec - a.tv_nsec) * 1e-3;
        }
        if (tt / 1000 < best) { best = tt / 1000; bd = td / 1000; }
        done(e);
    }
    metric("fx_us_per_block", best);
    check(best < 5.0, "everything on (crush, rate, fold at 100 %%, a resonant filter, DARK, the tape dragging with wow and flutter, a 15 s HALL, glue): %.2f us per block (drums %.2f, master %.2f; budget 5)",
          best, bd, best - bd);
    note("memory: the tape 256 KB of PSRAM; the reverb's lines, diffusers and pre-delay 36.3 KB of internal RAM (eight 4.1 KB blocks and five small ones)");
}

// ---------------------------------------------------------------- through the whole instrument, and WAVs
static void t_live(void)
{
    section("fx: in the instrument");
    restore_params();
    perf.axis = 0.1f;
    // each zone, the amp silent: the drums through that zone's FX
    float mamp = 0; { int p = ptag("mamp"); if (p >= 0) mamp = pv[p]; }
    set("mamp", 0); set("xchg", 1);
    static const float ax[4] = { 10, 37, 62, 90 };
    static const char *const zn[4] = { "breakcore", "hardcore", "sludge", "drone" };
    for (int z = 0; z < 4; z++) {
        set("xaxs", ax[z]); sim_run(2.0f);
        float t0 = sim_t; sim_run(5.0f);
        char b[40]; snprintf(b, sizeof b, "-fx-drums-%d-%s", z + 1, zn[z]); sim_wav(b, t0, sim_t);
        fx_view_t v; fx_view(&v);
        note("%s: rms %.3f peak %.3f; crush %.2f drive %.2f room %.2f", zn[z], (double)m_rms(t0, sim_t), (double)m_peak(t0, sim_t), (double)v.crush, (double)v.drive, (double)v.room);
    }
    set("xaxs", 10); sim_run(2.0f);
    // the stick: crush, then drive
    {
        float t0 = sim_t;
        sim_run(1.0f); SI.jx = -1.0f; sim_run(1.5f); SI.jx = 0; sim_run(1.0f); SI.jx = 1.0f; sim_run(1.5f); SI.jx = 0; sim_run(1.0f);
        sim_wav("-fx-stick", t0, sim_t);
        check(m_bad() == 0, "the stick left (crush) and right (drive) on the drums: no NaN");
    }
    set("mamp", mamp);
    // the drag through the instrument: tipped back 2 s, flat again: back in time
    {
        set("xaxs", 10); sim_run(1.0f);
        float t0 = sim_t;
        sim_tilt(0, -35); sim_run(2.0f);
        float behind = fx_tape_lag();
        sim_tilt(0, 0); sim_run(4.0f);
        float lag = fx_tape_lag();
        sim_wav("-fx-drag", t0, sim_t);
        check(behind > 3200 && fabsf(lag) < 32, "tipped back 2 s the tape falls %.0f ms behind; flat again, 4 s later it is %.2f ms off time", (double)behind * 1000 / SR, (double)lag * 1000 / SR);
    }
    // REC tapped: the brake, then a spin; at DRONE with the cathedral ringing on
    {
        set("xaxs", 10); sim_run(1.0f);
        float t0 = sim_t;
        sim_rec(0.1f); sim_run(2.0f);
        bool st = fx_stopped();
        sim_rec(0.1f); sim_run(2.5f);
        sim_wav("-fx-brake-spin", t0, sim_t);
        check(st && !fx_stopped() && fabsf(fx_tape_lag()) < 1.0f, "REC tapped: the tape brakes and stops (fx_stopped), tapped again it spins up and is in time");
        set("xaxs", 90); sim_run(4.0f);
        t0 = sim_t;
        sim_rec(0.1f); sim_run(5.0f); sim_rec(0.1f); sim_run(4.0f);
        sim_wav("-fx-brake-spin-drone", t0, sim_t);
    }
    // the feel changing with CHANGE: TAPE STOP: the brake and the spin at each zone
    {
        set("xchg", 0); set("xaxs", 10); sim_run(2.0f);
        float t0 = sim_t;
        for (int a = 10; a <= 90; a += 2) { set("xaxs", (float)a); sim_run(0.15f); }
        sim_run(3.0f);
        sim_wav("-fx-feel-changes", t0, sim_t);
        set("xchg", 1); set("xaxs", 10); sim_run(2.0f);
        check(m_bad() == 0, "the axis swept with CHANGE: TAPE STOP (brakes and spins at each zone): no NaN");
    }
}

// direct renders: the built-in break (as KIT has it), looped, through each zone's FX with its send into the reverb
static void t_renders(void)
{
    const brk_t *bk = kit_break();
    if (!bk || !bk->len) return;
    restore_params();
    static const float ax[4] = { 0.1f, 0.37f, 0.62f, 0.9f };
    static const char *const zn[4] = { "breakcore", "hardcore", "sludge", "drone" };
    for (int z = 0; z < 4; z++) {
        perf_set(ax[z], 0, 0, 0);
        fx_engine_t *e = fresh();
        int n = 8 * SR;
        float *d = malloc(sizeof(float) * n), *s = malloc(sizeof(float) * n);
        float sp = z == 3 ? 0.25f : z == 2 ? 0.5f : 1.0f;      // the break slowed with the feel, as BEAT would
        for (int i = 0; i < n; i++) {
            float p = fmodf((float)i * sp, (float)bk->len); int k = (int)p; float f = p - (float)k;
            float v = ((float)bk->pcm[k] + ((float)bk->pcm[(k + 1) % bk->len] - (float)bk->pcm[k]) * f) / 32768.0f;
            d[i] = 0.6f * v; s[i] = 0.25f * v;
        }
        drums(d, n);
        master(d, s, n);
        char fn[80]; snprintf(fn, sizeof fn, "out/wreck-fx-direct-%d-%s.wav", z + 1, zn[z]);
        wav_write(fn, d, n);
        done(e); free(d); free(s);
    }
}

void test_mod_fx(void)
{
    perf_t keep = *(perf_t *)&perf;
    save_params();
    t_crush();
    t_drive();
    t_filter();
    t_reverb();
    t_tape();
    t_cost();
    t_renders();
    restore_params();
    *(perf_t *)&perf = keep;
    t_live();
    SI.jx = SI.jy = 0; sim_tilt(0, 0);
    set("xaxs", 10); set("xchg", 0); sim_run(1.0f);
}
