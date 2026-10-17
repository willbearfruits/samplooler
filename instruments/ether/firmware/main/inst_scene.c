// ETHER's screen: the sky is Bluetooth, the ground is Wi-Fi.
//   Sky:    three wires (BLE's three advertising channels) with a bird for each device that advertises, as
//           big as it is near; a bird that calls opens its beak, tugs its wire and lets a note go (CHIRP's).
//   Ground: the room's radio shape (CSI) is the terrain, rippling when someone moves; each Wi-Fi sender is a
//           reed growing from it, taller the nearer, its tip flashing when it sends (a router's reed has an
//           antenna); scents drift from the reeds to the dog's nose. The dog sniffs, looks up at a bird that
//           sings loudly, pricks its ears when the room moves, cowers in a storm, sleeps when the air is empty.
// Leaning the BALANCE one way greys the other world out. HUNT: a gauge, the target's dBm, which radio it is.
#include "inst.h"
#include "ui.h"
#include "looper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DT (1.0f / 25.0f)
static uint32_t seed = 0xe7e2e7e2u;
static float wag_ph, beep_ring; static uint32_t beeps_seen;
static parts_t scents;
static bool dim_on;                                  // greyed out: only every other pixel
static void px(int x, int y) { if (!dim_on || ((x + y) & 1)) oled_pixel(x, y, true); }
static void blk(int x, int y, int w, int h) { for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) px(x + i, y + j); }

// ================================================================ the dog (SNIFF's)
enum { DOG_STAND, DOG_WALK, DOG_SNIFF, DOG_ALERT, DOG_LOOK, DOG_POINT, DOG_COWER, DOG_SLEEP };
static int dog_x, dog_y, dog_k = 1; static bool dog_flip;
static void dset(int x, int y, bool on)
{
    int X = dog_flip ? dog_x + (27 - x) * dog_k : dog_x + x * dog_k, Y = dog_y + y * dog_k;
    if (!on) { oled_rect(X, Y, dog_k, dog_k, false); return; }
    if (dog_k == 1) px(X, Y); else blk(X, Y, dog_k, dog_k);
}
static void dpx(int x, int y) { dset(x, y, true); }
static void dclr(int x, int y) { dset(x, y, false); }
static void dline(int x0, int y0, int x1, int y1)
{
    int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) { dpx(x0, y0); if (x0 == x1 && y0 == y1) break; int e2 = 2 * e; if (e2 >= dy) { e += dy; x0 += sx; } if (e2 <= dx) { e += dx; y0 += sy; } }
}
static void dsprite(int x, int y, const char *const *rows, int h) { for (int r = 0; r < h; r++) for (int c = 0; rows[r][c]; c++) if (rows[r][c] == '#') dpx(x + c, y + r); }
static const char *const body_s[] = { "..#########...", ".############.", "##############", "##############", ".#############", "..##########.." };
static const char *const head_s[] = { ".####......", "######.....", "#######....", "###########", "###########", ".########..", "..####....." };
static const char *const head_up[] = { "......##...", ".....####..", "....######.", "..#######..", ".#######...", "#######....", ".#####....." };   // muzzle to the sky
static const char *const ear_up[] = { "#..", "##.", "###", "###" };

static void dog(int x, int y, int pose, float t, bool twitch, bool flip, int k)
{
    dog_x = x; dog_y = y; dog_flip = flip; dog_k = k;
    int by = pose == DOG_COWER ? 7 : pose == DOG_SLEEP ? 9 : 6, hx = 15, hy = 1;
    switch (pose) {
    case DOG_SNIFF: hx = 16; hy = 7; break;
    case DOG_ALERT: hy = 0; break;
    case DOG_LOOK:  hx = 14; hy = -1; break;
    case DOG_POINT: hx = 16; hy = 2; break;
    case DOG_COWER: hy = 5; break;
    case DOG_SLEEP: hx = 16; hy = 9; break;
    }
    float wag = sinf(wag_ph);
    if (pose == DOG_POINT) dline(4, by + 1, -1, by);
    else if (pose == DOG_COWER) dline(4, by + 2, 1, by + 6);
    else if (pose == DOG_SLEEP) { dline(4, by + 3, 1, by + 4); dline(1, by + 4, 2, by + 6); dline(2, by + 6, 6, by + 6); }
    else dline(4, by + 1, (int)lroundf(-1.0f + 1.5f * wag), by - 4 + (int)lroundf(1.5f * wag));
    if (pose == DOG_SLEEP) dline(15, 15, 26, 15);
    else {
        int ly = by + 5, sw = pose == DOG_WALK ? ((int)(t * 8.0f) & 1 ? 1 : -1) : 0;
        dline(5, ly, 5 - sw, 15); dline(7, ly, 7 + sw, 15);
        if (pose == DOG_POINT) { dline(14, ly, 14, 15); dline(16, ly, 18, ly + 2); }
        else { dline(14, ly, 14 + sw, 15); dline(16, ly, 16 - sw, 15); }
    }
    dsprite(4, by, body_s, 6);
    if (pose == DOG_LOOK) {
        dsprite(hx, hy, head_up, 7);
        dsprite(hx + 1, hy - 3, ear_up, 3);
        dclr(hx + 5, hy + 3);                                      // the eye, looking up
        if (twitch) dpx(hx + 9, hy - 1);
    } else {
        dsprite(hx, hy, head_s, 7);
        if (pose == DOG_ALERT || pose == DOG_POINT) { dsprite(hx + 1, hy - 4, ear_up, 4); dpx(hx + 1, hy - 5); }
        else if (pose == DOG_COWER || pose == DOG_SLEEP) { dclr(hx + 2, hy + 1); dclr(hx + 2, hy + 2); dclr(hx + 1, hy + 3); dpx(hx - 1, hy + 2); dpx(hx - 1, hy + 3); }
        else dsprite(hx + 1, hy - 3, ear_up, 3);
        if (pose == DOG_SLEEP) { dclr(hx + 4, hy + 2); dclr(hx + 5, hy + 2); } else dclr(hx + 4, hy + 2);
        dclr(hx + 7, hy + 4); dclr(hx + 8, hy + 4);
        if (twitch) { dpx(hx + 11, hy + 3); dpx(hx + 11, hy + 2); } else dpx(hx + 11, hy + 3);
    }
    dog_k = 1;
}

// ================================================================ the birds (CHIRP's)
static const int wire_y0[3] = { 24, 31, 38 };
static int wire_at(int w, int x) { float u = ((float)x - 64.0f) / 64.0f; return wire_y0[w] + (int)lroundf(2.0f * (1.0f - u * u)); }
static int slot_x(int w, int s) { return 9 + s * 14 + w * 4; }
static int bird_dir(uint32_t who) { return (who >> 9) & 1 ? 1 : -1; }
static const char *const spr_small[] = { "....##..", "...####.", "#..####>", ".######.", "..####..", "...#.#.." };
static const char *const spr_med[] = { ".......##...", "......####..", "......#.##>.", "##...#####..", ".#########..", "..#######...", "...#####....", "....#..#...." };
static const char *const spr_up[] = { "..#.......#.", "..##.....##.", "...##...##..", "....##.##...", ".....####>..", "#..#######..", ".#######....", "..####......" };
static const char *const spr_down[] = { "............", "............", ".....###....", "....#####>..", "#..#######..", ".#######....", "..##..##....", ".##....##..." };
static const char *const spr_sleep[] = { "......###...", ".....#####..", "##..#######.", ".#.########.", "..#########.", "...######...", "....#..#...." };

static void sprite(int x, int y, const char *const *rows, int h, int dir, bool open, int k)
{
    int w = (int)strlen(rows[0]);
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++) {
            char ch = rows[r][c];
            if (ch != '#' && ch != '>') continue;
            int x0 = x + (dir > 0 ? c : w - 1 - c) * k;
            if (ch == '#' || !open) blk(x0, y + r * k, k, k);
            else { blk(x0, y + (r - 1) * k, k, k); blk(x0, y + (r + 1) * k, k, k); }
        }
}
static void draw_bird(int cx, int fy, bool big, int dir, bool open, float wing, bool flying, bool sleeping, int k)
{
    if (sleeping) { sprite(cx - 6 * k, fy - 7 * k, spr_sleep, 7, dir, false, k); return; }
    if (flying) { sprite(cx - 6 * k, fy - 8 * k, wing > 0 ? spr_up : spr_down, 8, dir, false, k); return; }
    if (big) sprite(cx - 6 * k, fy - 8 * k, spr_med, 8, dir, open, k);
    else sprite(cx - 4 * k, fy - 6 * k, spr_small, 6, dir, open, k);
}

static void bird_pos(int i, int *x, int *fy, bool *flying, float *wing)
{
    const bird_t *b = &E.birds[i];
    int px_ = slot_x(b->wire, b->slot), py = wire_at(b->wire, px_);
    *x = px_; *fy = py; *flying = false; *wing = 0;
    if (b->arrive < 1.0f) {
        float e = ease_out(b->arrive);
        int sx = bird_dir(b->who) > 0 ? -14 : OLED_W + 14, sy = 25;
        *x = sx + (int)lroundf((float)(px_ - sx) * e);
        *fy = sy + (int)lroundf((float)(py - sy) * e - 6.0f * sinf(e * 3.14159f));
        if (*fy < 25) *fy = 25;
        *flying = b->arrive < 0.97f; *wing = sinf(b->arrive * 40.0f);
    } else if (b->leaving) {
        float e = b->leave * b->leave;
        *x = px_ + (int)lroundf((float)bird_dir(b->who) * 70.0f * e);
        *fy = py - (int)lroundf(30.0f * e);
        if (*fy < 25) *fy = 25;                                    // flying birds stay below the header band
        *flying = true; *wing = sinf(b->leave * 45.0f);
    }
}

typedef struct { float x, y, vx, vy, life; uint8_t big; } note_t;
static note_t notes[20];
static int nnotes;
static void add_note(float x, float y, float vx, bool big)
{
    if (nnotes >= 20) { memmove(notes, notes + 1, sizeof notes[0] * 19); nnotes--; }
    notes[nnotes++] = (note_t){ x, y, vx, -12.0f - 8.0f * (float)(rng_next(&seed) % 100) / 100.0f, 1.0f, big };
}
static void notes_step_draw(void)
{
    int j = 0;
    for (int i = 0; i < nnotes; i++) {
        note_t n = notes[i];
        n.life -= DT * 1.1f; n.x += n.vx * DT + 0.25f * sinf(n.life * 9.0f); n.y += n.vy * DT; n.vy *= 0.97f;
        if (n.life <= 0 || n.y < 17.0f) continue;
        if (n.life > 0.25f || ((int)(n.life * 40.0f) & 1)) {
            int x = (int)n.x, y = (int)n.y, b = n.big;
            oled_rect(x, y, 2 + b, 2, true); oled_line(x + 1 + b, y - 1, x + 1 + b, y - 3 - b, true); oled_pixel(x + 2 + b, y - 3 - b, true);
        }
        notes[j++] = n;
    }
    nnotes = j;
}

static bool splash_on;
void scene_call(int i)
{
    if (splash_on || E.mode == MODE_HUNT || E.flock == FL_SWARM) return;
    int x, fy; bool fl; float w;
    bird_pos(i, &x, &fy, &fl, &w);
    const bird_t *b = &E.birds[i];
    int dir = bird_dir(b->who);
    bool big = b->near > 0.5f;
    add_note((float)(x + dir * (big ? 5 : 3)), (float)(fy - (big ? 8 : 6)), (float)dir * (4.0f + 6.0f * b->near), b->near > 0.6f);
}

static float twang(int w, int x)
{
    float d = 0;
    for (int i = 0; i < MAXB; i++) {
        const bird_t *b = &E.birds[i];
        if (!b->alive || b->leaving || b->arrive < 1.0f || b->wire != w || b->call_t > 0.5f) continue;
        int dx = x - slot_x(b->wire, b->slot);
        if (dx < -20 || dx > 20) continue;
        d += (0.6f + 1.0f * b->near) * (1.0f - (float)abs(dx) / 20.0f) * expf(-b->call_t * 7.0f) * cosf(b->call_t * 70.0f);
    }
    return d;
}

static void sky(void)
{
    dim_on = sc.bt < 0.35f || !E.bt_ok;
    for (int w = 0; w < 3; w++) for (int x = 2; x < OLED_W - 2; x++) px(x, wire_at(w, x) + (int)lroundf(twang(w, x)));
    oled_line(1, 20, 1, 42, true); oled_line(126, 20, 126, 42, true);
    for (int w = 0; w < 3; w++) { oled_rect(0, wire_y0[w] - 1, 3, 2, true); oled_rect(125, wire_y0[w] - 1, 3, 2, true); }
    for (int i = 0; i < MAXB; i++) {
        const bird_t *b = &E.birds[i];
        if (!b->alive) continue;
        int x, fy; bool fl; float w;
        bird_pos(i, &x, &fy, &fl, &w);
        if (!fl) fy += (int)lroundf(twang(b->wire, x));
        bool open = b->call_t < 0.12f;
        draw_bird(x, fy - (b->call_t < 0.08f && !fl ? 1 : 0), b->near > 0.5f, bird_dir(b->who), open, w, fl, false, 1);
        if (!fl && E.flock == FL_CLOCKS && b->interval > 0) {     // CLOCKS: the pulse winding up over its head
            int h = (int)lroundf(b->phase * 3.0f), top = fy - (b->near > 0.5f ? 10 : 8);
            if (h > 0) oled_line(x, top, x, top - h + 1, true);
        }
    }
    dim_on = false;
}

// ================================================================ SWARM: a murmuration in the sky
#define NSW 48
static struct { float x, y, vx, vy; } sw[NSW];
static int sw_n; static float sw_t, sw_want;
static void swarm(void)
{
    sw_t += DT;
    sw_want += 0.08f * (fminf((float)NSW, 6.0f + (float)E.nbirds * 2.0f) - sw_want);
    int want = (int)sw_want;
    while (sw_n < want) { sw[sw_n].x = (float)(rng_next(&seed) % 120) + 4; sw[sw_n].y = 40; sw[sw_n].vx = 0; sw[sw_n].vy = -20; sw_n++; }
    if (sw_n > want) sw_n--;
    float ax = 64.0f + 40.0f * sinf(sw_t * 0.41f), ay = 28.0f + 6.0f * sinf(sw_t * 0.73f + 1.0f);
    dim_on = sc.bt < 0.35f;
    for (int i = 0; i < sw_n; i++) {
        float dx = ax - sw[i].x, dy = ay - sw[i].y;
        sw[i].vx += (dx * 2.2f + ((float)(rng_next(&seed) % 200) - 100.0f) * 1.2f) * DT;
        sw[i].vy += (dy * 2.2f + ((float)(rng_next(&seed) % 200) - 100.0f) * 0.8f) * DT;
        sw[i].vx *= 0.94f; sw[i].vy *= 0.94f;
        sw[i].x += sw[i].vx * DT; sw[i].y += sw[i].vy * DT;
        if (sw[i].y < 18.0f) { sw[i].y = 18.0f; sw[i].vy = fabsf(sw[i].vy) * 0.5f; }
        if (sw[i].y > 40.0f) { sw[i].y = 40.0f; sw[i].vy = -fabsf(sw[i].vy) * 0.5f; }
        int x = (int)sw[i].x, y = (int)sw[i].y, f = ((int)(sw_t * 10.0f) + i) % 3, wy = f == 0 ? -1 : f == 1 ? 0 : 1;
        px(x, y); px(x - 1, y + wy); px(x + 1, y + wy);
    }
    dim_on = false;
}

// ================================================================ the ground
#define GROUND 61
static int reed_x(uint32_t who) { return 30 + (int)((who >> 4) % 56u); }
static int ground_at(int x, float t)
{
    float u = (float)x / (float)(OLED_W - 1) * (float)(CSI_BANDS - 1);
    int b = (int)u; if (b > CSI_BANDS - 2) b = CSI_BANDS - 2;
    float f = u - (float)b, v = E.room_live ? E.shape[b] * (1.0f - f) + E.shape[b + 1] * f : 1.0f;
    float rip = E.motion * 2.0f * sinf(t * 7.0f + u * 1.1f);
    return GROUND - (int)clampf((v - 0.7f) * 4.0f + rip + 1.0f, 0.0f, 4.0f);
}
static void ground(float t)
{
    dim_on = sc.wifi < 0.35f || !E.wifi_ok;
    for (int x = 0; x < OLED_W; x++) { int g = ground_at(x, t); px(x, g); if (((x + g) & 1) == 0) for (int y = g + 2; y < OLED_H; y += 2) px(x, y); }
    for (int i = 0; i < NDEV; i++) {
        const wdev_t *d = &E.dev[i];
        if (!d->who) continue;
        int x = reed_x(d->who), g = ground_at(x, t);
        float near = clampf((d->rssi + 95.0f) / 55.0f, 0, 1);
        int h = 2 + (int)(near * 9.0f);
        float sway = E.motion * 1.5f * sinf(t * 3.0f + (float)x);
        int tx = x + (int)lroundf(sway);
        for (int k = 0; k < h; k++) px(x + (int)lroundf(sway * (float)k / (float)h), g - k);
        if (d->ap) { px(tx - 1, g - h); px(tx + 1, g - h); px(tx, g - h - 1); }
        if (d->pulse > 0.4f) { px(tx - 1, g - h - 1); px(tx + 1, g - h - 1); px(tx, g - h - 2); }
        if (d->pulse > 0.75f) oled_circle(tx, g - h, 2 + (int)((1.0f - d->pulse) * 8.0f), true);
    }
    dim_on = false;
}

void scene_heard(int i)
{
    if (scents.n > 30) return;
    float t = 0;
    const wdev_t *d = &E.dev[i];
    int x = reed_x(d->who), g = ground_at(x, t), h = 2 + (int)(clampf((d->rssi + 95.0f) / 55.0f, 0, 1) * 9.0f);
    float life = 0.5f + 0.3f * rng_uni(&seed), nx = 93.0f, ny = 46.0f;      // the dog's nose (it faces left)
    parts_emit(&scents, (float)x, (float)(g - h), (nx - (float)x) / life, (ny - (float)(g - h)) / life - 8.0f, life, PART_DOT, 1);
}

static void storm_fx(void)
{
    int n = (int)(E.storm * E.storm * 140.0f);
    for (int i = 0; i < n; i++) { uint32_t r = rng_next(&seed); int x = (int)(r % 128u), y = 16 + (int)((r >> 8) % 48u); oled_pixel(x, y, !oled_get(x, y)); }
    if (E.thunder > 0.5f) {
        int x = 20 + (int)(rng_next(&seed) % 70u), y = 16;
        while (y < GROUND - 2) { int nx = x + (int)(rng_next(&seed) % 9u) - 4, ny = y + 3 + (int)(rng_next(&seed) % 4u); oled_line(x, y, nx, ny, true); x = nx; y = ny; }
        if (E.thunder > 0.85f) oled_invert(0, 16, OLED_W, 48);
    }
}

// ================================================================ HUNT
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
    oled_rect(11, 57, 4, 1, true);
    oled_rect(57, 57, 4, 1, true); oled_rect(58, 56, 2, 3, true);
    static float needle;
    needle += 0.25f * (E.prox - needle);
    float na = (180.0f + needle * 180.0f) * 0.0174533f;
    oled_line(cx, cy, cx + (int)lroundf(27.0f * cosf(na)), cy + (int)lroundf(27.0f * sinf(na)), true);
    oled_line(cx + 1, cy, cx + 1 + (int)lroundf(27.0f * cosf(na)), cy + (int)lroundf(27.0f * sinf(na)), true);
    oled_disc(cx, cy, 3, true);
    if (snd_beeps != beeps_seen) { beeps_seen = snd_beeps; beep_ring = 1.0f; }
    if (beep_ring > 0.05f) { oled_circle(cx, cy, 4 + (int)((1.0f - beep_ring) * 14.0f), true); beep_ring *= 0.7f; }
    char b[24];
    if (E.target < 0 || E.lost) {
        oled_ftext_r(F_MED, 127, 21, E.target < 0 ? "NOBODY" : "LOST", 1);
        oled_ftext_r(F_TINY, 127, 33, E.target < 0 ? "WAITING FOR ONE" : "NOT HEARD FOR 5 S", 1);
        return;
    }
    snprintf(b, sizeof b, "%d", (int)lroundf(E.target_rssi));
    oled_ftext_r(F_BIG, 127, 17, b, 1);
    snprintf(b, sizeof b, "DBM  %d OF %d", E.target_rank, E.target_of);
    oled_ftext_r(F_TINY, 127, 37, b, 1);
    if (v->cover) return;
    const char *tr = E.trend > 0 ? "WARMER" : E.trend < 0 ? "COLDER" : "STEADY";
    ui_pill_r(127, 45, tr, E.trend > 0);
    if (E.tw == W_WIFI) snprintf(b, sizeof b, "WI-FI %s", E.dev[E.target].ap ? "ROUTER" : "DEVICE");
    else snprintf(b, sizeof b, "BT %s", cat_name[E.birds[E.target].cat]);
    oled_ftext_r(F_TINY, 127, 57, b, 1);
    if (E.tw == W_BT) sprite(74, 49, spr_small, 6, 1, snd_beeps & 1, 1);            // a little bird or a little antenna
    else { oled_line(77, 49, 77, 56, true); oled_line(75, 50, 77, 49, true); oled_line(79, 50, 77, 49, true); ui_arc(77, 50, 3, 220, 320); }
}

// ================================================================ play
static int dog_pose(void)
{
    if (!E.wifi_ok && !E.bt_ok) return DOG_SLEEP;
    if (E.rate < 0.5f && E.nbirds == 0) return DOG_SLEEP;
    if (E.thunder > 0.2f || E.storm > 0.6f) return DOG_COWER;
    if (E.look > 0.5f) return DOG_LOOK;
    if (E.motion > 0.35f) return DOG_ALERT;
    if (E.sniff > 0.55f) return DOG_SNIFF;
    return DOG_STAND;
}

void scene_draw(const view_t *v)
{
    float t = v->t;
    wag_ph += DT * (3.0f + fminf(E.rate + E.adverts, 300.0f) * 0.06f);
    loop_state_t ls = looper_state();
    bool hunt = E.mode == MODE_HUNT;
    ui_head(hunt && ls == LOOP_EMPTY ? "HUNT" : NULL, hunt && ls == LOOP_EMPTY);
    if (hunt) { hunt_scene(v); storm_fx(); return; }

    if (E.flock == FL_SWARM) { dim_on = sc.bt < 0.35f; for (int w = 0; w < 3; w++) for (int x = 2; x < OLED_W - 2; x += 2) px(x, wire_at(w, x)); dim_on = false; swarm(); }
    else sky();
    notes_step_draw();
    ground(t);
    int pose = dog_pose();
    float shake = pose == DOG_COWER && E.thunder > 0.1f ? (float)((v->frame & 1) ? 1 : -1) : 0;
    dim_on = sc.wifi < 0.35f && sc.bt < 0.35f;
    dog(92 + (int)shake, GROUND - 16, pose, t, E.sniff > 0.3f && (v->frame & 1) == 0, true, 1);
    dim_on = false;
    if (pose == DOG_SLEEP) {
        float zt = fmodf(t, 2.4f);
        oled_ftext_ex(F_TINY, 92 - (int)(zt * 3.0f), 46 - (int)(zt * 5.0f), "Z", 1);
    }
    parts_step(&scents, DT, 20.0f, 0.5f);
    parts_draw(&scents);
    // the numbers, small, bottom left
    char b[20];
    if (!E.wifi_ok) snprintf(b, sizeof b, "NO WI-FI");
    else if (E.scanning) snprintf(b, sizeof b, "SCAN %d", E.chan);
    else snprintf(b, sizeof b, "CH %d", E.chan);
    oled_rect(0, 44, 27, 13, false);
    oled_ftext_ex(F_TINY, 1, 45, b, 1);
    snprintf(b, sizeof b, "%d/S", (int)(E.rate + 0.5f));
    oled_ftext_ex(F_TINY, 1, 51, b, 1);
    if (!E.bt_ok) { int w = oled_ftext_w(F_TINY, "BLUETOOTH OFF", 1) + 6; oled_rect(64 - w / 2, 26, w, 8, false); oled_ftext_c(F_TINY, 64, 28, "BLUETOOTH OFF", 1); }
    if (E.bt_ok && E.nbirds == 0 && E.flock != FL_SWARM && t > 4.0f) { int w = oled_ftext_w(F_TINY, "NO BIRDS YET", 1) + 6; oled_rect(64 - w / 2, 26, w, 8, false); oled_ftext_c(F_TINY, 64, 28, "NO BIRDS YET", 1); }
    storm_fx();
    // BALANCE, while it moves
    if (E.bal_shown > 0) {
        float bal = clampf(sc.bt - sc.wifi, -1, 1);
        oled_rect(0, 16, OLED_W, 8, false);
        oled_ftext_ex(F_TINY, 2, 17, "WI-FI", 1);
        oled_ftext_r(F_TINY, OLED_W - 2, 17, "BT", 1);
        oled_hline_pat(26, 19, 86, 0x55, true);
        int k = 69 + (int)lroundf(bal * 42.0f);
        oled_rrect(k - 2, 16, 5, 7, 1, true);
    }
}

// ================================================================ splash and screensaver
bool scene_splash(int f)
{
    // the dog trots in on the ground, three birds land on the wire one after another, the title
    float t = (float)f / 25.0f;
    splash_on = f < 95;
    if (f == 0) nnotes = 0;
    wag_ph += 0.3f;
    for (int x = 0; x < OLED_W; x++) { oled_pixel(x, GROUND, true); if ((x & 1) == 0) oled_pixel(x, GROUND + 2, true); }
    for (int x = 0; x < (int)(ease_in_out((float)f / 16.0f) * (float)OLED_W); x++) { float u = ((float)x - 64.0f) / 64.0f; oled_pixel(x, 31 + (int)lroundf(2.0f * (1.0f - u * u)), true); }
    int x = f < 34 ? OLED_W + 2 - (int)(ease_out((float)f / 34.0f) * 38.0f) : OLED_W - 36;
    int pose = f < 34 ? DOG_WALK : f < 50 ? DOG_SNIFF : DOG_LOOK;
    dog(x, GROUND - 16, pose, t, f >= 34 && f < 50 && (f & 2), true, 1);
    static const int bx[3] = { 22, 46, 70 };
    for (int k = 0; k < 3; k++) {
        float st = 18.0f + (float)k * 9.0f, p = ((float)f - st) / 12.0f;
        if (p <= 0) continue;
        float u = ((float)bx[k] - 64.0f) / 64.0f;
        int py = 31 + (int)lroundf(2.0f * (1.0f - u * u));
        if (p < 1.0f) {
            float e = ease_out(p);
            int x0 = -12, x1 = x0 + (int)lroundf((float)(bx[k] - x0) * e), y1 = 25 + (int)lroundf((float)(py - 25) * e - 3.0f * sinf(e * 3.14159f));
            draw_bird(x1, y1, k == 1, 1, false, sinf((float)f * 1.6f + (float)k), true, false, 1);
        } else {
            draw_bird(bx[k], py, k == 1, 1, p < 1.5f, 0, false, false, 1);
            if (p < 1.0f + 1.0f / 12.0f) add_note((float)(bx[k] + 5), (float)(py - 8), 6.0f, k == 1);
        }
    }
    notes_step_draw();
    if (f >= 58) {
        float e = ease_back((float)(f - 58) / 14.0f);
        oled_ftext_c(F_MED, 64, -12 + (int)(e * 15.0f), "ETHER", 3);
    }
    if (f >= 70) oled_ftext_c(F_TINY, 64, 17, INST.tagline, 1);
    if (f >= 94) nnotes = 0;
    return f < 96;
}

void scene_saver(uint32_t f)
{
    int place = (int)((f / 1500) % 4);                      // they move every minute (burn-in)
    int y = 30 + place * 4, x = 18 + place * 14;
    for (int k = -16; k <= 16; k++) oled_pixel(x + k, y + (k * k) / 110, true);
    draw_bird(x, y, true, 1, false, 0, false, true, 1);
    for (int k = -4; k <= 34; k++) oled_pixel(x + 26 + k, 60, true);
    dog(x + 30, 45, DOG_SLEEP, 0, false, true, 1);
    float ph = fmodf((float)f / 75.0f, 1.0f);
    if (ph < 0.9f) oled_ftext_ex(F_TINY, x + 6 + (int)(ph * 8.0f), y - 12 - (int)(ph * 12.0f), "Z", 0);
}
