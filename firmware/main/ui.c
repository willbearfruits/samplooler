// The screen. A tape runs between two reels; the loop's waveform is the tape; a stick figure runs on
// it. The reels and the figure move with the real speed and direction, the figure suffers whatever
// the user does to the loop (flung at 8x, ghosted by the delay, juggling grains, pressed down by the hand
// over the sensor, dragged by the left knob), and the odd boot gets its own gag. Fonts are TWOFOLD's
// proportional faces (font.h); the layout keeps the 16-row band that is yellow on two-colour panels for
// the header.
//
// 128x64 grid:  header 0..15 | figure zone 16..27 | tape 28..46 (edges 28 and 46, reels either side)
//               | bottom strip 48..63: tilt ball, speed, effects; or a toast, a note.
//               The menu is a list over rows 16..63, four settings at a time, under the header.
//
// Everything moves by the clock (ui_t.now_ms), not by the frame: the screen's task draws 30 frames a second,
// the hosts' sims 20, and the pace is the same.
#include "ui.h"
#include "oled.h"
#include "recorder.h"
#include "audio.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#if OLED_ENABLE

// ---------------------------------------------------------------- the clock
static uint32_t last_ms;
static bool  started;
static float dt = 0.05f;         // seconds since the last frame
static float T;                  // seconds since the first frame
static int   tk;                 // T in twentieths: the blinks and the gags count in these
static int   fps_n, fps_v;
static uint32_t fps_t0;

static void clock_step(const ui_t *u)
{
    if (!started || u->now_ms == 0) dt = 0.05f;
    else { uint32_t d = u->now_ms - last_ms; dt = d < 5 ? 0.005f : d > 100 ? 0.1f : (float)d * 0.001f; }
    started = true; last_ms = u->now_ms;
    T += dt; tk = (int)(T * 20.0f);
    fps_n++;
    if (u->now_ms - fps_t0 >= 1000) { fps_v = fps_n; fps_n = 0; fps_t0 = u->now_ms; }
}
int ui_fps(void) { return fps_v; }
// towards a target, the same whatever the frame rate: rate is how many times a second the gap shrinks by e
static float glide(float v, float target, float rate) { return v + (target - v) * (1.0f - expf(-rate * dt)); }

static uint8_t env[OLED_W];
static float reel_a;             // reel angle, degrees, follows the tape
static float tape_x;             // tape edge dot phase
static float run_ph;             // the figure's stride, 0..1

static int   clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static float clampf(float v, float lo, float hi) { return !(v >= lo) ? lo : v > hi ? hi : v; }   // NaN lands on lo
static bool  starts(const char *s, const char *p) { return s && strncmp(s, p, strlen(p)) == 0; }
static const char *upper(const char *s)        // pills shout; the small face keeps a few lowercase unit letters
{
    static char b[48]; int i = 0;
    for (; s && s[i] && i < 47; i++) b[i] = (s[i] >= 'a' && s[i] <= 'z') ? s[i] - 32 : s[i];
    b[i] = 0; return b;
}
static float ease_out(float p) { p = 1.0f - clampf(p, 0.0f, 1.0f); return 1.0f - p * p * p; }

// ---------------------------------------------------------------- pieces

// Segmented meter, LED style, with peak hold and a clip flash.
static float peak_hold;
static void meter(int x, int y, int w, int h, float v, float *hold, bool clip)
{
    v = clampf(v, 0.0f, 2.0f);
    if (v > *hold) *hold = v; else *hold -= 0.4f * dt;
    if (*hold < 0) *hold = 0;
    int segw = 2, gap = 1, n = (w - 2) / (segw + gap);
    oled_rframe(x, y, w, h, 1);
    int lit = (int)(v * n + 0.5f); if (lit > n) lit = n;
    for (int i = 0; i < lit; i++) oled_rect(x + 1 + i * (segw + gap), y + 2, segw, h - 4, true);
    int hp = (int)(*hold * n); if (hp >= n) hp = n - 1;
    if (hp >= 0 && *hold > 0.02f) oled_rect(x + 1 + hp * (segw + gap), y + 1, segw, h - 2, true);
    if (clip) oled_invert(x, y, w, h);
}

static void slider(int x, int y, int w, float frac)
{
    frac = clampf(frac, 0.0f, 1.0f);
    oled_hline_pat(x, y + 1, w, 0x55, true);
    int fw = (int)(frac * (float)(w - 1) + 0.5f);
    oled_rect(x, y, fw + 1, 3, true);
    oled_rect(x + fw, y - 2, 2, 7, true);
}

// A stick figure. (x, floor) is where the feet touch. dir +1 faces right, -1 left. 11 rows tall:
// head rows 0..2, body 3..7, legs 7..10. Poses carry their own arms and legs.
enum { P_STAND, P_MIC, P_CARRY, P_FLAT, P_SHRUG, P_JUMP, P_SLEEP, P_SIT, P_LISTEN };
static void figure(int x, int floor, int pose, int dir, int ph)
{
    int hy = floor - 10;                                        // head centre row
    if (pose == P_FLAT || pose == P_SLEEP) {                    // lying: head to the left, feet to the right
        int y = floor - 1;
        oled_circle(x - 7, y, 1, true);
        oled_line(x - 5, y, x + 2, y, true);                    // body
        oled_line(x + 2, y, x + 6, y - 1 + (ph & 1), true);     // legs
        oled_line(x - 2, y, x - 1, y - 3, true);                // an arm up in the air
        return;
    }
    if (pose == P_SIT) {
        oled_circle(x, hy + 3, 1, true);
        oled_line(x, hy + 5, x, hy + 8, true);                  // short body
        oled_line(x, hy + 8, x + 3 * dir, hy + 8, true);        // thighs out
        oled_line(x + 3 * dir, hy + 8, x + 3 * dir, floor, true);
        oled_line(x, hy + 6, x + 2 * dir, hy + 8, true);        // arm on the knee
        return;
    }
    oled_circle(x, hy, 1, true);                                // head
    oled_pixel(x + dir, hy, true);                              // the nose: which way it faces
    oled_line(x, hy + 2, x, floor - 3, true);                   // body
    int lk = floor - 3;                                         // hips
    switch (pose) {
    case P_MIC:
        oled_line(x, lk, x - 2, floor, true); oled_line(x, lk, x + 2, floor, true);
        oled_line(x, hy + 4, x + 3 * dir, hy + 2, true);        // arm up with the mic
        oled_disc(x + 4 * dir, hy + 1, 1, true);
        oled_line(x, hy + 4, x - 2 * dir, hy + 7, true);
        break;
    case P_LISTEN:                                              // a hand behind the ear, waiting for a sound
        oled_line(x, lk, x - 2, floor, true); oled_line(x, lk, x + 2, floor, true);
        oled_line(x, hy + 4, x + 3 * dir, hy + 3, true); oled_line(x + 3 * dir, hy + 3, x + 2 * dir, hy + 1, true);
        oled_line(x, hy + 4, x - 2 * dir, hy + 7, true);
        for (int k = 0; k < 2; k++) if (((ph + k) & 3) < 2) {   // what there is to hear, so far
            int ax = x + (5 + 3 * k) * dir;
            oled_pixel(ax, hy - 1, true); oled_pixel(ax + dir, hy, true); oled_pixel(ax, hy + 1, true);
        }
        break;
    case P_CARRY:
        oled_line(x, lk, x - 2 + (ph & 1), floor, true); oled_line(x, lk, x + 2 - (ph & 1), floor, true);
        oled_line(x, hy + 4, x - 3, hy - 1, true); oled_line(x, hy + 4, x + 3, hy - 1, true);   // both arms up
        break;
    case P_SHRUG:
        oled_line(x, lk, x - 2, floor, true); oled_line(x, lk, x + 2, floor, true);
        oled_line(x, hy + 4, x - 4, hy + 2, true); oled_line(x, hy + 4, x + 4, hy + 2, true);   // palms up
        oled_pixel(x - 4, hy + 1, true); oled_pixel(x + 4, hy + 1, true);
        break;
    case P_JUMP:
        oled_line(x, lk, x - 3, lk + 1, true); oled_line(x, lk, x + 3, lk + 1, true);          // legs tucked
        oled_line(x, hy + 4, x - 3, hy, true); oled_line(x, hy + 4, x + 3, hy, true);          // arms up
        break;
    default:                                                    // standing
        oled_line(x, lk, x - 2, floor, true); oled_line(x, lk, x + 2, floor, true);
        oled_line(x, hy + 4, x - 2, hy + 7, true); oled_line(x, hy + 4, x + 2, hy + 7, true);
        if (ph == 1) oled_line(x, hy + 4, x + 3 * dir, hy + 1, true);                           // scratching the head
        if (ph == 2) oled_rect(x + 2 * dir, floor - 1, 1, 1, false);                           // tapping a foot
        break;
    }
}

// The runner: the same figure with its stride drawn from a phase (0..1 is one cycle of both legs), so it runs as
// smoothly as the frames come. squash: pressed down by this many rows (the hand), knees out. lean: the head ahead
// of the hips by this many pixels (dragged by the knob, leaning into speed).
static void runner(int x, int floor, int dir, float ph, int squash, int lean)
{
    float s = sinf(ph * 6.2832f), c = cosf(ph * 6.2832f);
    squash = clampi(squash, 0, 5);
    int hy = floor - 10 + squash, hip = floor - 3 + (squash > 3 ? 1 : 0);
    int hx = x + lean * dir;
    oled_circle(hx, hy, 1, true);
    oled_pixel(hx + dir, hy, true);
    if (hy + 2 <= hip) oled_line(hx, hy + 2, x, hip, true);
    float st = 3.0f + 0.5f * (float)squash;                      // the stride widens as the figure is pressed down
    int a = (int)lroundf(st * s);
    oled_line(x, hip, x + a * dir, floor - (c > 0 ? (int)lroundf(1.4f * c) : 0), true);      // the leg coming forward lifts
    oled_line(x, hip, x - a * dir, floor - (c < 0 ? (int)lroundf(-1.4f * c) : 0), true);
    int sh = hy + 4 < hip ? hy + 4 : hip - 1;                    // shoulders
    int b = (int)lroundf(3.0f * s);
    oled_line(hx, sh, hx - b * dir, sh + 1, true);               // arms against the legs
    oled_line(hx, sh, hx + b * dir, sh + 2, true);
}

// A figure rotated about its middle: the slapstick tumble at high speed.
static void tumble(int cx, int cy, float a)
{
    float c = cosf(a), s = sinf(a);
    int bx = (int)(5 * c), by = (int)(5 * s);
    oled_line(cx - bx, cy - by, cx + bx, cy + by, true);        // body
    oled_circle(cx - bx - (int)(2 * c), cy - by - (int)(2 * s), 1, true);
    oled_line(cx + bx, cy + by, cx + bx + (int)(3 * s), cy + by - (int)(3 * c), true);   // legs splayed
    oled_line(cx + bx, cy + by, cx + bx - (int)(3 * s), cy + by + (int)(3 * c), true);
    oled_line(cx, cy, cx - (int)(4 * s), cy + (int)(4 * c), true);                       // arms
}

// A cassette, 13 x 8, top-left corner (x, y).
static void cassette(int x, int y)
{
    oled_rframe(x, y, 13, 8, 1);
    oled_circle(x + 3, y + 4, 1, true); oled_circle(x + 9, y + 4, 1, true);
    oled_rect(x + 3, y + 1, 7, 1, true);
}

// A reel: rim, three spokes at angle a (degrees), hub, and the tape wound on it as a ring (pack 0 empty: a small
// ring round the hub .. 1 full: out by the rim), so the two reels trade tape as the loop runs.
static void reel(int cx, int cy, int r, float a, float pack)
{
    oled_circle(cx, cy, r, true);
    for (int k = 0; k < 3; k++) {
        float t = (a + 120.0f * k) * 0.0174533f;
        oled_line(cx, cy, cx + (int)lroundf((r - 1) * cosf(t)), cy + (int)lroundf((r - 1) * sinf(t)), true);
    }
    int rp = 2 + (int)lroundf(clampf(pack, 0.0f, 1.0f) * (float)(r - 4));
    if (rp > 2) oled_circle(cx, cy, rp, true);
    oled_rect(cx - 1, cy - 1, 3, 3, false); oled_pixel(cx, cy, true);
}

// The vessel: an urn, 15 wide and 20 tall, standing on (cx, base). The widths of its rows from the lip down.
static const uint8_t URN_W[20] = { 9, 9, 5, 5, 5, 7, 11, 13, 15, 15, 15, 15, 13, 13, 11, 9, 7, 5, 7, 9 };
static void urn(int cx, int base, int wob)
{
    int top = base - 19;
    for (int r = 0; r < 20; r++) {
        int w = URN_W[r], h = w / 2, y = top + r, x0 = cx - h + (r < 6 ? wob : 0), x1 = x0 + w - 1;
        if (r == 0 || r == 19 || r == 1) { oled_rect(x0, y, w, 1, true); continue; }
        int pw = URN_W[r - 1], nw = r < 19 ? URN_W[r + 1] : w;
        int ow = pw > nw ? pw : nw;                             // close the outline where the next row is wider or narrower
        oled_pixel(x0, y, true); oled_pixel(x1, y, true);
        if (ow > w) { int g = (ow - w) / 2; if (pw > w && nw > w) g = 0; (void)g; }
        if (pw < w) { oled_rect(x0, y, (w - pw) / 2 + 1, 1, true); oled_rect(x1 - (w - pw) / 2, y, (w - pw) / 2 + 1, 1, true); }
        if (nw < w) { oled_rect(x0, y, (w - nw) / 2 + 1, 1, true); oled_rect(x1 - (w - nw) / 2, y, (w - nw) / 2 + 1, 1, true); }
    }
    oled_hline_pat(cx - 6, top + 9, 13, 0x55, true);            // a band round the belly
    oled_hline_pat(cx - 5, top + 12, 11, 0x55, true);
    oled_pixel(cx - 5 + wob, top + 3, true); oled_pixel(cx - 6 + wob, top + 4, true); oled_pixel(cx - 6, top + 5, true);   // two small handles
    oled_pixel(cx + 5 + wob, top + 3, true); oled_pixel(cx + 6 + wob, top + 4, true); oled_pixel(cx + 6, top + 5, true);
}

// ---------------------------------------------------------------- 128x64

#define FIG_FLOOR   28           // the figure stands on the tape's top edge
#define TAPE_TOP    28
#define TAPE_BOT    46
#define TAPE_MID    37
#define TAPE_HALF   8
#define TAPE_X0     16
#define TAPE_X1     111
#define TAPE_W      (TAPE_X1 - TAPE_X0 + 1)
#define REEL_L      8
#define REEL_R      119
#define STRIP_Y     48
#define BODY_Y      16
#define ROW_H       12

static float clip_flash;         // seconds of the clip mark left
static int   last_state = -1;
static float flash;              // seconds of whole-screen invert left
static float menu_t;             // 0 closed .. 1 open
static int8_t  scope[AUDIO_SCOPE_N];
static int   gag; static float gag_t;   // idle gag in progress
static float fig_x = 64;         // where the figure is (it drifts with the tape)
static float press;              // how far the hand has the figure down, rows, gliding
static float marquee;            // the invitation's scroll, pixels

static float head_speed(const ui_t *u)   // what the tape really does: the speed and the knob's push
{
    return u->state == REC_RECORDING ? 1.0f : u->state == REC_PLAYING ? u->speed_now + u->jog : 0.0f;
}

static void tape_motion(const ui_t *u)
{
    float v = clampf(head_speed(u), -16.0f, 16.0f);
    reel_a += v * 180.0f * dt;  reel_a = fmodf(reel_a, 360.0f); if (reel_a < 0) reel_a += 360.0f;
    tape_x += v * 30.0f * dt;   tape_x = fmodf(tape_x, 4.0f);   if (tape_x < 0) tape_x += 4.0f;
    run_ph += fabsf(v) * 2.5f * dt; run_ph -= floorf(run_ph);
}

// The link's glyph: a dot with two arcs over it, 7 px wide; the arcs fill in on the beat.
static void link_glyph(int x, int y, int flash_)
{
    oled_disc(x + 3, y + 7, 1, true);
    if (flash_ > 0) {
        oled_line(x + 1, y + 4, x + 5, y + 4, true); oled_pixel(x + 1, y + 5, true); oled_pixel(x + 5, y + 5, true);
        oled_line(x, y + 1, x + 6, y + 1, true); oled_pixel(x, y + 2, true); oled_pixel(x + 6, y + 2, true);
    } else {
        oled_pixel(x + 2, y + 4, true); oled_pixel(x + 3, y + 4, true); oled_pixel(x + 4, y + 4, true); oled_pixel(x + 1, y + 5, true); oled_pixel(x + 5, y + 5, true);
        oled_pixel(x + 1, y + 1, true); oled_pixel(x + 5, y + 1, true); oled_pixel(x, y + 2, true); oled_pixel(x + 6, y + 2, true);
    }
}

static void header(const ui_t *u, bool in_menu)
{
    char b[24];
    int x = 2;
    int right = 89;
    if (u->peers > 0) { link_glyph(82, 4, u->beat_flash); right = 80; }           // boards heard on the link
    if (in_menu) {                                              // the menu: its group, the place in the list, what the tape does
        x += oled_pill(F_MED, x, 2, u->menu_group ? u->menu_group : "MENU", true) + 4;
        snprintf(b, sizeof b, "%d/%d", u->menu_sel + 1, u->menu_n);
        oled_ftext(F_TINY, x, 6, b);
        int gx = right - 9;
        if (u->state == REC_RECORDING) { if ((tk / 4) & 1) oled_disc(gx + 3, 8, 3, true); else oled_circle(gx + 3, 8, 3, true); }
        else if (u->state == REC_PLAYING) for (int k = 0; k < 4; k++) oled_rect(gx + 1 + k, 5 + k, 1, 7 - 2 * k, true);
    } else if (u->state == REC_RECORDING && u->waiting) {       // REC is down, the take waits for the first sound
        if ((tk / 5) & 1) x += oled_pill(F_MED, x, 2, "REC", false) + 4; else x += oled_pill_w(F_MED, "REC") + 4;
        oled_ftext_ex(F_SMALL, x, 6, "LISTENING", 1);
    } else if (u->state == REC_RECORDING) {
        x += oled_pill(F_MED, x, 2, u->closing ? "BEAT" : "REC", true) + 3;   // closing: recording on to the beat
        if ((tk / 4) & 1) oled_disc(x + 3, 8, 3, true); else oled_circle(x + 3, 8, 3, true);
        snprintf(b, sizeof b, "%ds", (int)(u->rec_fill * (float)u->max_s));
        oled_ftext(F_SMALL, x + 9, 5, b);
    } else if (u->state == REC_PLAYING) {
        x += oled_pill(F_MED, x, 2, "LOOP", true) + 3;
        snprintf(b, sizeof b, "%.1fs", (double)u->loop_s);
        oled_ftext(F_SMALL, x, 5, b);
    } else {
        oled_pill(F_MED, x, 2, "READY", false);
        if (u->voice) oled_ftext_r(F_TINY, right, 6, u->voice, 1);   // the voice on the input (else in the strip)
    }
    meter(92, 4, 36, 9, u->meter_in, &peak_hold, u->clip);
}

// The tape between the reels: the loop's envelope, the record fill, or the live scope.
static int   env_state = -1; static float env_loop, env_age;
static void tape(const ui_t *u)
{
    // edges, dotted, moving with the tape
    for (int x = TAPE_X0 - 2; x <= TAPE_X1 + 2; x++) if (((x - (int)tape_x) & 3) != 0) { oled_pixel(x, TAPE_TOP, true); oled_pixel(x, TAPE_BOT, true); }
    // the reels, with the tape going from the left one to the right one as the loop (or the take) runs
    float along = u->state == REC_RECORDING ? u->rec_fill : u->state == REC_PLAYING ? u->pos : 0.5f;
    reel(REEL_L, TAPE_MID, 6, reel_a, 1.0f - along); reel(REEL_R, TAPE_MID, 6, reel_a, along);
    oled_line(REEL_L + 4, TAPE_MID - 5, TAPE_X0 - 2, TAPE_TOP, true); oled_line(REEL_L + 4, TAPE_MID + 5, TAPE_X0 - 2, TAPE_BOT, true);
    oled_line(REEL_R - 4, TAPE_MID - 5, TAPE_X1 + 2, TAPE_TOP, true); oled_line(REEL_R - 4, TAPE_MID + 5, TAPE_X1 + 2, TAPE_BOT, true);

    if (u->state == REC_IDLE || (u->state == REC_RECORDING && u->waiting)) {   // live oscilloscope of the input
        audio_scope(scope);
        int py = TAPE_MID - scope[0] * (TAPE_HALF - 1) / 127;
        for (int x = 1; x < TAPE_W; x++) {
            int y = TAPE_MID - scope[(x * AUDIO_SCOPE_N) / TAPE_W] * (TAPE_HALF - 1) / 127;
            oled_line(TAPE_X0 + x - 1, py, TAPE_X0 + x, y, true);
            py = y;
        }
        env_state = -1;
        return;
    }
    // The envelope reads the take all over (PSRAM, and the sound's task shares that cache): while a loop plays it
    // does not change, so it is read again only when the loop does, and once a second in case.
    env_age += dt;
    if (u->state == REC_RECORDING || u->state != env_state || u->loop_s != env_loop || env_age > 1.0f) {
        recorder_envelope(env, TAPE_W);
        env_state = u->state; env_loop = u->loop_s; env_age = 0;
    }
    int fill_x = u->state == REC_RECORDING ? (int)(clampf(u->rec_fill, 0.0f, 1.0f) * (TAPE_W - 1)) : TAPE_W - 1;
    for (int x = 0; x < TAPE_W; x++) {
        if (u->state == REC_RECORDING && x > fill_x) { if ((x & 3) == 0) oled_pixel(TAPE_X0 + x, TAPE_MID, true); continue; }
        int col = u->state == REC_RECORDING ? (int)((float)x / (float)(fill_x ? fill_x : 1) * (TAPE_W - 1)) : x;
        int h = env[clampi(col, 0, TAPE_W - 1)] * (TAPE_HALF - 1) / 255;
        if (h == 0 && env[clampi(col, 0, TAPE_W - 1)]) h = 1;
        if (h > 0) oled_rect(TAPE_X0 + x, TAPE_MID - h, 1, 2 * h + 1, true); else oled_pixel(TAPE_X0 + x, TAPE_MID, true);
    }
    if (u->state == REC_RECORDING) {
        if ((tk / 3) & 1) oled_rect(TAPE_X0 + fill_x, TAPE_TOP + 1, 2, TAPE_BOT - TAPE_TOP - 1, true);
        return;
    }
    // playhead with a comet tail that lengthens with speed
    float v = head_speed(u);
    int hx = TAPE_X0 + (int)(clampf(u->pos, 0.0f, 1.0f) * (TAPE_W - 1));
    float a = fabsf(v);
    int tail = (int)(a * 1.5f); if (tail > 8) tail = 8;
    for (int k = 1; k <= tail; k++) {
        int x = v >= 0 ? hx - k : hx + k;
        if (x < TAPE_X0) x += TAPE_W;
        if (x > TAPE_X1) x -= TAPE_W;
        int span = (TAPE_HALF - 1) - k * (TAPE_HALF - 1) / (tail + 1);
        oled_invert(x, TAPE_MID - span, 1, 2 * span + 1);
    }
    oled_invert(hx, TAPE_TOP + 1, 1, TAPE_BOT - TAPE_TOP - 1);
}

// The hand over the sensor, as the figure meets it: a shade that comes down from under the header, across the
// whole tape, and the figure ducks beneath it.
static void shade(int rows)
{
    int py = BODY_Y + rows;
    if (rows > 0) oled_dither(TAPE_X0, BODY_Y, TAPE_W, rows, 1, true);
    oled_hline_pat(TAPE_X0, py, TAPE_W, 0xFF, true);
    oled_pixel(TAPE_X0, py + 1, true); oled_pixel(TAPE_X1, py + 1, true);
}

// The operator: what the loop is doing to them.
static void operator_(const ui_t *u)
{
    const int floor = FIG_FLOOR;
    const bool handed = u->hand_mode != UI_HAND_OFF && (u->hand_seen || u->hand > 0.02f);
    press = glide(press, handed ? u->hand * 5.0f : 0.0f, 25.0f);
    int sq = (int)lroundf(press);
    if (u->state == REC_IDLE) {
        if (gag_t <= 0 && (tk % 60) == 0) { gag = 1 + (tk / 60) % 2; gag_t = 0.5f; }   // a gag every 3 s
        figure(64, floor, P_STAND, 1, gag_t > 0 ? gag : 0);
        if (gag_t > 0) gag_t -= dt;
        return;
    }
    if (u->state == REC_RECORDING) {
        if (u->waiting) { figure(64, floor, P_LISTEN, 1, tk / 3); return; }
        figure(64, floor, P_MIC, 1, 0);
        if (u->rec_fill > 0.85f) {                              // tape nearly full: sweat
            int k = tk % 6;
            oled_pixel(60 - k / 2, floor - 12 + k, true); oled_pixel(69 + k / 2, floor - 11 + k, true);
        }
        if (u->clip) {                                          // the mic blew up
            for (int k = 0; k < 8; k++) { float t = k * 0.785f + T * 6.0f; oled_pixel(68 + (int)(5 * cosf(t)), floor - 9 + (int)(5 * sinf(t)), true); }
        }
        return;
    }
    // playing: run on the tape, drift with it, come back from the edge
    float v = head_speed(u), a = fabsf(v);
    int dir = v >= 0 ? 1 : -1;
    fig_x += v * 12.0f * dt;
    fig_x = clampf(fig_x, 28, 100);
    if (a < 0.3f) fig_x = glide(fig_x, 64.0f, 1.0f);            // wanders home when the tape crawls
    int x = (int)lroundf(fig_x);
    bool dragged = fabsf(u->jog) > 0.15f;                       // the knob has the tape: the figure is pulled along by it
    int lean = dragged ? 2 : a > 2.5f ? 1 : 0;
    if (a >= 4.5f && !dragged && sq == 0) {                     // too fast: flung and tumbling
        tumble(x, floor - 6, T * 12.0f * (float)dir);
        if (tk & 2) oled_ftext(F_SMALL, x - 1, floor - 14, "!");
    } else if (u->fx_stutter) {                                 // stutter: the figure stutters too
        int j = (tk & 1) ? 1 : -1;                              // a shiver: the figure between two faint copies of itself
        runner(x - 4, floor, dir, run_ph, sq, 0); runner(x + 4, floor, dir, run_ph, sq, 0);
        oled_dither(x - 9, floor - 12, 19, 13, 2, false);
        runner(x + j, floor, dir, run_ph, sq, 0);
    } else if (a < 0.08f) {
        if (sq > 0) runner(x, floor, dir, 0.0f, sq, 0);
        else figure(x, floor, P_STAND, dir, (tk / 10) & 1 ? 2 : 0);   // frozen tape: tapping a foot
    } else {
        if (u->fx_delay) {                                      // delay: echoes of the runner trail behind
            for (int k = 3; k >= 1; k--) {
                int gx = x - k * 9 * dir;
                runner(gx, floor, dir, run_ph - 0.18f * (float)k, sq, 0);
                if (k == 3) oled_dither(gx - 5, floor - 12, 11, 12, 2, false);   // the oldest one is fading
            }
        }
        runner(x, floor, dir, run_ph, sq, lean);
        if (dragged) {                                          // heels dug in: dust behind
            int d = (tk & 1);
            oled_pixel(x - (5 + d) * dir, floor - 1, true); oled_pixel(x - (7 + d) * dir, floor - 2 - d, true);
        }
        if (u->mode == REC_MODE_GRAIN && sq == 0) {             // grains: juggling
            for (int k = 0; k < 3; k++) {
                float t = T * 7.0f + k * 2.094f;
                oled_rect(x + (int)(5 * cosf(t)) - 1, floor - 14 - (int)(3 * fabsf(sinf(t))), 2, 2, true);
            }
        }
    }
    if (handed) shade(sq);
    else if (u->clip) { oled_line(x - 2, floor - 13, x - 3, floor - 15, true); oled_line(x, floor - 13, x, floor - 16, true); oled_line(x + 2, floor - 13, x + 3, floor - 15, true); }
}

// Attitude ball: a horizon that rolls with roll and slides with pitch.
static void attitude(int cx, int cy, int r, float pitch, float roll, bool on)
{
    oled_circle(cx, cy, r, true);
    if (!on) { oled_line(cx - 3, cy - 3, cx + 3, cy + 3, true); oled_line(cx - 3, cy + 3, cx + 3, cy - 3, true); return; }
    float a = roll * 0.0174533f, off = clampf(pitch / 90.0f * (float)r, -(r - 1), r - 1);
    float c = cosf(a), s = sinf(a);
    for (int j = -r; j <= r; j++) for (int i = -r; i <= r; i++) {
        if (i * i + j * j > (r - 1) * (r - 1)) continue;
        float d = (float)j * c - (float)i * s - off * c;
        if (d > 0.5f) oled_pixel(cx + i, cy + j, (i + j) & 1);
        else if (d > -0.5f) oled_pixel(cx + i, cy + j, true);
    }
    oled_rect(cx - 1, cy, 3, 1, true);
}

// A pill whose left part is filled as far as frac: a name and a level in one. Drawn only if it clears `left`.
// Returns the new right edge.
static int level_pill(int rx, int y, int left, const char *s, float frac)
{
    int w = oled_pill_w(F_SMALL, s), h = F_SMALL->cap + 4, x = rx - w;
    if (x < left + 3) return rx;
    oled_pill(F_SMALL, x, y, s, false);
    int fw = (int)lroundf(clampf(frac, 0.0f, 1.0f) * (float)(w - 2));
    if (fw > 0) oled_invert(x + 1, y + 1, fw, h - 2);
    return x - 2;
}
static int plain_pill(int rx, int y, int left, const char *s, bool filled)
{
    int w = oled_pill_w(F_SMALL, s), x = rx - w;
    if (x < left + 3) return rx;
    oled_pill(F_SMALL, x, y, s, filled);
    return x - 2;
}

static void bottom_strip(const ui_t *u)
{
    const int y = STRIP_Y;
    char b[20];
    if (u->state == REC_IDLE) {                                 // the invitation marquee
        char msg[160];
        snprintf(msg, sizeof msg, "HOLD REC TO RECORD   " S_RIGHT " TILT TO PLAY   " S_RIGHT " ROLL FOR EFFECTS   %s%s   ",
                 u->has_knob ? S_RIGHT " TURN THE LEFT KNOB   " : "", u->has_hand ? S_RIGHT " HOLD A HAND OVER IT   " : "");
        int len = oled_ftext_w(F_SMALL, msg, 1) + 4;
        marquee += 40.0f * dt; if (marquee >= (float)len) marquee -= (float)len;
        int off = (int)marquee;
        oled_ftext_ex(F_SMALL, 2 - off, y + 6, msg, 1);
        oled_ftext_ex(F_SMALL, 2 - off + len, y + 6, msg, 1);
        oled_rect(0, y, 2, 16, false); oled_rect(OLED_W - 2, y, 2, 16, false);
        return;
    }
    if (u->state == REC_RECORDING && u->waiting) {              // nothing recorded yet: say what it waits for
        oled_ftext_c(F_SMALL, OLED_W / 2, y + 6, "MAKE A SOUND TO START", 1);
        return;
    }
    attitude(8, y + 8, 7, u->has_imu ? u->tilt_pitch : 0, u->has_imu ? u->tilt_roll : 0, u->has_imu && u->tilt_on);
    float sp = u->state == REC_PLAYING ? u->speed_now + u->jog : u->speed_cmd, a = fabsf(sp);
    snprintf(b, sizeof b, "%s%.2f", sp < 0 ? "-" : "", (double)(a > 99.0f ? 99.0f : a));
    int x = oled_ftext(F_MED, 19, y + 3, b);
    x = oled_ftext(F_SMALL, x + 1, y + 7, S_RIGHT S_RIGHT);
    if (!u->tilt_on) x = oled_ftext(F_SMALL, x + 3, y + 5, u->has_joy ? "JOY" : "1X");
    if (u->frozen) x += oled_pill(F_SMALL, x + 3, y + 3, "HOLD", true) + 1;         // the tilt is latched
    if (u->mode == REC_MODE_GRAIN) {
        snprintf(b, sizeof b, "GRN %+.0f", (double)(12.0f * log2f(clampf(u->pitch_ratio, 0.01f, 100.0f))));
        x += oled_pill(F_SMALL, x + 4, y + 3, b, true) + 4;
    }
    // right corner, as far as there is room: the effects, the hand, the left knob's own values; else the output level and the cpu
    int rx = OLED_W - 1, rx0 = rx;
    if (u->fx_stutter) rx = plain_pill(rx, y + 3, x, "STUTTER", (tk & 2) != 0);
    if (u->fx_delay)   rx = plain_pill(rx, y + 3, x, "DELAY", false);
    if (u->hand_mode != UI_HAND_OFF && u->hand > 0.02f) {
        static const char *const HN[UI_HAND_N] = { "", "FILTER", "PITCH", "BRAKE", "VOLUME", "ECHO" };
        rx = level_pill(rx, y + 3, x, HN[clampi(u->hand_mode, 0, UI_HAND_N - 1)], u->hand);
    } else if (u->filter > 0.02f || u->filter < -0.02f) {
        rx = level_pill(rx, y + 3, x, u->filter < 0 ? "LOW" : "HIGH", fabsf(u->filter));
    }
    if (u->knob_st) { snprintf(b, sizeof b, "%+d", u->knob_st); rx = plain_pill(rx, y + 3, x, b, true); }
    if (u->voice && rx == rx0) {                                // the voice on the input, where there is room
        if (oled_ftext_w(F_TINY, u->voice, 0) <= 96 - (x + 5)) oled_ftext_ex(F_TINY, x + 5, y + 6, u->voice, 0);
    }
    if (rx == rx0) {
        snprintf(b, sizeof b, "%d%%", u->cpu);
        oled_ftext_r(F_TINY, OLED_W - 1, y + 3, b, 0);
        oled_rframe(98, y + 11, 30, 4, 1);
        int w = (int)(clampf(u->meter_out, 0.0f, 1.0f) * 28.0f);
        oled_rect(99, y + 12, w, 2, true);
    }
}

// ---------------------------------------------------------------- the menu
// The list: four rows of the settings under the header, the chosen one under a bar. The list and the bar both
// glide, so a turn of the knob reads as movement, not as a new picture. Editing opens the chosen setting over the
// whole body: its value large, a slider where it has a range.
static float list_y, bar_y, edit_t;
static int   list_sel = -1;

// A value, as large as fits in w: numbers in the 18-row numerals with their unit beside them, words at twice the
// medium face, then twice the small one, then plain.
static void big_value(int cx, int y, int w, const char *s)
{
    int n = 0;
    while (s[n] && strchr("0123456789.-+:", s[n])) n++;
    char num[16], rest[UI_VAL_N + 8];
    if (n > 0 && n < (int)sizeof num) {
        memcpy(num, s, (size_t)n); num[n] = 0;
        const char *r = s + n; while (*r == ' ') r++;
        snprintf(rest, sizeof rest, "%s", r);
        int wn = oled_ftext_w(F_BIG, num, 1), wr = rest[0] ? oled_ftext_w(F_MED, rest, 1) + 3 : 0;
        if (wn + wr <= w) {
            int x = cx - (wn + wr) / 2;
            x = oled_ftext_ex(F_BIG, x, y, num, 1);
            if (rest[0]) oled_ftext_ex(F_MED, x + 3, y + 10, rest, 1);
            return;
        }
    }
    if (oled_ftext2_w(F_MED, s, 1) <= w) oled_ftext2(F_MED, cx - oled_ftext2_w(F_MED, s, 1) / 2, y + 1, s, 1);
    else if (oled_ftext2_w(F_SMALL, upper(s), 1) <= w) { const char *up = upper(s); oled_ftext2(F_SMALL, cx - oled_ftext2_w(F_SMALL, up, 1) / 2, y + 4, up, 1); }
    else oled_ftext_c(F_MED, cx, y + 5, s, 1);
}

static void menu_list(const ui_t *u, int off)
{
    const int n = u->menu_n, sel = clampi(u->menu_sel, 0, n - 1);
    int first = clampi(sel - 1, 0, n > 4 ? n - 4 : 0);          // the chosen row sits second from the top where it can
    float want_list = (float)(first * ROW_H), want_bar = (float)(sel * ROW_H);
    if (list_sel < 0 || fabsf(want_bar - bar_y) > 3.5f * ROW_H) { list_y = want_list; bar_y = want_bar; }   // opened, or wrapped round: no long flight
    list_sel = sel;
    list_y = glide(list_y, want_list, 22.0f); bar_y = glide(bar_y, want_bar, 30.0f);
    if (fabsf(list_y - want_list) < 0.4f) list_y = want_list;
    if (fabsf(bar_y - want_bar) < 0.4f) bar_y = want_bar;
    edit_t = glide(edit_t, u->menu_edit ? 1.0f : 0.0f, 28.0f);
    if (edit_t < 0.02f) edit_t = 0; else if (edit_t > 0.98f) edit_t = 1;

    const int top = BODY_Y + off;
    oled_clip(0, top, OLED_W, OLED_H - top);
    oled_rect(0, top, OLED_W, OLED_H - top, false);
    if (edit_t < 1.0f) {
        for (int r = 0; r < UI_ROWS; r++) {
            int row = sel - UI_ROWS / 2 + r;
            if (!u->row_name[r] || row < 0 || row >= n) continue;
            int y = top + (int)lroundf((float)(row * ROW_H) - list_y);
            if (y <= top - ROW_H || y >= OLED_H) continue;
            int nx = oled_ftext_ex(F_SMALL, 5, y + 3, upper(u->row_name[r]), 1);
            int rx = 119;
            if (u->row_act[r]) { oled_ftext(F_SMALL, 115, y + 3, S_RIGHT); rx = 111; }   // an action: an arrow, and what a press will do
            const char *val = upper(u->row_val[r]);
            if (rx - oled_ftext_w(F_SMALL, val, 1) >= nx + 5) oled_ftext_r(F_SMALL, rx, y + 3, val, 1);
            else if (rx - oled_ftext_w(F_TINY, val, 0) >= nx + 4) oled_ftext_r(F_TINY, rx, y + 3, val, 0);   // a long one: the narrow face
        }
        int by = top + (int)lroundf(bar_y - list_y);            // the bar: the row under it turns dark on light
        oled_invert(1, by, 121, ROW_H - 1);
        oled_pixel(1, by, false); oled_pixel(121, by, false); oled_pixel(1, by + ROW_H - 2, false); oled_pixel(121, by + ROW_H - 2, false);
        // where we are in the list
        oled_hline_pat(126, top, 1, 0xFF, false);
        for (int y = top + 1; y < OLED_H; y += 2) oled_pixel(126, y, true);
        int th = (OLED_H - top) * 4 / (n > 4 ? n : 4); if (th < 5) th = 5;
        int ty = top + (int)lroundf(list_y / (float)(n > 4 ? (n - 4) * ROW_H : 1) * (float)(OLED_H - top - th));
        oled_rect(125, ty, 3, th, true);
    }
    if (edit_t > 0.0f) {                                        // the chosen setting opens out of its row
        float e = ease_out(edit_t);
        int by = top + (int)lroundf(bar_y - list_y);
        int y0 = by + (int)lroundf((float)(top + 1 - by) * e), y1 = by + ROW_H - 1 + (int)lroundf((float)(OLED_H - 1 - (by + ROW_H - 1)) * e);
        oled_rect(0, y0, OLED_W, y1 - y0 + 1, false);
        oled_rframe(0, y0, OLED_W, y1 - y0 + 1, 2);
        if (edit_t >= 1.0f) {
            oled_pill(F_SMALL, 4, top + 4, upper(u->param_name ? u->param_name : ""), true);
            oled_ftext_r(F_TINY, OLED_W - 5, top + 6, "PRESS: DONE", 0);
            const char *val = u->param_val ? u->param_val : "";
            if (u->param_frac >= 0) { big_value(OLED_W / 2, top + 16, OLED_W - 12, val); slider(6, OLED_H - 7, OLED_W - 12, u->param_frac); }
            else big_value(OLED_W / 2, top + 20, OLED_W - 12, val);
        }
    }
    oled_clip_off();
}

// Without a list (an older caller: the showcase's twin): the chosen setting alone in the strip, as it used to be.
static void menu_strip(const ui_t *u, float e)
{
    int top = STRIP_Y + (int)((1.0f - e) * 18.0f);
    oled_rect(0, top, OLED_W, OLED_H - top, false);
    oled_hline_pat(0, top, OLED_W, 0x55, true);
    char b[16];
    snprintf(b, sizeof b, "%d/%d", u->param + 1, u->param_n);
    oled_ftext(F_TINY, 2, top + 3, b);
    int nx = oled_ftext_w(F_TINY, b, 0) + 6;
    oled_pill(F_SMALL, nx, top + 2, upper(u->param_name ? u->param_name : ""), u->menu_edit);
    const char *val = u->param_val ? u->param_val : "";
    oled_ftext(F_MED, OLED_W - 2 - oled_ftext_w(F_MED, val, 0), top + 2, val);
    if (u->menu_edit && u->param_frac >= 0) slider(4, top + 13, OLED_W - 8, u->param_frac);
    else if (u->param_frac < 0) oled_ftext_c(F_TINY, OLED_W / 2, top + 11, "PRESS TO RUN", 1);
    else oled_ftext_c(F_TINY, OLED_W / 2, top + 11, "TURN: CHOOSE   PRESS: EDIT", 1);
}

static void toast(const ui_t *u)
{
    int t = u->toast_ms;
    if (t <= 0) return;
    int y = STRIP_Y;
    if (t < 150) y += (150 - t) * 17 / 150;
    oled_rect(0, y, OLED_W, OLED_H - y, false);
    oled_pill(F_SMALL, 2, y + 1, upper(u->param_name ? u->param_name : ""), true);
    oled_ftext_r(F_MED, OLED_W - 3, y + 1, u->param_val ? u->param_val : "", 0);
    if (u->param_frac >= 0) slider(4, y + 12, OLED_W - 8, u->param_frac);
}

// A note in the strip, with its gag in the figure zone: the result of a job, or why we rebooted.
static void note_scene(const char *s)
{
    const int floor = FIG_FLOOR;
    oled_rect(0, 16, OLED_W, 12, false);
    if (starts(s, "CRASH")) {                                   // flattened by a falling cassette, stars
        figure(60, floor, P_FLAT, 1, tk / 4);
        cassette(66, floor - 9);
        for (int k = 0; k < 3; k++) { float t = T * 5.0f + k * 2.094f; oled_pixel(53 + (int)(5 * cosf(t)), floor - 6 + (int)(2 * sinf(t)), true); }
    } else if (starts(s, "WATCHDOG")) {                         // the dog got them
        runner(50, floor, 1, T * 2.5f, 0, 1);
        oled_line(66, floor - 1, 72, floor - 1, true); oled_line(66, floor - 1, 66, floor, true); oled_line(72, floor - 1, 72, floor, true);
        oled_rect(72, floor - 4, 3, 3, true); oled_pixel(75, floor - 3, true); oled_line(65, floor - 3, 63, floor - 5, true);
        if (tk & 4) oled_ftext(F_TINY, 77, floor - 10, "WOOF");
    } else if (starts(s, "BROWNOUT")) {                         // the lights went out
        figure(64, floor, P_STAND, 1, 0);
        if (tk % 7 < 4) oled_circle(72, floor - 12, 2, true); else oled_pixel(72, floor - 12, true);
        oled_rect(71, floor - 9, 3, 2, true);
    } else if (starts(s, "SAVED") || starts(s, "LOADED")) {      // hooray
        figure(64, floor + ((tk & 2) ? 0 : -2), P_JUMP, 1, 0);
    } else if (starts(s, "NOTHING") || starts(s, "SLOT") || strstr(s, "FAILED")) {
        figure(64, floor, P_SHRUG, 1, 0);
    } else figure(64, floor, P_STAND, 1, 0);
    const int y = STRIP_Y;
    oled_rect(0, y, OLED_W, OLED_H - y, false);
    const char *up = upper(s);
    int w = oled_pill_w(F_SMALL, up);
    if (w <= OLED_W) oled_pill(F_SMALL, (OLED_W - w) / 2, y + 3, up, true);
    else if (oled_ftext_w(F_TINY, up, 0) <= OLED_W - 2) oled_ftext_c(F_TINY, OLED_W / 2, y + 5, up, 0);
    else {                                                      // a long one (who is there): two lines in the narrow face
        char l1[48]; snprintf(l1, sizeof l1, "%s", up);
        int cut = (int)strlen(l1);
        for (int i = (int)strlen(l1) - 1; i > 0; i--) if (l1[i] == ' ') { l1[i] = 0; cut = i; if (oled_ftext_w(F_TINY, l1, 0) <= OLED_W - 2) break; }
        oled_ftext_c(F_TINY, OLED_W / 2, y + 2, l1, 0);
        if (up[cut]) oled_ftext_c(F_TINY, OLED_W / 2, y + 9, up + cut + 1, 0);
    }
}

// A slot job: the operator carries the cassette to the shelf (save) or fetches it (load).
static void busy_scene(const ui_t *u)
{
    const int floor = FIG_FLOOR;
    bool save = starts(u->busy_text, "SAV");
    float fr_ = clampf(u->busy_frac, 0.0f, 1.0f);
    oled_rect(0, 16, OLED_W, 12, false);
    int x = save ? 20 + (int)(fr_ * 76) : 96 - (int)(fr_ * 76);
    figure(x, floor, P_CARRY, save ? 1 : -1, tk / 3);
    cassette(x - 6, floor - 18 + ((tk / 3) & 1));
    oled_rframe(104, floor - 11, 20, 11, 1);                    // the shelf
    oled_rect(106, floor - 9, 16, 1, true);
    const int y = STRIP_Y;
    oled_rect(0, y, OLED_W, OLED_H - y, false);
    oled_pill(F_SMALL, 2, y + 1, u->busy_text, true);
    slider(4, y + 12, OLED_W - 8, fr_);
}

// Splash, 2.3 s: the operator carries a tape in and throws it into the vessel, which rings; the name lands.
static bool splash(void)
{
    const float END = 2.3f;
    if (T >= END) return false;
    oled_clear();
    const int floor = 50, ux = 34;
    oled_hline_pat(0, floor + 1, OLED_W, 0x55, true);
    float t = T;
    bool in = t >= 1.15f;                                       // the tape has gone in
    urn(ux, floor, in && t < 1.6f ? ((int)(t * 30.0f) & 1 ? 1 : -1) : 0);
    if (t < 0.8f) {                                             // walking in with the tape overhead
        int x = -8 + (int)(t / 0.8f * 24.0f);
        figure(x, floor, P_CARRY, 1, (int)(t * 10.0f));
        cassette(x - 6, floor - 18 + ((int)(t * 10.0f) & 1));
    } else {
        figure(16, floor, t < 1.15f ? P_JUMP : P_STAND, 1, 0);
        if (!in) {                                              // the throw: an arc into the mouth
            float p = (t - 0.8f) / 0.35f;
            cassette(10 + (int)(p * 18.0f), floor - 20 - (int)(26.0f * p * (1.0f - p)) - (int)(p * 2.0f));
        } else {
            for (int k = 0; k < 3; k++) {                       // it rings: arcs rise out of it
                float a = (t - 1.15f) * 1.6f - 0.25f * (float)k;
                if (a < 0 || a > 1) continue;
                int r = 3 + (int)(a * 9.0f), cy = floor - 20;
                for (int d = -r; d <= r; d++) if (((d + r + k) & 1) == 0) oled_pixel(ux + d, cy - (int)lroundf(sqrtf((float)(r * r - d * d)) * 0.7f) - (int)(a * 4.0f), true);
            }
        }
    }
    if (t > 1.2f) {                                             // the name comes in from the right and settles
        float e = ease_out((t - 1.2f) / 0.45f);
        int w = oled_ftext2_w(F_MED, "VESSEL", 1), x = OLED_W - 3 - w + (int)((1.0f - e) * 80.0f);
        oled_ftext2(F_MED, x, 20, "VESSEL", 1);
        if (t > 1.65f) oled_ftext_c(F_TINY, x + w / 2, 40, "SAMPLOOLER", 1);
    }
    oled_flush();
    return true;
}

// Nobody is there. Idle: the operator sleeps, somewhere new every so often. A loop still playing: the operator
// runs on alone in the dark with the speed beside them. Both leave most of the panel off and never stay put.
static void screensaver(const ui_t *u)
{
    oled_clear();
    int x = 20 + (tk / 40) % 88, floor = 30 + (tk / 97) % 20;    // drifts: no burn-in
    if (u->state == REC_PLAYING) {
        float v = head_speed(u);
        runner(x, floor, v >= 0 ? 1 : -1, run_ph, 0, 0);
        char b[12]; snprintf(b, sizeof b, "%.2f", (double)clampf(v, -99.0f, 99.0f));
        oled_ftext(F_TINY, x + 8, floor - 6, b);
        oled_hline_pat(x - 10, floor + 1, 20, (uint8_t)(0x11 << ((int)tape_x & 3)), true);
    } else {
        oled_hline_pat(x - 12, floor + 1, 24, 0x55, true);
        figure(x, floor, P_SLEEP, 1, 0);
        for (int k = 0; k < 3; k++) {
            int t = (tk / 3 + k * 7) % 21;
            oled_ftext(k == 0 ? F_TINY : F_SMALL, x - 6 + t / 3, floor - 4 - t, "Z");
        }
    }
    oled_flush();
}

// The board check: every part and control on one screen, live. For whoever builds or sells one.
static void mark(int x, int y, bool ok) { if (ok) oled_disc(x + 2, y + 2, 2, true); else oled_circle(x + 2, y + 2, 2, true); }
static void check_scene(const ui_t *u)
{
    char b[24];
    oled_clear();
    oled_pill(F_MED, 2, 2, "CHECK", true);
    if (u->version) oled_ftext_r(F_TINY, OLED_W - 2, 6, u->version, 0);
    const int c0 = 2, c1 = 68;
    int y = 19;
    mark(c0, y, u->has_imu);
    if (u->has_imu) snprintf(b, sizeof b, "TILT %+d %+d", (int)u->tilt_pitch, (int)u->tilt_roll); else snprintf(b, sizeof b, "TILT NONE");
    oled_ftext(F_SMALL, c0 + 8, y, b);
    mark(c1, y, u->has_hand);
    if (!u->has_hand) snprintf(b, sizeof b, "HAND NONE"); else if (u->hand_mm > 0) snprintf(b, sizeof b, "HAND %d", u->hand_mm); else snprintf(b, sizeof b, "HAND CLEAR");
    oled_ftext(F_SMALL, c1 + 8, y, b);
    y += 11;
    mark(c0, y, u->has_knob || u->has_joy);
    if (u->has_knob) snprintf(b, sizeof b, "LEFT %+d", u->chk_knob); else snprintf(b, sizeof b, u->has_joy ? "LEFT STICK" : "LEFT NONE");
    oled_ftext(F_SMALL, c0 + 8, y, b);
    mark(c1, y, true); snprintf(b, sizeof b, "RIGHT %+d", u->chk_enc); oled_ftext(F_SMALL, c1 + 8, y, b);
    y += 11;
    mark(c0, y, u->chk_rec); oled_ftext(F_SMALL, c0 + 8, y, "REC");
    mark(c1, y, u->chk_sw); oled_ftext(F_SMALL, c1 + 8, y, "CLICK");
    snprintf(b, sizeof b, "LINK %d", u->peers); oled_ftext_r(F_SMALL, OLED_W - 2, y, b, 0);
    y += 11;
    oled_ftext(F_SMALL, c0, y + 1, "MIC");
    static float hold2; meter(22, y, 62, 7, u->meter_in, &hold2, u->clip);
    oled_ftext_r(F_TINY, OLED_W - 2, y + 1, "CLICK: BACK", 0);
    oled_flush();
}

// Whatever main.c hands over, the drawing code gets numbers it can count on: finite, in range, and a state it knows.
// (A NaN angle or a loop length of minus one would otherwise decide how many pixels a loop draws.)
static float fin(float x, float lo, float hi) { return !(x >= lo) ? lo : x > hi ? hi : x; }   // a NaN lands on lo
static void clean(ui_t *v)
{
    v->state = v->state == REC_RECORDING || v->state == REC_PLAYING ? v->state : REC_IDLE;
    v->mode = v->mode == REC_MODE_GRAIN ? REC_MODE_GRAIN : REC_MODE_TAPE;
    v->speed_now = fin(v->speed_now, -16, 16); v->speed_cmd = fin(v->speed_cmd, -16, 16); v->spd_max = fin(v->spd_max, 0, 16);
    v->pitch_ratio = fin(v->pitch_ratio, 0.01f, 100); v->tilt_pitch = fin(v->tilt_pitch, -180, 180); v->tilt_roll = fin(v->tilt_roll, -180, 180);
    v->pos = fin(v->pos, 0, 1); v->rec_fill = fin(v->rec_fill, 0, 1); v->loop_s = fin(v->loop_s, 0, 999); v->max_s = clampi(v->max_s, 0, 999);
    v->meter_in = fin(v->meter_in, 0, 4); v->meter_out = fin(v->meter_out, 0, 4); v->cpu = clampi(v->cpu, 0, 999);
    v->param_frac = fin(v->param_frac, -1, 1); v->busy_frac = fin(v->busy_frac, 0, 1);
    v->toast_ms = clampi(v->toast_ms, 0, 60000); v->idle_ms = v->idle_ms < 0 ? 0 : v->idle_ms; v->still_ms = v->still_ms < 0 ? 0 : v->still_ms;
    v->peers = clampi(v->peers, 0, 99); v->param = clampi(v->param, 0, 998); v->param_n = clampi(v->param_n, 0, 999);
    v->jog = fin(v->jog, -16, 16); v->hand = fin(v->hand, 0, 1); v->filter = fin(v->filter, -1, 1); v->knob_st = clampi(v->knob_st, -99, 99);
    v->knob_mode = clampi(v->knob_mode, 0, UI_KNOB_N - 1); v->hand_mode = clampi(v->hand_mode, 0, UI_HAND_N - 1);
    v->menu_n = clampi(v->menu_n, 0, 999); v->menu_sel = clampi(v->menu_sel, 0, v->menu_n > 0 ? v->menu_n - 1 : 0);
    v->hand_mm = clampi(v->hand_mm, 0, 9999); v->chk_enc = clampi(v->chk_enc, -9999, 9999); v->chk_knob = clampi(v->chk_knob, -9999, 9999);
    for (int r = 0; r < UI_ROWS; r++) v->row_val[r][UI_VAL_N - 1] = 0;
    if (v->state != REC_RECORDING) v->waiting = false;
}

void ui_draw(const ui_t *in)
{
    ui_t v = *in;
    clean(&v);
    const ui_t *u = &v;
    clock_step(u);
    oled_dim(u->dim);
    if (splash()) return;
    if (u->check) { check_scene(u); return; }
    tape_motion(u);
    bool listed = u->menu_n > 0;
    bool saver = !u->menu && !u->busy_text && !u->note && u->toast_ms <= 0
              && ((u->state == REC_IDLE && u->idle_ms > 180000) || (u->state == REC_PLAYING && u->still_ms > 600000));
    if (saver) { last_state = u->state; screensaver(u); return; }
    if (u->state != last_state) {
        if ((u->state == REC_RECORDING && !u->waiting) || (u->state == REC_PLAYING && last_state == REC_RECORDING)) flash = 0.1f;
        last_state = u->waiting ? -2 : u->state;                // the take's first sound flashes too
    }
    if (u->clip) clip_flash = 0.3f; else if (clip_flash > 0) clip_flash -= dt;
    menu_t = glide(menu_t, u->menu ? 1.0f : 0.0f, 18.0f);
    if (menu_t < 0.02f && !u->menu) { menu_t = 0; list_sel = -1; edit_t = 0; }
    if (menu_t > 0.98f && u->menu) menu_t = 1;
    oled_clear();
    v.clip = clip_flash > 0;
    header(u, listed && menu_t > 0.5f);
    if (!(listed && menu_t >= 1.0f)) {                          // the tape, unless the list covers it all
        tape(u);
        if (u->busy_text) busy_scene(u);
        else if (u->note && !u->menu) note_scene(u->note);
        else {
            operator_(u);
            if (u->toast_ms > 0 && !u->menu) toast(u); else bottom_strip(u);
        }
    }
    if (menu_t > 0.0f) {
        float e = menu_t * (2.0f - menu_t);
        if (listed) menu_list(u, (int)((1.0f - e) * (float)(OLED_H - BODY_Y)));
        else menu_strip(u, e);
    }
    if (flash > 0) { flash -= dt; oled_invert(0, 0, OLED_W, OLED_H); }
    oled_flush();
}

void ui_fault(const char *l1, const char *l2)
{
    oled_clear();
    oled_ftext_c(F_MED, OLED_W / 2, 3, "VESSEL", 2);
    figure(20, 50, P_SHRUG, 1, 0);
    int w = oled_pill_w(F_MED, l1);
    oled_pill(F_MED, 30 + (OLED_W - 30 - w) / 2, 24, l1, true);
    oled_ftext_c(F_SMALL, 30 + (OLED_W - 30) / 2, 40, l2, 1);
    oled_hline_pat(0, 51, OLED_W, 0x55, true);
    oled_flush();
}
#endif
