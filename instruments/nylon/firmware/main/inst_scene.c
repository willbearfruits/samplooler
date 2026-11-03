// NYLON's screen, UI task. On the left a small guitar that lifts its neck as you lift yours, over a dotted arc of
// NECK RANGE; on the right the soundhole and its rosette, the loop running round it (a growing arc while it records,
// the head going round while it plays, a dot for each layer), and in and around the hole what this mode does: six
// strings that shake with the guitar (CLEAN), grains flying out of it or hanging still (CLOUD), petals that open as
// the note is held (BLOOM), rings for the repeats (ECHO), a mouth opening (WAH), sparks rising (SHIMMER), strings
// doubled (OCTAVE), strings wavering (WOBBLE), the drum's name and a flash (GOLPE), the note heard (VOICE). At the
// bottom, in words, what lifting the neck does now.
#include "inst_nylon.h"
#include "dsp.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

#define GX 15                                  // the guitar's pivot: the lower bout's centre
#define GY 44
#define GS 0.85f                               // its size
#define SX 88                                  // the soundhole
#define SY 32
#define SR 9

static parts_t parts;
static uint32_t pseed = 0x51ed27u;
static float neck_vis = 0.0f, bloom_vis, slap_flash, echo_t, ring_r[4], ring_a[4];
static int   slap_seen, drum_shown, drum_ms;
static uint32_t frame_n;

static void line_w(float x0, float y0, float x1, float y1) { oled_line((int)lroundf(x0), (int)lroundf(y0), (int)lroundf(x1), (int)lroundf(y1), true); }

// ---------------------------------------------------------------- the small guitar
static void draw_guitar(int px, int py, float deg, float scale, float str_amp, float t)
{
    float a = deg * 0.0174533f, dx = cosf(a), dy = -sinf(a), nx = -dy, ny = dx;
    float s = scale;
    // the bouts: a lower, an upper, the waist between
    int lr = (int)lroundf(8.0f * s), ur = (int)lroundf(6.0f * s);
    float ux = (float)px + dx * 10.0f * s, uy = (float)py + dy * 10.0f * s;
    oled_disc(px, py, lr, false); oled_circle(px, py, lr, true);
    oled_disc((int)lroundf(ux), (int)lroundf(uy), ur, false); oled_circle((int)lroundf(ux), (int)lroundf(uy), ur, true);
    float wx = (float)px + dx * 5.5f * s, wy = (float)py + dy * 5.5f * s;    // the waist: clear the circles' overlap
    for (int k = -4; k <= 4; k++) oled_pixel((int)lroundf(wx + nx * (float)k * s), (int)lroundf(wy + ny * (float)k * s), false);
    oled_pixel((int)lroundf(wx + nx * 5.0f * s), (int)lroundf(wy + ny * 5.0f * s), true);
    oled_pixel((int)lroundf(wx - nx * 5.0f * s), (int)lroundf(wy - ny * 5.0f * s), true);
    // the soundhole, the bridge
    oled_circle((int)lroundf((float)px + dx * 7.5f * s), (int)lroundf((float)py + dy * 7.5f * s), (int)lroundf(2.5f * s), true);
    line_w((float)px - dx * 4.0f * s + nx * 2.5f * s, (float)py - dy * 4.0f * s + ny * 2.5f * s, (float)px - dx * 4.0f * s - nx * 2.5f * s, (float)py - dy * 4.0f * s - ny * 2.5f * s);
    // the neck and the head
    float n0 = 15.0f * s, n1 = 34.0f * s;
    line_w((float)px + dx * n0 + nx, (float)py + dy * n0 + ny, (float)px + dx * n1 + nx, (float)py + dy * n1 + ny);
    line_w((float)px + dx * n0 - nx, (float)py + dy * n0 - ny, (float)px + dx * n1 - nx, (float)py + dy * n1 - ny);
    float hx = (float)px + dx * (n1 + 4.0f * s), hy = (float)py + dy * (n1 + 4.0f * s);
    line_w((float)px + dx * n1 + nx * 2.0f, (float)py + dy * n1 + ny * 2.0f, hx + nx * 2.0f, hy + ny * 2.0f);
    line_w((float)px + dx * n1 - nx * 2.0f, (float)py + dy * n1 - ny * 2.0f, hx - nx * 2.0f, hy - ny * 2.0f);
    line_w(hx + nx * 2.0f, hy + ny * 2.0f, hx - nx * 2.0f, hy - ny * 2.0f);
    // the strings shake with the sound
    if (str_amp > 0.2f) {
        float w = str_amp * sinf(t * 47.0f);
        line_w((float)px - dx * 4.0f * s + nx * w * 0.3f, (float)py - dy * 4.0f * s + ny * w * 0.3f, (float)px + dx * n0 + nx * w, (float)py + dy * n0 + ny * w);
    } else line_w((float)px - dx * 4.0f * s, (float)py - dy * 4.0f * s, (float)px + dx * n0, (float)py + dy * n0);
}

// ---------------------------------------------------------------- the loop round the soundhole
static void dotted_circle(int cx, int cy, int r, int step_deg, int phase)
{
    for (int d = phase; d < 360 + phase; d += step_deg) {
        float a = (float)d * 0.0174533f;
        oled_pixel(cx + (int)lroundf((float)r * cosf(a)), cy + (int)lroundf((float)r * sinf(a)), true);
    }
}

static void draw_loop_ring(float t)
{
    int r = SR + 6;
    lstate_t s = (lstate_t)NY.lstate;
    switch (s) {
    case L_EMPTY: dotted_circle(SX, SY, r, 30, 0); break;
    case L_ARMED: if ((frame_n / 6) & 1) dotted_circle(SX, SY, r, 10, (int)(t * 60.0f) % 10); else dotted_circle(SX, SY, r, 30, 0); break;
    case L_REC: {
        float f = clampf((float)NY.take_ms / (loop_max_s() * 1000.0f), 0.0f, 1.0f);
        dotted_circle(SX, SY, r, 30, 0);
        ui_arc(SX, SY, r, -90.0f, -90.0f + 360.0f * f);
        ui_arc(SX, SY, r + 1, -90.0f, -90.0f + 360.0f * f);
        float a = (-90.0f + 360.0f * f) * 0.0174533f;
        if ((frame_n / 3) & 1) oled_disc(SX + (int)lroundf((float)r * cosf(a)), SY + (int)lroundf((float)r * sinf(a)), 2, true);
        break;
    }
    case L_PLAY: case L_DUB: {
        float p = loop_pos();
        oled_circle(SX, SY, r, true);
        if (s == L_DUB) oled_circle(SX, SY, r + 1, true);
        float a = (-90.0f + 360.0f * p) * 0.0174533f;
        int hx = SX + (int)lroundf((float)r * cosf(a)), hy = SY + (int)lroundf((float)r * sinf(a));
        oled_disc(hx, hy, 2, s != L_DUB || ((frame_n / 3) & 1));
        oled_line(SX, SY - r - 2, SX, SY - r + 1, true);          // the loop's start
        int bars; loop_bar(&bars);
        for (int b = 1; b < bars && bars <= 16; b++) {            // a tick at each bar
            float ab = (-90.0f + 360.0f * (float)b / (float)bars) * 0.0174533f;
            oled_pixel(SX + (int)lroundf((float)(r + 2) * cosf(ab)), SY + (int)lroundf((float)(r + 2) * sinf(ab)), true);
        }
        break;
    }
    case L_STOP: dotted_circle(SX, SY, r, 15, 0); oled_line(SX, SY - r - 2, SX, SY - r + 1, true); break;
    }
    int layers = loop_layers();
    for (int k = 0; k < layers && k < 8; k++) oled_pixel(SX - 7 + 2 * k, SY - r - 4, true);
}

static void rosette(void)
{
    oled_circle(SX, SY, SR, true);
    for (int d = 0; d < 360; d += 24) {                          // the mosaic round the hole
        float a = (float)d * 0.0174533f;
        oled_pixel(SX + (int)lroundf((float)(SR + 2) * cosf(a)), SY + (int)lroundf((float)(SR + 2) * sinf(a)), true);
        float b = (float)(d + 12) * 0.0174533f;
        oled_pixel(SX + (int)lroundf((float)(SR + 3) * cosf(b)), SY + (int)lroundf((float)(SR + 3) * sinf(b)), true);
    }
}

// six strings across the hole: amp shakes them, wob bends them, dbl doubles them
static void strings(float amp, float wob, float wob_ph, float sub, float up)
{
    for (int k = 0; k < 6; k++) {
        int y = SY - 5 + 2 * k;
        int half = (int)sqrtf((float)(SR * SR - (y - SY) * (y - SY))) - 1;
        int px = SX - half, py = y;
        for (int x = SX - half + 1; x <= SX + half; x++) {
            float u = (float)(x - (SX - half)) / (float)(2 * half);
            float env = sinf(3.14159f * u);
            float d = amp * env * sinf((float)frame_n * (1.7f + 0.37f * (float)k)) + wob * env * sinf(wob_ph + u * 6.0f + (float)k);
            int yy = y + (int)lroundf(d);
            oled_line(px, py, x, yy, true);
            if (sub > 0.15f && (k & 1) == 0) oled_pixel(x, yy + 1, true);
            px = x; py = yy;
        }
        if (up > 0.15f && (k & 1)) for (int x = SX - half + 2; x < SX + half - 1; x += 3) oled_pixel(x, y - 1, true);
    }
}

static void neck_words(int cover)
{
    if (cover >= 10 || !NY.neck_what) return;
    char b[32];
    if (!NK.ok) { oled_ftext_ex(F_TINY, 46, 57, "NO NECK: SET HOME", 0); return; }
    snprintf(b, sizeof b, "NECK: %s", NY.neck_what);
    int w = oled_ftext_ex(F_TINY, 46, 57, b, 0);
    int x0 = w + 3, x1 = 126;
    if (x1 - x0 < 12) return;
    oled_frame(x0, 58, x1 - x0, 4);
    if (NY.neck_signed) {
        int mid = (x0 + x1) / 2, v = (int)lroundf(clampf(NY.neck_amt, -1.0f, 1.0f) * (float)(x1 - x0 - 2) * 0.5f);
        oled_line(mid, 57, mid, 62, true);
        if (v > 0) oled_rect(mid, 59, v, 2, true); else if (v < 0) oled_rect(mid + v, 59, -v, 2, true);
    } else oled_rect(x0 + 1, 59, (int)lroundf(clampf(NY.neck_amt, 0.0f, 1.0f) * (float)(x1 - x0 - 2)), 2, true);
}

// ---------------------------------------------------------------- per mode
static void scene_mode(const view_t *v)
{
    float t = v->t;
    float lvl = clampf(eng_env * 25.0f, 0.0f, 3.0f);
    grain_ev_t g;
    switch (NY.mode) {
    case M_CLOUD: {
        while (eng_grain_pop(&g)) {
            if (g.kind != 0) continue;
            float a = rng_uni(&pseed) * 6.2831853f, sp = 10.0f + 25.0f * clampf(g.back / 2.8f, 0.0f, 1.0f);
            if (!NY.frozen) parts_emit(&parts, (float)SX + 3.0f * cosf(a), (float)SY + 3.0f * sinf(a), sp * cosf(a), sp * sinf(a), 0.7f + 0.6f * rng_uni(&pseed), g.rate > 1.5f ? PART_SPARK : PART_DOT, g.rate < 0.75f ? 2 : 1);
            else parts_emit(&parts, (float)SX + (8.0f + 14.0f * rng_uni(&pseed)) * cosf(a), (float)SY + (8.0f + 12.0f * rng_uni(&pseed)) * sinf(a), 3.0f * -sinf(a), 3.0f * cosf(a), 1.5f, PART_DOT, 1);
        }
        strings(lvl * 0.6f, 0, 0, 0, 0);
        if (NY.frozen) {                                         // a snowflake in the hole
            for (int k = 0; k < 3; k++) { float a = (float)k * 1.0472f + t * 0.3f; line_w((float)SX - 6.0f * cosf(a), (float)SY - 6.0f * sinf(a), (float)SX + 6.0f * cosf(a), (float)SY + 6.0f * sinf(a)); }
            oled_ftext_c(F_TINY, SX, 49, "FROZEN", 0);
        }
        break;
    }
    case M_BLOOM: {
        while (eng_grain_pop(&g)) { }
        bloom_vis += 0.25f * (eng_bloom_lvl - bloom_vis);
        float L = 11.0f * bloom_vis, h = clampf(E.b_harm, 0.0f, 1.0f) * smoothstep(0.4f, 3.0f, eng_bloom_age);
        for (int k = 0; k < 6; k++) {                            // petals: two curves from the rim
            float a = (float)k * 1.0472f + 0.2f * sinf(t * 0.5f);
            float r0 = (float)SR + 1.0f, r1 = r0 + L;
            for (int side = -1; side <= 1; side += 2) {
                float px = (float)SX + r0 * cosf(a + 0.18f * (float)side), py = (float)SY + r0 * sinf(a + 0.18f * (float)side);
                float qx = (float)SX + (r0 + L * 0.6f) * cosf(a + 0.3f * (float)side), qy = (float)SY + (r0 + L * 0.6f) * sinf(a + 0.3f * (float)side);
                float ex = (float)SX + r1 * cosf(a), ey = (float)SY + r1 * sinf(a);
                if (L > 1.0f) { line_w(px, py, qx, qy); line_w(qx, qy, ex, ey); }
            }
            if (h > 0.1f && L > 2.0f) {                          // the harmonics: small petals between
                float b = a + 0.5236f, r2 = r0 + L * 0.55f * h;
                line_w((float)SX + r0 * cosf(b), (float)SY + r0 * sinf(b), (float)SX + r2 * cosf(b), (float)SY + r2 * sinf(b));
            }
        }
        if (E.b_choke && bloom_vis > 0.05f && (frame_n & 1)) parts_emit(&parts, (float)SX + 12.0f * rng_bi(&pseed), (float)SY + 4.0f, 0, 12.0f, 0.8f, PART_DOT, 1);
        strings(lvl * 0.5f, 0, 0, 0, 0);
        if (eng_bloom_caps == 0) oled_ftext_c(F_TINY, SX, 49, "PLAY A NOTE", 0);
        break;
    }
    case M_ECHO: {
        echo_t += 0.04f;
        float et = clampf(E.e_time, 0.04f, 1.4f);
        if (echo_t >= et) {                                      // a new ring each repeat, as loud as the repeats
            echo_t -= et * (float)(int)(echo_t / et);
            for (int k = 0; k < 4; k++) if (ring_a[k] <= 0.05f) { ring_r[k] = (float)SR; ring_a[k] = clampf(eng_echo_lvl * 6.0f, 0.0f, 1.0f); break; }
        }
        for (int k = 0; k < 4; k++) {
            if (ring_a[k] <= 0.05f) continue;
            ring_r[k] += 0.6f + 0.4f / et;
            ring_a[k] *= 0.93f;
            if (ring_r[k] > 26.0f) { ring_a[k] = 0.0f; continue; }
            if (ring_a[k] > 0.4f) oled_circle(SX, SY, (int)ring_r[k], true); else dotted_circle(SX, SY, (int)ring_r[k], 20, (int)ring_r[k] * 7);
        }
        strings(lvl * 0.6f, 0, 0, 0, 0);
        if (E.e_fb > 1.0f && (frame_n & 2)) oled_ftext_c(F_TINY, SX, 49, "RUNAWAY", 0);
        break;
    }
    case M_WAH: {                                                // the hole is a mouth
        int hgt = 2 + (int)lroundf(eng_wah_pos * 7.0f);
        oled_disc(SX, SY, SR - 1, false);
        for (int y = -hgt; y <= hgt; y++) {
            float k = (float)y / (float)(hgt + 1);
            int w = (int)lroundf((float)(SR - 2) * sqrtf(1.0f - k * k));
            oled_pixel(SX - w, SY + y, true); oled_pixel(SX + w, SY + y, true);
        }
        oled_line(SX - SR + 2, SY, SX + SR - 2, SY, eng_wah_pos < 0.15f);
        oled_ftext_c(F_TINY, SX, 49, eng_wah_pos > 0.66f ? "AAH" : eng_wah_pos > 0.33f ? "WAH" : "OOH", 0);
        break;
    }
    case M_SHIMMER: {
        int k = (int)(clampf(eng_shim_lvl * 60.0f + E.shim * 1.5f, 0.0f, 3.0f));
        for (int i = 0; i < k; i++) if (rng_uni(&pseed) < 0.5f) parts_emit(&parts, (float)SX + 8.0f * rng_bi(&pseed), (float)SY - 4.0f, 6.0f * rng_bi(&pseed), -14.0f - 10.0f * rng_uni(&pseed), 1.2f, PART_SPARK, 1);
        strings(lvl * 0.6f, 0, 0, 0, 0);
        break;
    }
    case M_OCTAVE: strings(lvl * 0.7f, 0, 0, E.o_sub, E.o_up); break;
    case M_WOBBLE: { static float wph; wph += E.m_rate * 0.25f; strings(lvl * 0.3f, 1.6f * E.m_depth, wph, 0, 0); break; }
    case M_GOLPE: {
        if (NY.slap_seq != slap_seen) {
            slap_seen = NY.slap_seq; slap_flash = 1.0f;
            drum_shown = NY.slap_drum; drum_ms = 600;
            float sx = NY.slap_top ? (float)SX : (float)(SX + SR + 4);
            parts_emit(&parts, sx, (float)SY, 0, 0, 0.35f, PART_RING, 3);
            parts_burst(&parts, sx, (float)SY, 6, 30.0f * NY.slap_vel + 10.0f, 0.4f, PART_STREAK, &pseed);
        }
        if (slap_flash > 0.05f) oled_dither(SX - SR + 2, SY - SR + 2, 2 * SR - 3, 2 * SR - 3, (int)lroundf(slap_flash * 4.0f), true);
        slap_flash *= 0.8f;
        strings(lvl * 0.6f, 0, 0, 0, 0);
        if (drum_ms > 0) { drum_ms -= 40; if (drum_shown) oled_ftext_c(F_TINY, SX, 49, drum_names[drum_shown], 0); }
        else oled_ftext_c(F_TINY, SX, 49, "SLAP THE TOP", 0);
        break;
    }
    case M_VOICE: {
        oled_disc(SX, SY, SR - 1, false);
        if (NY.note >= 0) {
            char nb[8]; ui_note_name(NY.note, nb, sizeof nb);
            oled_ftext_c(F_SMALL, SX, SY - 4, nb, 1);
            float c = clampf(NY.cents / 50.0f, -1.0f, 1.0f);      // a needle over the hole: in tune is straight up
            float a = (-90.0f + 40.0f * c) * 0.0174533f;
            line_w((float)SX, (float)SY - (float)SR - 2.0f, (float)SX + 7.0f * cosf(a), (float)SY - (float)SR - 2.0f + 7.0f * sinf(a));
        } else strings(lvl * 0.6f, 0, 0, 0, 0);
        break;
    }
    default: strings(lvl, 0, 0, 0, 0); break;
    }
    if (NY.mode != M_CLOUD && NY.mode != M_BLOOM) while (eng_grain_pop(&g)) { }   // a tail's grains: not news when CLOUD comes back
}

void scene_draw(const view_t *v)
{
    frame_n++;
    char hb[24];
    // the header: the loop's state
    int bars, bar = loop_bar(&bars), beats = clampi((int)pv[P_BEATS], 1, 16);
    bool met = (int)pv[P_METRO] != MET_OFF;
    switch ((lstate_t)NY.lstate) {
    case L_ARMED:
        if (met && eng_beat >= 0) { snprintf(hb, sizeof hb, "IN %d", clampi(beats - eng_beat, 1, 16)); ui_head(hb, true); }
        else if (met) ui_head("READY", true);
        else ui_head((frame_n / 8) & 1 ? "ARMED" : "PLAY!", true);
        break;
    case L_REC:
        if (bars > 0) snprintf(hb, sizeof hb, "REC %d/%d", clampi(bar, 1, bars), bars);
        else snprintf(hb, sizeof hb, "REC %d.%d", NY.take_ms / 1000, NY.take_ms / 100 % 10);
        ui_head(hb, true); break;
    case L_DUB: ui_head("ADDING", true); break;
    case L_PLAY:
        if (fabsf(NY.tape - 1.0f) > 0.02f) snprintf(hb, sizeof hb, "%s %.1fX", NY.tape < -0.02f ? "BACK" : "TAPE", (double)fabsf(NY.tape));
        else snprintf(hb, sizeof hb, "LOOP %d", loop_layers());
        ui_head(hb, false); break;
    case L_STOP: ui_head("STOPPED", false); break;
    default: ui_head(NULL, false); break;
    }
    // the guitar, its neck where yours is
    float nd = NK.ok ? clampf(NK.neck, -40.0f, 45.0f) : 0.0f;
    neck_vis += 0.35f * (nd - neck_vis);
    float base = 40.0f, range = pv[P_RANGE];
    for (int d = (int)(base - range); d <= (int)(base + range); d += 5) {     // the range, dotted round the pivot
        float a = (float)clampf((float)d, 2.0f, 80.0f) * 0.0174533f;
        int py = GY - (int)lroundf(31.0f * sinf(a));
        if (py >= 17) oled_pixel(GX + (int)lroundf(31.0f * cosf(a)), py, true);
    }
    { float a = base * 0.0174533f; oled_line(GX + (int)lroundf(29.0f * cosf(a)), GY - (int)lroundf(29.0f * sinf(a)), GX + (int)lroundf(33.0f * cosf(a)), GY - (int)lroundf(33.0f * sinf(a)), true); }
    draw_guitar(GX, GY, clampf(base + neck_vis, 2.0f, 72.0f), GS, clampf(eng_env * 30.0f, 0.0f, 2.0f), v->t);
    // the soundhole, the loop, the mode
    rosette();
    draw_loop_ring(v->t);
    scene_mode(v);
    parts_step(&parts, 0.04f, NY.mode == M_BLOOM ? 20.0f : 0.0f, NY.mode == M_CLOUD ? 0.6f : 1.2f);
    parts_draw(&parts);
    neck_words(v->cover);
}

// ---------------------------------------------------------------- NECK SETUP
// Four steps, numbered in the header; the guitar on the left moves with the angle measured from home.
void scene_setup(const view_t *v)
{
    frame_n++;
    int s = ear_setup_state();
    float p = ear_setup_progress(), a = ear_setup_angle();
    static const char *const head[] = { "", "SETUP 1/4", "SETUP 1/4", "SETUP 2/4", "SETUP 3/4", "SETUP 4/4", "SETUP DONE" };
    ui_head(head[clampi(s, 0, SU_DONE)], true);
    const char *l1 = "", *l2 = "", *foot = "REC OR KNOB: STOP";
    float deg = 40.0f, tip = 0.0f;
    char b1[24], b2[24];
    switch (s) {
    case SU_STILL: l1 = "SIT AS YOU"; l2 = "PLAY, STILL"; break;
    case SU_HOME: l1 = "HOME TAKEN"; break;
    case SU_LIFT:
        l1 = "LIFT THE NECK"; snprintf(b2, sizeof b2, "AND HOLD: %d", (int)lroundf(a)); l2 = b2;
        deg = 40.0f + clampf(a, 0.0f, 32.0f); break;
    case SU_BACK: l1 = "BACK TO"; l2 = "WHERE YOU SIT"; deg = 40.0f + clampf(a, 0.0f, 32.0f); break;
    case SU_TIP:
        l1 = "TIP THE TOP UP"; snprintf(b2, sizeof b2, "TO YOU: %d", (int)lroundf(a)); l2 = b2;
        tip = clampf(a, 0.0f, 30.0f); foot = "KNOB: SKIP  REC: STOP"; break;
    default: {
        float lift, tp; ear_setup_result(&lift, &tp);
        snprintf(b1, sizeof b1, "LIFT %d: ALL", (int)lroundf(lift));
        if (tp > 0.0f) snprintf(b2, sizeof b2, "TIP %d: ALL", (int)lroundf(tp)); else snprintf(b2, sizeof b2, "TIP: AS IT WAS");
        l1 = b1; l2 = b2; deg = 40.0f + clampf(lift, 0.0f, 32.0f); foot = "LIFT IS UP NOW";
        break;
    }
    }
    // the tip: the guitar drawn a little shorter, as a top leaning back would look from the front
    draw_guitar(GX, GY, deg, GS * (1.0f - 0.012f * tip), 0.0f, v->t);
    oled_ftext_ex(F_SMALL, 50, 21, l1, 1);
    oled_ftext_ex(F_SMALL, 50, 32, l2, 1);
    if (s == SU_STILL || s == SU_LIFT || s == SU_BACK || s == SU_TIP) ui_slider(50, 44, 74, p);
    oled_ftext_ex(F_TINY, 50, 57, foot, 0);
}

// ---------------------------------------------------------------- SLAP SETUP
void scene_slapset(const view_t *v)
{
    frame_n++;
    int s = ear_slapset_state();
    ui_head(s == SS_DONE ? "SLAPS SET" : "SLAP SETUP", true);
    char b1[24], b2[24];
    const char *l1 = "", *l2 = "";
    float j = ear_jolt();
    draw_guitar(GX, GY, 40.0f, GS, s == SS_STRUM ? clampf(eng_env * 30.0f, 0.0f, 2.0f) : 0.0f, v->t);
    if (s == SS_STRUM) { l1 = "STRUM HARD"; l2 = "AS YOU PLAY"; }
    else if (s == SS_SLAP) { l1 = "NOW SLAP"; l2 = "THE TOP 3 TIMES"; }
    else {
        float st, sl, th; bool apart; ear_slapset_result(&st, &sl, &th, &apart);
        snprintf(b1, sizeof b1, "STRUM %.1f G", (double)st);
        snprintf(b2, sizeof b2, "SLAP %.1f G", (double)sl);
        l1 = b1; l2 = b2;
    }
    oled_ftext_ex(F_SMALL, 50, 21, l1, 1);
    oled_ftext_ex(F_SMALL, 50, 32, l2, 1);
    if (s == SS_STRUM || s == SS_SLAP) {
        ui_slider(50, 44, 74, ear_slapset_progress());
        snprintf(b1, sizeof b1, "JOLT %.1f G", (double)j);
        oled_ftext_ex(F_TINY, 50, 57, b1, 0);
        if (j > 0.3f) parts_emit(&parts, (float)GX, (float)GY - 8.0f, 30.0f * rng_bi(&pseed), -20.0f * j, 0.4f, PART_SPARK, 1);
    } else {
        float st, sl, th; bool apart; ear_slapset_result(&st, &sl, &th, &apart);
        if (apart) snprintf(b1, sizeof b1, "A SLAP FROM %.1f G", (double)th); else snprintf(b1, sizeof b1, "TOO ALIKE: SLAP HARDER");
        oled_ftext_ex(F_TINY, 50, 57, b1, 0);
    }
    parts_step(&parts, 0.04f, 40.0f, 1.0f);
    parts_draw(&parts);
}

// ---------------------------------------------------------------- the splash: a guitar, its strings plucked, the name
bool scene_splash(int f)
{
    if (f == 0) { parts.n = 0; frame_n = 0; }
    frame_n++;
    float t = (float)f / 25.0f;
    // the guitar swings up into playing position
    float deg = f < 20 ? 5.0f + 35.0f * ease_back(clampf((float)f / 20.0f, 0.0f, 1.0f)) : 40.0f;
    float amp = f >= 22 && f < 70 ? 2.0f * expf(-(float)((f - 22) % 12) / 5.0f) : 0.0f;
    draw_guitar(22, 52, deg, 1.25f, amp, t);
    if (f >= 22 && f < 70 && (f - 22) % 12 == 0) {                // each pluck sends notes out of the hole
        float a = deg * 0.0174533f;
        float hx = 22.0f + cosf(a) * 9.4f, hy = 52.0f - sinf(a) * 9.4f;
        for (int k = 0; k < 4; k++) parts_emit(&parts, hx, hy, 18.0f + 20.0f * rng_uni(&pseed), -20.0f * rng_uni(&pseed) - 6.0f, 1.2f, k & 1 ? PART_SPARK : PART_DOT, 1);
    }
    parts_step(&parts, 0.04f, -6.0f, 0.4f);
    parts_draw(&parts);
    if (f >= 36) {
        float e = ease_bounce(clampf((float)(f - 36) / 16.0f, 0.0f, 1.0f));
        int y = (int)(-14.0f + 34.0f * e);
        oled_ftext_ex(F_MED, 66, y, "NYLON", 2); oled_ftext_ex(F_MED, 67, y, "NYLON", 2);
        if (f >= 56) { oled_ftext_ex(F_TINY, 66, y + 15, "THE NECK IS", 1); oled_ftext_ex(F_TINY, 66, y + 22, "THE PEDAL", 1); }
    }
    return f < 96;
}
