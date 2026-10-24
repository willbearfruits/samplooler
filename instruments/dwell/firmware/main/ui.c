#include "ui.h"
#include "core.h"
#include "front.h"
#include "looper.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static inline float clampf_(float v, float lo, float hi) { return !(v >= lo) ? lo : v > hi ? hi : v; }

void ui_meter(int x, int y, int w, int h, float v, bool clip)
{
    oled_rframe(x, y, w, h, 1);
    int n = (w - 2) / 3;
    float db = v > 1e-5f ? 20.0f * log10f(v) : -100.0f;            // -36 .. 0 dBFS over the segments
    int lit = (int)((db + 36.0f) / 36.0f * (float)n + 0.5f);
    if (lit > n) lit = n;
    for (int i = 0; i < lit; i++) oled_rect(x + 2 + i * 3, y + 2, 2, h - 4, true);
    if (clip) oled_invert(x, y, w, h);
}

static void padlock(int x, int y)
{
    oled_rframe(x, y + 3, 6, 5, 1); oled_pixel(x + 2, y + 5, true); oled_pixel(x + 3, y + 5, true);
    oled_line(x + 1, y + 3, x + 1, y + 1, true); oled_line(x + 4, y + 3, x + 4, y + 1, true);
    oled_line(x + 2, y, x + 3, y, true);
}

static const char *loop_pill(char *b, int n, bool *filled)
{
    loop_state_t s = looper_state();
    float l = looper_len_s();
    *filled = s == LOOP_RECORDING || s == LOOP_DUBBING;
    switch (s) {
    case LOOP_RECORDING: snprintf(b, (size_t)n, "REC %.1fs", (double)l); return b;
    case LOOP_DUBBING:   snprintf(b, (size_t)n, "ADD %.1fs", (double)l); return b;
    case LOOP_PLAYING:   snprintf(b, (size_t)n, "LOOP %.1fs", (double)l); return b;
    case LOOP_STOPPED:   snprintf(b, (size_t)n, "STOP %.1fs", (double)l); return b;
    default: return NULL;
    }
}

void ui_head(const char *left, bool filled)
{
    const front_status_t *st = front_status();
    char lb[24];
    int x_end;
    if (!left && INST.looper) left = loop_pill(lb, sizeof lb, &filled);
    if (left) x_end = oled_pill(F_SMALL, 0, 2, left, filled);
    else x_end = oled_ftext_ex(F_MED, 1, 3, INST.name, 1);
    int rx = OLED_W - 22;                                           // the meter
    ui_meter(rx, 3, 22, 9, st->out_level, st->clip);
    rx -= 3;
    if (st->frozen) { padlock(rx - 6, 3); rx -= 9; }
    char nm[28];
    snprintf(nm, sizeof nm, "%s%s", st->sound ? st->sound : "", st->edited ? "*" : "");
    int room = rx - (x_end + 5);
    if (room > 8) {
        int w = oled_ftext_w(F_SMALL, nm, 1);
        if (w <= room) oled_ftext_ex(F_SMALL, rx - w, 5, nm, 1);
        else ui_text_fit(F_SMALL, x_end + 5, 5, room, nm);
    }
}

void ui_pill_r(int rx, int y, const char *s, bool filled) { oled_pill(F_SMALL, rx - oled_pill_w(F_SMALL, s), y, s, filled); }

void ui_box(int cy, const char *s)
{
    int w = oled_ftext_w(F_SMALL, s, 1) + 10;
    if (w > OLED_W) w = OLED_W;
    oled_rrect(64 - w / 2, cy - 7, w, 15, 3, false);
    oled_rframe(64 - w / 2, cy - 7, w, 15, 3);
    oled_ftext_c(F_SMALL, 64, cy - 2, s, 1);
}

void ui_slider(int x, int y, int w, float frac)
{
    frac = clampf_(frac, 0.0f, 1.0f);
    oled_hline_pat(x, y + 3, w, 0x55, true);
    int fw = (int)(frac * (float)(w - 2) + 0.5f);
    oled_rect(x, y + 2, fw + 1, 3, true);
    oled_rrect(x + fw - 1, y, 3, 7, 1, true);
}

void ui_vbar(int x, int y, int w, int h, float v)
{
    oled_frame(x, y, w, h);
    v = clampf_(v, 0.0f, 1.0f);
    int f = (int)(v * (float)(h - 2) + 0.5f);
    if (f > 0) oled_rect(x + 1, y + h - 1 - f, w - 2, f, true);
}

void ui_arc(int cx, int cy, int r, float a0, float a1)
{
    if (a1 < a0) { float t = a0; a0 = a1; a1 = t; }
    float step = r > 0 ? 40.0f / (float)r : 10.0f;
    for (float a = a0; a <= a1; a += step) {
        float rad = a * 0.0174533f;
        oled_pixel(cx + (int)lroundf((float)r * cosf(rad)), cy + (int)lroundf((float)r * sinf(rad)), true);
    }
}

void ui_note_name(int midi, char *b, int n)
{
    static const char *nm[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (midi < 0) midi = 0;
    snprintf(b, (size_t)n, "%s%d", nm[midi % 12], midi / 12 - 1);
}

int ui_text_fit(const gfx_font_t *f, int x, int y, int w, const char *s)
{
    if (oled_ftext_w(f, s, 1) <= w) return oled_ftext_ex(f, x, y, s, 1);
    char b[48]; int n = (int)strlen(s); if (n > 40) n = 40;
    while (n > 0) {
        memcpy(b, s, (size_t)n); strcpy(b + n, "..");
        if (oled_ftext_w(f, b, 1) <= w) break;
        n--;
    }
    return oled_ftext_ex(f, x, y, b, 1);
}

int ui_wrap(const gfx_font_t *f, const char *s, int w, char lines[][48], int max)
{
    int n = 0;
    while (*s) {
        while (*s == ' ') s++;
        if (!*s) break;
        if (n >= max) return max + 1;
        int len = (int)strlen(s), cut = len, best = -1;
        if (len > 47) len = cut = 47;
        for (int i = 1; i <= len; i++) {
            if (i == len || s[i] == ' ') {
                char b[48]; memcpy(b, s, (size_t)i); b[i] = 0;
                if (oled_ftext_w(f, b, 1) <= w) best = i; else break;
            }
        }
        if (best < 0) best = cut;                              // one word too long: it is cut
        memcpy(lines[n], s, (size_t)best); lines[n][best] = 0;
        n++; s += best;
    }
    return n;
}

void ui_loop(int x, int y, int w)
{
    if (!INST.looper) return;
    loop_state_t s = looper_state();
    char b[16]; bool filled;
    const char *p = loop_pill(b, sizeof b, &filled);
    if (!p) { oled_ftext_ex(F_TINY, x, y + 2, "HOLD REC TO LOOP", 1); return; }
    const char *tag = s == LOOP_RECORDING ? "REC" : s == LOOP_DUBBING ? "ADD" : s == LOOP_STOPPED ? "STOP" : "LOOP";
    int px = oled_pill(F_SMALL, x, y, tag, filled) + 3;
    int bw = x + w - px;
    if (bw < 8) return;
    oled_rframe(px, y + 1, bw, 7, 2);
    float f = s == LOOP_RECORDING ? looper_fill() : looper_pos();
    int fill = (int)(f * (float)(bw - 4) + 0.5f);
    if (s == LOOP_RECORDING) oled_rect(px + 2, y + 3, fill, 3, true);
    else { oled_hline_pat(px + 2, y + 4, bw - 4, 0x11, true); oled_rect(px + 2 + fill, y + 2, 1, 5, true); }
}

void ui_sprite(int x, int y, const char *const *rows, int h, bool flip_x, bool on)
{
    int w = (int)strlen(rows[0]);
    for (int r = 0; r < h; r++) {
        const char *row = rows[r];
        for (int c = 0; c < w && row[c]; c++) if (row[c] == '#') oled_pixel(flip_x ? x + w - 1 - c : x + c, y + r, on);
    }
}

float ease_out(float t) { t = clampf_(t, 0, 1); float u = 1.0f - t; return 1.0f - u * u * u; }
float ease_in_out(float t) { t = clampf_(t, 0, 1); return t * t * (3.0f - 2.0f * t); }
float ease_back(float t) { t = clampf_(t, 0, 1); const float c1 = 1.70158f, c3 = c1 + 1.0f; float u = t - 1.0f; return 1.0f + c3 * u * u * u + c1 * u * u; }
float ease_bounce(float t)
{
    t = clampf_(t, 0, 1);
    const float n = 7.5625f, d = 2.75f;
    if (t < 1 / d) return n * t * t;
    if (t < 2 / d) { t -= 1.5f / d; return n * t * t + 0.75f; }
    if (t < 2.5f / d) { t -= 2.25f / d; return n * t * t + 0.9375f; }
    t -= 2.625f / d; return n * t * t + 0.984375f;
}

void parts_emit(parts_t *ps, float x, float y, float vx, float vy, float life, int kind, int size)
{
    if (ps->n >= PARTS_MAX) return;
    ps->p[ps->n++] = (particle_t){ x, y, vx, vy, life, life, (uint8_t)kind, (uint8_t)size };
}

void parts_burst(parts_t *ps, float x, float y, int count, float speed, float life, int kind, uint32_t *seed)
{
    for (int i = 0; i < count; i++) {
        uint32_t s = *seed; s ^= s << 13; s ^= s >> 17; s ^= s << 5; *seed = s;
        float a = (float)(s & 0xffff) / 65536.0f * 6.2831853f, r = speed * (0.4f + 0.6f * (float)(s >> 16) / 65536.0f);
        parts_emit(ps, x, y, r * cosf(a), r * sinf(a), life * (0.6f + 0.4f * (float)((s >> 8) & 0xff) / 255.0f), kind, 1);
    }
}

void parts_step(parts_t *ps, float dt, float gravity, float drag)
{
    int j = 0;
    float k = 1.0f - drag * dt;
    if (k < 0) k = 0;
    for (int i = 0; i < ps->n; i++) {
        particle_t q = ps->p[i];
        q.life -= dt;
        if (q.life <= 0) continue;
        q.vy += gravity * dt; q.vx *= k; q.vy *= k;
        q.x += q.vx * dt; q.y += q.vy * dt;
        if (q.x < -20 || q.x > OLED_W + 20 || q.y < -20 || q.y > OLED_H + 20) continue;
        ps->p[j++] = q;
    }
    ps->n = j;
}

void parts_draw(const parts_t *ps)
{
    for (int i = 0; i < ps->n; i++) {
        const particle_t *q = &ps->p[i];
        int x = (int)lroundf(q->x), y = (int)lroundf(q->y);
        float age = 1.0f - q->life / q->max;
        switch (q->kind) {
        case PART_SQUARE: oled_rect(x, y, q->size + 1, q->size + 1, true); break;
        case PART_STREAK: oled_line(x, y, (int)lroundf(q->x - q->vx * 0.04f), (int)lroundf(q->y - q->vy * 0.04f), true); break;
        case PART_RING: { int r = (int)(age * (float)(q->size + 2) * 3.0f) + 1; if (age < 0.85f) oled_circle(x, y, r, true); break; }
        case PART_SPARK: if (((int)(q->life * 50.0f)) & 1) oled_pixel(x, y, true); break;
        default: oled_pixel(x, y, true); break;
        }
    }
}
