// MANTRA's screen (mantra.h): the header band, the Buddha singing, the splash, the screensaver. Drawing only, in the
// UI task; kept out of the IRAM list, nothing here runs per sample.
//
//   the header   the core's: the looper's state (or the name), the sound, the meter
//   the Buddha   seated in lotus on a lotus base, hands in his lap, eyes closed; his mouth is the lead voice's (how
//                open, how round), his chest rises and falls with the score's breath, the halo glows with the voices,
//                sound rings leave his mouth on every syllable and swell
//   the monks    in MONKS, the other voices sit beside him, mouths moving with their own levels
//   the ladder   in THROAT and KARGYRAA, the harmonics of the drone up the right side, the whistle's harmonic lit
//   the words    in DAISY and OM MANI, the line along the top, the syllable sung now lit
//   the bowl     at his left, it ripples when struck
//   the amps     with the drone up, a wall of cabinets behind him (two rows across the back), their cones pumping
//                with the amps' level, sparks over the wall while the feedback sings
#include "mantra.h"
#include "ui.h"
#include "front.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int mn_beat_flash(void);


// ---------------------------------------------------------------- the Buddha
// cx: his centre; top: the top of his topknot. 47 rows down to the lotus' foot. The body is a silhouette filled with
// the robe's grey, its edge solid; the face is dark with lit features.
static const int8_t body_w[22] = { 4, 8, 11, 12, 12, 13, 13, 13, 14, 14, 14, 15, 15, 16, 18, 20, 21, 22, 23, 23, 23, 22 };
static void buddha(int cx, int top, float mouth, float round, float breath, float glow)
{
    int hy = top + 10;                                            // the head's centre
    // the halo behind him: a ring, a second one as he sings louder, short rays when he sings out
    int hr = 13 + (int)(glow * 1.5f + 0.5f);
    oled_circle(cx, hy, hr, true);
    if (glow > 0.3f) for (int a = 0; a < 360; a += 8) {
        float r = (float)a * 0.0174533f;
        oled_pixel(cx + (int)((float)(hr + 2) * cosf(r)), hy + (int)((float)(hr + 2) * sinf(r)), true);
    }
    if (glow > 0.6f) for (int a = 0; a < 360; a += 45) {
        float r = (float)a * 0.0174533f, r0 = (float)hr + 4.0f, r1 = r0 + 2.0f + glow * 2.0f;
        oled_line(cx + (int)(r0 * cosf(r)), hy + (int)(r0 * sinf(r)), cx + (int)(r1 * cosf(r)), hy + (int)(r1 * sinf(r)), true);
    }
    // the body: shoulders that rise and widen with the breath
    int y0 = hy + 9, b = (int)(breath * 1.5f + 0.5f);
    for (int k = 0; k < 22; k++) {
        int y = y0 + k - (k < 4 ? b : 0), w = body_w[k] + (k >= 2 && k < 13 ? b : 0);
        oled_rect(cx - w, y, 2 * w + 1, 1, false);                    // clear the halo behind him
        for (int x = cx - w + 1; x < cx + w; x++) if (((x + y) & 1) == 0) oled_pixel(x, y, true);
        oled_pixel(cx - w, y, true); oled_pixel(cx + w, y, true);
    }
    oled_line(cx - body_w[2] - b, y0 + 2 - b, cx - 4, y0, true);   // the shoulders' line
    oled_line(cx + 4, y0, cx + body_w[2] + b, y0 + 2 - b, true);
    oled_line(cx - 9, y0 + 4, cx - 6, y0 + 13, true);              // the arms' inner edges
    oled_line(cx + 9, y0 + 4, cx + 6, y0 + 13, true);
    oled_line(cx - 3, y0 + 1, cx + 8, y0 + 12, true);              // the robe's edge over his left shoulder
    oled_disc(cx, y0 + 13, 3, false); oled_rframe(cx - 6, y0 + 12, 13, 4, 2);   // the hands in his lap
    oled_rect(cx - 15, y0 + 16, 31, 1, true);                       // the shin across
    oled_rframe(cx - 13, y0 + 17, 6, 3, 1); oled_rframe(cx + 8, y0 + 17, 6, 3, 1);   // the soles, turned up
    oled_line(cx - body_w[21], y0 + 21, cx + body_w[21], y0 + 21, true);
    // the lotus under him
    int py = y0 + 22;
    for (int k = -3; k <= 3; k++) { int px = cx + k * 7; oled_rframe(px - 4, py, 9, 5, 2); oled_pixel(px, py + 2, true); }
    // the head: dark, outlined; the topknot; the curls on the crown; long earlobes
    oled_disc(cx, hy, 8, false);
    oled_circle(cx, hy, 8, true);
    oled_disc(cx, top + 1, 2, true);
    for (int y = hy - 7; y <= hy - 4; y++) for (int x = cx - 6; x <= cx + 6; x++)
        if (((x + y) & 1) == 0 && (x - cx) * (x - cx) + (y - hy) * (y - hy) < 49) oled_pixel(x, y, true);
    oled_line(cx - 9, hy - 1, cx - 9, hy + 6, true); oled_line(cx + 9, hy - 1, cx + 9, hy + 6, true);
    // the face: the dot between the brows, the eyes closed, the nose, the mouth
    oled_pixel(cx, hy - 2, true);
    oled_line(cx - 5, hy, cx - 2, hy + 1, true); oled_pixel(cx - 1, hy, true);
    oled_line(cx + 2, hy + 1, cx + 5, hy, true); oled_pixel(cx + 1, hy, true);
    oled_line(cx, hy + 2, cx, hy + 3, true);
    int open = (int)(mouth * 3.0f + 0.5f), my = hy + 5;
    if (open <= 0) oled_line(cx - 2, my, cx + 2, my, true);                  // shut: a hum, a faint smile
    else if (round > 0.5f) oled_circle(cx, my + 1, open > 1 ? 2 : 1, true);  // OO, OH: round lips
    else { oled_rect(cx - 2, my, 5, open, true); if (open > 1) oled_line(cx - 1, my + 1, cx + 1, my + 1, false); }
}

// a monk: a small seated figure whose mouth opens with his voice
static void monk(int cx, int base, float lv, int k)
{
    int hy = base - 13;
    oled_circle(cx, hy, 3, true);
    oled_line(cx - 3, hy + 4, cx - 6, base, true); oled_line(cx + 3, hy + 4, cx + 6, base, true);
    oled_line(cx - 6, base, cx + 6, base, true);
    for (int y = hy + 5; y < base; y++) for (int x = cx - 4; x <= cx + 4; x++) if (((x + y + k) & 1) == 0 && abs(x - cx) <= 3 + (y - hy - 5) / 3) oled_pixel(x, y, true);
    if (lv > 0.05f) oled_rect(cx - 1, hy + 1, 3, lv > 0.4f ? 2 : 1, true);
}

// ---------------------------------------------------------------- sound rings
typedef struct { float r, x, y; int life; } ring_t;
static ring_t rings[10];
static void ring_add(float x, float y)
{
    for (int i = 0; i < 10; i++) if (rings[i].life <= 0) { rings[i] = (ring_t){ 3.0f, x, y, 26 }; return; }
}
static void rings_draw(void)
{
    for (int i = 0; i < 10; i++) {
        ring_t *g = &rings[i];
        if (g->life <= 0) continue;
        g->r += 1.6f; g->life--;
        if (g->life > 6 || (g->life & 1)) {
            ui_arc((int)g->x, (int)g->y, (int)g->r, -35, 35);
            ui_arc((int)g->x, (int)g->y, (int)g->r, 145, 215);
        }
    }
}

// ---------------------------------------------------------------- the harmonic ladder (the throat modes)
static void ladder(int x, int y0, int y1, int lit, float whistle, uint32_t f)
{
    for (int h = 2; h <= 16; h++) {
        float t = (log2f((float)h) - 1.0f) / 3.0f;               // 2 at the bottom, 16 at the top
        int y = y1 - (int)(t * (float)(y1 - y0) + 0.5f);
        bool on = h == lit;
        int w = on ? 6 + (int)(whistle * 10.0f) : (h % 2 ? 2 : 4);
        if (on) { oled_rect(x + 18 - w, y, w, 1, true); if ((f / 3) & 1) oled_pixel(x + 17 - w - 2, y, true); }
        else oled_hline_pat(x + 18 - w, y, w, 0x55, true);
        if (on) { char b[4]; snprintf(b, sizeof b, "%d", h); oled_ftext(F_TINY, x, y - 2, b); }
    }
}

// ---------------------------------------------------------------- the words (DAISY, OM MANI)
static void words(const sc_view_t *s)
{
    if (!s->line) return;
    const char *l = s->line;
    int n = (int)strlen(l), a = s->word_at, z = s->word_at + s->word_len;
    if (a < 0) a = 0;
    if (z > n) z = n;
    char pre[64], mid[32];
    snprintf(pre, sizeof pre, "%.*s", a, l);
    snprintf(mid, sizeof mid, "%.*s", z - a, l + a);
    int wpre = oled_ftext_w(F_TINY, pre, 1), wmid = oled_ftext_w(F_TINY, mid, 1);
    int x = OLED_W / 2 - wpre - wmid / 2;                         // the syllable sung now at the middle
    int full = oled_ftext_w(F_TINY, l, 1);
    if (full <= OLED_W - 4) x = (OLED_W - full) / 2;
    oled_rect(0, 16, OLED_W, 8, false);                           // a strip of its own, over the halo
    oled_ftext_ex(F_TINY, x, 18, l, 1);
    if (z > a) oled_invert(x + wpre - 1, 17, wmid + 1, 7);
}

// ---------------------------------------------------------------- the bowl
static float bowl_ph;
static void bowl(int x, int y, float ring, uint32_t f)
{
    oled_line(x - 9, y, x + 9, y, true);                          // its rim
    for (int i = -8; i <= 8; i++) { int d = (int)(5.0f * sqrtf(1.0f - (float)(i * i) / 81.0f)); oled_pixel(x + i, y + d, true); }
    oled_line(x - 3, y + 6, x + 3, y + 6, true);                  // its foot
    if (ring > 0.02f) {                                           // ripples above it, shaking a little
        bowl_ph += 0.25f;
        int k = 1 + (int)(ring * 3.0f);
        for (int r = 0; r < k; r++) {
            int rr = 4 + r * 3 + (int)(bowl_ph * 1.5f) % 3;
            ui_arc(x, y - 1, rr, 200, 340);
        }
        if (ring > 0.5f && (f & 1)) { oled_pixel(x - 10, y - 1, true); oled_pixel(x + 10, y - 1, true); }
    }
}

// ---------------------------------------------------------------- a bicycle built for two (DAISY's last line)
static float bike_x = -40.0f;
static bool has_word(const char *l, const char *w)          // w in l, ignoring case
{
    for (; l && *l; l++) {
        int k = 0;
        while (w[k] && l[k] && ((l[k] | 32) == (w[k] | 32))) k++;
        if (!w[k]) return true;
    }
    return false;
}
static void bicycle(int x, int y, uint32_t f)
{
    oled_rect(x - 5, y - 12, 25, 16, false);                      // it rides in front of everything
    for (int k = 0; k < 2; k++) {                                 // two wheels, their spokes turning
        int wx = x + k * 14;
        oled_circle(wx, y, 3, true);
        int sp = (int)((f / 2 + (uint32_t)k) % 2);
        if (sp) oled_line(wx - 2, y, wx + 2, y, true); else oled_line(wx, y - 2, wx, y + 2, true);
    }
    oled_line(x, y, x + 5, y - 4, true); oled_line(x + 5, y - 4, x + 11, y - 4, true); oled_line(x + 11, y - 4, x + 14, y, true);
    oled_line(x + 5, y - 4, x + 7, y, true);                      // the frame and the pedals
    for (int r = 0; r < 2; r++) {                                 // two riders
        int rx = x + 4 + r * 7, legs = (int)((f / 3 + (uint32_t)r) % 2);
        oled_circle(rx, y - 10, 1, true);
        oled_line(rx, y - 9, rx, y - 6, true);
        oled_line(rx, y - 6, rx + (legs ? 2 : 1), y - 4, true);
        oled_line(rx, y - 8, rx + 3, y - 7, true);
    }
}

// ---------------------------------------------------------------- the wall of amps (the drone)
static float cone_s, wall_s;
static void amp_wall(const dr_view_t *d, uint32_t f, bool throat)
{
    wall_s += 0.1f * ((d->playing ? 0.4f + 0.6f * d->swell : 0.0f) - wall_s);
    cone_s += 0.4f * (d->level - cone_s);
    if (wall_s < 0.02f) return;
    int right = throat ? 100 : OLED_W;                             // the ladder keeps its column
    for (int row = 0; row < 2; row++) {
        int y = 16 + row * 12;
        for (int x = 2 - (row * 11); x < right - 4; x += 22) {
            int x0 = x < 0 ? 0 : x, w = 21 - (x0 - x);
            if (x0 + w > right) w = right - x0;
            if (w < 8) continue;
            oled_rframe(x0, y, w, 11, 1);                           // the cabinet
            oled_dither(x0 + 1, y + 1, w - 2, 9, wall_s > 0.7f ? 2 : 1, true);   // its cloth
            int cxx = x + 10, r = 3 + (cone_s > 0.5f ? 1 : 0);      // the cone, pumping with the amps
            if (cxx - r >= x0 && cxx + r < x0 + w) { oled_disc(cxx, y + 5, r, false); oled_circle(cxx, y + 5, r, true); if (cone_s > 0.2f && (f & 2)) oled_pixel(cxx, y + 5, true); }
        }
    }
    if (d->feedback > 0.3f) for (int k = 0; k < 6; k++) {          // the feedback sings: sparks over the wall
        uint32_t h = (f / 2 + (uint32_t)k * 977u) * 2654435761u;
        if ((h >> 28) < (uint32_t)(d->feedback * 12.0f)) oled_pixel((int)((h >> 8) % (uint32_t)(right - 2)) + 1, 16 + (int)((h >> 20) & 7), true);
    }
}

// ---------------------------------------------------------------- the whole frame
static uint32_t frame_n, last_hits;
static float glow_s, mouth_s, round_s, breath_s;
static int last_word = -1;

void scene_draw(const view_t *v)
{
    frame_n++;
    mv_view_t mvv; mv_view(&mvv);
    sc_view_t scv; sc_view(&scv);
    dr_view_t drv; dr_view(&drv);
    oled_clear();
    float lv = 0.0f;
    for (int i = 0; i < MV_N; i++) if (mvv.level[i] > lv) lv = mvv.level[i];
    glow_s += 0.3f * (clampf(lv * 2.5f, 0.0f, 1.0f) - glow_s);
    mouth_s += 0.5f * (mvv.mouth - mouth_s);
    round_s += 0.5f * (mvv.round - round_s);
    breath_s += 0.2f * (scv.breath - breath_s);
    bool wordsy = scv.line != NULL;
    bool throat = scv.mode == MODE_THROAT || scv.mode == MODE_KARGYRAA;
    int cx = throat ? 56 : OLED_W / 2, top = wordsy ? 23 : 17;
    // rings: on every new syllable, and steadily while the voices are loud
    if (scv.word_at != last_word) { last_word = scv.word_at; if (lv > 0.02f) ring_add((float)cx, (float)(top + 15)); }
    else if (lv > 0.05f && frame_n % (uint32_t)(14 - (int)(clampf(lv, 0.0f, 1.0f) * 8.0f)) == 0) ring_add((float)cx, (float)(top + 15));
    (void)last_hits;
    amp_wall(&drv, frame_n, throat);                              // behind everything
    rings_draw();
    bool bowl_out = scv.bowl > 0.0f || hands.knocked;
    if (scv.monks > 1) {                                          // the other voices beside him (the far left one makes
        static const int8_t off[4] = { -34, 34, -50, 50 };        // room for the bowl)
        for (int k = 0; k < scv.monks - 1 && k < 4; k++) if (!(k == 2 && bowl_out)) monk(cx + off[k], 62, mvv.level[k + 1], k);
    }
    buddha(cx, top, mouth_s, round_s, breath_s, glow_s);
    if (throat) ladder(108, 20, 60, mvv.harmonic ? mvv.harmonic : scv.harmonic, mvv.whistle, frame_n);
    if (wordsy) words(&scv);
    if (has_word(scv.line, "BICYCLE") && bike_x <= -40.0f) bike_x = (float)OLED_W + 2.0f;   // he sings it: it sets off
    if (bike_x > -20.0f) { bike_x -= 1.6f; bicycle((int)bike_x, 60, frame_n); }            // and rides all the way across
    else if (!has_word(scv.line, "BICYCLE")) bike_x = -40.0f;
    if (bowl_out) bowl(throat ? 14 : 14, 54, scv.bowl, frame_n);
    oled_rect(0, 0, OLED_W, 16, false);
    ui_head(NULL, false);
    (void)v;
}

// ---------------------------------------------------------------- the splash: he appears, the halo opens
bool scene_splash(int f)
{
    if (f >= 90) return false;
    oled_clear();
    float t = (float)f / 90.0f;
    buddha(OLED_W / 2, 17, f > 50 ? 0.5f + 0.5f * sinf((float)f * 0.3f) : 0.0f, 1.0f, 0.5f + 0.5f * sinf((float)f * 0.12f), t);
    if (f < 40) oled_dither(0, 16, OLED_W, 48, 4 - f / 10, false);   // out of the dark
    if (f > 50 && f % 6 == 0) ring_add(OLED_W / 2.0f, 32.0f);
    rings_draw();
    oled_rect(0, 0, OLED_W, 16, false);
    int w = oled_ftext_w(F_MED, "MANTRA", 3);
    oled_ftext_ex(F_MED, (OLED_W - w) / 2, 3, "MANTRA", 3);
    return true;
}

// ---------------------------------------------------------------- the screensaver: he meditates, dimly
void scene_saver(uint32_t f)
{
    oled_clear();
    float br = 0.5f + 0.5f * sinf((float)f * 0.05f);
    buddha(40 + (int)(20.0f * sinf((float)f * 0.004f)), 17, 0.0f, 0.0f, br, 0.0f);
    oled_dither(0, 0, OLED_W, OLED_H, 2, false);
}
