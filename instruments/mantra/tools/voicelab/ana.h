// tools/voicelab: measurements on rendered voice (double precision, tools only)
#pragma once
#include <math.h>
#include <complex.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// LPC formants: decimate by `dec` (windowed-sinc low pass), pre-emphasis, Hamming, autocorrelation, Levinson,
// roots (Durand-Kerner). F[], B[] sorted by frequency; returns how many (bandwidth < bwmax, freq > fmin).
static int lpc_roots(const double *a, int p, double complex *z)
{
    for (int i = 0; i < p; i++) z[i] = cpow(0.4 + 0.9 * I, i);
    for (int it = 0; it < 800; it++) {
        double md = 0;
        for (int i = 0; i < p; i++) {
            double complex num = 1.0;
            for (int k = 1; k <= p; k++) num = num * z[i] + a[k];
            double complex den = 1.0;
            for (int j = 0; j < p; j++) if (j != i) den *= (z[i] - z[j]);
            double complex d = num / den;
            z[i] -= d;
            if (cabs(d) > md) md = cabs(d);
        }
        if (md < 1e-12) break;
    }
    return p;
}
static int lpc_frame(const double *x, int n, double fs, int p, double *F, double *Bw, double fmin, double bwmax)
{
    double *w = malloc(sizeof(double) * n), r[40], a[40], tmp[40];
    for (int i = 0; i < n; i++) w[i] = (i ? x[i] - 0.95 * x[i - 1] : x[i]) * (0.54 - 0.46 * cos(2 * M_PI * i / (n - 1)));
    for (int k = 0; k <= p; k++) { r[k] = 0; for (int i = k; i < n; i++) r[k] += w[i] * w[i - k]; }
    free(w);
    if (r[0] <= 0) return 0;
    r[0] *= 1.0 + 1e-6;
    double e = r[0]; a[0] = 1; for (int k = 1; k <= p; k++) a[k] = 0;
    for (int i = 1; i <= p; i++) {
        double acc = r[i]; for (int j = 1; j < i; j++) acc += a[j] * r[i - j];
        double k = -acc / e;
        memcpy(tmp, a, sizeof(double) * (p + 1));
        for (int j = 1; j < i; j++) a[j] = tmp[j] + k * tmp[i - j];
        a[i] = k; e *= 1 - k * k;
    }
    double complex z[40]; lpc_roots(a, p, z);
    int nf = 0;
    for (int i = 0; i < p; i++) {
        if (cimag(z[i]) <= 0) continue;
        double f = carg(z[i]) * fs / (2 * M_PI), bw = -log(cabs(z[i])) * fs / M_PI;
        if (f < fmin || bw > bwmax) continue;
        F[nf] = f; Bw[nf] = bw; nf++;
    }
    for (int i = 0; i < nf; i++) for (int j = i + 1; j < nf; j++) if (F[j] < F[i]) { double t = F[i]; F[i] = F[j]; F[j] = t; t = Bw[i]; Bw[i] = Bw[j]; Bw[j] = t; }
    return nf;
}
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
// the median F1, F2, F3 over frames of `frame_s` every 10 ms in x (fs 32 kHz), at fs/dec
static void formants(const float *x, int n, double fs, int dec, int p, double frame_s, double *out3)
{
    int taps = 64 * dec + 1, nd = n / dec;
    double *d = calloc(nd, sizeof(double)), fc = 0.45 / dec;
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
    double fsd = fs / dec; int fl = (int)(frame_s * fsd), hop = (int)(0.01 * fsd);
    double f1[400], f2[400], f3[400]; int nfr = 0;
    for (int s = 0; s + fl <= nd && nfr < 400; s += hop) {
        double F[20], B[20]; int nf = lpc_frame(d + s, fl, fsd, p, F, B, 150, 500);
        if (nf >= 3) { f1[nfr] = F[0]; f2[nfr] = F[1]; f3[nfr] = F[2]; nfr++; }
        else if (nf == 2) { f1[nfr] = F[0]; f2[nfr] = F[1]; f3[nfr] = 0; nfr++; }
    }
    free(d);
    if (!nfr) { out3[0] = out3[1] = out3[2] = 0; return; }
    qsort(f1, nfr, sizeof(double), cmpd); qsort(f2, nfr, sizeof(double), cmpd); qsort(f3, nfr, sizeof(double), cmpd);
    out3[0] = f1[nfr / 2]; out3[1] = f2[nfr / 2]; out3[2] = f3[nfr / 2];
}
// power at f (Hann-windowed DFT), and the strongest within +-w Hz
static double pw_at(const float *x, int n, double fs, double f)
{
    double re = 0, im = 0, ws = 0;
    for (int i = 0; i < n; i++) {
        double w = 0.5 - 0.5 * cos(2 * M_PI * i / (n - 1)), a = 2 * M_PI * f * i / fs;
        re += x[i] * w * cos(a); im -= x[i] * w * sin(a); ws += w;
    }
    return (re * re + im * im) / (ws * ws);
}
static double pw_peak(const float *x, int n, double fs, double f, double w, double *at)
{
    double best = 0, bf = f;
    for (double g = f - w; g <= f + w; g += 1.0) { double p = pw_at(x, n, fs, g); if (p > best) { best = p; bf = g; } }
    if (at) *at = bf;
    return best;
}
static double db(double p) { return 10 * log10(p + 1e-30); }
static double rms(const float *x, int n) { double e = 0; for (int i = 0; i < n; i++) e += (double)x[i] * x[i]; return n ? sqrt(e / n) : 0; }
static double peakabs(const float *x, int n) { double p = 0; for (int i = 0; i < n; i++) if (fabs(x[i]) > p) p = fabs(x[i]); return p; }
static void wav_write(const char *fn, const float *x, int n, double gain)
{
    FILE *f = fopen(fn, "wb"); if (!f) return;
    unsigned sr = 32000, br = sr * 2, ds = n * 2, rs = 36 + ds, fl = 16; unsigned short one = 1, bits = 16, ba = 2;
    fwrite("RIFF", 1, 4, f); fwrite(&rs, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fl, 4, 1, f);
    fwrite(&one, 2, 1, f); fwrite(&one, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&ds, 4, 1, f);
    for (int i = 0; i < n; i++) { double v = x[i] * gain; v = v > 1 ? 1 : v < -1 ? -1 : v; short q = (short)(v * 32767); fwrite(&q, 2, 1, f); }
    fclose(f);
}
