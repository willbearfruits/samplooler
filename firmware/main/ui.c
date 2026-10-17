// The screen. A tape runs between two reels; the loop's waveform is the tape; a stick figure runs on
// it. The reels and the figure move with the real speed and direction, the figure suffers whatever
// the user does to the loop (flung at 8x, ghosted by the delay, juggling grains), and the odd boot
// gets its own gag. Fonts are TWOFOLD's proportional faces (font.h); the layout keeps the 16-row band
// that is yellow on two-colour panels for the header.
//
// 128x64 grid:  header 0..15 | figure zone 16..27 | tape 28..46 (edges 28 and 46, reels either side)
//               | bottom strip 48..63: tilt ball, speed, effects; or the menu strip, a toast, a note.
#include "ui.h"
#include "oled.h"
#include "recorder.h"
#include "audio.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#if OLED_ENABLE

static int   fr;                 // frames drawn, 20 per second
static float peak_hold;
static uint8_t env[OLED_W];
static float reel_a;             // reel angle, degrees, follows the tape
static float tape_x;             // tape edge dot phase
static float run_ph;             // the figure's leg cycle

static int   clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static bool  starts(const char *s, const char *p) { return s && strncmp(s, p, strlen(p)) == 0; }
static const char *upper(const char *s)        // pills shout; the small face keeps a few lowercase unit letters
{
    static char b[32]; int i = 0;
    for (; s[i] && i < 31; i++) b[i] = (s[i] >= 'a' && s[i] <= 'z') ? s[i] - 32 : s[i];
    b[i] = 0; return b;
}
static float __attribute__((unused)) ease_t(int t) { float p = t > 8 ? 1.0f : t / 8.0f; p = 1.0f - p; return 1.0f - p * p * p; }   // ease-out over 8 frames

// ---------------------------------------------------------------- pieces

// Segmented meter, LED style, with peak hold and a clip flash.
static void meter(int x, int y, int w, int h, float v, float *hold, bool clip)
{
    if (v > *hold) *hold = v; else *hold -= 0.02f;
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
    oled_hline_pat(x, y + 1, w, 0x55, true);
    int fw = (int)(frac * (float)(w - 1) + 0.5f);
    oled_rect(x, y, fw + 1, 3, true);
    oled_rect(x + fw, y - 2, 2, 7, true);
}

// A stick figure. (x, floor) is where the feet touch. dir +1 faces right, -1 left. 11 rows tall:
// head rows 0..2, body 3..7, legs 7..10. Poses carry their own arms and legs.
enum { P_STAND, P_RUN, P_MIC, P_CARRY, P_FLAT, P_SHRUG, P_JUMP, P_SLEEP, P_SIT };
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
    case P_RUN: {
        static const int8_t L[4][4] = { { 3, 0, -2, 0 }, { 1, -1, -1, -1 }, { -2, 0, 3, 0 }, { -1, -1, 1, -1 } };
        const int8_t *l = L[ph & 3];
        oled_line(x, lk, x + l[0] * dir, floor + l[1], true);
        oled_line(x, lk, x + l[2] * dir, floor + l[3], true);
        oled_line(x, hy + 4, x + (ph & 2 ? 3 : -3) * dir, hy + 5, true);   // arms pumping
        oled_line(x, hy + 4, x + (ph & 2 ? -3 : 3) * dir, hy + 6, true);
        break;
    }
    case P_MIC:
        oled_line(x, lk, x - 2, floor, true); oled_line(x, lk, x + 2, floor, true);
        oled_line(x, hy + 4, x + 3 * dir, hy + 2, true);        // arm up with the mic
        oled_disc(x + 4 * dir, hy + 1, 1, true);
        oled_line(x, hy + 4, x - 2 * dir, hy + 7, true);
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

// A reel: rim, hub, three spokes at angle a (degrees).
static void reel(int cx, int cy, int r, float a)
{
    oled_circle(cx, cy, r, true);
    for (int k = 0; k < 3; k++) {
        float t = (a + 120.0f * k) * 0.0174533f;
        oled_line(cx, cy, cx + (int)((r - 1) * cosf(t)), cy + (int)((r - 1) * sinf(t)), true);
    }
    oled_rect(cx - 1, cy - 1, 3, 3, false); oled_pixel(cx, cy, true);
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

static int   clip_flash;
static int   last_state = -1;
static int   flash;              // whole-screen invert frames left
static float menu_t;             // 0 closed .. 1 open
static int8_t  scope[AUDIO_SCOPE_N];
static int   gag, gag_t;         // idle gag in progress
static float fig_x = 64;         // where the figure is (it drifts with the tape)

static void tape_motion(const ui_t *u)
{
    float v = u->state == REC_RECORDING ? 1.0f : u->state == REC_PLAYING ? u->speed_now : 0.0f;
    reel_a += v * 9.0f;  while (reel_a >= 360) reel_a -= 360;  while (reel_a < 0) reel_a += 360;
    tape_x += v * 1.5f;  while (tape_x >= 4) tape_x -= 4;      while (tape_x < 0) tape_x += 4;
    run_ph += fabsf(v) * 0.5f; if (run_ph >= 4) run_ph -= 4;
}

static void header(const ui_t *u)
{
    char b[24];
    int x = 2;
    if (u->state == REC_RECORDING) {
        x += oled_pill(F_MED, x, 2, "REC", true) + 3;
        if ((fr / 4) & 1) oled_disc(x + 3, 8, 3, true); else oled_circle(x + 3, 8, 3, true);
        snprintf(b, sizeof b, "%ds", (int)(u->rec_fill * (float)u->max_s));
        oled_ftext(F_SMALL, x + 9, 5, b);
    } else if (u->state == REC_PLAYING) {
        x += oled_pill(F_MED, x, 2, "LOOP", true) + 3;
        snprintf(b, sizeof b, "%.1fs", (double)u->loop_s);
        oled_ftext(F_SMALL, x, 5, b);
    } else {
        oled_pill(F_MED, x, 2, "READY", false);
    }
    meter(92, 4, 36, 9, u->meter_in, &peak_hold, u->clip);
}

// The tape between the reels: the loop's envelope, the record fill, or the live scope.
static void tape(const ui_t *u)
{
    // edges, dotted, moving with the tape
    for (int x = TAPE_X0 - 2; x <= TAPE_X1 + 2; x++) if (((x - (int)tape_x) & 3) != 0) { oled_pixel(x, TAPE_TOP, true); oled_pixel(x, TAPE_BOT, true); }
    reel(REEL_L, TAPE_MID, 6, reel_a); reel(REEL_R, TAPE_MID, 6, reel_a);
    oled_line(REEL_L + 4, TAPE_MID - 5, TAPE_X0 - 2, TAPE_TOP, true); oled_line(REEL_L + 4, TAPE_MID + 5, TAPE_X0 - 2, TAPE_BOT, true);
    oled_line(REEL_R - 4, TAPE_MID - 5, TAPE_X1 + 2, TAPE_TOP, true); oled_line(REEL_R - 4, TAPE_MID + 5, TAPE_X1 + 2, TAPE_BOT, true);

    if (u->state == REC_IDLE) {                                 // live oscilloscope of the input
        audio_scope(scope);
        int py = TAPE_MID - scope[0] * (TAPE_HALF - 1) / 127;
        for (int x = 1; x < TAPE_W; x++) {
            int y = TAPE_MID - scope[(x * AUDIO_SCOPE_N) / TAPE_W] * (TAPE_HALF - 1) / 127;
            oled_line(TAPE_X0 + x - 1, py, TAPE_X0 + x, y, true);
            py = y;
        }
        return;
    }
    recorder_envelope(env, TAPE_W);
    int fill_x = u->state == REC_RECORDING ? (int)(u->rec_fill * (TAPE_W - 1)) : TAPE_W - 1;
    for (int x = 0; x < TAPE_W; x++) {
        if (u->state == REC_RECORDING && x > fill_x) { if ((x & 3) == 0) oled_pixel(TAPE_X0 + x, TAPE_MID, true); continue; }
        int col = u->state == REC_RECORDING ? (int)((float)x / (float)(fill_x ? fill_x : 1) * (TAPE_W - 1)) : x;
        int h = env[clampi(col, 0, TAPE_W - 1)] * (TAPE_HALF - 1) / 255;
        if (h == 0 && env[clampi(col, 0, TAPE_W - 1)]) h = 1;
        if (h > 0) oled_rect(TAPE_X0 + x, TAPE_MID - h, 1, 2 * h + 1, true); else oled_pixel(TAPE_X0 + x, TAPE_MID, true);
    }
    if (u->state == REC_RECORDING) {
        if ((fr / 3) & 1) oled_rect(TAPE_X0 + fill_x, TAPE_TOP + 1, 2, TAPE_BOT - TAPE_TOP - 1, true);
        return;
    }
    // playhead with a comet tail that lengthens with speed
    int hx = TAPE_X0 + (int)(u->pos * (TAPE_W - 1));
    float a = fabsf(u->speed_now);
    int tail = (int)(a * 1.5f); if (tail > 8) tail = 8;
    for (int k = 1; k <= tail; k++) {
        int x = u->speed_now >= 0 ? hx - k : hx + k;
        if (x < TAPE_X0) x += TAPE_W;
        if (x > TAPE_X1) x -= TAPE_W;
        int span = (TAPE_HALF - 1) - k * (TAPE_HALF - 1) / (tail + 1);
        oled_invert(x, TAPE_MID - span, 1, 2 * span + 1);
    }
    oled_invert(hx, TAPE_TOP + 1, 1, TAPE_BOT - TAPE_TOP - 1);
}

// The operator: what the loop is doing to them.
static void operator_(const ui_t *u)
{
    const int floor = FIG_FLOOR;
    if (u->state == REC_IDLE) {
        if (gag_t <= 0 && (fr % 60) == 0) { gag = 1 + (fr / 60) % 2; gag_t = 10; }   // a gag every 3 s
        figure(64, floor, P_STAND, 1, gag_t > 0 ? gag : 0);
        if (gag_t > 0) gag_t--;
        return;
    }
    if (u->state == REC_RECORDING) {
        figure(64, floor, P_MIC, 1, 0);
        if (u->rec_fill > 0.85f) {                              // tape nearly full: sweat
            int k = fr % 6;
            oled_pixel(60 - k / 2, floor - 12 + k, true); oled_pixel(69 + k / 2, floor - 11 + k, true);
        }
        if (u->clip) {                                          // the mic blew up
            for (int k = 0; k < 8; k++) { float t = k * 0.785f + fr * 0.3f; oled_pixel(68 + (int)(5 * cosf(t)), floor - 9 + (int)(5 * sinf(t)), true); }
        }
        return;
    }
    // playing: run on the tape, drift with it, come back from the edge
    float v = u->speed_now, a = fabsf(v);
    int dir = v >= 0 ? 1 : -1;
    fig_x += v * 0.6f;
    fig_x = clampf(fig_x, 28, 100);
    if (a < 0.3f) fig_x += (64 - fig_x) * 0.05f;               // wanders home when the tape crawls
    int x = (int)fig_x, ph = (int)run_ph;
    if (a >= 4.5f) {                                            // too fast: flung and tumbling
        tumble(x, floor - 6, fr * 0.6f * dir);
        if (fr & 2) oled_ftext(F_SMALL, x - 1, floor - 14, "!");
    } else if (u->fx_stutter) {                                 // stutter: the figure stutters too
        int j = (fr & 1) ? 2 : -2;
        figure(x + j, floor, P_RUN, dir, ph);
        oled_dither(x + j - 8, floor - 12, 16, 12, 2, false);
        figure(x - j, floor, P_RUN, dir, ph ^ 2);
    } else if (a < 0.08f) {
        figure(x, floor, P_STAND, dir, (fr / 10) & 1 ? 2 : 0);  // frozen tape: tapping a foot
    } else {
        if (u->fx_delay) {                                      // delay: echoes of the runner trail behind
            for (int k = 3; k >= 1; k--) {
                int gx = x - k * 9 * dir;
                figure(gx, floor, P_RUN, dir, (ph + 4 - k) & 3);
                if (k == 3) oled_dither(gx - 5, floor - 12, 11, 12, 2, false);   // the oldest one is fading
            }
        }
        figure(x, floor, P_RUN, dir, ph);
        if (u->mode == REC_MODE_GRAIN) {                        // grains: juggling
            for (int k = 0; k < 3; k++) {
                float t = fr * 0.35f + k * 2.094f;
                oled_rect(x + (int)(5 * cosf(t)) - 1, floor - 14 - (int)(3 * fabsf(sinf(t))), 2, 2, true);
            }
        }
    }
    if (u->clip) { oled_line(x - 2, floor - 13, x - 3, floor - 15, true); oled_line(x, floor - 13, x, floor - 16, true); oled_line(x + 2, floor - 13, x + 3, floor - 15, true); }
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

static void bottom_strip(const ui_t *u)
{
    const int y = STRIP_Y;
    char b[20];
    if (u->state == REC_IDLE) {                                 // the invitation marquee
        const char *msg = "HOLD TO RECORD   "  S_RIGHT " TILT TO PLAY   " S_RIGHT " ROLL FOR EFFECTS      ";
        int len = oled_ftext_w(F_SMALL, msg, 1) + 4, off = (fr * 2) % len;
        oled_ftext_ex(F_SMALL, 2 - off, y + 6, msg, 1);
        oled_ftext_ex(F_SMALL, 2 - off + len, y + 6, msg, 1);
        oled_rect(0, y, 2, 16, false); oled_rect(OLED_W - 2, y, 2, 16, false);
        return;
    }
    attitude(8, y + 8, 7, u->has_imu ? u->tilt_pitch : 0, u->has_imu ? u->tilt_roll : 0, u->has_imu && u->tilt_on);
    float a = fabsf(u->state == REC_PLAYING ? u->speed_now : u->speed_cmd);
    snprintf(b, sizeof b, "%s%.2f", (u->state == REC_PLAYING ? u->speed_now : u->speed_cmd) < 0 ? "-" : "", (double)a);
    int x = oled_ftext(F_MED, 19, y + 3, b);
    x = oled_ftext(F_SMALL, x + 1, y + 7, S_RIGHT S_RIGHT) ;
    if (!u->tilt_on) x = oled_ftext(F_SMALL, x + 3, y + 5, u->has_joy ? "JOY" : "1X");
    if (u->frozen) x += oled_pill(F_SMALL, x + 3, y + 3, "HOLD", true) + 1;         // the tilt is latched
    if (u->mode == REC_MODE_GRAIN) {
        snprintf(b, sizeof b, "GRN %+.0f", (double)(12.0f * log2f(u->pitch_ratio)));
        oled_pill(F_SMALL, x + 4, y + 3, b, true);
    }
    // right corner: effect pills, else the output level and the cpu
    int rx = OLED_W - 1;
    if (u->fx_stutter) rx -= oled_pill(F_SMALL, rx - oled_pill_w(F_SMALL, "STUTTER"), y + 3, "STUTTER", (fr & 2) != 0) + 2;
    if (u->fx_delay)   rx -= oled_pill(F_SMALL, rx - oled_pill_w(F_SMALL, "DELAY"), y + 3, "DELAY", false) + 2;
    if (!u->fx_delay && !u->fx_stutter) {
        snprintf(b, sizeof b, "%d%%", u->cpu);
        oled_ftext_r(F_TINY, OLED_W - 1, y + 3, b, 0);
        oled_rframe(98, y + 11, 30, 4, 1);
        int w = (int)(u->meter_out * 28.0f); if (w > 28) w = 28;
        oled_rect(99, y + 12, w, 2, true);
    }
}

// The menu strip: the chosen setting's name in a pill (outlined while choosing, filled while editing),
// its value at the right, and a slider or a hint underneath.
static void menu_strip(const ui_t *u)
{
    float target = u->menu ? 1.0f : 0.0f;
    menu_t += 0.35f * (target - menu_t);
    if (menu_t < 0.02f && !u->menu) { menu_t = 0; return; }
    float e = menu_t * (2.0f - menu_t);
    int top = STRIP_Y + (int)((1.0f - e) * 18.0f);
    oled_rect(0, top, OLED_W, OLED_H - top, false);
    oled_hline_pat(0, top, OLED_W, 0x55, true);
    char b[16];
    snprintf(b, sizeof b, "%d/%d", u->param + 1, u->param_n);
    oled_ftext(F_TINY, 2, top + 3, b);
    int nx = oled_ftext_w(F_TINY, b, 0) + 6;
    oled_pill(F_SMALL, nx, top + 2, upper(u->param_name), u->menu_edit);
    int vw = oled_ftext_w(F_MED, u->param_val, 0);
    oled_ftext(F_MED, OLED_W - 2 - vw, top + 2, u->param_val);
    if (u->menu_edit && u->param_frac >= 0) slider(4, top + 13, OLED_W - 8, u->param_frac);
    else if (u->param_frac < 0) oled_ftext_c(F_TINY, OLED_W / 2, top + 12, "PRESS TO RUN", 1);
    else oled_ftext_c(F_TINY, OLED_W / 2, top + 12, "TURN: CHOOSE   PRESS: EDIT", 1);
}

static void toast(const ui_t *u)
{
    int t = u->toast_ms;
    if (t <= 0) return;
    int y = STRIP_Y;
    if (t < 150) y += (150 - t) * 17 / 150;
    oled_rect(0, y, OLED_W, OLED_H - y, false);
    oled_pill(F_SMALL, 2, y + 1, upper(u->param_name), true);
    oled_ftext_r(F_MED, OLED_W - 3, y + 1, u->param_val, 0);
    if (u->param_frac >= 0) slider(4, y + 12, OLED_W - 8, u->param_frac);
}

// A note in the strip, with its gag in the figure zone: the result of a job, or why we rebooted.
static void note_scene(const char *s)
{
    const int floor = FIG_FLOOR;
    oled_rect(0, 16, OLED_W, 12, false);
    if (starts(s, "CRASH")) {                                   // flattened by a falling cassette, stars
        figure(60, floor, P_FLAT, 1, fr / 4);
        cassette(66, floor - 9);
        for (int k = 0; k < 3; k++) { float t = fr * 0.25f + k * 2.094f; oled_pixel(53 + (int)(5 * cosf(t)), floor - 6 + (int)(2 * sinf(t)), true); }
    } else if (starts(s, "WATCHDOG")) {                         // the dog got them
        figure(50, floor, P_RUN, 1, fr & 3);
        oled_line(66, floor - 1, 72, floor - 1, true); oled_line(66, floor - 1, 66, floor, true); oled_line(72, floor - 1, 72, floor, true);
        oled_rect(72, floor - 4, 3, 3, true); oled_pixel(75, floor - 3, true); oled_line(65, floor - 3, 63, floor - 5, true);
        if (fr & 4) oled_ftext(F_TINY, 77, floor - 10, "WOOF");
    } else if (starts(s, "BROWNOUT")) {                         // the lights went out
        figure(64, floor, P_STAND, 1, 0);
        if (fr % 7 < 4) oled_circle(72, floor - 12, 2, true); else oled_pixel(72, floor - 12, true);
        oled_rect(71, floor - 9, 3, 2, true);
    } else if (starts(s, "SAVED") || starts(s, "LOADED")) {      // hooray
        figure(64, floor + ((fr & 2) ? 0 : -2), P_JUMP, 1, 0);
    } else if (starts(s, "NOTHING") || starts(s, "SLOT") || strstr(s, "FAILED")) {
        figure(64, floor, P_SHRUG, 1, 0);
    } else figure(64, floor, P_STAND, 1, 0);
    const int y = STRIP_Y;
    oled_rect(0, y, OLED_W, OLED_H - y, false);
    int w = oled_pill_w(F_SMALL, s); if (w > OLED_W) w = OLED_W;
    oled_pill(F_SMALL, (OLED_W - w) / 2, y + 3, s, true);
}

// A slot job: the operator carries the cassette to the shelf (save) or fetches it (load).
static void busy_scene(const ui_t *u)
{
    const int floor = FIG_FLOOR;
    bool save = starts(u->busy_text, "SAV");
    oled_rect(0, 16, OLED_W, 12, false);
    int x = save ? 20 + (int)(u->busy_frac * 76) : 96 - (int)(u->busy_frac * 76);
    figure(x, floor, P_CARRY, save ? 1 : -1, fr / 3);
    cassette(x - 6, floor - 18 + ((fr / 3) & 1));
    oled_rframe(104, floor - 11, 20, 11, 1);                    // the shelf
    oled_rect(106, floor - 9, 16, 1, true);
    const int y = STRIP_Y;
    oled_rect(0, y, OLED_W, OLED_H - y, false);
    oled_pill(F_SMALL, 2, y + 1, u->busy_text, true);
    slider(4, y + 12, OLED_W - 8, u->busy_frac);
}

// Splash: the operator carries the tape in, trips over the reel, the title lands.
static bool splash(void)
{
    const int N = 46;
    if (fr >= N) return false;
    oled_clear();
    const int floor = 44;
    oled_hline_pat(0, floor + 1, OLED_W, 0x55, true);
    reel(44, floor - 4, 4, fr * 5.0f);
    if (fr < 22) {
        int x = -8 + fr * 2;
        figure(x, floor, P_CARRY, 1, fr / 2);
        cassette(x - 6, floor - 18 + ((fr / 2) & 1));
    } else {
        int t = fr - 22;                                        // the fall
        figure(40 + (t < 3 ? t : 3), floor, P_FLAT, 1, t / 3);
        float ft = t < 12 ? t / 12.0f : 1.0f;                   // the cassette flies in an arc and lands
        int cx = 36 + (int)(ft * 50), cy = floor - 18 - (int)(28 * ft * (1.0f - ft)) + (int)(ft * 10);
        cassette(cx, cy);
        if (t >= 12) {
            int ty = 64 - (int)(ease_t(t - 12) * 48);
            oled_rect(0, ty - 2, OLED_W, 16, false);
            oled_ftext_c(F_MED, OLED_W / 2, ty, "SAMPLOOLER", 2);
            if (t > 18) oled_ftext_c(F_TINY, OLED_W / 2, ty + 11, "TILT TAPE LOOPER", 1);
        }
    }
    oled_flush();
    return true;
}

static void screensaver(const ui_t *u)
{
    (void)u;
    oled_clear();
    int x = 20 + (fr / 40) % 88, floor = 30 + (fr / 97) % 20;    // drifts: no burn-in
    oled_hline_pat(x - 12, floor + 1, 24, 0x55, true);
    figure(x, floor, P_SLEEP, 1, 0);
    for (int k = 0; k < 3; k++) {
        int t = (fr / 3 + k * 7) % 21;
        oled_ftext(k == 0 ? F_TINY : F_SMALL, x - 6 + t / 3, floor - 4 - t, "Z");
    }
    oled_flush();
}

void ui_draw(const ui_t *u)
{
    fr++;
    if (splash()) return;
    if (u->idle_ms > 180000 && u->state == REC_IDLE) { screensaver(u); return; }
    if (u->state != last_state) {
        if (u->state == REC_RECORDING || (u->state == REC_PLAYING && last_state == REC_RECORDING)) flash = 2;
        last_state = u->state;
    }
    if (u->clip) clip_flash = 6; else if (clip_flash) clip_flash--;
    tape_motion(u);
    oled_clear();
    ui_t v = *u; v.clip = clip_flash > 0;
    header(&v);
    tape(&v);
    if (u->busy_text) busy_scene(&v);
    else if (u->note && !u->menu) note_scene(u->note);
    else {
        operator_(&v);
        if (u->toast_ms > 0 && !u->menu) toast(&v); else bottom_strip(&v);
    }
    menu_strip(&v);
    if (flash > 0) { flash--; oled_invert(0, 0, OLED_W, OLED_H); }
    oled_flush();
}

void ui_fault(const char *l1, const char *l2)
{
    oled_clear();
    oled_ftext_c(F_MED, OLED_W / 2, 3, "SAMPLOOLER", 2);
    figure(20, 50, P_SHRUG, 1, 0);
    int w = oled_pill_w(F_MED, l1);
    oled_pill(F_MED, 30 + (OLED_W - 30 - w) / 2, 24, l1, true);
    oled_ftext_c(F_SMALL, 30 + (OLED_W - 30) / 2, 40, l2, 1);
    oled_hline_pat(0, 51, OLED_W, 0x55, true);
    oled_flush();
}
#endif
