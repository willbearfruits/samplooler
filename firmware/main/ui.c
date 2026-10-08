#include "ui.h"
#include "oled.h"
#include "recorder.h"
#include "audio.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#if OLED_ENABLE

static int   fr;                 // frames drawn
static float peak_hold;
static uint8_t env[OLED_W];

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// ---------------------------------------------------------------- pieces

// Segmented meter, LED style, with peak hold and a clip flash.
static void meter(int x, int y, int w, int h, float v, float *hold, bool clip)
{
    if (v > *hold) *hold = v; else *hold -= 0.02f;
    if (*hold < 0) *hold = 0;
    int segw = 3, gap = 1, n = (w - 2) / (segw + gap);
    oled_frame(x, y, w, h);
    int lit = (int)(v * n + 0.5f); if (lit > n) lit = n;
    for (int i = 0; i < lit; i++) oled_rect(x + 1 + i * (segw + gap), y + 2, segw, h - 4, true);
    int hp = (int)(*hold * n); if (hp >= n) hp = n - 1;
    if (hp >= 0 && *hold > 0.02f) oled_rect(x + 1 + hp * (segw + gap), y + 1, segw, h - 2, true);
    if (clip) oled_invert(x, y, w, h);
}

static void slider(int x, int y, int w, float frac)
{
    oled_frame(x, y, w, 5);
    int fw = (int)(frac * (float)(w - 2) + 0.5f);
    if (fw > 0) oled_rect(x + 1, y + 1, fw, 3, true);
    int kx = x + 1 + clampi(fw - 1, 0, w - 4);
    oled_rect(kx, y - 1, 2, 7, true);
}

// ---------------------------------------------------------------- 128x64

#if OLED_H >= 64
static int   clip_flash;
static int   last_state = -1;
static int   flash;              // whole-screen invert frames left
static float chev_phase;
static float menu_t;             // 0 closed .. 1 open, eased over a few frames
static int8_t  scope[AUDIO_SCOPE_N];
static float   sine_ph;

// Attitude ball: a horizon that rolls with roll and slides with pitch.
static void attitude(int cx, int cy, int r, float pitch, float roll, bool on)
{
    oled_circle(cx, cy, r, true);
    if (!on) { oled_line(cx - 3, cy - 3, cx + 3, cy + 3, true); oled_line(cx - 3, cy + 3, cx + 3, cy - 3, true); return; }
    float a = roll * 0.0174533f, off = pitch / 90.0f * (float)r;
    if (off > r - 1) off = r - 1;
    if (off < -(r - 1)) off = -(r - 1);
    float c = cosf(a), s = sinf(a);
    // horizon through (0, off) rotated by roll; fill the "ground" below it inside the circle
    for (int j = -r; j <= r; j++) for (int i = -r; i <= r; i++) {
        if (i * i + j * j > (r - 1) * (r - 1)) continue;
        // signed distance of the pixel from the horizon line
        float d = (float)j * c - (float)i * s - off * c;
        if (d > 0.5f) oled_pixel(cx + i, cy + j, (i + j) & 1);      // dithered ground
        else if (d > -0.5f) oled_pixel(cx + i, cy + j, true);       // the line itself
    }
    oled_pixel(cx, cy, true); oled_pixel(cx - 1, cy, true); oled_pixel(cx + 1, cy, true);
}

// Marching chevrons: direction and rate follow the speed.
static void chevrons(int x, int y, int w, float speed, float max)
{
    float a = fabsf(speed);
    if (a < 0.01f) { oled_rect(x + w / 2 - 3, y + 3, 6, 1, true); return; }   // stopped: a dash
    int n = 1 + (int)(a / (max > 0 ? max : 1.0f) * 3.0f); if (n > 4) n = 4;
    chev_phase += speed * 0.8f;
    while (chev_phase >= 8.0f) chev_phase -= 8.0f;
    while (chev_phase < 0.0f) chev_phase += 8.0f;
    int per = w / n; if (per < 6) per = 6;
    for (int k = 0; k < n; k++) {
        int px = x + (int)((float)k * (float)per + chev_phase * (float)per / 8.0f) % w;
        if (speed > 0) { oled_line(px, y, px + 3, y + 3, true); oled_line(px, y + 6, px + 3, y + 3, true); }
        else           { oled_line(px + 3, y, px, y + 3, true); oled_line(px + 3, y + 6, px, y + 3, true); }
    }
}

// Echo arcs rippling outwards (delay) or a jittering square wave (stutter).
static void fx_glyphs(int x, int y, bool dly, bool stt)
{
    if (dly) {
        for (int k = 0; k < 3; k++) {
            int r = 2 + ((fr / 2) + k * 3) % 9;
            for (int j = -r; j <= r; j++) {                                   // right half arc
                int i = (int)sqrtf((float)(r * r - j * j));
                oled_pixel(x + i, y + 8 + j, true);
            }
        }
        oled_rect(x - 1, y + 7, 2, 3, true);
    }
    if (stt) {
        int ph = fr % 4, bx = x + 14;
        for (int i = 0; i < 16; i++) {
            int hi = ((i + ph) / 2) & 1;
            oled_pixel(bx + i, y + (hi ? 3 : 12), true);
            if (((i + ph) & 1) == 0) oled_line(bx + i, y + 3, bx + i, y + 12, true);
        }
        if (fr & 2) oled_invert(bx - 1, y + 1, 18, 14);
    }
}

static bool splash(void)
{
    const int N = 32;
    if (fr >= N) return false;
    oled_clear();
    int reveal = fr * 6;                                               // columns unveiled so far
    oled_text2(4, 1, "SAMPLOOLER");                                   // 10 x 12 px, in the yellow band
    oled_rect(reveal, 0, OLED_W - reveal, 40, false);                 // wipe from the left
    if (reveal < OLED_W) oled_rect(reveal, 0, 1, 40, (fr & 1));
    sine_ph += 0.35f;
    for (int x = 0; x < OLED_W; x++) {                                // a wave rolling underneath
        float amp = 8.0f * (fr < 8 ? fr / 8.0f : fr > N - 8 ? (N - fr) / 8.0f : 1.0f);
        int y = 53 + (int)(amp * sinf(x * 0.12f + sine_ph));
        oled_pixel(x, y, true);
        if (x & 1) oled_pixel(x, 53, true);
    }
    oled_flush();
    return true;
}

static void header(const ui_t *u)
{
    char b[24];
    if (u->state == REC_RECORDING) {
        oled_text2(0, 1, "REC");
        if ((fr / 4) & 1) oled_disc(44, 8, 5, true); else oled_circle(44, 8, 5, true);
        snprintf(b, sizeof b, "%2ds", (int)(u->rec_fill * (float)u->max_s));
        oled_text(56, 5, b);
    } else if (u->state == REC_PLAYING) {
        oled_text2(0, 1, "LOOP");
        snprintf(b, sizeof b, "%.1fs", (double)u->loop_s);
        oled_text(52, 5, b);
    } else {
        oled_text2(0, 1, "READY");
    }
    meter(92, 4, 36, 9, u->meter_in, &peak_hold, u->clip);
}

static void wave_panel(const ui_t *u)
{
    const int top = 17, mid = 31, half = 14, bot = 45;
    if (u->state == REC_IDLE) {
        // live oscilloscope of the input, plus a marquee invitation
        audio_scope(scope);
        int py = mid - scope[0] * half / 127;
        for (int x = 1; x < OLED_W; x++) {
            int y = mid - scope[x] * half / 127;
            oled_line(x - 1, py, x, y, true);
            py = y;
        }
        const char *msg = "HOLD BOOT TO RECORD   TILT TO PLAY   ROLL FOR FX   ";
        int len = (int)strlen(msg) * 6, off = (fr * 2) % len;
        for (int i = 0; msg[i]; i++) {
            int x = i * 6 - off; if (x < -6) x += len;
            if (x >= 0 && x < OLED_W) { char c[2] = { msg[i], 0 }; oled_rect(x, bot - 8, 6, 8, false); oled_text(x, bot - 7, c); }
        }
        return;
    }

    recorder_envelope(env, OLED_W);
    int fill_x = u->state == REC_RECORDING ? (int)(u->rec_fill * (OLED_W - 1)) : OLED_W - 1;
    for (int x = 0; x < OLED_W; x++) {
        if (u->state == REC_RECORDING && x > fill_x) { if ((x & 3) == 0) oled_pixel(x, mid, true); continue; }
        int col = u->state == REC_RECORDING ? (int)((float)x / (float)(fill_x ? fill_x : 1) * (OLED_W - 1)) : x;
        int h = env[clampi(col, 0, OLED_W - 1)] * half / 255;
        if (h == 0 && env[clampi(col, 0, OLED_W - 1)]) h = 1;
        if (h > 0) oled_rect(x, mid - h, 1, 2 * h + 1, true); else oled_pixel(x, mid, true);
    }

    if (u->state == REC_RECORDING) {
        // the record cursor, blinking, with the take filling behind it
        if ((fr / 3) & 1) oled_rect(fill_x, top, 2, bot - top + 1, true);
        return;
    }

    // playhead with a comet tail that lengthens with speed
    int hx = (int)(u->pos * (OLED_W - 1));
    float a = fabsf(u->speed_now);
    int tail = (int)(a * 1.5f); if (tail > 8) tail = 8;
    for (int k = 1; k <= tail; k++) {
        int x = u->speed_now >= 0 ? hx - k : hx + k;
        if (x < 0) x += OLED_W;
        if (x >= OLED_W) x -= OLED_W;
        int span = half - k * half / (tail + 1);
        oled_invert(x, mid - span, 1, 2 * span + 1);
    }
    oled_invert(hx, top, 1, bot - top + 1);
    if (u->mode == REC_MODE_GRAIN) {                                  // grains: bracket the head
        int gx = hx + (fr & 1 ? 2 : 3);
        oled_rect(hx - 3, top, 1, 2, true); oled_rect(gx, top, 1, 2, true);
        oled_rect(hx - 3, bot - 1, 1, 2, true); oled_rect(gx, bot - 1, 1, 2, true);
    }
    // the seam
    oled_pixel(0, top - 1, fr & 1); oled_pixel(OLED_W - 1, top - 1, fr & 1);
}

static void bottom_strip(const ui_t *u)
{
    const int y = 47;
    attitude(8, y + 8, 7, u->has_imu ? u->tilt_pitch : 0, u->has_imu ? u->tilt_roll : 0, u->has_imu && u->tilt_on);
    if (!u->tilt_on) oled_text(18, y + 5, u->has_joy ? "joy" : "1x");
    else chevrons(18, y + 5, 22, u->speed_now, u->spd_max);

    char b[16];
    float a = fabsf(u->speed_now);
    if (u->state == REC_PLAYING) snprintf(b, sizeof b, "%s%.2fx", u->speed_now < 0 ? "-" : "", (double)a);
    else snprintf(b, sizeof b, "%.2fx", (double)fabsf(u->speed_cmd));
    if (u->mode == REC_MODE_GRAIN) {
        oled_text(44, y + 1, b);
        snprintf(b, sizeof b, "GRN %+.0fst", (double)(12.0f * log2f(u->pitch_ratio)));
        oled_text(44, y + 9, b);                                       // second line under the speed
    } else oled_text(44, y + 5, b);
    fx_glyphs(96, y, u->fx_delay, u->fx_stutter);
    if (!u->fx_delay && !u->fx_stutter) {                              // idle corner: output level as a thin bar
        oled_frame(98, y + 12, 30, 4);
        int w = (int)(u->meter_out * 28.0f); if (w > 28) w = 28;
        oled_rect(99, y + 13, w, 2, true);
        snprintf(b, sizeof b, "%2d%%", u->cpu);
        oled_text(104, y + 3, b);
    }
}

static void menu_panel(const ui_t *u)
{
    float target = u->menu ? 1.0f : 0.0f;
    menu_t += 0.35f * (target - menu_t);
    if (menu_t < 0.02f && !u->menu) { menu_t = 0; return; }
    float e = menu_t * (2.0f - menu_t);                                 // ease out
    int top = 22 + (int)((1.0f - e) * 42.0f);
    if (top >= OLED_H) return;
    oled_rect(0, top, OLED_W, OLED_H - top, false);
    oled_frame(0, top, OLED_W, OLED_H - top);
    char b[32];
    snprintf(b, sizeof b, "%d/%d %s", u->param + 1, u->param_n, u->param_name);
    oled_text(4, top + 2, b); oled_invert(1, top + 1, OLED_W - 2, 10);   // white bar, black text
    int vw = (int)strlen(u->param_val) * 12; if (vw > OLED_W) vw = OLED_W;
    oled_text2((OLED_W - vw) / 2, top + 14, u->param_val);
    if (u->param_frac >= 0) slider(8, top + 32, OLED_W - 16, u->param_frac);
    else oled_text((OLED_W - 12 * 6) / 2, top + 32, "turn to run");
    // tick marks that nudge left/right when the encoder moves
    oled_text(2, top + 14 + 4, "<"); oled_text(OLED_W - 8, top + 14 + 4, ">");
}

// A slot job: the body is replaced by its name and a progress bar; the header keeps the state.
static void busy_panel(const ui_t *u)
{
    const int top = 20;
    oled_rect(0, top, OLED_W, 28, false);
    oled_frame(8, top, OLED_W - 16, 28);
    int w = (int)strlen(u->busy_text) * 12;
    oled_text2((OLED_W - w) / 2, top + 4, u->busy_text);
    slider(16, top + 20, OLED_W - 32, u->busy_frac);
}

// A note in the bottom strip's place: the job result, or why the last boot was not a clean one.
static void note_bar(const char *s)
{
    const int y = 47;
    oled_rect(0, y, OLED_W, OLED_H - y, false);
    oled_frame(0, y, OLED_W, 17);
    int w = (int)strlen(s) * 6; if (w > OLED_W - 6) w = OLED_W - 6;
    oled_text((OLED_W - w) / 2, y + 5, s);
    oled_invert(1, y + 1, OLED_W - 2, 15);
}

static void toast(const ui_t *u)
{
    int t = u->toast_ms;
    if (t <= 0) return;
    int y = 47;
    if (t < 150) y += (150 - t) * 17 / 150;                            // slide out
    oled_rect(0, y, OLED_W, OLED_H - y, false);
    oled_frame(0, y, OLED_W, 17);
    oled_text(3, y + 2, u->param_name);
    int vw = (int)strlen(u->param_val) * 6;
    oled_text(OLED_W - 3 - vw, y + 2, u->param_val);
    if (u->param_frac >= 0) slider(3, y + 11, OLED_W - 6, u->param_frac);
}

void ui_draw(const ui_t *u)
{
    fr++;
    if (splash()) return;
    if (u->state != last_state) {
        if (u->state == REC_RECORDING || (u->state == REC_PLAYING && last_state == REC_RECORDING)) flash = 2;
        last_state = u->state;
    }
    if (u->clip) clip_flash = 6; else if (clip_flash) clip_flash--;
    oled_clear();
    ui_t v = *u; v.clip = clip_flash > 0;
    header(&v);
    wave_panel(&v);
    if (u->busy_text) busy_panel(&v);
    if (u->note && !u->menu) note_bar(u->note);
    else if (u->toast_ms > 0 && !u->menu) toast(&v); else bottom_strip(&v);
    menu_panel(&v);
    if (flash > 0) { flash--; oled_invert(0, 0, OLED_W, OLED_H); }
    oled_flush();
}

void ui_fault(const char *l1, const char *l2)
{
    oled_clear();
    oled_text2(4, 1, "SAMPLOOLER");
    int w = (int)strlen(l1) * 12;
    oled_text2((OLED_W - w) / 2, 24, l1); oled_invert((OLED_W - w) / 2 - 3, 22, w + 6, 18);
    w = (int)strlen(l2) * 6;
    oled_text((OLED_W - w) / 2, 48, l2);
    oled_flush();
}

// ---------------------------------------------------------------- 72x40
#else
void ui_draw(const ui_t *u)
{
    fr++;
    char line[28];
    oled_clear();
    if (u->state == REC_RECORDING) snprintf(line, sizeof line, "REC %2ds", (int)(u->rec_fill * (float)u->max_s));
    else if (u->state == REC_PLAYING) snprintf(line, sizeof line, "LOOP %.1fs", (double)u->loop_s);
    else snprintf(line, sizeof line, "HOLD BOOT");
    oled_text(0, 0, line);
    meter(OLED_W - 16, 0, 16, 7, u->meter_in, &peak_hold, u->clip);    // 9 header chars (54 px) fit beside it

    if (u->menu) {
        oled_text(0, 10, u->param_name);                                // 12 chars across, the counter goes below
        oled_text(0, 20, u->param_val);
        snprintf(line, sizeof line, "%d/%d", u->param + 1, u->param_n);
        int cw = 6 * (int)strlen(line);
        if (6 * (int)strlen(u->param_val) + 6 + cw <= OLED_W) oled_text(OLED_W - cw, 20, line);
        if (u->param_frac >= 0) slider(2, 31, OLED_W - 4, u->param_frac);
        oled_flush();
        return;
    }

    if (u->busy_text) {                                                 // a slot job: its name and a bar
        oled_text(0, 10, u->busy_text);
        slider(2, 21, OLED_W - 4, u->busy_frac);
        oled_flush();
        return;
    }
    if (u->note) { oled_text(0, 10, u->note); oled_invert(0, 9, OLED_W, 9); }
    else {
        if (u->state == REC_PLAYING) snprintf(line, sizeof line, "%s %s%.2fx", u->mode == REC_MODE_GRAIN ? "GRN" : "TAPE", u->speed_now < 0 ? "<" : ">", (double)fabsf(u->speed_now));
        else snprintf(line, sizeof line, "%s %s", u->mode == REC_MODE_GRAIN ? "GRAIN" : "TAPE", u->state == REC_RECORDING ? "rec" : "");
        oled_text(0, 10, line);
    }

    // waveform strip with the head
    const int top = 19, mid = 24, half = 5;
    recorder_envelope(env, OLED_W);
    for (int x = 0; x < OLED_W; x++) {
        int h = env[x] * half / 255;
        if (h > 0) oled_rect(x, mid - h, 1, 2 * h + 1, true); else if ((x & 1) == 0) oled_pixel(x, mid, true);
    }
    if (u->state == REC_RECORDING) { int fx = (int)(u->rec_fill * (OLED_W - 1)); if ((fr / 3) & 1) oled_rect(fx, top, 1, 2 * half + 3, true); }
    else if (u->state == REC_PLAYING) oled_invert((int)(u->pos * (OLED_W - 1)), top, 1, 2 * half + 3);

    // tilt line
    int y_tilt = OLED_H - 4;
    oled_rect(0, y_tilt, OLED_W, 1, true);
    oled_rect(OLED_W / 2, y_tilt - 3, 1, 7, true);
    float t = u->tilt_on ? u->tilt_pitch / 45.0f : (u->spd_max > 0 ? u->speed_cmd / u->spd_max : 0);
    if (t > 1) t = 1;
    if (t < -1) t = -1;
    oled_rect(OLED_W / 2 + (int)(t * (OLED_W / 2 - 2)) - 1, y_tilt - 2, 3, 5, true);
    oled_flush();
}

void ui_fault(const char *l1, const char *l2)
{
    oled_clear();
    oled_text(0, 0, "SAMPLOOLER");
    oled_text(0, 14, l1); oled_invert(0, 13, OLED_W, 9);
    oled_text(0, 26, l2);
    oled_flush();
}
#endif
#endif
