// HOWL GUARD, UI task: finds a howl in the input and puts a notch on it.
//
// A board stuck on a guitar's top has its mic a few centimetres from the strings and from its own speaker, and an
// amp in the room is not far either: one frequency can start to feed back and climb into a whistle. Every other
// tick the last 32 ms of the input (after LOW CUT and HIGH CUT and the notches already set) is windowed and
// transformed. A howl is a narrow peak from 1 kHz to 14 kHz, 22 dB over the spectrum around it, with at most six
// other peaks within 12 dB of it there, its own harmonics aside (a held tone's harmonics are a long row of peaks, a
// howl is a line or a few), louder than -42 dBFS, in the same place for 12 looks (a quarter of a second; a look that
// does not fit takes one off) and not dying away: a plucked note's high
// partials fall several dB in that time, a howl sits at the limiter or climbs. It gets a notch (Q 10, faded in
// over 50 ms by the audio task) at its frequency. Four notches at most; a fifth takes the oldest's place; a notch
// is let go 30 s after its howl was last seen, and caught again if it comes back. HOWL GUARD OFF clears them.
#include "inst_nylon.h"
#include "dsp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FN    1024
#define LO    32                                   // 1 kHz (31.25 Hz a bin)
#define HI    448                                  // 14 kHz
#define LOOKS 12
#define LIFE  30.0f

static UI_RAM float re[FN], im[FN], db[FN / 2];
static UI_RAM float win[FN];
static bool win_ok;
typedef struct { float hz, life, age; } gnotch_t;
static gnotch_t gn[4];
static int cand_bin = -1, cand_n, total;
static float cand_l0;

int guard_count(void) { return total; }
int guard_active(float *hz, int max)
{
    int n = 0;
    for (int k = 0; k < 4 && n < max; k++) if (gn[k].hz > 0.0f) hz[n++] = gn[k].hz;
    return n;
}

static void fft(float *xr, float *xi, int n)
{
    for (int i = 1, j = 0; i < n; i++) {                    // bit reversal
        int b = n >> 1;
        for (; j & b; b >>= 1) j ^= b;
        j |= b;
        if (i < j) { float t = xr[i]; xr[i] = xr[j]; xr[j] = t; t = xi[i]; xi[i] = xi[j]; xi[j] = t; }
    }
    for (int len = 2; len <= n; len <<= 1) {
        float a = -6.2831853f / (float)len, wr = cosf(a), wi = sinf(a);
        for (int i = 0; i < n; i += len) {
            float cr = 1.0f, ci = 0.0f;
            for (int k = 0; k < len / 2; k++) {
                int p = i + k, q = p + len / 2;
                float tr = xr[q] * cr - xi[q] * ci, ti = xr[q] * ci + xi[q] * cr;
                xr[q] = xr[p] - tr; xi[q] = xi[p] - ti;
                xr[p] += tr; xi[p] += ti;
                float nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr;
            }
        }
    }
}

static void set_notch(float hz)
{
    int k = -1;
    for (int i = 0; i < 4; i++) if (gn[i].hz > 0.0f && fabsf(gn[i].hz - hz) < 0.04f * hz) { gn[i].life = LIFE; return; }   // there already
    for (int i = 0; i < 4 && k < 0; i++) if (gn[i].hz <= 0.0f) k = i;
    if (k < 0) { k = 0; for (int i = 1; i < 4; i++) if (gn[i].age > gn[k].age) k = i; }      // the oldest makes room
    gn[k] = (gnotch_t){ hz, LIFE, 0.0f };
    E.notch_hz[k] = hz; E.notch_on[k] = 1;
    total++;
    char b[24];
    snprintf(b, sizeof b, "HOWL CUT %.1fK", (double)(hz * 0.001f));
    toast(b, 1200);
}

void guard_tick(void)
{
    const float dt = 0.02f;
    if (pv[P_GUARD] < 1.0f) {
        for (int k = 0; k < 4; k++) { gn[k].hz = 0.0f; E.notch_on[k] = 0; }
        cand_n = 0;
        return;
    }
    for (int k = 0; k < 4; k++) if (gn[k].hz > 0.0f) {
        gn[k].age += dt;
        if ((gn[k].life -= dt) <= 0.0f) { gn[k].hz = 0.0f; E.notch_on[k] = 0; }
    }
    if (!win_ok) { for (int i = 0; i < FN; i++) win[i] = 0.5f - 0.5f * cosf(6.2831853f * (float)i / (float)FN); win_ok = true; }
    uint32_t w = __atomic_load_n(&eng_hf_w, __ATOMIC_ACQUIRE);
    for (int i = 0; i < FN; i++) { re[i] = eng_hf[(w - FN + (uint32_t)i) & (HF_N - 1)] * win[i]; im[i] = 0.0f; }
    fft(re, im, FN);
    // dB of a sine's amplitude (Hann: a sine of amplitude a gives |X| = a N / 4)
    int kmax = -1; float dmax = -200.0f;
    for (int k = LO - 24; k < HI + 24; k++) {
        float m = sqrtf(re[k] * re[k] + im[k] * im[k]) * (4.0f / (float)FN);
        db[k] = 20.0f * log10f(m + 1e-9f);
        if (k >= LO && k < HI && db[k] > dmax) { dmax = db[k]; kmax = k; }
    }
    bool howl = false;
    if (kmax > 0 && dmax > -42.0f) {
        float sum = 0; int nsum = 0;
        for (int k = kmax - 24; k <= kmax + 24; k++) { if (k > kmax - 4 && k < kmax + 4) continue; sum += db[k]; nsum++; }
        // a howl is a line or two (a loop can ring at two places at once, a few hundred Hz apart); a held tone (an
        // organ, a sawtooth, a chord) is a row of harmonics: count the peaks within 12 dB of the top one, not counting
        // its own harmonics (a howl at the limiter or in the fuzz has them, at 2, 3, 4.. times it)
        float around = sum / (float)nsum;
        int rivals = 0;
        for (int k = LO; k < HI; k++) {
            if (k >= kmax - 2 && k <= kmax + 2) continue;
            if (!(db[k] > db[k - 1] && db[k] >= db[k + 1] && db[k] > dmax - 12.0f)) continue;
            int h = (k + kmax / 2) / kmax;                       // the nearest multiple of the line
            if (h >= 2 && abs(k - h * kmax) <= 2 + h) continue;
            rivals++;
        }
        bool same = cand_bin >= 0 && kmax >= cand_bin - 2 && kmax <= cand_bin + 2;
        if (dmax - around > 22.0f && rivals <= 6) {
            if (same) cand_n++;
            else { cand_bin = kmax; cand_n = 1; cand_l0 = dmax; }
            if (cand_n >= LOOKS && dmax > cand_l0 - 1.5f) howl = true;
            else if (cand_n >= LOOKS) { cand_n = 0; cand_bin = -1; }       // it fell: a note, not a howl
        } else if (same && cand_n > 0) cand_n--;                         // one look that does not fit: a step back, not a restart
        else { cand_n = 0; cand_bin = -1; }
    } else { cand_n = 0; cand_bin = -1; }
    if (howl) {
        float a = db[kmax - 1], b = db[kmax], c = db[kmax + 1], den = a - 2.0f * b + c;
        float off = fabsf(den) > 1e-6f ? clampf(0.5f * (a - c) / den, -0.5f, 0.5f) : 0.0f;
        set_notch(((float)kmax + off) * ((float)AUDIO_SR / (float)FN));
        cand_n = 0; cand_bin = -1;
    }
}
