// DWELL's screens (inst_dwell.h is the contract; D is what instrument.c tells us each tick). Drawing only, in the UI
// task, nothing per sample.
//
//   the header    the lead's ratio in the pill (REC or PASS, filled, while that runs); the core draws the rest
//   the lattice   rows 16..45: a window on the lattice of ratios, five columns (the sevenths, c -2..2) by three rows
//                 (the fifths, b), scrolled so the lead's row sits in the middle. The lead's column is the widest
//                 (a fisheye: the names near you are whole, far cells too long for their column are a dot). A cell
//                 that sounds is a filled pill with its ratio in black; the lead's pill has a frame round it; the
//                 cursor (the tilt itself) is a notch that slides under the cells; 1/1 has the root's base line.
//                 Pills fade in and out with their tone's level (a dither), pulse at the beat when DENSE, are hollow
//                 when another board sounds them (SPREAD). A new cell flashes once; a knock sends a ripple up from
//                 the floor through everything.
//   the floor     rows 46..63 (the core covers them for the menu, so only what you can do without for a moment):
//                 a figure lying on the floor listening (a Dream House visitor), breathing slowly, still at rest,
//                 lifting its head at a strum, sitting up to sing (REC) or to listen to the room (PASS); the room's
//                 standing wave beside it, its wavelength the lead's ratio, its height the output; the link's lamps;
//                 the readout: the ratio in F_MED with its cents off equal temperament, 1/1 in Hz, the beat rate when
//                 dense, the difference tone when it sounds, REST when resting
//   REC           the floor says SING ONE NOTE, a level bar and the seconds; then LISTENING
//   ROOM PASS     a scene of its own: THE ROOM, PASS n OF m, one square per pass, the pass's progress, the last
//                 recording's envelope, LISTENING / PLAYING, LISTENING; the figure sits up
//   the splash    DWELL opens out from a point while a lattice of dots spreads from the centre; the figure lies down;
//                 the tagline
//   the saver     the sounding ratios drifting across a dark screen
#include "inst_dwell.h"
#include "ui.h"
#include "dsp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LAT_Y0    16                 // the lattice window
#define LAT_Y1    46                 // (exclusive)
#define ROW_H     10
#define LEAD_CY   30                 // the lead's row's centre
#define PILL_H    9
#define FLOOR_Y   62                 // the floor line
#define STRIP_Y   46                 // the bottom strip the core may cover

// ---------------------------------------------------------------- state
static float scroll_b;               // the row in the window's middle, eased to the lead's
static float focus_c = 0.0f;         // the wide column, eased to the lead's
static float colw[5], colx[5];       // this frame's column widths and centres (floats; rounded when drawn)
static int   flash_b, flash_c, flash_n;
static struct { float r; int life; } ripple[3];
static float head_lift, breath, chest;
static uint32_t fr;                  // frames drawn
static float env_room[128];          // the room loop's envelope, 0..1 per column
static int   env_pass = -1, env_frames;   // which pass it came from
static bool  env_have;

// ---------------------------------------------------------------- primitives
// a pill of our own (oled_pill's tracking is too wide for a cell): PILL_H rows, corners rounded
static int pill_inset(int j) { return j == 0 || j == PILL_H - 1 ? 2 : j == 1 || j == PILL_H - 2 ? 1 : 0; }
static void pill_rows(int x, int y, int w, bool on)
{
    for (int j = 0; j < PILL_H; j++) { int in = pill_inset(j); oled_rect(x + in, y + j, w - 2 * in, 1, on); }
}
// the pill's text in black on white over its first `fw` columns (the rest stays white text on dark)
static void pill_invert(int x, int y, int w, int fw)
{
    for (int j = 0; j < PILL_H; j++) {
        int in = pill_inset(j), x0 = x + in, x1 = x + w - in;
        if (x1 > x + fw) x1 = x + fw;
        if (x1 > x0) oled_invert(x0, y + j, x1 - x0, 1);
    }
}
// an outlined pill with its ratio, filled from the left by `fill` (0: hollow, 1: solid white with black text)
static void pill_text(int x, int y, int w, const char *s, float fill)
{
    pill_rows(x, y, w, false);
    oled_ftext_ex(F_TINY, x + 2, y + 2, s, 0);
    int fw = (int)(clampf(fill, 0.0f, 1.0f) * (float)w + 0.5f);
    if (fw > 0) pill_invert(x, y, w, fw);
    oled_rframe(x, y, w, PILL_H, 3);
}

// a dotted circle (every other pixel), clipped to rows y0..y1
static void dot_circle(int cx, int cy, int r, int y0, int y1)
{
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        const int px[8] = { cx + x, cx + y, cx - y, cx - x, cx - x, cx - y, cx + y, cx + x };
        const int py[8] = { cy + y, cy + x, cy + x, cy + y, cy - y, cy - x, cy - x, cy - y };
        for (int k = 0; k < 8; k++) if (py[k] >= y0 && py[k] <= y1 && ((px[k] + py[k]) & 1) == 0) oled_pixel(px[k], py[k], true);
        y++;
        if (err < 0) err += 2 * y + 1; else { x--; err += 2 * (y - x) + 1; }
    }
}

// ---------------------------------------------------------------- the figure
// lying on the floor, head on the left: 22 x 4 on the rows above `base` (the floor's row); the head lifts, the chest rises
static void figure_lying(int x, int base, float lift, float chest_up)
{
    int hy = base - 4 - (int)(lift + 0.5f);                        // the head's top row
    oled_rect(x + 1, hy, 3, 1, true);                               // the head: a 5 x 4 blob
    oled_rect(x, hy + 1, 5, 2, true);
    oled_rect(x + 1, hy + 3, 3, 1, true);
    if (lift >= 1.0f) oled_pixel(x + 5, base - 2, true);           // the neck, when the head is up
    oled_rect(x + 6, base - 2, 14, 2, true);                        // the body and legs
    oled_rect(x + 20, base - 3, 2, 3, true);                        // the feet, toes up
    if (chest_up > 0.5f) oled_rect(x + 7, base - 3, 5, 1, true);   // the chest, on the in-breath
}
// sitting up on the floor, legs out to the right: 10 x 10 above `base`
static const char *const fig_sit[10] = {
    "..###.....",
    ".#####....",
    ".#####....",
    "..###.....",
    "..###.....",
    "..###.....",
    "..###...##",
    "..###...##",
    "..#######.",
    "..#######.",
};
static void figure_sitting(int x, int base) { ui_sprite(x, base - 10, fig_sit, 10, false, true); }

// ---------------------------------------------------------------- the lattice window
static void columns(int limit)
{
    if (limit == LIM_3) { for (int i = 0; i < 5; i++) colw[i] = 0.0f; colw[2] = 44.0f; colx[2] = 64.0f; return; }
    float w[5], sum = 0.0f;
    for (int i = 0; i < 5; i++) {                                   // 30 px under the lead, 27 beside it, 22 far off
        float d = fabsf((float)(i - 2) - focus_c);
        w[i] = 22.0f + (d < 1.0f ? 8.0f - 3.0f * d : d < 2.0f ? 5.0f - 5.0f * (d - 1.0f) : 0.0f);
        sum += w[i];
    }
    float x = 0.0f;
    for (int i = 0; i < 5; i++) { colw[i] = w[i] * 128.0f / sum; colx[i] = x + colw[i] * 0.5f; x += colw[i]; }
}
static float col_x(float c)                                         // a column centre for a fractional c
{
    if (D.limit == LIM_3) return colx[2];
    c = clampf(c, -2.0f, 2.0f);
    int i = (int)floorf(c); if (i > 1) i = 1;
    float t = c - (float)i;
    return colx[i + 2] + (colx[i + 3] - colx[i + 2]) * t;
}
static float row_y(float b) { return (float)LEAD_CY - (b - scroll_b) * (float)ROW_H; }

static const dw_slot_t *slot_at(int b, int c)
{
    const dw_slot_t *best = NULL;
    for (int i = 0; i < NV; i++) {
        const dw_slot_t *s = &D.slot[i];
        if (!s->on || s->b != b || s->c != c) continue;
        if (!best || s->is_lead || (s->lvl > best->lvl && !best->is_lead)) best = s;
    }
    return best;
}

// how full a sounding pill is: its level (a tone swelling in fills it from the left), breathing at the beat when dense
static float pill_fill(const dw_slot_t *s)
{
    float l = clampf(s->lvl / 0.65f, 0.0f, 1.0f);
    if (D.dense > 0.1f) l *= 1.0f - 0.35f * D.dense * (0.5f - 0.5f * sin_turns(eng_beat_phase));
    return l;
}

static void lattice(void)
{
    int cmax = D.limit == LIM_3 ? 0 : LAT_C;
    for (int b = LAT_B; b >= -LAT_B; b--) {
        float fy = row_y((float)b);
        int cy = (int)lroundf(fy);
        if (cy + 5 < LAT_Y0 || cy - 5 >= LAT_Y1) continue;
        for (int c = -cmax; c <= cmax; c++) {
            int cw = (int)colw[c + 2], cx = (int)lroundf(colx[c + 2]);
            char nm[12]; lat_name(D.limit, b, c, nm, sizeof nm);
            int tw = oled_ftext_w(F_TINY, nm, 0);
            const dw_slot_t *s = slot_at(b, c);
            bool lead_cell = b == D.lead_b && c == D.lead_c;
            int pw = tw + 4, px = cx - pw / 2, py = cy - PILL_H / 2;
            if (s) {
                bool hollow = D.spread && s->lvl < 0.01f;             // another board sounds it
                pill_text(px, py, pw, nm, hollow ? 0.0f : pill_fill(s));
            } else if (tw <= cw - 1) {
                oled_rect(cx - tw / 2 - 1, cy - 3, tw + 2, 7, false);
                oled_ftext_ex(F_TINY, cx - tw / 2, cy - 2, nm, 0);
            } else {
                oled_rect(cx - 1, cy - 1, 2, 2, true);              // too long for this far: a dot (its name when you reach it)
            }
            if (lead_cell) oled_rframe(px - 2, py - 1, pw + 4, PILL_H + 2, 4);
            if (b == 0 && c == 0 && D.root_on) oled_rect(cx - 5, cy + 6, 11, 1, true);   // the root's base line under 1/1
        }
    }
    // the new cell's flash: a frame opening out and fading
    if (flash_n > 0) {
        int cx = (int)lroundf(col_x((float)flash_c)), cy = (int)lroundf(row_y((float)flash_b));
        int k = 12 - flash_n;
        if (k < 6 || (k & 1)) oled_rframe(cx - 12 - k, cy - 5 - k / 2, 24 + 2 * k, 11 + k, 4);
        flash_n--;
    }
    // the cursor: a marker hanging off the bottom of the cells, where the tilt is now
    {
        int mx = (int)lroundf(col_x(D.cur_c)), my = (int)lroundf(row_y(D.cur_b));
        oled_invert(mx - 1, my + 4, 3, 1);
        oled_rect(mx - 1, my + 5, 3, 1, true);
    }
}

// the room's envelope, faint behind the lattice when the source is the room
static void room_shadow(void)
{
    for (int x = 0; x < 128; x += 2) {
        int h = (int)(env_room[x] * 13.0f);
        if (h < 1) continue;
        oled_pixel(x, LEAD_CY - h, true); oled_pixel(x, LEAD_CY + h, true);
    }
}

// ---------------------------------------------------------------- the floor strip
static void lamps(int x, int y)
{
    int n = D.peers + 1; if (n > 8) n = 8;
    for (int i = 0; i < n; i++) {
        bool lit = !D.spread || i == D.rank;
        if (lit) oled_rect(x + i * 5, y, 3, 3, true); else oled_frame(x + i * 5, y, 3, 3);
    }
    if (D.follow) oled_ftext_ex(F_TINY, x + n * 5 + 2, y - 1, "FOLLOW", 0);
}

static void ribbon(int x0, int x1, int cy, float amp, float lam, float stand)
{
    if (amp < 0.5f) { oled_hline_pat(x0, cy, x1 - x0, 0x11, true); return; }
    for (int x = x0; x < x1; x += 2) {
        float s = sin_turns((float)(x - x0) / lam) * stand;
        oled_pixel(x, cy + (int)lroundf(amp * s), true);
    }
}

static void readout(void)
{
    char b[32];
    int x = 66;
    const char *nm = D.lead_name[0] ? D.lead_name : "1/1";
    int xe = oled_ftext_ex(F_MED, x, 47, nm, 0);
    if (fabsf(D.lead_et) < 0.5f) snprintf(b, sizeof b, "0C"); else snprintf(b, sizeof b, "%+dC", (int)lroundf(D.lead_et));
    if (xe + 4 + oled_ftext_w(F_TINY, b, 0) <= 128) oled_ftext_ex(F_TINY, xe + 4, 49, b, 0);
    snprintf(b, sizeof b, "%dHZ", (int)lroundf(D.fund_hz));
    oled_ftext_ex(F_TINY, x, 57, b, 0);
    // the right slot: REST, else DIFF and BEAT taking turns when both
    const char *r = NULL;
    if (D.rest) r = "REST";
    else {
        bool beat = D.dense > 0.1f, diff = D.diff_hz > 0.5f;
        bool show_diff = diff && (!beat || ((fr / 50) & 1));
        if (show_diff) { snprintf(b, sizeof b, "DIFF %dHZ", (int)lroundf(D.diff_hz)); r = b; }
        else if (beat) { snprintf(b, sizeof b, "BEAT %.*fHZ", D.beat_hz < 1.0f ? 2 : 1, (double)D.beat_hz); r = b; }
    }
    if (r) oled_ftext_r(F_TINY, 127, 57, r, 0);
}

static void floor_strip(const view_t *v)
{
    (void)v;
    oled_rect(0, STRIP_Y, 128, 64 - STRIP_Y, false);
    oled_hline_pat(0, FLOOR_Y, 128, 0xFF, true);
    bool sit = D.rec || D.analysing;
    if (sit) {
        figure_sitting(3, FLOOR_Y);
        if (D.rec) {
            oled_ftext_ex(F_SMALL, 18, 47, "SING ONE NOTE", 1);
            float l = clampf(eng_take_level * 3.0f, 0.0f, 1.0f);
            oled_frame(18, 55, 72, 5);
            oled_rect(19, 56, (int)(l * 70.0f), 3, true);
            char b[12]; snprintf(b, sizeof b, "%.1fS", (double)D.rec_s);
            oled_ftext_r(F_TINY, 127, 55, b, 0);
        } else {
            oled_ftext_ex(F_SMALL, 18, 47, "LISTENING", 1);
            int k = (int)(fr % 12) * 6;
            oled_hline_pat(18, 57, 72, 0x33, true);
            oled_rect(18 + k, 56, 4, 3, true);
        }
        return;
    }
    // the figure lying down: it breathes slowly, is still at rest, lifts its head at a strum
    if (!D.rest) breath = wrap1(breath + 0.012f);
    float lift_want = D.strum > 0.05f ? clampf(D.strum * 3.0f, 0.0f, 2.0f) : 0.0f;
    head_lift += (lift_want - head_lift) * (lift_want > head_lift ? 0.6f : 0.12f);
    chest = D.rest ? 0.0f : 0.5f + 0.5f * sin_turns(breath);
    figure_lying(3, FLOOR_Y, head_lift, chest);
    // the room's standing wave: wavelength the lead's ratio, height the output, swinging at the beat when dense
    float ratio = lat_ratio(D.limit, D.lead_b, D.lead_c);
    float stand = D.dense > 0.1f ? sin_turns(eng_beat_phase) : sin_turns((float)fr * 0.006f);
    float amp = clampf(eng_out_lvl * 18.0f, 0.0f, 4.5f) * (D.rest ? 0.3f : 1.0f);
    ribbon(28, 62, 56, amp, 20.0f / ratio, stand);
    if (D.peers > 0) lamps(28, 47);
    readout();
}

// ---------------------------------------------------------------- ROOM PASS: its own scene
static void room_env_build(const int16_t *loop, int n)
{
    if (!loop || n <= 0) { env_have = false; return; }
    int step = n / 128, sub = step > 64 ? step / 64 : 1;             // at most 64 reads per column
    if (step < 1) step = 1;
    float pk = 1.0f;
    for (int x = 0; x < 128; x++) {
        int a = x * step, m = 0;
        for (int i = a; i < a + step && i < n; i += sub) { int v = loop[i]; if (v < 0) v = -v; if (v > m) m = v; }
        env_room[x] = (float)m / 32768.0f;
        if (env_room[x] > pk) pk = env_room[x];
    }
    float pkf = 0.0f; for (int x = 0; x < 128; x++) if (env_room[x] > pkf) pkf = env_room[x];
    if (pkf > 0.0f) for (int x = 0; x < 128; x++) env_room[x] /= pkf;
    env_have = true;
}
// for the host's tests, which have no room: feed a loop as if eng_room_loop() returned it
void scene_room_wave(const int16_t *loop, int frames) { room_env_build(loop, frames); env_pass = -2; }

static void room_env_refresh(void)
{
    int n; const int16_t *l = eng_room_loop(&n);
    if (!l) return;
    if (env_pass == D.room_pass && env_frames == n) return;
    env_pass = D.room_pass; env_frames = n;
    room_env_build(l, n);
}

static void room_scene(void)
{
    char b[24];
    room_env_refresh();
    oled_ftext_ex(F_SMALL, 2, 18, "THE ROOM", 1);
    snprintf(b, sizeof b, "PASS %d OF %d", D.room_pass + 1, D.room_passes);
    oled_ftext_r(F_MED, 126, 17, b, 0);
    // one square per pass: done ones filled, the running one blinking, the rest hollow
    int n = D.room_passes < 1 ? 1 : D.room_passes > 30 ? 30 : D.room_passes;
    int pitch = n > 16 ? 4 : 7, sz = n > 16 ? 3 : 5, x0 = 64 - (n * pitch - (pitch - sz)) / 2;
    for (int i = 0; i < n; i++) {
        int x = x0 + i * pitch;
        if (i < D.room_pass || (i == D.room_pass && ((fr / 6) & 1))) oled_rect(x, 28, sz, sz, true);
        else oled_frame(x, 28, sz, sz);
    }
    // this pass's progress
    oled_frame(2, 35, 124, 4);
    oled_rect(3, 36, (int)(clampf(D.room_frac, 0.0f, 1.0f) * 122.0f), 2, true);
    // the last recording's envelope
    if (env_have) {                                                 // its edges solid, its body a grey
        for (int x = 2; x < 126; x++) {
            int h = (int)(env_room[x] * 6.0f + 0.5f);
            if (!h) { oled_pixel(x, 46, true); continue; }
            oled_pixel(x, 46 - h, true); oled_pixel(x, 46 + h, true);
            for (int y = 47 - h; y < 46 + h; y++) if (((x + y) & 1) == 0) oled_pixel(x, y, true);
        }
    } else oled_hline_pat(2, 46, 124, 0x55, true);
    figure_sitting(3, FLOOR_Y);
    oled_hline_pat(0, FLOOR_Y, 128, 0xFF, true);
    oled_ftext_ex(F_SMALL, 18, 55, D.room_pass == 0 ? "LISTENING" : "PLAYING, LISTENING", 1);
}

// ---------------------------------------------------------------- the whole frame
void scene_knock(float s)
{
    for (int i = 0; i < 3; i++) if (ripple[i].life <= 0) { ripple[i].r = 3.0f; ripple[i].life = 18 + (int)(s * 10.0f); return; }
}
void scene_cell_new(int b, int c) { flash_b = b; flash_c = c; flash_n = 12; }

static void ripples(void)
{
    for (int i = 0; i < 3; i++) {
        if (ripple[i].life <= 0) continue;
        ripple[i].life--; ripple[i].r += 3.2f;
        if (ripple[i].life > 8 || (ripple[i].life & 1)) dot_circle(6, FLOOR_Y - 3, (int)ripple[i].r, LAT_Y0, FLOOR_Y - 1);
    }
}

void scene_draw(const view_t *v)
{
    fr++;
    float want_b = clampf((float)D.lead_b, -(float)(LAT_B - 1), (float)(LAT_B - 1));   // at the edge the window stays full
    scroll_b += (want_b - scroll_b) * 0.15f;
    focus_c += ((float)D.lead_c - focus_c) * 0.2f;
    columns(D.limit);
    const char *nm = D.lead_name[0] ? D.lead_name : "1/1";
    if (D.room_state == ROOM_PASSING) {
        room_scene();
        oled_rect(0, 0, 128, 16, false);
        ui_head("PASS", true);
        return;
    }
    if (D.source == SRC_ROOM && env_have) room_shadow();
    lattice();
    if (D.rest) oled_dither(0, LAT_Y0, 128, LAT_Y1 - LAT_Y0, 2, false);
    if (v->cover > 0) oled_rect(0, STRIP_Y, 128, 64 - STRIP_Y, false);
    else floor_strip(v);
    ripples();
    oled_rect(0, 0, 128, 16, false);
    ui_head(D.rec ? "REC" : nm, D.rec);
}

// ---------------------------------------------------------------- the splash: the word opens out, the lattice spreads, the figure lies down
bool scene_splash(int f)
{
    if (f >= 90) return false;
    float t = (float)f / 90.0f;
    // a lattice of dots spreading from the centre
    float reach = ease_out(clampf((float)f / 45.0f, 0.0f, 1.0f)) * 80.0f;
    for (int b = -4; b <= 4; b++) for (int c = -5; c <= 5; c++) {
        float x = 64.0f + (float)c * 12.0f, y = 32.0f + (float)b * 7.0f;
        float d = sqrtf((x - 64.0f) * (x - 64.0f) + (y - 32.0f) * (y - 32.0f) * 2.0f);
        if (d <= reach && y >= 17.0f && y <= 58.0f) oled_pixel((int)x, (int)y, true);
    }
    // DWELL, from a point: a window opening out from the middle over the word
    int w = oled_ftext_w(F_MED, "DWELL", 3);
    oled_rect(64 - w / 2 - 2, 20, w + 4, 11, false);
    oled_ftext_ex(F_MED, 64 - w / 2, 21, "DWELL", 3);
    int open = (int)(ease_out(clampf((float)f / 30.0f, 0.0f, 1.0f)) * (float)(w / 2 + 2));
    oled_rect(0, 20, 64 - open, 11, false);
    oled_rect(64 + open, 20, 64 - open, 11, false);
    // the figure: sitting, then lying down
    oled_rect(0, 50, 128, 14, false);
    oled_hline_pat(0, FLOOR_Y, 128, 0xFF, true);
    int fx = 52;
    if (f < 40) figure_sitting(fx, FLOOR_Y);
    else if (f < 46) {                                             // half way: leaning back
        oled_rect(fx + 1, 55, 3, 1, true); oled_rect(fx, 56, 5, 2, true); oled_rect(fx + 1, 58, 3, 1, true);
        oled_line(fx + 5, 58, fx + 9, 61, true); oled_rect(fx + 9, 60, 9, 2, true); oled_rect(fx + 17, 59, 2, 3, true);
    } else figure_lying(fx, FLOOR_Y, 0.0f, 0.5f + 0.5f * sin_turns((float)f * 0.02f));
    if (f > 55) {
        int k = (f - 55) * 2;
        const char *tag = "A DRONE YOU LIVE INSIDE";
        char b[32]; snprintf(b, sizeof b, "%.*s", k, tag);
        oled_ftext_c(F_TINY, 64, 40, b, 1);
    }
    (void)t;
    return true;
}

// ---------------------------------------------------------------- the screensaver: the sounding ratios drift by
void scene_saver(uint32_t f)
{
    int n = 0;
    for (int i = 0; i < NV; i++) {
        const dw_slot_t *s = &D.slot[i];
        if (!s->on || !s->name[0]) continue;
        float sp = 0.25f + 0.08f * (float)i;
        int tw = oled_ftext_w(F_TINY, s->name, 0);
        int x = (int)fmodf((float)f * sp + (float)i * 37.0f, (float)(128 + tw + 20)) - tw - 10;
        int y = 8 + i * 9 + (int)(4.0f * sin_turns((float)f * 0.003f + (float)i * 0.17f));
        if (s->is_lead) pill_text(x - 2, y - 2, tw + 4, s->name, 0.0f);
        else oled_ftext_ex(F_TINY, x, y, s->name, 0);
        n++;
    }
    if (!n) oled_ftext_ex(F_TINY, (int)(f / 3 % 150) - 20, 30 + (int)(6.0f * sin_turns((float)f * 0.004f)), "DWELL", 0);
}
