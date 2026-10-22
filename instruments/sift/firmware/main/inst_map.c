// SIFT's ear and map (UI task). Each finished 32 ms unit of the corpus is measured:
//   LOUD    its level, dB
//   BRIGHT  the frequency of its average wiggle: the first difference's RMS over the signal's gives 2 sin(pi f / fs)
//           for a sine, so asin turns it back into Hz; for noise it lands near a quarter of the rate
//   pitch   the normalised square difference (McLeod and Wyvill's NSDF) of the unit taken down to 16 kHz, lags for
//           62 Hz to 2 kHz; the first peak within 90 % of the highest, refined by a parabola. Its height is how
//           periodic the unit is: NOISE is one minus that
//   TONE    the pitch when there is one, else the brightness: "the note it sounds like"
//   TIME    the order the units were heard in
// A take's level is evened out when it ends (its peak to half scale) and what lies 42 dB under that peak is silence:
// silence is not on the map and never plays. The map's axes are any two measures, each spread over what the corpus
// covers (see map_update), so the dots always fill the screen; the screen eases them to their new places.
#include "inst_sift.h"
#include "dsp.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

UI_RAM unit_t units[NU_MAX];                    // only the UI task reads them (the audio task gets the catch)
float map_lo[ND], map_hi[ND];
static int ax_x = D_TONE, ax_y = D_NOISE;
static int take_no, nok;
static float stamp;
static bool dirty;

static const float min_span[ND] = { 1.0f, 1.5f, 1.5f, 12.0f, 0.25f };

void map_set_axes(int ax, int ay) { ax_x = clampi(ax, 0, ND - 1); ax_y = clampi(ay, 0, ND - 1); }
int  map_axis_x(void) { return ax_x; }
int  map_axis_y(void) { return ax_y; }
int  map_count(void) { return nok; }

void map_init(void)
{
    map_reset();
    for (int k = 0; k < ND; k++) { map_lo[k] = 0.0f; map_hi[k] = 1.0f; }
}

void map_reset(void)
{
    for (int u = 0; u < NU_MAX; u++) { units[u].state = U_EMPTY; units[u].born = 0; }
    nok = 0; dirty = true;
}

int map_take_begin(void) { return ++take_no; }

void map_forget(int u)
{
    if (u < 0 || u >= NU_MAX) return;
    if (units[u].state != U_EMPTY) { units[u].state = U_EMPTY; dirty = true; }
}

// ---------------------------------------------------------------- measuring a piece of sound
static UI_RAM float sx_buf[UNIT], sy_buf[UNIT / 2];

void map_measure(const float *x, int n, desc_t *d)
{
    if (n > UNIT) n = UNIT;
    float s2 = 0, d2 = 0;
    float prev = x[0];
    for (int i = 0; i < n; i++) { float v = x[i]; s2 += v * v; float df = v - prev; d2 += df * df; prev = v; }
    float rms = sqrtf(s2 / (float)n), drms = sqrtf(d2 / (float)n);
    d->level = rms;
    d->d[D_LOUD] = 20.0f * log10f(rms + 1e-9f);
    float ratio = rms > 1e-9f ? drms / rms : 0.0f;
    float fr = (float)AUDIO_SR / 3.14159265f * asinf(fminf(ratio * 0.5f, 1.0f));
    if (fr < 30.0f) fr = 30.0f;
    d->d[D_BRIGHT] = log2f(fr);
    d->d[D_TIME] = 0.0f;
    d->pitch = -1.0f;
    float clarity = 0.0f;
    if (d->d[D_LOUD] > -70.0f) {
        // down to 16 kHz (each pair averaged), then the NSDF for lags 8..200
        int m = n / 2;
        float e = 0;
        for (int i = 0; i < m; i++) { float v = 0.5f * (x[2 * i] + x[2 * i + 1]); sy_buf[i] = v; e += v * v; }
        const int t0 = 8, t1 = m - 112 < 256 ? m - 112 : 256;
        float mm = 2.0f * e;
        float best_v = 0, cand_t[24], cand_v[24];
        int nc = 0;
        bool neg = false, in_lobe = false;
        float lobe_v = 0, lobe_t = 0, prev_n = 1.0f;
        for (int t = 1; t <= t1 + 1; t++) {
            mm -= sy_buf[t - 1] * sy_buf[t - 1] + sy_buf[m - t] * sy_buf[m - t];
            if (t < t0 - 1) continue;
            float r = 0;
            for (int i = 0; i < m - t; i++) r += sy_buf[i] * sy_buf[i + t];
            float nv = mm > 1e-12f ? 2.0f * r / mm : 0.0f;
            if (!neg) { if (nv < 0.0f) neg = true; prev_n = nv; continue; }
            if (!in_lobe && prev_n <= 0.0f && nv > 0.0f) { in_lobe = true; lobe_v = nv; lobe_t = (float)t; }
            else if (in_lobe) {
                if (nv > lobe_v) { lobe_v = nv; lobe_t = (float)t; }
                if (nv <= 0.0f || t == t1 + 1) {
                    if (nc < 24) { cand_t[nc] = lobe_t; cand_v[nc] = lobe_v; nc++; }
                    if (lobe_v > best_v) best_v = lobe_v;
                    in_lobe = false;
                }
            }
            prev_n = nv;
        }
        for (int k = 0; k < nc; k++) {
            if (cand_v[k] < 0.9f * best_v) continue;
            // refine by a parabola through the three lags round the peak
            int t = (int)cand_t[k];
            float a, b = 0, c;
            {
                float r0 = 0, r1 = 0, r2 = 0, m0 = 0, m1 = 0, m2 = 0;
                for (int i = 0; i < m - (t + 1); i++) {
                    r0 += sy_buf[i] * sy_buf[i + t - 1]; r1 += sy_buf[i] * sy_buf[i + t]; r2 += sy_buf[i] * sy_buf[i + t + 1];
                    m0 += sy_buf[i] * sy_buf[i] + sy_buf[i + t - 1] * sy_buf[i + t - 1];
                    m1 += sy_buf[i] * sy_buf[i] + sy_buf[i + t] * sy_buf[i + t];
                    m2 += sy_buf[i] * sy_buf[i] + sy_buf[i + t + 1] * sy_buf[i + t + 1];
                }
                a = m0 > 0 ? 2 * r0 / m0 : 0; b = m1 > 0 ? 2 * r1 / m1 : 0; c = m2 > 0 ? 2 * r2 / m2 : 0;
            }
            float den = a - 2.0f * b + c, dt = fabsf(den) > 1e-9f ? 0.5f * (a - c) / den : 0.0f;
            dt = clampf(dt, -0.5f, 0.5f);
            float lag = (float)t + dt;
            clarity = cand_v[k];
            if (clarity > 0.8f && lag > 1.0f) {
                float f0 = 0.5f * (float)AUDIO_SR / lag;
                d->pitch = 69.0f + 12.0f * log2f(f0 / 440.0f);
            }
            break;
        }
    }
    d->d[D_NOISE] = clampf(1.0f - clarity, 0.0f, 1.0f);
    d->d[D_TONE] = d->pitch >= 0.0f ? log2f(440.0f) + (d->pitch - 69.0f) / 12.0f : d->d[D_BRIGHT];
}

void map_listen(int u, int take)
{
    if (u < 0 || u >= corp_units || !corp) return;
    const int16_t *b = corp + (size_t)u * UNIT;
    int pk = 0;
    for (int i = 0; i < UNIT; i++) { int v = b[i] < 0 ? -b[i] : b[i]; if (v > pk) pk = v; sx_buf[i] = (float)b[i] * (1.0f / 32768.0f); }
    desc_t d;
    map_measure(sx_buf, UNIT, &d);
    unit_t *un = &units[u];
    memcpy(un->d, d.d, sizeof d.d);
    un->d[D_TIME] = stamp; stamp += 1.0f;
    un->pitch = d.pitch;
    un->peak = (float)pk;
    un->take = (uint16_t)take;
    // its attack: the first sample over half its peak, a little before
    int on = 0, thr = pk / 2;
    for (int i = 0; i < UNIT; i++) { int v = b[i] < 0 ? -b[i] : b[i]; if (v > thr) { on = i; break; } }
    un->onset = (uint16_t)(on > 24 ? on - 24 : 0);
    // the take so far: its peak sets the level and what counts as silence (again at its end)
    float tpk = 0;
    for (int k = 0; k < corp_units; k++) if (units[k].take == take && units[k].state != U_EMPTY && units[k].peak > tpk) tpk = units[k].peak;
    if (un->peak > tpk) tpk = un->peak;
    if (tpk < 160.0f) tpk = 160.0f;
    un->gain = 0.5f / tpk;
    bool silent = d.d[D_LOUD] < -62.0f || d.d[D_LOUD] < 20.0f * log10f(tpk / 32768.0f) - 42.0f;
    un->state = silent ? U_SILENT : U_OK;
    un->born = 0;
    dirty = true;
}

void map_take_end(int take)
{
    float tpk = 0;
    for (int k = 0; k < corp_units; k++) if (units[k].take == take && units[k].state != U_EMPTY && units[k].peak > tpk) tpk = units[k].peak;
    if (tpk < 160.0f) tpk = 160.0f;
    float floor_db = 20.0f * log10f(tpk / 32768.0f) - 42.0f;
    for (int k = 0; k < corp_units; k++) {
        unit_t *un = &units[k];
        if (un->take != take || un->state == U_EMPTY) continue;
        un->gain = 0.5f / tpk;
        un->state = un->d[D_LOUD] < -62.0f || un->d[D_LOUD] < floor_db ? U_SILENT : U_OK;
    }
    dirty = true;
}

// ---------------------------------------------------------------- the map
// Each axis is half the measure's range (min to max) and half its rank among the units: the range keeps what is near
// near, the rank spreads the dots over the whole map, so wherever the cursor goes there are grains, and the middle
// of the map is the middle of the material. The coordinates are worked out again when the units or the axes change.
typedef struct { float v; int16_t u; } rank_t;
static UI_RAM rank_t rk[2][NU_MAX];
static UI_RAM float ux_c[NU_MAX], uy_c[NU_MAX];
static int nrk;
static int c_ax = -1, c_ay = -1;

static int by_v(const void *a, const void *b) { float x = ((const rank_t *)a)->v, y = ((const rank_t *)b)->v; return x < y ? -1 : x > y; }

static float mm(int axis, float v)
{
    float span = map_hi[axis] - map_lo[axis];
    return span > 1e-6f ? clampf((v - map_lo[axis]) / span, 0.0f, 1.0f) : 0.5f;
}

void map_update(void)
{
    if (dirty || c_ax != ax_x || c_ay != ax_y) {
        dirty = false; c_ax = ax_x; c_ay = ax_y;
        float lo[ND], hi[ND];
        for (int k = 0; k < ND; k++) { lo[k] = 1e9f; hi[k] = -1e9f; }
        nok = 0;
        for (int u = 0; u < corp_units; u++) {
            if (units[u].state != U_OK) continue;
            for (int k = 0; k < ND; k++) { float v = units[u].d[k]; if (v < lo[k]) lo[k] = v; if (v > hi[k]) hi[k] = v; }
            rk[0][nok] = (rank_t){ units[u].d[ax_x], (int16_t)u };
            rk[1][nok] = (rank_t){ units[u].d[ax_y], (int16_t)u };
            nok++;
        }
        if (nok) for (int k = 0; k < ND; k++) {
            float span = hi[k] - lo[k];
            if (span < min_span[k]) { float c = 0.5f * (hi[k] + lo[k]); lo[k] = c - 0.5f * min_span[k]; hi[k] = c + 0.5f * min_span[k]; span = min_span[k]; }
            map_lo[k] = lo[k] - 0.03f * span; map_hi[k] = hi[k] + 0.03f * span;
        }
        nrk = nok;
        qsort(rk[0], (size_t)nok, sizeof(rank_t), by_v);
        qsort(rk[1], (size_t)nok, sizeof(rank_t), by_v);
        float inv = nok > 1 ? 1.0f / (float)(nok - 1) : 0.0f;
        for (int i = 0; i < nok; i++) {
            int u = rk[0][i].u; ux_c[u] = 0.5f * mm(ax_x, rk[0][i].v) + 0.5f * (nok > 1 ? (float)i * inv : 0.5f);
            u = rk[1][i].u;     uy_c[u] = 0.5f * mm(ax_y, rk[1][i].v) + 0.5f * (nok > 1 ? (float)i * inv : 0.5f);
        }
    }
    for (int u = 0; u < corp_units; u++) if (units[u].born < 255) units[u].born++;
}

// where any measured sound would sit on the map: its range, and its rank by where it falls among the units
float map_norm(int axis, float v)
{
    int k = axis == ax_x ? 0 : axis == ax_y ? 1 : -1;
    if (k < 0 || nrk < 2) return mm(axis, v);
    int a = 0, b = nrk;
    while (a < b) { int m = (a + b) / 2; if (rk[k][m].v < v) a = m + 1; else b = m; }
    float r = clampf((float)a / (float)(nrk - 1), 0.0f, 1.0f);
    return 0.5f * mm(axis, v) + 0.5f * r;
}
float map_ux(int u) { return ux_c[u]; }
float map_uy(int u) { return uy_c[u]; }
float map_tone_of(int u) { return 69.0f + 12.0f * (units[u].d[D_TONE] - log2f(440.0f)); }
