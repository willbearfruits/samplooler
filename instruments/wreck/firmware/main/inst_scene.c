// WRECK's screen (wreck.h): the header band, the scene, the splash, the screensaver. Drawing only, in the UI task;
// kept out of the IRAM list, nothing here runs per sample.
//
//   the header   the zone (a pill: BREAKCORE .. DRONE; STOP when stopped, REC while sampling), the axis as a gauge
//                with the zones' borders, the song's beat, the link, the meter; along row 15 the pattern's steps
//   the break    (towards BREAKCORE) the break's waveform with its slices; the slice playing lit, jolted sideways by
//                the edits, shards flying off it (more with chaos); a reversed slice has its arrow, a roll its teeth
//   the amp      (towards DRONE) a stack of two cabinets sliding in from the right, its cones pumping with the amp,
//                smoke rising off it; the waveform is left as a slow line along the floor
//   the grid     BEAT's own drawing while the knob holds it
#include "wreck.h"
#include "ui.h"
#include "front.h"
#include "link.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int  wr_zone(void);
int  wr_beat_flash(void);
bool wr_stopping(void);
int  wr_peers(void);

// ---------------------------------------------------------------- the link glyph (as BAND's and SING's)
static void link_glyph(int x, int y, const view_t *v)
{
    if (!link_radio()) return;
    int bf = wr_beat_flash();
    if (!wr_peers()) { if ((v->frame / 12) % 2 == 0) { oled_pixel(x + 2, y + 3, true); ui_arc(x + 2, y + 3, 3, -60, 60); } return; }
    bool on = bf > 1 || (bf > 0 && (v->frame & 1));
    oled_frame(x, y + 1, 3, 6); oled_frame(x + 4, y + 1, 3, 6);
    if (link_synced() && link_error_ms() < 2.0f) { if (on) { oled_rect(x, y + 1, 3, 6, true); oled_rect(x + 4, y + 1, 3, 6, true); } }
    else if ((v->frame / 3) % 2 == 0) oled_pixel(x + 3, y + 3, true);
}

// ---------------------------------------------------------------- the header band
static void header(const view_t *v, const beat_view_t *bv)
{
    const front_status_t *st = front_status();
    int x_end;
    if (kit_recording() || kit_armed()) x_end = (v->frame / 6) % 2 ? oled_pill(F_SMALL, 0, 2, "REC", true) : oled_pill(F_SMALL, 0, 2, "REC", false);
    else if (kit_rendering()) x_end = oled_pill(F_SMALL, 0, 2, "...", false);
    else if (!perf.running || wr_stopping()) x_end = oled_pill(F_SMALL, 0, 2, "STOP", false);
    else x_end = oled_pill(F_SMALL, 0, 2, host_zone_name(perf.zone), true);
    // the axis: a track with the zones' borders and a marker
    int gx = x_end + 4, gw = 88 - gx;
    if (gw > 12) {
        oled_hline_pat(gx, 8, gw, 0x55, true);
        for (int z = 0; z <= Z_N; z++) oled_pixel(gx + z * (gw - 1) / Z_N, 7, true);
        int mx = gx + (int)(perf.axis * (float)(gw - 1) + 0.5f);
        oled_rect(mx - 1, 5, 3, 7, true);
    }
    // the song's beat, the link, the meter
    int bx = 92, bf = wr_beat_flash();
    uint32_t fpb = song.fpb ? song.fpb : 19200;
    int beat = (int)((uint32_t)wr_mod((int32_t)(audio_frames() - song.origin), (int32_t)(fpb * 4u)) / fpb);
    for (int k = 0; k < 4; k++) {
        int x = bx + k * 3;
        if (k == beat) { bool big = bf > 1; oled_rect(x - (big ? 1 : 0), big ? 5 : 6, big ? 3 : 2, big ? 4 : 3, true); }
        else oled_pixel(x, 8, true);
    }
    link_glyph(105, 4, v);
    ui_meter(OLED_W - 11, 3, 11, 9, v->out_level, st->clip);
    // the pattern's steps along row 15
    int n = bv->steps > 0 && bv->steps <= 64 ? bv->steps : 16;
    for (int k = 0; k < n; k++) oled_pixel(k * (OLED_W - 1) / n, 15, true);
    if (perf.running) { int px = bv->step * (OLED_W - 1) / n; oled_rect(px - 1, 14, 3, 2, true); }
}

// ---------------------------------------------------------------- the break
#define WAVE_Y 39                                  // its middle line
static uint8_t env[OLED_W];
static uint32_t env_serial, last_hits, seed = 0x77aa11u;
static float jolt, jolt_x0, jolt_x1;
static UI_RAM parts_t shards, smoke;

static int rnd(int n) { seed = seed * 1664525u + 1013904223u; return (int)((seed >> 8) % (uint32_t)(n > 0 ? n : 1)); }

static void break_scene(const view_t *v, const beat_view_t *bv, int x_right, float fade)
{
    const brk_t *b = kit_break();
    if (b->serial != env_serial || (v->frame % 25) == 0) { kit_envelope(env, OLED_W); env_serial = b->serial; }
    int h_max = (int)(14.0f * (1.0f - 0.7f * fade));                // towards DRONE it flattens
    int sx0 = -1, sx1 = -1;
    if (bv->slice >= 0 && bv->slice < b->nslices && b->len) {
        sx0 = (int)((int64_t)b->slice[bv->slice] * OLED_W / b->len);
        int e = bv->slice + 1 < b->nslices ? b->slice[bv->slice + 1] : b->len;
        sx1 = (int)((int64_t)e * OLED_W / b->len);
    }
    if (bv->hits != last_hits) {                                    // a new hit: jolt it, throw shards
        last_hits = bv->hits;
        jolt = (float)(2 + rnd(3)) * (0.4f + perf.chaos + perf.stutter) * (rnd(2) ? 1.0f : -1.0f) * (1.0f - fade);
        jolt_x0 = (float)sx0; jolt_x1 = (float)sx1;
        int k = (int)(2.0f + 8.0f * perf.chaos) + bv->roll;
        if (sx0 >= 0 && fade < 0.8f) parts_burst(&shards, (float)(sx0 + sx1) * 0.5f, (float)WAVE_Y, k, 30.0f + 50.0f * perf.chaos, 0.5f, PART_SPARK, &seed);
    }
    jolt *= 0.75f;
    int prev_a = -1;
    for (int x = 0; x < x_right && x < OLED_W; x++) {
        int a = env[x] * h_max / 255;
        bool lit = x >= sx0 && x < sx1 && perf.running;
        int dx = lit ? (int)jolt : 0, xx = x + dx;
        if (xx < 0 || xx >= x_right) { prev_a = a; continue; }
        if (lit) oled_line(xx, WAVE_Y - a - 1, xx, WAVE_Y + a + 1, true);
        else if (fade > 0.5f) {                                       // towards DRONE: an outline, melting
            int pa = prev_a < 0 ? a : prev_a;
            oled_line(xx - 1, WAVE_Y - pa, xx, WAVE_Y - a, true);
            oled_line(xx - 1, WAVE_Y + pa, xx, WAVE_Y + a, true);
            if (env[x] > 150 && (x * 7) % 5 == 0) {                   // a drip off the loud bits, slowly lengthening
                int len = (int)((float)((v->frame / 3 + (uint32_t)x * 13u) % 18u) * (fade - 0.5f) * 2.0f);
                if (WAVE_Y + a + len < OLED_H - 1) { oled_line(xx, WAVE_Y + a, xx, WAVE_Y + a + len, true); oled_pixel(xx, WAVE_Y + a + len + 1, true); }
            }
        }
        else if (a < 1) oled_pixel(xx, WAVE_Y, true);
        else oled_line(xx, WAVE_Y - a, xx, WAVE_Y + a, (x & 1) == 0);
        prev_a = a;
    }
    for (int s = 0; s < b->nslices && b->len && fade < 0.5f; s++) { // the slices' cuts
        int x = (int)((int64_t)b->slice[s] * OLED_W / b->len);
        if (x < x_right) for (int y = WAVE_Y - 16; y <= WAVE_Y + 16; y += 4) oled_pixel(x, y, true);
    }
    if (sx0 >= 0 && perf.running && fade < 0.8f) {                  // the slice's tricks: reversed, rolled
        int cx = (sx0 + sx1) / 2;
        if (bv->reversed) { oled_line(cx - 3, 22, cx + 3, 22, true); oled_line(cx - 3, 22, cx - 1, 20, true); oled_line(cx - 3, 22, cx - 1, 24, true); }
        for (int r = 0; r < bv->roll && r < 8; r++) oled_rect(sx0 + r * 3, 56, 2, 3, true);
    }
    parts_step(&shards, 1.0f / 25.0f, 90.0f, 0.95f);
    parts_draw(&shards);
}

// ---------------------------------------------------------------- the amp
static float cone_a[2], fb_ph;
// a 2 x 2 cabinet, w x h; its cones open with the amp (pump 0..1), rings off them when it is loud
static void cab(int x, int y, int w, int h, float pump, int k)
{
    oled_rframe(x, y, w, h, 2);
    oled_hline_pat(x + 2, y + h - 3, w - 4, 0x55, true);                // the grille's bottom edge
    int r0 = (w - 6) / 4 - 1, cy0 = y + 2 + (h - 6) / 4 + 1;
    for (int i = 0; i < 2; i++) for (int j = 0; j < 2; j++) {
        int cx = x + 3 + (w - 6) / 4 + i * ((w - 6) / 2), cy = cy0 + j * ((h - 6) / 2);
        int r = r0 - 1 + (int)(pump * 1.5f + 0.5f);
        oled_circle(cx, cy, r, true);
        oled_disc(cx, cy, 1 + (int)(pump * 1.2f), true);
        if (pump > 0.55f && ((k / 2 + i + j) & 1)) oled_circle(cx, cy, r > 3 ? r - 2 : 1, true);
    }
}
static void amp_scene(const view_t *v, int x0, float fade)
{
    amp_view_t av; amp_view(&av);
    float lv = clampf(av.level * 1.6f, 0.0f, 1.0f);
    cone_a[0] += 0.5f * (lv - cone_a[0]);
    cone_a[1] += 0.35f * (lv - cone_a[1]);
    // the head on top, two cabinets under it, taller towards DRONE
    int x = x0, hh = 22 + (int)(fade * 14.0f), cy = OLED_H - hh;
    oled_rframe(x, cy - 9, 54, 8, 1);
    for (int k = 0; k < 6; k++) oled_disc(x + 5 + k * 5, cy - 5, 1, true);
    if (lv > 0.05f) oled_rect(x + 32, cy - 7, 2, 4, (v->frame / 2) % 2 == 0);   // the pilot light
    oled_ftext(F_TINY, x + 36, cy - 7, "DOOM");
    cab(x, cy, 26, hh, cone_a[0], (int)v->frame);
    cab(x + 28, cy, 26, hh, cone_a[1], (int)v->frame + 1);
    // smoke off the top, thicker with the level and towards DRONE
    if (fade > 0.15f) {
        int k = (int)(fade * (1.0f + 3.0f * lv)) + ((v->frame & 1) ? 1 : 0);
        for (int i = 0; i < k; i++)
            parts_emit(&smoke, (float)(x + 2 + rnd(50)), (float)(cy - 10), (float)(rnd(9) - 4) * 0.7f - 2.0f * fade, -5.0f - (float)rnd(7), 3.0f, rnd(3) ? PART_DOT : PART_SQUARE, 1);
    }
    parts_step(&smoke, 1.0f / 25.0f, -1.5f, 0.985f);
    parts_draw(&smoke);
    // the feedback: a line singing up off the head, wider and faster as it grows
    if (av.feedback > 0.08f) {
        fb_ph += 0.6f + 1.5f * av.feedback;
        int top = 17, bot = cy - 10, cxm = x + 27, amp_w = 1 + (int)(av.feedback * 5.0f);
        for (int y = bot; y > top; y--) {
            float s1 = sinf(fb_ph + (float)y * 0.9f);
            oled_pixel(cxm + (int)(s1 * (float)amp_w), y, true);
        }
    }
}

// ---------------------------------------------------------------- the whole frame
void scene_draw(const view_t *v)
{
    beat_view_t bv; beat_view(&bv);
    oled_clear();
    if (beat_editing()) beat_draw_editor(v);
    else {
        float a = perf.axis;
        float fade = clampf((a - 0.35f) / 0.4f, 0.0f, 1.0f);       // 0: all break .. 1: all amp
        int amp_x = OLED_W - (int)(fade * 56.0f);                    // the stack slides in from the right
        if (amp_x > OLED_W - 1) amp_x = OLED_W - 1;
        break_scene(v, &bv, fade > 0.0f ? amp_x - 2 : OLED_W, fade);
        if (fade > 0.0f) amp_scene(v, amp_x, fade);
        // the break's name and the machine's tempo, small, top left of the scene
        grid_t g; wr_grid_get(&g);
        char b[32];
        float bpm = song.bpm * (g.feel_q >= 0 ? (float)(1 << g.feel_q) : 1.0f / (float)(1 << -g.feel_q));
        snprintf(b, sizeof b, "%s %d", kit_break()->name ? kit_break()->name : "", (int)(bpm + 0.5f));
        oled_ftext(F_TINY, 1, 18, b);
        if (kit_rendering()) {                                        // a built-in break being printed: a bar fills
            int pc = kit_rendering();
            ui_box(36, "PRINTING THE BREAK");
            oled_frame(24, 46, 80, 4); oled_rect(25, 47, pc * 78 / 100, 2, true);
        }
        else if (kit_armed()) ui_box(40, (v->frame / 12) % 2 ? "WAITING FOR A SOUND" : "PLAY IT NOW");
        else if (kit_recording()) { snprintf(b, sizeof b, "SAMPLING %.1fS", (double)kit_rec_seconds()); ui_box(40, b); }
    }
    oled_rect(0, 0, OLED_W, 16, false);
    header(v, &bv);
}

// ---------------------------------------------------------------- the splash: WRECK chopped up, then sinking
// 0..44: the word in strips, each strip jolted sideways, some repeated (a stutter), flashing on the snare;
// 45..54 it holds still; 55..99 it sinks, drips run off it and smoke rises, BREAKCORE TO DOOM under it.
static void strip_shift(int y0, int h, int dx)
{
    for (int y = y0; y < y0 + h && y < OLED_H; y++) {
        uint8_t row[OLED_W];
        for (int x = 0; x < OLED_W; x++) row[x] = oled_get(x, y);
        for (int x = 0; x < OLED_W; x++) { int xs = x - dx; oled_pixel(x, y, xs >= 0 && xs < OLED_W && row[xs]); }
    }
}
bool scene_splash(int f)
{
    if (f >= 100) return false;
    oled_clear();
    const int w = 5 * 12 - 2, x0 = (OLED_W - w) / 2;
    if (f < 8) {                                                     // the needle down: crackle
        for (int k = 0; k < 6; k++) oled_pixel(rnd(OLED_W), 16 + rnd(48), true);
        return true;
    }
    if (f < 45) {
        int y0 = 26;
        oled_text2(x0, y0, "WRECK");
        int n = 2 + (f % 5);
        for (int k = 0; k < n; k++) {                                // strips jolted sideways
            int ys = y0 + rnd(14), h = 1 + rnd(4), dx = (rnd(2) ? 1 : -1) * (2 + rnd(f > 30 ? 20 : 9));
            strip_shift(ys, h, dx);
        }
        if (f % 7 == 3) {                                            // a stutter: a slice of it repeated below
            int ys = y0 + rnd(10);
            for (int y = 0; y < 3; y++) for (int x = 0; x < OLED_W; x++) if (oled_get(x, ys + y)) oled_pixel(x, ys + y + 14, true);
        }
        if (f % 6 == 0) oled_invert(0, 16, OLED_W, 48);               // the snare
        return true;
    }
    int sink = f < 55 ? 0 : (f - 55) / 5;
    oled_text2(x0, 26 + sink, "WRECK");
    if (f >= 55) {
        static const int8_t at[6] = { 3, 14, 25, 35, 47, 56 };
        for (int k = 0; k < 6; k++) {
            int len = ((f - 55) * (2 + (k * 5) % 4)) / 9, dx = x0 + at[k];
            int top = 26 + sink + 13;
            if (top + len > 54) len = 54 - top;                       // above the tagline
            if (len > 0) { oled_line(dx, top, dx, top + len, true); if (top + len + 1 < OLED_H) oled_disc(dx, top + len + 1, 1, true); }
        }
        if (f % 2 == 0) parts_emit(&smoke, (float)(x0 + rnd(w)), (float)(24 + sink), (float)(rnd(5) - 2) * 0.6f, -5.0f - (float)rnd(5), 2.0f, PART_DOT, 1);
        parts_step(&smoke, 1.0f / 25.0f, -1.0f, 0.99f);
        parts_draw(&smoke);
    }
    if (f >= 62) oled_ftext_c(F_TINY, OLED_W / 2, 58, "BREAKCORE TO DOOM", 1);
    return true;
}

// ---------------------------------------------------------------- the screensaver: shards drifting down, slowly
void scene_saver(uint32_t f)
{
    oled_clear();
    if (f % 6 == 0) parts_emit(&shards, (float)rnd(OLED_W), 0.0f, (float)(rnd(5) - 2), 4.0f + (float)rnd(6), 9.0f, PART_STREAK, 1);
    parts_step(&shards, 1.0f / 25.0f, 2.0f, 1.0f);
    parts_draw(&shards);
}
