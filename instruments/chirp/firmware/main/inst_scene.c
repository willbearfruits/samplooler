// CHIRP's screens. Birds sit on three wires (BLE advertises on three channels); a bird is as big as it is
// near, opens its beak and lets a note go when it calls, flies in when it is first heard and off when it
// falls silent. CLOCKS shows each bird's pulse winding up over its head; SWARM lifts them all into a
// murmuration that thickens with the traffic; HUNT is one big bird and a radar of how near it is.
#include "inst_chirp.h"
#include "dsp.h"
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define DT (1.0f / 25.0f)
static const int wire_y[3] = { 27, 38, 49 };

static int wire_at(int w, int x)
{
    float u = ((float)x - 64.0f) / 64.0f;
    return wire_y[w] + (int)lroundf(2.0f * (1.0f - u * u));       // a little sag in the middle
}
static int slot_x(int w, int s) { return 9 + s * 14 + w * 4; }
static int bird_size(float near) { return 1 + (int)(clampf(near, 0.0f, 1.0f) * 2.99f); }
static int bird_dir(uint32_t who) { return (who >> 9) & 1 ? 1 : -1; }

// ---------------------------------------------------------------- one bird
// Sprites facing right: '#' a pixel, '>' the beak (drawn open as two pixels, one up and one down).
static const char *const spr_small[] = { "....##..", "...####.", "#..####>", ".######.", "..####..", "...#.#.." };
static const char *const spr_med[] = { ".......##...", "......####..", "......#.##>.", "##...#####..", ".#########..", "..#######...", "...#####....", "....#..#...." };
static const char *const spr_large[] = {
    "..........###...", ".........#####..", "........###.##>>", "........######..", "##.....#######..", ".###.#########..",
    "..############..", "...##########...", "....########....", ".....######.....", "......#...#.....",
};
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
            int px = x + (dir > 0 ? c : w - 1 - c) * k;
            if (ch == '#' || !open) oled_rect(px, y + r * k, k, k, true);
            else { oled_rect(px, y + (r - 1) * k, k, k, true); oled_rect(px, y + (r + 1) * k, k, k, true); }
        }
}

// feet at (cx, fy). size 1..3 on the wires, 4 and up: the big one (twice the large sprite).
static void draw_bird(int cx, int fy, int s, int dir, bool beak_open, float wing, bool flying, bool sleeping)
{
    if (sleeping) { sprite(cx - 6, fy - 7, spr_sleep, 7, dir, false, 1); return; }
    if (flying) { sprite(cx - 6, fy - 8, wing > 0 ? spr_up : spr_down, 8, dir, false, 1); return; }
    switch (s) {
    case 1: sprite(cx - 4, fy - 6, spr_small, 6, dir, beak_open, 1); break;
    case 2: sprite(cx - 6, fy - 8, spr_med, 8, dir, beak_open, 1); break;
    case 3: sprite(cx - 8, fy - 11, spr_large, 11, dir, beak_open, 1); break;
    default: sprite(cx - 16, fy - 22, spr_large, 11, dir, beak_open, 2); break;
    }
}
static int bird_h(int s) { return s <= 1 ? 6 : s == 2 ? 8 : 11; }

// where bird i sits (or flies) right now
static void bird_pos(int i, int *x, int *fy, bool *flying, float *wing)
{
    const bird_t *b = &birds[i];
    int px = slot_x(b->wire, b->slot), py = wire_at(b->wire, px);
    *x = px; *fy = py; *flying = false; *wing = 0;
    if (b->arrive < 1.0f) {
        float e = ease_out(b->arrive);
        int sx = bird_dir(b->who) > 0 ? -14 : OLED_W + 14, sy = 26;
        *x = sx + (int)lroundf((float)(px - sx) * e);
        *fy = sy + (int)lroundf((float)(py - sy) * e - 10.0f * sinf(e * 3.14159f));
        *flying = b->arrive < 0.97f; *wing = sinf(b->arrive * 40.0f);
    } else if (b->leaving) {
        float e = b->leave * b->leave;
        *x = px + (int)lroundf((float)bird_dir(b->who) * 70.0f * e);
        *fy = py - (int)lroundf(50.0f * e);
        *flying = true; *wing = sinf(b->leave * 45.0f);
    }
}

// ---------------------------------------------------------------- notes in the air
typedef struct { float x, y, vx, vy, life; uint8_t big; } note_t;
static note_t notes[24];
static int nnotes;
static void add_note(float x, float y, float vx, bool big)
{
    if (nnotes >= 24) { memmove(notes, notes + 1, sizeof notes[0] * 23); nnotes--; }
    notes[nnotes++] = (note_t){ x, y, vx, -14.0f - 10.0f * (float)(chirp_rand() % 100) / 100.0f, 1.0f, big };
}
static void draw_note(int x, int y, bool big)
{
    oled_rect(x, y, 2 + big, 2, true);
    oled_line(x + 1 + big, y - 1, x + 1 + big, y - 4 - big, true);
    oled_pixel(x + 2 + big, y - 4 - big, true);
    if (big) oled_pixel(x + 4, y - 3, true);
}
static void notes_step_draw(int ymin)
{
    int j = 0;
    for (int i = 0; i < nnotes; i++) {
        note_t n = notes[i];
        n.life -= DT * 0.9f;
        n.x += n.vx * DT + 0.25f * sinf(n.life * 9.0f);
        n.y += n.vy * DT;
        n.vy *= 0.97f;
        if (n.life <= 0 || n.y < (float)ymin) continue;
        if (n.life > 0.25f || ((int)(n.life * 40.0f) & 1)) draw_note((int)n.x, (int)n.y, n.big);
        notes[j++] = n;
    }
    nnotes = j;
}

static bool splash_on;
void scene_note(int i, float near)
{
    if (mode == MODE_SWARM || splash_on) return;
    int x, fy; bool fl; float w;
    bird_pos(i, &x, &fy, &fl, &w);
    if (mode == MODE_HUNT) return;                               // HUNT draws its own rings
    int s = bird_size(near), dir = bird_dir(birds[i].who);
    float hx = (float)(x + dir * (3 + 2 * s)), hy = (float)(fy - bird_h(s) + 1);
    add_note(hx, hy, (float)dir * (4.0f + 6.0f * near), near > 0.6f);
}

// ---------------------------------------------------------------- the wires
// A bird that sings tugs its wire: a short, damped wobble around where it sits.
static float twang(int w, int x)
{
    float d = 0;
    for (int i = 0; i < MAXB; i++) {
        const bird_t *b = &birds[i];
        if (!b->alive || b->leaving || b->arrive < 1.0f || b->wire != w || b->call_t > 0.5f) continue;
        int dx = x - slot_x(b->wire, b->slot);
        if (dx < -24 || dx > 24) continue;
        float shape = 1.0f - (float)(dx < 0 ? -dx : dx) / 24.0f;
        d += (0.8f + 1.4f * b->near) * shape * expf(-b->call_t * 7.0f) * cosf(b->call_t * 70.0f);
    }
    return d;
}
static void wires(int upto_x)
{
    for (int w = 0; w < 3; w++)
        for (int x = 1; x < upto_x && x < OLED_W - 1; x++) oled_pixel(x, wire_at(w, x) + (int)lroundf(twang(w, x)), true);
    oled_line(1, 22, 1, 56, true); oled_line(126, 22, 126, 56, true);           // the poles
    for (int w = 0; w < 3; w++) { oled_rect(0, wire_y[w] - 1, 3, 2, true); oled_rect(125, wire_y[w] - 1, 3, 2, true); }
}

static void perched(bool clocks_on)
{
    for (int i = 0; i < MAXB; i++) {
        const bird_t *b = &birds[i];
        if (!b->alive) continue;
        int x, fy; bool fl; float w;
        bird_pos(i, &x, &fy, &fl, &w);
        int s = bird_size(b->near), dir = bird_dir(b->who);
        bool open = b->call_t < 0.12f;
        int bob = b->call_t < 0.08f && !fl ? 1 : 0;
        if (!fl) fy += (int)lroundf(twang(b->wire, x));
        draw_bird(x, fy - bob, s, dir, open, w, fl, false);
        if (fl) continue;
        if (i == spot) {                                         // the spotlight: a little arrow overhead, bobbing
            int ty = fy - bird_h(s) - 5;
            oled_line(x - 2, ty, x + 2, ty, true); oled_line(x - 1, ty + 1, x + 1, ty + 1, true); oled_pixel(x, ty + 2, true);
        }
        if (clocks_on && b->interval > 0) {                      // CLOCKS: the pulse winding up over its head
            int h = (int)lroundf(b->phase * 4.0f);
            int top = fy - bird_h(s) - 2;
            if (h > 0) oled_line(x - dir * 1, top, x - dir * 1, top - h + 1, true);
            if (b->call_t < 0.1f) { oled_pixel(x - 3, top - 2, true); oled_pixel(x + 1, top - 2, true); }
        }
    }
}

// ---------------------------------------------------------------- SWARM: a murmuration
#define NSW 64
static struct { float x, y, vx, vy; } sw[NSW];
static int sw_n;
static float sw_t, sw_want;
static void swarm(void)
{
    sw_t += DT;
    sw_want += 0.08f * (fminf((float)NSW, 6.0f + (float)bird_count * 2.0f + calls_per_s * 0.6f) - sw_want);
    int want = (int)sw_want;
    while (sw_n < want) { sw[sw_n].x = (float)(chirp_rand() % 120) + 4; sw[sw_n].y = 60; sw[sw_n].vx = 0; sw[sw_n].vy = -20; sw_n++; }
    if (sw_n > want) sw_n--;
    float ax = 64.0f + 40.0f * sinf(sw_t * 0.41f), ay = 34.0f + 9.0f * sinf(sw_t * 0.73f + 1.0f);
    for (int i = 0; i < sw_n; i++) {
        float dx = ax - sw[i].x, dy = ay - sw[i].y;
        sw[i].vx += (dx * 2.2f + ((float)(chirp_rand() % 200) - 100.0f) * 1.2f) * DT;
        sw[i].vy += (dy * 2.2f + ((float)(chirp_rand() % 200) - 100.0f) * 0.8f) * DT;
        for (int k = i + 1; k < sw_n && k < i + 12; k++) {        // keep a little apart from the nearest few
            float ex = sw[k].x - sw[i].x, ey = sw[k].y - sw[i].y, d2 = ex * ex + ey * ey;
            if (d2 < 20.0f && d2 > 0.01f) { sw[i].vx -= ex * 2.5f; sw[i].vy -= ey * 2.5f; sw[k].vx += ex * 2.5f; sw[k].vy += ey * 2.5f; }
        }
        sw[i].vx *= 0.94f; sw[i].vy *= 0.94f;                    // air drag keeps the flock together
        float sp = sqrtf(sw[i].vx * sw[i].vx + sw[i].vy * sw[i].vy);
        if (sp > 45.0f) { sw[i].vx *= 45.0f / sp; sw[i].vy *= 45.0f / sp; }
        sw[i].x += sw[i].vx * DT; sw[i].y += sw[i].vy * DT;
        if (sw[i].y < 19.0f) { sw[i].y = 19.0f; sw[i].vy = fabsf(sw[i].vy) * 0.5f; }
        if (sw[i].y > 50.0f) { sw[i].y = 50.0f; sw[i].vy = -fabsf(sw[i].vy) * 0.5f; }
        int x = (int)sw[i].x, y = (int)sw[i].y, f = ((int)(sw_t * 10.0f) + i) % 3;
        oled_pixel(x, y, true);                                  // a body and two wings, flapping
        int wy = f == 0 ? -1 : f == 1 ? 0 : 1;
        oled_pixel(x - 1, y + wy, true); oled_pixel(x + 1, y + wy, true);
        if (f == 0) { oled_pixel(x - 2, y - 1, true); oled_pixel(x + 2, y - 1, true); }
    }
}

// ---------------------------------------------------------------- HUNT: one bird and how near it is
static float rings_t[4];
static float hunt_last_call = 1;
static void hunt(void)
{
    int tg = -1; float best = -1;
    if (spot >= 0 && birds[spot].alive && !birds[spot].leaving) tg = spot;
    else for (int i = 0; i < MAXB; i++) if (birds[i].alive && !birds[i].leaving && birds[i].near > best) { best = birds[i].near; tg = i; }
    oled_line(6, 51, 50, 51, true); oled_line(44, 51, 52, 47, true);              // a branch
    if (tg < 0) {
        draw_bird(30, 51, 6, 1, false, 0, false, true);
        oled_ftext_c(F_SMALL, 92, 30, "NOTHING", 1);
        oled_ftext_c(F_SMALL, 92, 39, "TO FOLLOW", 1);
        return;
    }
    const bird_t *b = &birds[tg];
    bool open = b->call_t < 0.06f;
    if (b->call_t < hunt_last_call) for (int k = 0; k < 4; k++) if (rings_t[k] <= 0) { rings_t[k] = 1.0f; break; }
    hunt_last_call = b->call_t;
    draw_bird(34, 51, 5 + (int)(b->near * 2.99f), 1, open, 0, false, false);
    // the radar: four rings, lit as near as it is; pulses run out from the middle on each tick
    int cx = 96, cy = 34;
    int lit = (int)ceilf(b->near * 4.0f - 0.01f);
    for (int r = 1; r <= 4; r++) {
        int rr = 4 + r * 4;
        if (r <= lit) oled_circle(cx, cy, rr, true);
        else for (int a = 0; a < 360; a += 30) oled_pixel(cx + (int)lroundf((float)rr * cosf((float)a * 0.01745f)), cy + (int)lroundf((float)rr * sinf((float)a * 0.01745f)), true);
    }
    for (int k = 0; k < 4; k++) if (rings_t[k] > 0) {
        int rr = (int)((1.0f - rings_t[k]) * 20.0f) + 2;
        if (rings_t[k] > 0.3f) oled_circle(cx, cy, rr, true);
        rings_t[k] -= DT * 2.5f;
    }
    oled_disc(cx, cy, 2, true);
    char t[16]; snprintf(t, sizeof t, "%d", (int)lroundf(b->rssi));
    oled_ftext_ex(F_TINY, 3, 18, cat_name[b->cat], 1);
    int w = oled_ftext_w(F_SMALL, t, 1);
    oled_rect(cx - w / 2 - 2, cy + 6, w + 4, 8, false);
    oled_ftext_c(F_SMALL, cx, cy + 7, t, 1);
    const char *tr = hunt_trend > 2.0f ? S_UP " NEARER" : hunt_trend < -2.0f ? S_DOWN " FARTHER" : "";
    if (*tr) oled_ftext_ex(F_TINY, 3, 24, tr, 1);
}

// ---------------------------------------------------------------- nobody here yet
static void listening(float t)
{
    int cx = 64, cy = 33;
    oled_disc(cx, cy + 4, 1, true);
    for (int k = 0; k < 3; k++) {
        float ph = fmodf(t * 0.8f + (float)k / 3.0f, 1.0f);
        int r = 3 + (int)(ph * 12.0f);
        if (ph < 0.85f) { ui_arc(cx, cy + 4, r, 220, 320); }
    }
    const char *l = radio_failed ? "BLUETOOTH WOULD NOT START" : t < 8.0f ? "LISTENING FOR DEVICES" : "NO DEVICES NEAR YET";
    oled_ftext_c(F_SMALL, 64, 43, l, 1);
}

// ---------------------------------------------------------------- the play screen
void scene_draw(const view_t *v)
{
    ui_head(NULL, false);
    bool any = false;
    for (int i = 0; i < MAXB; i++) if (birds[i].alive) any = true;
    if (mode == MODE_HUNT) hunt();
    else if (mode == MODE_SWARM) { wires(OLED_W); swarm(); }
    else {
        wires(OLED_W);
        if (!any) listening(v->t);
        perched(mode == MODE_CLOCKS);
    }
    notes_step_draw(16);
    if (v->cover) return;
    // the strip: how many birds, how busy, the mode
    char b[24];
    oled_rect(0, 53, OLED_W, 11, false);
    snprintf(b, sizeof b, "%d BIRD%s", bird_count, bird_count == 1 ? "" : "S");
    int x = oled_ftext_ex(F_SMALL, 2, 56, b, 1);
    snprintf(b, sizeof b, "%d/S", (int)lroundf(calls_per_s));
    oled_ftext_ex(F_TINY, x + 5, 57, b, 1);
    static const char *const ml[4] = { "CHORUS", "CLOCKS", "SWARM", "HUNT" };
    ui_pill_r(OLED_W - 1, 54, ml[mode & 3], mode != MODE_CHORUS);
}

// ---------------------------------------------------------------- splash: dawn, the birds land, the title
bool scene_splash(int f)
{
    static const uint8_t stars[10][2] = { { 8, 20 }, { 25, 33 }, { 41, 18 }, { 58, 27 }, { 77, 21 }, { 93, 30 }, { 110, 19 }, { 120, 34 }, { 15, 44 }, { 68, 40 } };
    float t = (float)f;
    splash_on = f < 92;
    if (f == 0) nnotes = 0;
    for (int i = 0; i < 10; i++) if (f < 30 + i * 2 && ((f / 3 + i) % 5)) oled_pixel(stars[i][0], stars[i][1], true);
    // the sun comes up behind the wire
    float rise = ease_out(t / 45.0f);
    int sx = 114, sy = 68 - (int)lroundf(rise * 12.0f);
    oled_disc(sx, sy, 6, true);
    for (int k = 0; k < 8; k++) {
        float a = (float)k * 0.785f + t * 0.03f;
        int r0 = 9, r1 = 11 + (int)(rise * 2.0f);
        oled_line(sx + (int)lroundf((float)r0 * cosf(a)), sy + (int)lroundf((float)r0 * sinf(a)), sx + (int)lroundf((float)r1 * cosf(a)), sy + (int)lroundf((float)r1 * sinf(a)), true);
    }
    oled_rect(0, 60, OLED_W, 4, false);
    oled_hline_pat(0, 60, OLED_W, 0x11, true);
    // the wire, drawn across
    int wx = (int)(ease_in_out((t - 6.0f) / 18.0f) * (float)OLED_W);
    for (int x = 0; x < wx; x++) { float u = ((float)x - 64.0f) / 64.0f; oled_pixel(x, 42 + (int)lroundf(3.0f * (1.0f - u * u)), true); }
    // five birds land, one after another, each with a note
    static const int bx[5] = { 16, 36, 56, 76, 96 };
    for (int k = 0; k < 5; k++) {
        float st = 20.0f + (float)k * 7.0f, p = (t - st) / 12.0f;
        if (p <= 0) continue;
        int dir = k & 1 ? -1 : 1;
        float u = ((float)bx[k] - 64.0f) / 64.0f;
        int py = 42 + (int)lroundf(3.0f * (1.0f - u * u)), size = 1 + (k * 3 + 1) % 3;
        if (p < 1.0f) {
            float e = ease_out(p);
            int sx0 = dir > 0 ? -12 : OLED_W + 12;
            int x = sx0 + (int)lroundf((float)(bx[k] - sx0) * e), y = 18 + (int)lroundf((float)(py - 18) * e - 8.0f * sinf(e * 3.14159f));
            draw_bird(x, y, size, dir, false, sinf(t * 1.6f + (float)k), true, false);
        } else {
            bool open = p < 1.6f;
            draw_bird(bx[k], py, size, dir, open, 0, false, false);
            if (p >= 1.0f && p < 1.0f + 1.0f / 12.0f) add_note((float)(bx[k] + dir * 6), (float)(py - bird_h(size)), (float)dir * 8.0f, size == 3);
        }
    }
    notes_step_draw(16);
    // the title drops into the header band
    if (f >= 52) {
        float e = ease_back((t - 52.0f) / 14.0f);
        int y = -12 + (int)lroundf(e * 15.0f);
        oled_ftext_c(F_MED, 64, y, "CHIRP", 3);
    }
    if (f >= 64) {
        const char *tl = "THE BLUETOOTH AROUND YOU";
        int w = oled_ftext_w(F_TINY, tl, 1);
        oled_rect(64 - w / 2 - 2, 52, w + 4, 8, false);
        oled_ftext_c(F_TINY, 64, 53, tl, 1);
    }
    if (f >= 91) nnotes = 0;
    return f < 92;
}

// ---------------------------------------------------------------- screensaver: a bird asleep on the wire
void scene_saver(uint32_t f)
{
    int place = (int)((f / 1500) % 5);                           // it moves along every minute (burn-in)
    int x = 24 + place * 20, y = 40 + (int)(place & 1) * 6;
    for (int k = -18; k <= 18; k++) oled_pixel(x + k, y + (k * k) / 120, true);
    draw_bird(x, y, 3, place & 1 ? -1 : 1, false, 0, false, true);
    for (int k = 0; k < 3; k++) {
        float ph = fmodf((float)f / 75.0f + (float)k / 3.0f, 1.0f);
        int zx = x + 6 + (int)(ph * 10.0f), zy = y - 12 - (int)(ph * 16.0f);
        if (ph < 0.9f) oled_ftext_ex(ph > 0.5f ? F_SMALL : F_TINY, zx, zy, "Z", 0);
    }
}
