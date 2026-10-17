// SNIFF's screen. Play: a radar on the left (each device a blip: its angle from its hash, nearer the
// centre the stronger its signal; a ring when it sends; routers drawn as squares), the dog on the right
// sniffing the air (scents drift from the blips to its nose; ears up when the room moves, cowering in a
// storm, asleep when nothing is on the air), the room's radio shape as a landscape that ripples when
// people move, and the 13 channels as a histogram. Static and lightning in a storm. HUNT: a big gauge,
// the target's signal in dBm, warmer or colder.
#include "inst.h"
#include "ui.h"
#include "looper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static parts_t scents, fx;
static uint32_t seed = 0xd06d06u;
static float wag_ph, beam, shake;
static uint32_t beeps_seen; static float beep_ring;

// ---------------------------------------------------------------- the dog
enum { DOG_STAND, DOG_WALK, DOG_SNIFF, DOG_ALERT, DOG_POINT, DOG_COWER, DOG_SLEEP };
static int dog_x, dog_y, dog_k = 1; static bool dog_flip;
static void dset(int x, int y, bool on)
{
    int X = dog_flip ? dog_x + (27 - x) * dog_k : dog_x + x * dog_k, Y = dog_y + y * dog_k;
    if (dog_k == 1) oled_pixel(X, Y, on); else oled_rect(X, Y, dog_k, dog_k, on);
}
static void dpx(int x, int y) { dset(x, y, true); }
static void dclr(int x, int y) { dset(x, y, false); }
static void dline(int x0, int y0, int x1, int y1)
{
    int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) { dpx(x0, y0); if (x0 == x1 && y0 == y1) break; int e2 = 2 * e; if (e2 >= dy) { e += dy; x0 += sx; } if (e2 <= dx) { e += dx; y0 += sy; } }
}
static void dsprite(int x, int y, const char *const *rows, int h)
{
    for (int r = 0; r < h; r++) for (int c = 0; rows[r][c]; c++) if (rows[r][c] == '#') dpx(x + c, y + r);
}
static const char *const body_s[] = { "..#########...", ".############.", "##############", "##############", ".#############", "..##########.." };
// a skull with a stop and a muzzle, facing right; the eye and the mouth are cut out after
static const char *const head_s[] = { ".####......", "######.....", "#######....", "###########", "###########", ".########..", "..####....." };
static const char *const ear_up[] = { "#..", "##.", "###", "###" };

// The dog in a 28x16 box at (x, y) (times k), ground at y + 15, facing right (flip: left).
static void dog_k_at(int x, int y, int pose, float t, bool twitch, bool flip, int k);
static void dog(int x, int y, int pose, float t, bool twitch, bool flip) { dog_k_at(x, y, pose, t, twitch, flip, 1); }
static void dog_k_at(int x, int y, int pose, float t, bool twitch, bool flip, int k)
{
    dog_x = x; dog_y = y; dog_flip = flip; dog_k = k;
    int by = pose == DOG_COWER ? 7 : pose == DOG_SLEEP ? 9 : 6;
    int hx = 15, hy = 1;
    switch (pose) {
    case DOG_SNIFF: hx = 16; hy = 7; break;
    case DOG_ALERT: hx = 15; hy = 0; break;
    case DOG_POINT: hx = 16; hy = 2; break;
    case DOG_COWER: hx = 15; hy = 5; break;
    case DOG_SLEEP: hx = 16; hy = 9; break;
    }
    // tail
    float wag = sinf(wag_ph);
    if (pose == DOG_POINT) dline(4, by + 1, -1, by);
    else if (pose == DOG_COWER) dline(4, by + 2, 1, by + 6);
    else if (pose == DOG_SLEEP) { dline(4, by + 3, 1, by + 4); dline(1, by + 4, 2, by + 6); dline(2, by + 6, 6, by + 6); }
    else dline(4, by + 1, (int)lroundf(-1.0f + 1.5f * wag), by - 4 + (int)lroundf(1.5f * wag));
    // legs
    if (pose == DOG_SLEEP) dline(15, 15, 26, 15);                     // front paws stretched out under the chin
    else {
        int ly = by + 5, ground = 15;
        int sw = pose == DOG_WALK ? ((int)(t * 8.0f) & 1 ? 1 : -1) : 0;
        dline(5, ly, 5 - sw, ground); dline(7, ly, 7 + sw, ground);
        if (pose == DOG_POINT) { dline(14, ly, 14, ground); dline(16, ly, 18, ly + 2); }
        else { dline(14, ly, 14 + sw, ground); dline(16, ly, 16 - sw, ground); }
    }
    dsprite(4, by, body_s, 6);
    dsprite(hx, hy, head_s, 7);
    // ears: up, pricked high when alert, or folded down (a line cut into the skull)
    if (pose == DOG_ALERT || pose == DOG_POINT) { dsprite(hx + 1, hy - 4, ear_up, 4); dpx(hx + 1, hy - 5); }
    else if (pose == DOG_COWER || pose == DOG_SLEEP) { dclr(hx + 2, hy + 1); dclr(hx + 2, hy + 2); dclr(hx + 1, hy + 3); dpx(hx - 1, hy + 2); dpx(hx - 1, hy + 3); }
    else dsprite(hx + 1, hy - 3, ear_up, 3);
    // eye (shut when asleep), the mouth's line, the nose
    if (pose == DOG_SLEEP) { dclr(hx + 4, hy + 2); dclr(hx + 5, hy + 2); }
    else dclr(hx + 4, hy + 2);
    dclr(hx + 7, hy + 4); dclr(hx + 8, hy + 4);
    if (twitch) { dpx(hx + 11, hy + 3); dpx(hx + 11, hy + 2); }
    else dpx(hx + 11, hy + 3);
    dog_k = 1;
}
static void dog_nose(int x, int y, int pose, bool flip, int *nx, int *ny)
{
    int hx = 15, hy = 1;
    if (pose == DOG_SNIFF) { hx = 16; hy = 7; } else if (pose == DOG_ALERT) hy = 0; else if (pose == DOG_POINT) { hx = 16; hy = 2; }
    *nx = flip ? x + 27 - (hx + 11) : x + hx + 11; *ny = y + hy + 3;
}

// ---------------------------------------------------------------- the radar
#define RCX 27
#define RCY 40
#define RR  22
static float prox_of(float rssi) { return clampf((rssi + 95.0f) / 60.0f, 0, 1); }
static void dev_pos(const wdev_t *d, int cx, int cy, float r, int *x, int *y)
{
    float rr = 2.0f + (1.0f - prox_of(d->rssi)) * (r - 3.0f);
    *x = cx + (int)lroundf(rr * cosf(d->ang)); *y = cy + (int)lroundf(rr * sinf(d->ang));
}

void scene_heard(int di, int kind)
{
    (void)kind;
    if (scents.n > 40) return;
    int x, y; dev_pos(&S.dev[di], RCX, RCY, RR, &x, &y);
    int nx, ny; dog_nose(61, 18, DOG_STAND, false, &nx, &ny);
    float life = 0.5f + 0.3f * rng_uni(&seed);
    parts_emit(&scents, (float)x, (float)y, ((float)nx - (float)x) / life, ((float)ny - (float)y) / life - 10.0f, life, PART_DOT, 1);
}

static void radar(int cx, int cy, int r, float t, bool hunt)
{
    oled_circle(cx, cy, r, true);
    for (int a = 0; a < 360; a += 20) { float ra = (float)a * 0.0174533f; oled_pixel(cx + (int)lroundf((float)r * 0.5f * cosf(ra)), cy + (int)lroundf((float)r * 0.5f * sinf(ra)), true); }
    oled_pixel(cx, cy, true);
    for (int k = 0; k < 4; k++) { float ra = (float)k * 1.5708f; oled_line(cx + (int)lroundf((float)(r - 3) * cosf(ra)), cy + (int)lroundf((float)(r - 3) * sinf(ra)), cx + (int)lroundf((float)r * cosf(ra)), cy + (int)lroundf((float)r * sinf(ra)), true); }
    beam = fmodf(t * 2.6f, TWO_PI);
    for (int k = 0; k < 4; k++) {                                 // the beam and its fading trail
        float a = beam - (float)k * 0.09f;
        float ca = cosf(a), sa = sinf(a);
        for (int s = 2; s < r; s++) if (k == 0 || ((s + k) % (k + 1)) == 0) oled_pixel(cx + (int)lroundf((float)s * ca), cy + (int)lroundf((float)s * sa), true);
    }
    for (int i = 0; i < NDEV; i++) {
        const wdev_t *d = &S.dev[i];
        if (!d->who) continue;
        int x, y; dev_pos(d, cx, cy, (float)r, &x, &y);
        float since = fmodf(beam - d->ang + TWO_PI * 4.0f, TWO_PI);  // the beam just passed: the blip glows
        bool lit = since < 1.2f || d->pulse > 0.3f;
        if (d->ap) { oled_frame(x - 1, y - 1, 3, 3); if (lit) oled_pixel(x, y, true); }
        else if (lit && d->rate > 1.5f) oled_rect(x - 1, y - 1, 2, 2, true);
        else oled_pixel(x, y, true);
        if (d->pulse > 0.3f && d->rssi > -85.0f) { int pr = 2 + (int)((1.0f - d->pulse) * 5.0f); oled_circle(x, y, pr, true); }
        if (hunt && i == S.target) { oled_line(x - 4, y - 4, x - 2, y - 4, true); oled_line(x - 4, y - 4, x - 4, y - 2, true); oled_line(x + 4, y + 4, x + 2, y + 4, true); oled_line(x + 4, y + 4, x + 4, y + 2, true); }
    }
}

// ---------------------------------------------------------------- the room and the channels
static void landscape(int x0, int y0, int w, int h, float t)
{
    int base = y0 + h - 1;
    int px = -1, py = 0;
    for (int b = 0; b < CSI_BANDS; b++) {
        float v = S.room_live ? S.shape[b] : 1.0f;
        float rip = S.motion * 3.5f * sinf(t * 7.0f + (float)b * 1.1f);
        int x = x0 + b * (w - 1) / (CSI_BANDS - 1);
        int y = base - (int)clampf(v * (float)h * 0.45f + rip + 1.0f, 1.0f, (float)(h - 1));
        if (px >= 0) {
            oled_line(px, py, x, y, true);
            for (int xx = px; xx <= x; xx++) {                      // dithered ground under the line
                int yy = py + (y - py) * (xx - px) / (x - px > 0 ? x - px : 1);
                for (int k = yy + 2; k <= base; k++) if (((xx + k) & 1) == 0 && ((k & 1) || S.motion > 0.3f)) oled_pixel(xx, k, true);
            }
        }
        px = x; py = y;
    }
    if (!S.room_live) oled_hline_pat(x0, base, w, 0x11, true);
}

static void histogram(int x0, int y0, int h)
{
    float mx = 1.0f;
    for (int c = 1; c <= 13; c++) if (S.hist[c] > mx) mx = S.hist[c];
    for (int c = 1; c <= 13; c++) {
        int x = x0 + (c - 1) * 5;
        int bh = (int)(log10f(1.0f + S.hist[c]) / log10f(1.0f + mx) * (float)(h - 2) + 0.5f);
        if (S.hist[c] > 0.1f && bh < 1) bh = 1;
        if (bh > 0) oled_rect(x, y0 + h - 1 - bh, 4, bh, true);
        if (c == S.chan) { oled_rect(x, y0 + h, 4, 1, true); oled_pixel(x + 1, y0 + h + 1, true); oled_pixel(x + 2, y0 + h + 1, true); }
        else oled_pixel(x + 1, y0 + h, true);
    }
}

static void storm_fx(void)
{
    int n = (int)(S.storm * S.storm * 160.0f);
    for (int i = 0; i < n; i++) { uint32_t r = rng_next(&seed); int x = (int)(r % 128u), y = 16 + (int)((r >> 8) % 48u); oled_pixel(x, y, !oled_get(x, y)); }
    if (S.thunder > 0.5f) {
        int x = 30 + (int)(rng_next(&seed) % 70u), y = 16;
        while (y < 52) { int nx = x + (int)(rng_next(&seed) % 9u) - 4, ny = y + 3 + (int)(rng_next(&seed) % 4u); oled_line(x, y, nx, ny, true); oled_line(x + 1, y, nx + 1, ny, true); x = nx; y = ny; }
        if (S.thunder > 0.85f) oled_invert(0, 16, OLED_W, 48);
    }
}

// ---------------------------------------------------------------- play
static int dog_pose(void)
{
    if (!S.radio_ok || S.rate < 0.5f) return DOG_SLEEP;
    if (S.thunder > 0.2f || S.storm > 0.6f) return DOG_COWER;
    if (S.motion > 0.35f) return DOG_ALERT;
    if (S.sniff > 0.55f) return DOG_SNIFF;
    return DOG_STAND;
}

static void hunt_scene(const view_t *v)
{
    int cx = 36, cy = 61, r = 33;
    ui_arc(cx, cy, r, 180.0f, 360.0f);
    ui_arc(cx, cy, r - 3, 180.0f, 360.0f);
    for (int a = 0; a <= 180; a += 15) {
        float ra = (180.0f + (float)a) * 0.0174533f;
        int l = a % 45 == 0 ? 6 : 3;
        oled_line(cx + (int)lroundf((float)(r - 3) * cosf(ra)), cy + (int)lroundf((float)(r - 3) * sinf(ra)), cx + (int)lroundf((float)(r - 3 - l) * cosf(ra)), cy + (int)lroundf((float)(r - 3 - l) * sinf(ra)), true);
    }
    oled_rect(11, 57, 4, 1, true);                                 // cold - .. + hot
    oled_rect(57, 57, 4, 1, true); oled_rect(58, 56, 2, 3, true);
    static float needle;
    needle += 0.25f * (S.prox - needle);
    float na = (180.0f + needle * 180.0f) * 0.0174533f;
    oled_line(cx, cy, cx + (int)lroundf(27.0f * cosf(na)), cy + (int)lroundf(27.0f * sinf(na)), true);
    oled_line(cx + 1, cy, cx + 1 + (int)lroundf(27.0f * cosf(na)), cy + (int)lroundf(27.0f * sinf(na)), true);
    oled_disc(cx, cy, 3, true);
    if (snd_beeps != beeps_seen) { beeps_seen = snd_beeps; beep_ring = 1.0f; }
    if (beep_ring > 0.05f) { oled_circle(cx, cy, 4 + (int)((1.0f - beep_ring) * 14.0f), true); beep_ring *= 0.7f; }
    char b[16];
    if (S.target < 0 || S.lost) {
        oled_ftext_r(F_MED, 127, 21, S.target < 0 ? "NOBODY" : "LOST", 1);
        oled_ftext_r(F_TINY, 127, 33, S.target < 0 ? "WAITING FOR ONE" : "NOT HEARD FOR 5 S", 1);
    } else {
        snprintf(b, sizeof b, "%d", (int)lroundf(S.target_rssi));
        oled_ftext_r(F_BIG, 127, 17, b, 1);
        snprintf(b, sizeof b, "DBM  %d OF %d", S.target_rank, S.target_of);
        oled_ftext_r(F_TINY, 127, 37, b, 1);
    }
    if (!v->cover && S.target >= 0 && !S.lost) {
        const char *tr = S.trend > 0 ? "WARMER" : S.trend < 0 ? "COLDER" : "STEADY";
        ui_pill_r(127, 45, tr, S.trend > 0);
        oled_ftext_r(F_TINY, 127, 57, S.dev[S.target].ap ? "A ROUTER" : "A DEVICE", 1);
    }
}

void scene_draw(const view_t *v)
{
    float t = v->t;
    float dt = 0.04f;
    wag_ph += dt * (3.0f + fminf(S.rate, 300.0f) * 0.08f);
    bool hunt = S.mode == MODE_HUNT;
    loop_state_t ls = looper_state();
    ui_head(hunt && ls == LOOP_EMPTY ? "HUNT" : NULL, hunt && ls == LOOP_EMPTY);
    if (hunt) { hunt_scene(v); storm_fx(); return; }

    radar(RCX, RCY, RR, t, false);
    // the dog and the scents it catches
    int pose = dog_pose();
    shake = pose == DOG_COWER && S.thunder > 0.1f ? (shake > 0 ? -1.0f : 1.0f) : 0;
    bool twitch = S.sniff > 0.3f && ((v->frame & 1) == 0);
    dog(61 + (int)shake, 18, pose, t, twitch, false);
    if (pose == DOG_SLEEP) {
        float zt = fmodf(t, 2.4f);
        oled_ftext_ex(F_TINY, 84 + (int)(zt * 3.0f), 26 - (int)(zt * 5.0f), "Z", 1);
        if (zt > 1.2f) oled_ftext_ex(F_TINY, 80 + (int)((zt - 1.2f) * 3.0f), 26 - (int)((zt - 1.2f) * 5.0f), "Z", 1);
    }
    parts_step(&scents, dt, 25.0f, 0.5f);
    parts_draw(&scents);
    // readouts
    char b[20];
    if (!S.radio_ok) snprintf(b, sizeof b, "NO WI-FI");
    else if (S.scanning) snprintf(b, sizeof b, "SCAN %d", S.chan);
    else snprintf(b, sizeof b, "%s %d", (int)pv[P_CHAN] == CH_HOP ? "HOP" : "CH", S.chan);
    oled_ftext_r(F_SMALL, 127, 18, b, 1);
    snprintf(b, sizeof b, "%d/S", (int)(S.rate + 0.5f));
    oled_ftext_r(F_SMALL, 127, 27, b, 1);
    snprintf(b, sizeof b, "%d DEV", S.ndev);
    oled_ftext_r(F_TINY, 127, 36, b, 1);
    // the room's shape over the channels
    landscape(58, 41, 70, 13, t);
    if (S.motion > 0.35f) { oled_rect(58, 41, 27, 7, false); oled_ftext_ex(F_TINY, 59, 42, "MOVING", 1); }
    histogram(61, 55, 7);
    storm_fx();
    if (!S.radio_ok) ui_box(40, "WI-FI DID NOT START");
}

// ---------------------------------------------------------------- splash and screensaver
bool scene_splash(int f)
{
    // the dog trots in (twice its size), sniffs the floor, catches something in the air, ears up: SNIFF
    float t = (float)f / 25.0f;
    wag_ph += 0.3f;
    const int gy = 26;                                               // the box's top; the ground is at gy + 31
    oled_hline_pat(0, gy + 31, OLED_W, 0x55, true);
    int pose = DOG_WALK, x;
    if (f < 30) x = -56 + (int)(ease_out((float)f / 30.0f) * 90.0f);
    else x = 34;
    if (f >= 30 && f < 54) pose = DOG_SNIFF;
    else if (f >= 54) pose = DOG_ALERT;
    if (f >= 30 && f < 54 && (f % 4) == 0)
        parts_emit(&fx, (float)(x + 54), (float)(gy + 28), 6.0f + 10.0f * rng_uni(&seed), -22.0f - 14.0f * rng_uni(&seed), 0.9f, PART_SQUARE, 1);
    dog_k_at(x, gy, pose, t, f >= 30 && f < 54 && (f & 2), false, 2);
    parts_step(&fx, 0.04f, -6.0f, 0.3f);
    parts_draw(&fx);
    for (int k = 0; k < 3; k++)                                      // Wi-Fi arcs over its head, one by one
        if (f >= 38 + k * 5) { ui_arc(x + 37, gy + 2, 5 + k * 5, 228.0f, 312.0f); ui_arc(x + 37, gy + 3, 5 + k * 5, 232.0f, 308.0f); }
    if (f >= 56 && f < 84 && ((f / 4) & 1)) oled_ftext_ex(F_MED, x + 52, gy, "!", 1);
    if (f >= 58) {
        float e = ease_back((float)(f - 58) / 14.0f);
        int y = -12 + (int)(e * 15.0f);
        oled_ftext_c(F_MED, 64, y, "SNIFF", 3);
    }
    if (f >= 70) oled_ftext_c(F_TINY, 64, 59, INST.tagline, 1);
    return f < 100;
}

void scene_saver(uint32_t f)
{
    float t = (float)f / 25.0f;
    int cx = 64 + (int)(30.0f * sinf(t * 0.05f)), cy = 32 + (int)(4.0f * sinf(t * 0.031f));
    radar(cx, cy, 26, t * 0.35f, false);
}
