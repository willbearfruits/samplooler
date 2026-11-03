// SIFT's screens. DECOMPOSE: the recording drawn as a waveform across the screen, the head running through it; as it
// comes apart the waveform crumbles (its columns shake, lose their lines, turn to drifting dust), each grain lights the
// place it came from, and a grain from far away draws a thread back to the head. Daisy's words run along the top, the
// syllable heard underlined; they scramble and fall as the sound does. The stage (WHOLE, WEARING, SYLLABLES,
// PHONEMES, DUST) and the head's speed sit in the bottom corners.
//
// MAP: every unit of the corpus a dot, placed by what it sounds like. New
// units fall in from the top, like sand through a sieve, and land where they belong. Towards ORDER the dots snap to
// a lattice and the pattern the cursor holds is drawn as a constellation, its notes lighting up in turn; towards
// CHAOS the lattice melts, the dots circle their places faster and wider, and the cursor's catch opens into a turning
// ring. Each grain that plays sparks where it came from; a shake scatters the dots and they spring back. The header
// holds the state (REC, LIVE, LOAD, SAVE), the knob's gauge from a little grid to a spray of dots, the beat, the link
// and the meter.
#include "inst_sift.h"
#include "ui.h"
#include "front.h"
#include "link.h"
#include "dsp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static UI_RAM float ds_x[NU_MAX], ds_y[NU_MAX];       // each dot's place on the map, eased
static UI_RAM float kx[NU_MAX], ky[NU_MAX], kvx[NU_MAX], kvy[NU_MAX];   // the shake's push, on springs
static parts_t parts;
static uint32_t pseed = 0x9e3779b9u;
static float t_now;
static float pulse;                                    // the order grain that just played
static int   pulse_unit = -1;
static uint32_t pulse_frame;
static float shook;

static inline float hash01(int u, int k)
{
    uint32_t h = (uint32_t)u * 2654435761u ^ (uint32_t)k * 40503u;
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
    return (float)(h & 0xFFFF) / 65535.0f;
}

// a dotted line: pixel k is drawn when bit (k mod 8) of pat is set
static void dline(int x0, int y0, int x1, int y1, uint8_t pat)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy, k = 0;
    for (;;) {
        if (pat & (1u << (k++ & 7))) oled_pixel(x0, y0, true);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void dashed_circle(int cx, int cy, float r, float turn)
{
    int n = (int)(r * 6.2831853f / 2.0f);
    if (n < 8) n = 8;
    for (int i = 0; i < n; i++) {
        if ((i / 2) & 1) continue;
        float a = (float)i / (float)n + turn;
        int x = cx + (int)lroundf(r * sin_turns(a + 0.25f)), y = cy + (int)lroundf(r * sin_turns(a));
        if (y >= 16) oled_pixel(x, y, true);
    }
}

// where unit u's dot is drawn: its place, snapped to the lattice towards order, circling towards chaos, pushed by a
// shake, falling in when new
static void dot_pos(int u, float *x, float *y)
{
    float ce = S.chaos;
    float tx = ds_x[u], ty = ds_y[u];
    float lx = (float)MAP_X0 + (float)(int)((tx - (float)MAP_X0) * 0.25f + 0.5f) * 4.0f;
    float ly = (float)MAP_Y1 - (float)(int)(((float)MAP_Y1 - ty) * 0.25f + 0.5f) * 4.0f;
    float a = smoothstep(0.03f, 0.4f, ce);
    float px = lx + (tx - lx) * a, py = ly + (ty - ly) * a;
    float r = ce * ce * 2.5f + S.shake * 2.0f;
    if (r > 0.05f) {
        float ph = hash01(u, 1) + t_now * (0.3f + 0.5f * hash01(u, 2)) * (0.4f + 1.6f * ce);
        px += r * sin_turns(ph + 0.25f); py += 0.8f * r * sin_turns(ph);
    }
    px += kx[u]; py += ky[u];
    int born = units[u].born;
    if (born < 40) { float t = ease_out((float)born / 40.0f); py = (float)(MAP_Y0 - 3) + (py - (float)(MAP_Y0 - 3)) * t; }
    *x = px; *y = py;
}

static int16_t tl_at_s[NU_MAX];                       // each unit's column on the waveform, from the last frame drawn (-1: none)
static bool tl_have;

void scene_spawn(const spawn_t *s)
{
    if (s->unit < 0 || s->unit >= corp_units) return;
    if (S.mode == MODE_DECOMP) {                       // on the waveform: a stammer rings, a piece from far away sparks
        if (!tl_have || tl_at_s[s->unit] < 0 || s->kind == GK_ORDER) return;
        float x = (float)tl_at_s[s->unit], y = 41.0f;
        if (s->kind == GK_REPEAT) parts_emit(&parts, x, y, 0, 0, 0.25f, PART_RING, 1);
        else parts_emit(&parts, x, y, 40.0f * rng_bi(&pseed), -30.0f - 30.0f * rng_uni(&pseed), s->kind == GK_BURST ? 0.5f : 0.3f,
                        s->kind == GK_BURST ? PART_STREAK : PART_SPARK, 1);
        return;
    }
    float x, y; dot_pos(s->unit, &x, &y);
    if (s->kind == GK_ORDER) {
        if (s->frame != pulse_frame) { pulse = 1.0f; pulse_unit = s->unit; pulse_frame = s->frame; }
        parts_emit(&parts, x, y, 0, 0, 0.3f, PART_RING, 1);
    } else {
        float cx = map_px(S.cx), cy = map_py(S.cy);
        float dx = x - cx, dy = y - cy, l = sqrtf(dx * dx + dy * dy) + 1.0f;
        float sp = s->kind == GK_BURST ? 90.0f : 30.0f;
        parts_emit(&parts, x, y, dx / l * sp + 20.0f * rng_bi(&pseed), dy / l * sp + 20.0f * rng_bi(&pseed), s->kind == GK_BURST ? 0.5f : 0.25f,
                   s->kind == GK_BURST ? PART_STREAK : PART_SPARK, 1);
    }
}

void scene_shake(float amount)
{
    shook = 1.0f;
    for (int u = 0; u < corp_units; u++) {
        float a = rng_uni(&pseed);
        kvx[u] += amount * 160.0f * sin_turns(a + 0.25f) * (0.5f + rng_uni(&pseed));
        kvy[u] += amount * 110.0f * sin_turns(a) * (0.5f + rng_uni(&pseed));
    }
}

static void physics(void)
{
    const float dt = 0.04f;
    for (int u = 0; u < corp_units; u++) {
        if (units[u].state != U_OK) continue;
        float tx = map_px(map_ux(u)), ty = map_py(map_uy(u));
        if (units[u].born < 3) { ds_x[u] = tx; ds_y[u] = ty; }
        else { ds_x[u] += (tx - ds_x[u]) * 0.3f; ds_y[u] += (ty - ds_y[u]) * 0.3f; }
        if (kx[u] != 0.0f || kvx[u] != 0.0f || ky[u] != 0.0f || kvy[u] != 0.0f) {
            kvx[u] += (-60.0f * kx[u] - 6.0f * kvx[u]) * dt; kx[u] += kvx[u] * dt;
            kvy[u] += (-60.0f * ky[u] - 6.0f * kvy[u]) * dt; ky[u] += kvy[u] * dt;
            if (fabsf(kx[u]) + fabsf(ky[u]) + 0.05f * (fabsf(kvx[u]) + fabsf(kvy[u])) < 0.05f) kx[u] = ky[u] = kvx[u] = kvy[u] = 0.0f;
        }
    }
    parts_step(&parts, dt, 0.0f, 2.5f);
    pulse *= 0.82f;
    shook *= 0.9f;
}

// ---------------------------------------------------------------- the header
static void order_glyph(int x, int y)
{
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) oled_pixel(x + 2 * i, y + 2 * j, true);
}
static void chaos_glyph(int x, int y)
{
    static const int8_t p[][2] = { { 0, 1 }, { 2, 0 }, { 5, 2 }, { 1, 4 }, { 4, 5 }, { 6, 4 }, { 3, 3 } };
    for (int i = 0; i < 7; i++) oled_pixel(x + p[i][0], y + p[i][1], true);
}

static void header(const view_t *v)
{
    const front_status_t *st = front_status();
    char b[24];
    int x_end;
    if (S.job == JOB_RENDER) { snprintf(b, sizeof b, "%s %d%%", S.job_what == BK_DAISY ? "DAISY" : "AMEN", (int)(S.job_frac * 100.0f)); x_end = oled_pill(F_SMALL, 0, 2, b, true); }
    else if (S.job == JOB_LOAD) { snprintf(b, sizeof b, "LOAD %d%%", (int)(S.job_frac * 100.0f)); x_end = oled_pill(F_SMALL, 0, 2, b, true); }
    else if (S.job == JOB_SAVE) { snprintf(b, sizeof b, "SAVE %d%%", (int)(S.job_frac * 100.0f)); x_end = oled_pill(F_SMALL, 0, 2, b, true); }
    else if (S.rec == REC_HOLD) { snprintf(b, sizeof b, "REC %.1f", (double)S.rec_s); x_end = oled_pill(F_SMALL, 0, 2, b, true); }
    else if (S.rec == REC_LIVE) x_end = oled_pill(F_SMALL, 0, 2, "LIVE", (v->frame / 8) % 2 == 0);
    else x_end = oled_ftext_ex(F_MED, 1, 3, "SIFT", 1);
    // the knob's gauge: a grid at one end, a spray at the other; the shake shows as a hollow mark ahead of the knob's
    int gx = x_end + 4;
    if (gx < 40) gx = 40;
    int gw = 96 - gx;
    if (gw > 20) {
        if (S.mode == MODE_DECOMP) oled_rect(gx, 6, 6, 3, true);       // whole: a solid bar
        else order_glyph(gx, 4);
        chaos_glyph(gx + gw - 7, 3);
        int tx = gx + 8, tw = gw - 17;
        oled_hline_pat(tx, 8, tw, 0x55, true);
        float knob = pv[P_CHAOS] * 0.01f;
        int mx = tx + (int)(knob * (float)(tw - 1) + 0.5f);
        oled_rect(mx - 1, 5, 3, 7, true);
        if (S.chaos > knob + 0.03f) { int sx = tx + (int)(S.chaos * (float)(tw - 1) + 0.5f); oled_frame(sx - 1, 5, 3, 7); }
    }
    // the beat: a square that blinks on each one
    float bp = sift_beat_phase();
    if (bp < 0.18f) oled_rect(99, 6, 3, 3, true); else oled_pixel(100, 7, true);
    // the link
    if (S.peers > 0) {
        int x = 106, y = 4;
        oled_line(x, y, x, y + 6, true);
        oled_pixel(x - 2, y - 1, true); oled_pixel(x + 2, y - 1, true); oled_pixel(x - 1, y, true); oled_pixel(x + 1, y, true);
    }
    if (st->frozen) { oled_frame(109, 7, 5, 4); oled_pixel(110, 5, true); oled_pixel(112, 5, true); oled_pixel(110, 6, true); oled_pixel(112, 6, true); oled_pixel(111, 4, true); }
    ui_meter(OLED_W - 11, 3, 11, 9, v->out_level, st->clip);
}

// ---------------------------------------------------------------- the map
static void corners(void)
{
    const int x0 = MAP_X0 - 2, x1 = MAP_X1 + 2, y0 = MAP_Y0 - 1, y1 = MAP_Y1 + 1;
    oled_line(x0, y0, x0 + 3, y0, true); oled_line(x0, y0, x0, y0 + 2, true);
    oled_line(x1, y0, x1 - 3, y0, true); oled_line(x1, y0, x1, y0 + 2, true);
    oled_line(x0, y1, x0 + 3, y1, true); oled_line(x0, y1, x0, y1 - 2, true);
    oled_line(x1, y1, x1 - 3, y1, true); oled_line(x1, y1, x1, y1 - 2, true);
}

static void axis_names(void)
{
    static const char *const nm[ND][2] = { { "OLD", "NEW" }, { "LOW", "HIGH" }, { "DARK", "BRIGHT" }, { "SOFT", "LOUD" }, { "TONAL", "NOISY" } };
    int ax = map_axis_x(), ay = map_axis_y();
    char b[24];
    snprintf(b, sizeof b, "%s>", nm[ax][1]);
    int w = oled_ftext_w(F_TINY, b, 1);
    oled_rect(MAP_X1 - w - 1, MAP_Y1 - 6, w + 2, 7, false);
    oled_ftext_ex(F_TINY, MAP_X1 - w, MAP_Y1 - 5, b, 1);
    w = oled_ftext_w(F_TINY, nm[ax][0], 1);
    oled_rect(MAP_X0 - 1, MAP_Y1 - 6, w + 2, 7, false);
    oled_ftext_ex(F_TINY, MAP_X0, MAP_Y1 - 5, nm[ax][0], 1);
    snprintf(b, sizeof b, "^%s", nm[ay][1]);
    w = oled_ftext_w(F_TINY, b, 1);
    oled_rect(MAP_X0 - 1, MAP_Y0 - 1, w + 2, 7, false);
    oled_ftext_ex(F_TINY, MAP_X0, MAP_Y0, b, 1);
}

static void draw_map(const view_t *v)
{
    float ce = S.chaos;
    int bottom = v->cover ? OLED_H - v->cover : OLED_H;
    // the dots; the loud and pitched a little bigger
    float lo = map_lo[D_LOUD], hi = map_hi[D_LOUD];
    float loud_cut = lo + 0.72f * (hi - lo);
    for (int u = 0; u < corp_units; u++) {
        const unit_t *un = &units[u];
        if (un->state != U_OK) continue;
        float x, y; dot_pos(u, &x, &y);
        int ix = (int)lroundf(x), iy = (int)lroundf(y);
        if (iy < 16 || iy >= bottom) continue;
        if (ce < 0.5f && un->d[D_LOUD] > loud_cut && un->pitch >= 0.0f) oled_rect(ix, iy, 2, 2, true);   // the lattice's beads
        else oled_pixel(ix, iy, true);
        if (un->born < 25 && un->born > 2) oled_circle(ix, iy, 1 + un->born / 6, true);   // a ripple where it lands
    }
    int cx = (int)lroundf(map_px(S.cx)), cy = (int)lroundf(map_py(S.cy));
    // the order side's pattern as a constellation, fading out towards chaos
    if (ce < 0.6f && S.nseq > 1) {
        static const uint8_t pats[] = { 0xFF, 0x77, 0x55, 0x11 };
        uint8_t pat = pats[clampi((int)(ce * 6.6f), 0, 3)];
        int k = S.nseq;
        for (int i = 0; i + 1 < k; i++) {
            int a = S.seq_unit[i], b = S.seq_unit[i + 1];
            if (a < 0 || b < 0 || a >= corp_units || b >= corp_units || a == b) continue;
            float x0, y0, x1, y1; dot_pos(a, &x0, &y0); dot_pos(b, &x1, &y1);
            dline((int)lroundf(x0), (int)lroundf(y0), (int)lroundf(x1), (int)lroundf(y1), pat);
        }
    }
    // the grain that played last on the order side: a flash and a widening ring
    if (pulse > 0.05f && pulse_unit >= 0 && units[pulse_unit].state == U_OK) {
        float x, y; dot_pos(pulse_unit, &x, &y);
        int ix = (int)lroundf(x), iy = (int)lroundf(y);
        if (pulse > 0.45f) oled_disc(ix, iy, 2, true);
        oled_circle(ix, iy, 2 + (int)((1.0f - pulse) * 5.0f), true);
    }
    // every grain sounding now: a square that breathes with its window
    for (int i = 0; i < NV; i++) {
        int u = eng_vis_unit[i];
        if (u < 0 || u >= corp_units) continue;
        float x, y; dot_pos(u, &x, &y);
        int ix = (int)lroundf(x), iy = (int)lroundf(y);
        if (eng_vis_env[i] > 0.6f && ce < 0.6f) oled_rect(ix - 1, iy - 1, 3, 3, true);
        else if (eng_vis_env[i] > 0.3f) { oled_pixel(ix - 1, iy, true); oled_pixel(ix + 1, iy, true); oled_pixel(ix, iy - 1, true); oled_pixel(ix, iy + 1, true); }
        else oled_pixel(ix, iy, true);
    }
    parts_draw(&parts);
    // the cursor: a crosshair; the chaos side's catch, a turning dashed ring
    if (ce > 0.06f && S.ring > 3.5f) dashed_circle(cx, cy, S.ring, t_now * (0.05f + 0.6f * ce));
    oled_circle(cx, cy, 2, false);
    oled_circle(cx, cy, 3, true);
    oled_line(cx - 7, cy, cx - 5, cy, true); oled_line(cx + 5, cy, cx + 7, cy, true);
    oled_line(cx, cy - 7, cx, cy - 5, true); oled_line(cx, cy + 5, cx, cy + 7, true);
    if ((int)pv[P_CURSOR] == CUR_VOICE) {                               // it listens: waves round it when you are heard
        float w = S.voice ? wrap1(t_now * 2.0f) : 0.0f;
        if (S.voice) { ui_arc(cx, cy, 5 + (int)(w * 6.0f), -60, 60); ui_arc(cx, cy, 5 + (int)(w * 6.0f), 120, 240); }
        else oled_ftext_c(F_TINY, cx, cy + 9 < bottom - 6 ? cy + 9 : cy - 14, "SING", 1);
    }
    corners();
}

static void draw_empty(const view_t *v)
{
    if (S.rec != REC_IDLE) {
        oled_ftext_c(F_SMALL, 64, 26, "LISTENING", 1);
        float e = clampf((20.0f * log10f(eng_rec_level + 1e-5f) + 50.0f) / 50.0f, 0.0f, 1.0f);
        oled_rframe(24, 38, 80, 7, 2);
        int w = (int)(e * 76.0f);
        if (w > 0) oled_rect(26, 40, w, 3, true);
        return;
    }
    // a sieve with nothing on it, and a grain now and then falling through
    oled_ftext_c(F_SMALL, 64, 21, "NO GRAINS", 1);
    if (!v->cover) oled_ftext_c(F_TINY, 64, 33, "HOLD REC AND MAKE A SOUND", 1);
    oled_hline_pat(30, 44, 68, 0x55, true);
    for (int i = 0; i < 3; i++) {
        float ph = wrap1(t_now * 0.4f + (float)i * 0.33f);
        int x = 48 + i * 16, y = 36 + (int)(ph * 24.0f);
        if (y < (v->cover ? OLED_H - v->cover : OLED_H)) oled_pixel(x, y, true);
    }
}

// grains pouring into a jar while the corpus is written to flash (the output is muted meanwhile)
static void draw_jar(const view_t *v)
{
    oled_rect(40, 18, 48, 44, false);
    oled_rframe(50, 26, 28, 32, 3);
    oled_line(54, 24, 74, 24, true);
    int h = (int)(S.job_frac * 28.0f);
    if (h > 0) oled_dither(52, 56 - h, 24, h, 3, true);
    for (int i = 0; i < 4; i++) {
        float ph = wrap1(t_now * 1.5f + (float)i * 0.25f);
        oled_pixel(62 + i % 2 * 3, 14 + (int)(ph * (float)(42 - h)) + 4, true);
    }
    (void)v;
}

// ---------------------------------------------------------------- DECOMPOSE: the recording, crumbling
static UI_RAM int16_t tl[NU_MAX];
static UI_RAM int16_t tl_at[NU_MAX];                  // each unit's place in the timeline (-1: not in it)

static float n2(int a, int b, float t)                 // a slow wander, -1..1
{
    return sin_turns(hash01(a, b) + t * (0.13f + 0.3f * hash01(a, b + 7)));
}

// Daisy's line, the syllable heard underlined, scrambled and falling as D rises
static void draw_lyric(float D, int bottom)
{
    int j = S.syl_heard;
    if (S.material != MAT_DAISY || j < 0) return;
    int k = daisy_syl_line(j);
    const char *line = daisy_line(k);
    int len = (int)strlen(line);
    // which letters belong to the syllable heard: walk the line's letters against each syllable's
    int a = -1, b = -1, ci = 0;
    for (int s = daisy_line_first(k); s < daisy_line_first(k + 1); s++) {
        const char *t = daisy_syl(s);
        int start = -1;
        for (const char *q = t; *q; q++) {
            if (*q == '\'') continue;
            while (ci < len && line[ci] != *q) ci++;
            if (ci >= len) break;
            if (start < 0) start = ci;
            ci++;
        }
        if (s == j) { a = start; b = ci; }
    }
    const gfx_font_t *f = oled_ftext_w(F_SMALL, line, 1) <= 124 ? F_SMALL : F_TINY;
    int w = oled_ftext_w(f, line, 1), x = 64 - w / 2, y = 17;
    float garble = D < 0.08f ? 0.0f : powf((D - 0.08f) / 0.92f, 1.2f);
    int phase = (int)(t_now * (2.0f + 10.0f * D));
    char one[2] = { 0, 0 };
    for (int i = 0; i < len; i++) {
        char ch = line[i];
        int cw = oled_ftext_w(f, (char[2]){ ch, 0 }, 1);
        if (ch >= 'A' && ch <= 'Z' && hash01(i * 31 + k, phase) < garble) ch = (char)('A' + (int)(hash01(i, phase + 3) * 25.99f));
        float dy = D * D * 4.0f * n2(i, k, t_now * 3.0f);
        if (D > 0.8f) dy += (D - 0.8f) * 140.0f * hash01(i, 11) * wrap1(t_now * 0.7f + hash01(i, 12));
        int yy = y + (int)lroundf(dy);
        one[0] = ch;
        if (yy < bottom - 6) oled_ftext(f, x, yy, one);
        if (i >= a && i < b && a >= 0) oled_hline_pat(x, y + (f == F_SMALL ? 8 : 6), cw, 0xFF, true);
        x += cw + 1;
    }
}

static void draw_decomp(const view_t *v)
{
    float D = S.chaos;
    int bottom = v->cover ? OLED_H - v->cover : OLED_H;
    int n = sift_timeline(tl, NU_MAX);
    if (n <= 0) return;
    for (int u = 0; u < corp_units; u++) tl_at[u] = -1;
    for (int i = 0; i < n; i++) tl_at[tl[i]] = (int16_t)i;
    const int x0 = MAP_X0, W = MAP_X1 - MAP_X0 + 1, cy = 41;
    for (int u = 0; u < corp_units; u++) tl_at_s[u] = tl_at[u] < 0 ? -1 : (int16_t)(x0 + tl_at[u] * W / n);
    tl_have = true;
    bool lyric = S.material == MAT_DAISY;
    int top = lyric ? 27 : 19;
    float sol = 1.0f - smoothstep(0.3f, 0.62f, D);                       // how much of the line is still a line
    float shake_px = D * sqrtf(D) * 9.0f + S.shake * 6.0f;
    // the waveform, a column at a time
    for (int c = 0; c < W; c++) {
        int i0 = c * n / W, i1 = (c + 1) * n / W;
        if (i1 <= i0) i1 = i0 + 1;
        // its height: the loudest unit's level under its take's peak, 36 dB to the top (vowels and consonants show)
        float amp = 0;
        for (int i = i0; i < i1 && i < n; i++) {
            const unit_t *un = &units[tl[i]];
            if (un->state != U_OK || un->gain <= 0.0f) continue;
            float peak_db = 20.0f * log10f(0.5f / (un->gain * 32768.0f));
            float a = clampf((un->d[D_LOUD] - peak_db + 36.0f) / 30.0f, 0.0f, 1.0f);
            if (a > amp) amp = a;
        }
        int h = (int)(amp * amp * 13.0f + 0.5f);
        int x = x0 + c + (int)lroundf(D * D * 5.0f * n2(c, 2, t_now));
        int yc = cy + (int)lroundf(shake_px * n2(c, 1, t_now * (0.5f + 2.0f * D)));
        if (h == 0) { if (hash01(c, 9) < sol) oled_pixel(x, yc, true); continue; }
        for (int y = -h; y <= h; y++) {
            if (hash01(c * 64 + y, 5) >= sol) continue;
            int yy = yc + y;
            if (yy >= top && yy < bottom) oled_pixel(x, yy, true);
        }
        int dust = (int)((1.0f - sol) * (float)h * 0.7f);
        for (int d = 0; d < dust; d++) {
            float r = 1.0f + 1.6f * D;
            int dx = (int)lroundf(D * 7.0f * n2(c * 13 + d, 3, t_now * (0.4f + D)));
            int dy = (int)lroundf((float)h * r * n2(c * 13 + d, 4, t_now * (0.3f + 0.8f * D)));
            int yy = yc + dy;
            if (yy >= top && yy < bottom) oled_pixel(x + dx, yy, true);
        }
        // where syllables start: a tick under it, gone as the order goes
        if (D < 0.55f) for (int i = i0; i < i1 && i < n; i++) if (eng_cut[tl[i]] && hash01(c, 21) > D * 1.6f) { if (58 < bottom) oled_pixel(x, 58, true); break; }
    }
    // the head
    int hu = (int)(eng_head / (float)UNIT);
    int hx = -1;
    if (hu >= 0 && hu < corp_units && tl_at[hu] >= 0) {
        float fi = (float)tl_at[hu] + (eng_head - (float)(hu * UNIT)) / (float)UNIT;
        hx = x0 + (int)(fi * (float)W / (float)n);
        int hb = bottom < 57 ? bottom : 57;
        if (hb > top) oled_invert(hx, top, 1, hb - top);                // it cuts through the waveform
        if (!lyric) { oled_pixel(hx - 1, top - 2, true); oled_pixel(hx + 1, top - 2, true); oled_pixel(hx, top - 1, true); }
    }
    // every grain sounding now: where it came from; a thread back to the head when it came from far
    int threads = 0;
    for (int i = 0; i < NV; i++) {
        int u = eng_vis_unit[i];
        if (u < 0 || u >= corp_units || tl_at[u] < 0) continue;
        int gx = x0 + tl_at[u] * W / n;
        int gy = cy + (int)lroundf(shake_px * 0.5f * n2(u, 6, t_now));
        if (eng_vis_env[i] > 0.6f) oled_rect(gx - 1, gy - 1, 3, 3, true); else oled_pixel(gx, gy, true);
        if (hx >= 0 && abs(gx - hx) > 6 && threads < 8) { dline(gx, gy, hx, cy, 0x11); threads++; }
    }
    parts_draw(&parts);
    if (lyric) draw_lyric(D, bottom);
    else {                                                                // what it is made of, where Daisy's words would be
        const char *what = S.material == MAT_AMEN ? "AMEN" : S.material == MAT_STARTER ? "STARTER" : "YOUR TAKE";
        oled_ftext_r(F_TINY, 126, 17, what, 1);
    }
    // the stage and the speed, in the corners
    if (!v->cover) {
        static const char *const stage[] = { "WHOLE", "WEARING", "SYLLABLES", "PHONEMES", "DUST" };
        int st = D < 0.04f ? 0 : D < 0.2f ? 1 : D < 0.47f ? 2 : D < 0.76f ? 3 : 4;
        oled_rect(0, 57, oled_ftext_w(F_TINY, stage[st], 1) + 2, 7, false);
        oled_ftext_ex(F_TINY, 1, 58, stage[st], 1);
        char b[16];
        float sp = S.speed;
        if (fabsf(sp - 1.0f) > 0.04f) {
            if (sp == 0.0f) snprintf(b, sizeof b, "FROZEN");
            else if (sp < 0.0f) snprintf(b, sizeof b, "BACK X%.1f", (double)-sp);
            else snprintf(b, sizeof b, "X%.2f", (double)sp);
            int w = oled_ftext_w(F_TINY, b, 1);
            oled_rect(126 - w, 57, w + 2, 7, false);
            oled_ftext_ex(F_TINY, 127 - w, 58, b, 1);
        }
    }
    (void)v;
}

void scene_draw(const view_t *v)
{
    t_now = v->t;
    physics();
    header(v);
    if (S.empty && S.pending == 0 && S.job != JOB_LOAD && S.job != JOB_RENDER) draw_empty(v);
    else if (S.mode == MODE_DECOMP) draw_decomp(v);
    else draw_map(v);
    if (S.job == JOB_SAVE) draw_jar(v);
    if ((S.axes_shown > 0.0f || v->t < 7.0f) && !S.empty && !v->cover && S.mode == MODE_MAP) axis_names();
    // recording: what goes in, a bar up the left edge
    if (S.rec != REC_IDLE && !S.empty) {
        float e = clampf((20.0f * log10f(eng_rec_level + 1e-5f) + 50.0f) / 50.0f, 0.0f, 1.0f);
        int h = (int)(e * (float)(MAP_Y1 - MAP_Y0));
        if (h > 0) oled_rect(0, MAP_Y1 - h, 1, h, true);
    }
}

// ---------------------------------------------------------------- the splash: chaos on one side, order on the other
// Sixty grains: on the left they swarm, on the right they fall and stack in rows. A sieve between them. The name
// lands in the middle.
#define SPL_N 64
static struct { float x, y, vx, vy; } spl[SPL_N];

bool scene_splash(int f)
{
    const float dt = 0.04f;
    if (f == 0) {
        parts.n = 0;
        for (int i = 0; i < SPL_N; i++) { spl[i].x = 64.0f + 20.0f * rng_bi(&pseed); spl[i].y = 10.0f + 8.0f * rng_uni(&pseed); spl[i].vx = 40.0f * rng_bi(&pseed); spl[i].vy = 10.0f * rng_uni(&pseed); }
    }
    float t = (float)f * dt;
    for (int i = 0; i < SPL_N; i++) {
        bool order = (i & 1) != 0;
        if (order) {                                     // falls to its cell: ten to a row, from the bottom up
            int k = i / 2, col = k % 8, row = k / 8;
            float tx = 76.0f + (float)col * 6.0f, ty = 60.0f - (float)row * 5.0f;
            float delay = (float)k * 0.05f;
            if (t > delay) {
                spl[i].vx += (tx - spl[i].x) * 18.0f * dt - spl[i].vx * 5.0f * dt;
                spl[i].vy += (ty - spl[i].y) * 18.0f * dt - spl[i].vy * 5.0f * dt + 30.0f * dt;
            }
        } else {                                         // circles a wandering centre on the left, each at its own radius and speed
            float ccx = 28.0f + 8.0f * sin_turns(t * 0.21f), ccy = 40.0f + 6.0f * sin_turns(t * 0.33f + 0.25f);
            float open = ease_out(clampf(t / 0.8f, 0.0f, 1.0f));
            float r = open * (5.0f + 19.0f * hash01(i, 7)), ph = hash01(i, 8) + t * (0.25f + 0.6f * hash01(i, 9)) * (hash01(i, 10) > 0.3f ? 1.0f : -1.0f);
            float tx = ccx + r * sin_turns(ph + 0.25f) + 1.5f * rng_bi(&pseed), ty = ccy + 0.75f * r * sin_turns(ph) + 1.5f * rng_bi(&pseed);
            spl[i].vx = (tx - spl[i].x) / dt * 0.35f; spl[i].vy = (ty - spl[i].y) / dt * 0.35f;
        }
        spl[i].x += spl[i].vx * dt; spl[i].y += spl[i].vy * dt;
        if (order && spl[i].x < 68.0f && t > 0.6f) spl[i].x = 68.0f;
        int x = (int)lroundf(spl[i].x), y = (int)lroundf(spl[i].y);
        if (y >= 0 && y < 64) { if (order) oled_rect(x, y, 2, 2, true); else oled_pixel(x, y, true); }
    }
    // the sieve
    for (int y = 0; y < 64; y += 3) oled_pixel(64, y + (f / 2) % 3, true);
    // the name lands in a box, the tagline under it
    if (f >= 34) {
        float e = ease_back(clampf((float)(f - 34) / 14.0f, 0.0f, 1.0f));
        int y = (int)(-16.0f + 36.0f * e);
        oled_rrect(39, y - 4, 50, 19, 3, false);
        oled_rframe(39, y - 4, 50, 19, 3);
        oled_ftext_c(F_MED, 64, y, "SIFT", 3); oled_ftext_c(F_MED, 65, y, "SIFT", 3);
        if (f >= 54) {
            oled_rect(22, y + 17, 84, 8, false);
            oled_ftext_c(F_TINY, 64, y + 18, "WHOLE, THEN IN PIECES", 1);
        }
    }
    return f < 96;
}

// ---------------------------------------------------------------- the screensaver: a sieve drifting in the dark
void scene_saver(uint32_t f)
{
    float t = (float)f * 0.04f;
    int sx = 40 + (int)(30.0f * sin_turns(t * 0.013f));                 // the sieve wanders, so nothing burns in
    int sy = 30 + (int)(10.0f * sin_turns(t * 0.009f + 0.25f));
    oled_hline_pat(sx - 20, sy, 40, 0x11, true);
    for (int i = 0; i < 12; i++) {
        float ph = wrap1(t * 0.12f + hash01(i, 3));
        int x = sx - 18 + (int)(hash01(i, 4) * 36.0f);
        int y = sy - 24 + (int)(ph * 24.0f);
        if (ph < 0.98f) {                                                // above the sieve: they drift
            x += (int)(3.0f * sin_turns(t * 0.3f + hash01(i, 5)));
            if (y >= 0 && y < 64) oled_pixel(x, y, true);
        }
        int col = i % 6, row = i / 6;                                    // below: in rows
        oled_pixel(sx - 10 + col * 4, sy + 4 + row * 4, true);
    }
}
