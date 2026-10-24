// DWELL's WAVE source, the UI task's half: the built-in table (a voice-like spectrum, so WAVE sounds before any take)
// and the take's analysis. wave_analyse() is called every tick after a take until it returns true; each call does a
// slice of the work (a few hundred microseconds on the board):
//
//   1  where to look: the take's middle (the start and the end of a sung note waver), its level
//   2  the period: the normalised autocorrelation (McLeod's NSDF) over 60 Hz .. 1 kHz, a few lags a call; the first
//      peak within 85 % of the best wins (so an octave below is not taken), refined by a parabola
//   3  one cycle: up to eight periods in a row, each resampled to WAVE_N points, averaged
//   4  its first 16 harmonics (a DFT): the noise and what would alias at a high ratio are left out
//   5  the table rebuilt from them, DC gone, peak 0.8, into the table not playing; the audio task crossfades to it
#include "inst_eng.h"
#include "dsp.h"
#include <string.h>

#define AN_N     2048                       // samples compared per lag
#define LAG_MIN  (AUDIO_SR / 1000)          // 1 kHz
#define LAG_MAX  (AUDIO_SR / 60)            // 60 Hz
#define LAGS_PER 20                         // lags a call
#define NH       16                         // harmonics kept

static UI_RAM float an_x[AN_N + LAG_MAX + 2];
static UI_RAM float an_r[LAG_MAX + 2];
static UI_RAM float an_cyc[WAVE_N];
static float an_re[NH + 1], an_im[NH + 1];
static int   an_state, an_lag, an_from, an_len, an_k;
static float an_period;

void dw_wave_default(float *t)
{
    // 1/n with two formant bumps (around the 5th and the 11th harmonic): an open "ah" on any note
    for (int i = 0; i < WAVE_N; i++) t[i] = 0.0f;
    for (int h = 1; h <= 24; h++) {
        float fh = (float)h;
        float a = 1.0f / fh;
        a *= 1.0f + 2.5f * expf(-(fh - 5.0f) * (fh - 5.0f) / 3.0f) + 1.5f * expf(-(fh - 11.0f) * (fh - 11.0f) / 6.0f);
        float ph = 0.13f * fh * fh;                                    // spread phases: a softer peak
        for (int i = 0; i < WAVE_N; i++) t[i] += a * sin_turns(fh * (float)i / (float)WAVE_N + ph);
    }
    float pk = 0.0f;
    for (int i = 0; i < WAVE_N; i++) if (fabsf(t[i]) > pk) pk = fabsf(t[i]);
    for (int i = 0; i < WAVE_N; i++) t[i] *= 0.8f / pk;
    t[WAVE_N] = t[0];
}

static bool fail(void)
{
    eng_wave_hz = 0.0f;
    an_state = 0;
    return true;
}

bool wave_analyse(void)
{
    switch (an_state) {
    case 0: {                                                          // where to look
        int n = eng_take_frames;
        if (!dw_take || n < AUDIO_SR / 3) return fail();
        int skip = n / 4, need = AN_N + LAG_MAX + 2;
        if (n - skip - n / 8 < need) skip = n - n / 8 - need;
        if (skip < 0) skip = 0;
        if (skip + need > n) return fail();
        an_from = skip; an_len = n - n / 8 - skip;
        float pk = 0.0f, mean = 0.0f;
        for (int i = 0; i < need; i++) mean += (float)dw_take[skip + i];
        mean /= (float)need;
        for (int i = 0; i < need; i++) { float v = ((float)dw_take[skip + i] - mean) * (1.0f / 32768.0f); an_x[i] = v; if (fabsf(v) > pk) pk = fabsf(v); }
        if (pk < 0.01f) return fail();                                 // -40 dBFS: nothing sung
        an_lag = LAG_MIN; an_state = 1;
        return false;
    }
    case 1: {                                                          // the NSDF, a few lags a call
        int end = an_lag + LAGS_PER; if (end > LAG_MAX + 1) end = LAG_MAX + 1;
        for (int t = an_lag; t <= end; t++) {
            float acf = 0.0f, m = 0.0f;
            for (int i = 0; i < AN_N; i++) { float a = an_x[i], b = an_x[i + t]; acf += a * b; m += a * a + b * b; }
            an_r[t] = m > 0.0f ? 2.0f * acf / m : 0.0f;
        }
        an_lag = end + 1;
        if (an_lag > LAG_MAX) an_state = 2;
        return false;
    }
    case 2: {                                                          // the period
        float best = 0.0f;
        for (int t = LAG_MIN + 1; t < LAG_MAX; t++) if (an_r[t] > best && an_r[t] >= an_r[t - 1] && an_r[t] >= an_r[t + 1]) best = an_r[t];
        if (best < 0.6f) return fail();                                // no steady tone
        int pick = -1;
        for (int t = LAG_MIN + 1; t < LAG_MAX && pick < 0; t++)
            if (an_r[t] >= 0.85f * best && an_r[t] >= an_r[t - 1] && an_r[t] >= an_r[t + 1]) pick = t;
        float a = an_r[pick - 1], b = an_r[pick], c = an_r[pick + 1], den = a - 2.0f * b + c;
        float off = fabsf(den) > 1e-9f ? 0.5f * (a - c) / den : 0.0f;
        an_period = (float)pick + clampf(off, -0.5f, 0.5f);
        an_state = 3;
        return false;
    }
    case 3: {                                                          // one cycle: up to eight averaged
        int k = (int)((float)an_len / an_period) - 1;
        if (k > 8) k = 8;
        if (k < 1) return fail();
        memset(an_cyc, 0, sizeof an_cyc);
        float step = an_period / (float)WAVE_N;
        for (int p = 0; p < k; p++) {
            float base = (float)an_from + (float)p * an_period;
            for (int i = 0; i < WAVE_N; i++) {
                float pos = base + (float)i * step;
                int j = (int)pos; float fr = pos - (float)j;
                float v0 = (float)dw_take[j], v1 = (float)dw_take[j + 1];
                an_cyc[i] += v0 + (v1 - v0) * fr;
            }
        }
        an_state = 4; an_k = 1;
        return false;
    }
    case 4: {                                                          // its harmonics, four a call
        for (int h = an_k; h < an_k + 4 && h <= NH; h++) {
            float re = 0.0f, im = 0.0f;
            for (int i = 0; i < WAVE_N; i++) {
                float ph = (float)(h * i) / (float)WAVE_N;
                re += an_cyc[i] * sin_turns(ph + 0.25f);
                im += an_cyc[i] * sin_turns(ph);
            }
            an_re[h] = re; an_im[h] = im;
        }
        an_k += 4;
        if (an_k > NH) { an_state = 5; an_k = 0; }
        return false;
    }
    case 5: {                                                          // the table, into the one not playing
        if (dw_wave_busy || dw_wave_want != dw_wave_cur) return false; // a crossfade still runs: wait
        int spare = dw_wave_cur ^ 1;
        float *t = dw_wtab[spare];
        int i0 = an_k, i1 = an_k + WAVE_N / 4;                          // a quarter of the table a call
        for (int i = i0; i < i1; i++) {
            float s = 0.0f;
            for (int h = 1; h <= NH; h++) {
                float ph = (float)(h * i) / (float)WAVE_N;
                s += an_re[h] * sin_turns(ph + 0.25f) + an_im[h] * sin_turns(ph);
            }
            t[i] = s;
        }
        an_k = i1;
        if (an_k < WAVE_N) return false;
        float pk = 0.0f;
        for (int i = 0; i < WAVE_N; i++) if (fabsf(t[i]) > pk) pk = fabsf(t[i]);
        if (pk <= 0.0f) return fail();
        for (int i = 0; i < WAVE_N; i++) t[i] *= 0.8f / pk;
        t[WAVE_N] = t[0];
        eng_wave_hz = (float)AUDIO_SR / an_period;
        eng_wave_ready = 1;
        __atomic_store_n(&dw_wave_want, spare, __ATOMIC_RELEASE);
        an_state = 0;
        return true;
    }
    default:
        an_state = 0;
        return true;
    }
}
