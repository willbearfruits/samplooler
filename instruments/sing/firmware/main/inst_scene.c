// SING's screen (sing.h): the header band, the scene, the splash, the screensaver. Drawing only, in the UI task;
// kept out of the IRAM list (sing_iram.lf), as nothing here runs per sample.
//
//   the header   the loop's pill (REC, ADD, LOOP, STOP) or the name; the voice (sound); the bar's four beats; the link;
//                the meter; along the bottom row the loop's playhead (a growing bar while the first take records)
//   the lane     your pitch as you sing it (dots) and as it comes out tuned (a line), over the notes the tuning lands
//                on; the note's name, and how sharp or flat you are
//   the singers  a head for the tuned voice and one for every harmony and choir voice sounding, higher for a higher
//                note, mouths open with their level; the vocoder's bands instead when the vocoder is up
//   the tape     the Samplooler's reels: they turn with the loop's speed (backwards when it plays backwards), the tape
//                carries the loop's shape and the playhead; the stutter shakes it, the echo throw ripples off it
#include "sing.h"
#include "ui.h"
#include "front.h"
#include "link.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *scene_note(int midi, char *b, int n)
{
    static const char *const nm[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (midi < 0) midi = 0;
    snprintf(b, (size_t)n, "%s%d", nm[midi % 12], midi / 12 - 1);
    return b;
}

// ---------------------------------------------------------------- the link glyph (as BAND's)
void scene_link_glyph(int x, int y, const view_t *v)
{
    if (!link_radio()) return;
    if (!sui.peers) { if ((v->frame / 12) % 2 == 0) { oled_pixel(x + 2, y + 3, true); ui_arc(x + 2, y + 3, 3, -60, 60); } return; }
    bool on = sui.beat_flash > 1 || (sui.beat_flash > 0 && (v->frame & 1));
    oled_frame(x, y + 1, 3, 6); oled_frame(x + 4, y + 1, 3, 6);
    if (link_synced() && link_error_ms() < 2.0f) { if (on) { oled_rect(x, y + 1, 3, 6, true); oled_rect(x + 4, y + 1, 3, 6, true); } }
    else if ((v->frame / 3) % 2 == 0) oled_pixel(x + 3, y + 3, true);
    if (link_tempo_clash() && (v->frame / 6) % 2 == 0) oled_pixel(x + 3, y, true);
}

// ---------------------------------------------------------------- the header band
static void header(const view_t *v)
{
    const front_status_t *st = front_status();
    int ls = loop_state(), x_end;
    if (ls == LOOPER_ARM) x_end = (v->frame / 12) % 2 ? oled_pill(F_SMALL, 0, 2, "SING!", false) : oled_pill(F_SMALL, 0, 2, "REC", false);
    else if (ls == LOOPER_REC) x_end = (v->frame / 6) % 2 ? oled_pill(F_SMALL, 0, 2, "REC", true) : oled_pill(F_SMALL, 0, 2, "REC", false);
    else if (ls == LOOPER_DUB) x_end = oled_pill(F_SMALL, 0, 2, "ADD", true);
    else if (ls == LOOPER_PLAY) x_end = oled_pill(F_SMALL, 0, 2, "LOOP", true);
    else if (ls == LOOPER_STOP) x_end = oled_pill(F_SMALL, 0, 2, "STOP", false);
    else x_end = oled_ftext_ex(F_MED, 1, 3, "SING", 1);
    // the bar's beats, the link, the meter
    int bx = 92;
    uint32_t fpb = song.fpb ? song.fpb : 19200;
    int32_t rel = (int32_t)(audio_frames() - song.origin) % (int32_t)(fpb * 4u);
    if (rel < 0) rel += (int32_t)(fpb * 4u);
    int beat = (int)((uint32_t)rel / fpb);
    for (int k = 0; k < 4; k++) {
        int x = bx + k * 3;
        if (k == beat) { bool big = sui.beat_flash > 1; oled_rect(x - (big ? 1 : 0), big ? 5 : 6, big ? 3 : 2, big ? 4 : 3, true); }
        else oled_pixel(x, 8, true);
    }
    scene_link_glyph(105, 4, v);
    ui_meter(OLED_W - 11, 3, 11, 9, v->out_level, st->clip);
    // the voice's name, in what is left
    char nm[28];
    snprintf(nm, sizeof nm, "%s%s", st->sound ? st->sound : "", st->edited ? "*" : "");
    int room = bx - 4 - (x_end + 4) - (st->frozen ? 7 : 0);
    if (st->frozen) { int px = bx - 10; oled_rframe(px, 6, 6, 5, 1); oled_frame(px + 1, 3, 4, 4); }
    if (room > 8) ui_text_fit(F_SMALL, x_end + 4, 5, room, nm);
    // the loop along row 15
    if (ls == LOOPER_REC) {
        float s = loop_len_s();
        int w = (int)(s / (float)LOOP_MAX_S * (float)OLED_W);
        oled_rect(0, 15, w < OLED_W ? w : OLED_W, 1, true);
        if (loop_closing() && (v->frame / 3) % 2) oled_rect(w - 2 < 0 ? 0 : w - 2, 14, 3, 2, true);
    } else if (ls != LOOPER_EMPTY && ls != LOOPER_ARM) {
        int nb = loop_beats();
        for (int k = 0; k < nb && nb <= 64; k++) oled_pixel(k * (OLED_W - 1) / nb, 15, true);
        int px = (int)(loop_pos() * (float)(OLED_W - 1));
        oled_rect(px - 1, 14, 3, 2, true);
    }
}

// ---------------------------------------------------------------- the lane: pitch over time
#define LANE_W 84
#define LANE_Y0 17
#define LANE_Y1 47
static float hist_in[LANE_W], hist_out[LANE_W];
static uint8_t hist_on[LANE_W];
static int hist_w;
static float lane_c = 60.0f;                        // the note at the lane's middle, following the voice slowly
static int note_shown = -1;
static float note_pop;

static int lane_y(float note) { return (LANE_Y0 + LANE_Y1) / 2 - (int)((note - lane_c) * 2.0f + (note > lane_c ? 0.5f : -0.5f)); }

static void lane(const view_t *v, const voice_state_t *vs)
{
    (void)v;
    bool sung = vs->voiced && vs->level > 0.02f && fx_gate_open() > 0.5f;
    hist_in[hist_w] = sung ? vs->in_note : 0.0f;
    hist_out[hist_w] = sung ? vs->out_note : 0.0f;
    hist_on[hist_w] = sung;
    hist_w = (hist_w + 1) % LANE_W;
    if (sung) lane_c += 0.08f * (vs->out_note - lane_c);
    // the notes the tuning lands on: dotted lines; the C's a little stronger
    for (int k = -8; k <= 8; k++) {
        int n = (int)(lane_c + 0.5f) + k, y = lane_y((float)n);
        if (y < LANE_Y0 || y > LANE_Y1) continue;
        int pc = ((n % 12) + 12) % 12;
        bool on = sui.tune_mask ? (sui.tune_mask >> pc) & 1 : true;
        if (!on) continue;
        oled_hline_pat(0, y, LANE_W, pc == (song.key % 12) ? 0x11 : 0x01, true);
    }
    // the trace: the sung pitch as dots, the tuned one as a line
    int prev = -1;
    for (int c = 0; c < LANE_W; c++) {
        int i = (hist_w + c) % LANE_W;
        if (!hist_on[i]) { prev = -1; continue; }
        int yo = lane_y(hist_out[i]), yi = lane_y(hist_in[i]);
        if (yi >= LANE_Y0 && yi <= LANE_Y1 && yi != yo) oled_pixel(c, yi, true);
        if (yo >= LANE_Y0 && yo <= LANE_Y1) {
            if (prev >= 0 && prev != yo && abs(prev - yo) < 8) oled_line(c - 1, prev, c, yo, true);
            else oled_rect(c, yo, 1, 2, true);
            prev = yo;
        } else prev = -1;
    }
    // the extra voices at the lane's end
    for (int k = 0; k < vs->nvoices && k < VOICE_EXTRA; k++) {
        if (vs->vlevel[k] < 0.02f) continue;
        int y = lane_y(vs->vnote[k]);
        if (y < LANE_Y0 + 1 || y > LANE_Y1 - 1) continue;
        int s = vs->vlevel[k] > 0.3f ? 2 : 1;
        oled_frame(LANE_W - 4 - k % 2 * 3, y - s / 2, s + 1, s + 1);
    }
    // the note's name, and how far off you are (a needle that sits still when you are on it)
    if (sung) {
        int nn = (int)(vs->target + 0.5f);
        if (nn != note_shown) { note_shown = nn; note_pop = 0; }
        char b[8]; scene_note(nn, b, sizeof b);
        float e = ease_back(note_pop); note_pop += 0.2f; if (note_pop > 1.0f) note_pop = 1.0f;
        int y = LANE_Y0 + 1 - (int)((1.0f - e) * 4.0f);
        int w = oled_ftext_w(F_MED, b, 1);
        oled_rect(0, y - 1, w + 3, 12, false);
        oled_ftext_ex(F_MED, 1, y, b, 1);
        float cents = (vs->in_note - vs->target) * 100.0f;
        int nx = w + 6, cx = nx + 9;
        oled_rect(nx - 1, y + 2, 21, 7, false);
        oled_hline_pat(nx, y + 6, 19, 0x55, true);
        oled_rect(cx, y + 3, 1, 5, true);
        int m = (int)(clampf(cents / 50.0f, -1.0f, 1.0f) * 9.0f);
        oled_rect(cx + m - 1, y + 4, 3, 3, true);
    } else note_shown = -1;
    oled_line(LANE_W + 1, LANE_Y0, LANE_W + 1, LANE_Y1, true);
}

// ---------------------------------------------------------------- the singers, or the vocoder's bands
static float mouth[1 + VOICE_EXTRA];
static void head(int cx, int cy, float open, bool lead, uint32_t f, int k)
{
    oled_circle(cx, cy, 4, true);
    if (lead) oled_disc(cx, cy - 4, 1, true);                       // the lead wears a dot on top
    int blink = ((f + (uint32_t)k * 37u) % 90u) < 3u;
    if (!blink) { oled_pixel(cx - 2, cy - 1, true); oled_pixel(cx + 2, cy - 1, true); }
    int h = (int)(open * 4.0f + 0.5f);
    if (h <= 0) oled_line(cx - 1, cy + 2, cx + 1, cy + 2, true);
    else oled_rect(cx - 1, cy + 1, 3, h > 3 ? 3 : h, true);
}

static void singers(const view_t *v, const voice_state_t *vs)
{
    const int x0 = LANE_W + 3, w = OLED_W - x0;
    if (sui.voc > 0.25f) {                                          // the vocoder: its bands as bars
        float lv[24];
        int nb = vocoder_bands(lv, 24);
        if (nb < 1) return;
        int bw = w / nb; if (bw < 1) bw = 1;
        for (int b = 0; b < nb; b++) {
            int h = (int)(clampf(lv[b], 0.0f, 1.0f) * 28.0f);
            int x = x0 + b * bw;
            oled_rect(x, LANE_Y1 - h, bw > 1 ? bw - 1 : 1, h + 1, true);
        }
        oled_ftext_c(F_TINY, x0 + w / 2, LANE_Y0, "VOCODER", 1);
        return;
    }
    // the lead in front, the others in rows behind it, higher for a higher note
    float lead_open = vs->voiced ? clampf(vs->level * 6.0f, 0.0f, 1.0f) * (sui.lead > 0.05f || sui.dry > 0.05f ? 1.0f : 0.3f) : 0.0f;
    mouth[0] += 0.5f * (lead_open - mouth[0]);
    int n = vs->nvoices > VOICE_EXTRA ? VOICE_EXTRA : vs->nvoices;
    int shown = 0;
    for (int k = 0; k < n; k++) {
        if (vs->vlevel[k] < 0.02f) { mouth[1 + k] *= 0.6f; continue; }
        mouth[1 + k] += 0.5f * (clampf(vs->vlevel[k] * 4.0f, 0.0f, 1.0f) - mouth[1 + k]);
        int col = shown % 4, row = shown / 4;
        float dn = vs->vnote[k] - vs->out_note;
        int cy = 24 + row * 10 - (int)clampf(dn * 0.35f, -4.0f, 4.0f);
        int cx = x0 + 5 + col * 10 + (row ? 5 : 0);
        int bob = (int)(sin_turns((float)v->frame * 0.04f + (float)k * 0.3f) * 1.2f);
        head(cx, cy + bob, mouth[1 + k], false, v->frame, k + 1);
        shown++;
    }
    int lx = x0 + w / 2, ly = 40 - (shown ? 0 : 6);
    oled_rect(lx - 6, ly - 6, 13, 13, false);
    head(lx, ly, mouth[0], true, v->frame, 0);
    // the stick's bend and throat as little arrows by the lead
    if (sui.bend > 0.5f) oled_line(lx + 7, ly - 2, lx + 7, ly - 6, true);
    else if (sui.bend < -0.5f) oled_line(lx + 7, ly + 2, lx + 7, ly + 6, true);
    if (!shown && !vs->voiced && (v->frame / 25) % 4 == 0) oled_ftext_c(F_TINY, lx, LANE_Y0, "SING", 1);
}

// ---------------------------------------------------------------- the tape
static float reel_a;
static uint8_t env[96];
static void reel(int cx, int cy, float a)
{
    oled_circle(cx, cy, 6, true);
    oled_disc(cx, cy, 1, true);
    for (int k = 0; k < 3; k++) {
        float t = a + (float)k / 3.0f;
        int x = cx + (int)(sin_turns(t + 0.25f) * 4.5f), y = cy + (int)(sin_turns(t) * 4.5f);
        oled_line(cx, cy, x, y, true);
    }
}

static void tape(const view_t *v, int y0)
{
    int ls = loop_state();
    float sp = ls == LOOPER_PLAY || ls == LOOPER_DUB ? loop_speed() : ls == LOOPER_REC ? 1.0f : 0.0f;
    reel_a += sp * 0.04f;
    int cy = y0 + 7;
    reel(6, cy, reel_a);
    reel(OLED_W - 7, cy, reel_a * 1.3f);
    const int tx = 14, tw = 96;
    if (ls == LOOPER_EMPTY || ls == LOOPER_ARM) {
        oled_hline_pat(tx, cy + 6, tw + 4, 0x11, true);
        if (ls == LOOPER_ARM) oled_ftext_c(F_TINY, 64, cy - 3, "SING: THE LOOP STARTS WITH YOU", 1);
        else if ((v->frame / 30) % 2 == 0) oled_ftext_c(F_TINY, 64, cy - 3, "HOLD REC TO LOOP", 1);
        return;
    }
    oled_line(tx, y0 + 1, tx + tw + 3, y0 + 1, true);
    oled_line(tx, y0 + 13, tx + tw + 3, y0 + 13, true);
    if (v->frame % 3 == 0) loop_envelope(env, tw);
    float stt = sui.stutter;
    int seg = stt > 0.02f ? 4 + (int)((1.0f - stt) * 20.0f) : 0;
    int ph = (int)(loop_pos() * (float)tw);
    for (int c = 0; c < tw; c++) {
        int src = c;
        if (seg && c > ph) src = ph + ((c - ph) % seg);            // the stutter: the slice repeats along the tape
        int h = env[src < tw ? src : tw - 1] * 5 / 255;
        if (h > 0) oled_rect(tx + 2 + c, cy - h, 1, 2 * h + 1, true);
    }
    if (ls != LOOPER_REC) {
        oled_rect(tx + 2 + ph, y0 + 1, 1, 13, true);
        if (sui.throw_ > 0.05f) {                                  // the echo throw ripples off the playhead
            for (int k = 0; k < 3; k++) {
                int r = 3 + ((int)(v->frame % 12u) + k * 4) % 12;
                if (r > 2) ui_arc(tx + 2 + ph, cy, r, -40, 40);
            }
        }
        if (fabsf(sp - 1.0f) > 0.03f) {                            // a tilted loop: the speed under the left reel
            char b[12]; snprintf(b, sizeof b, "%+.1fx", (double)sp);
            int w = oled_ftext_w(F_TINY, b, 1) + 2;
            oled_rect(1, y0 - 6, w, 7, false);
            oled_ftext_ex(F_TINY, 2, y0 - 5, b, 1);
        }
    }
}

void scene_draw(const view_t *v)
{
    voice_state_t vs; voice_state(&vs);
    lane(v, &vs);
    singers(v, &vs);
    if (v->cover < 16) tape(v, 49);
    oled_rect(0, 0, OLED_W, 16, false);
    header(v);
}

// ---------------------------------------------------------------- splash: a singer, a choir, the tape
static void figure(int x, int base, float open, int arm)
{
    int y = base - 20;
    oled_circle(x, y, 3, true);
    int h = (int)(open * 3.0f);
    if (h) oled_rect(x + 1, y, 2, h, true); else oled_pixel(x + 2, y + 1, true);
    oled_line(x, y + 4, x, y + 12, true);
    oled_line(x, y + 12, x - 3, y + 19, true); oled_line(x, y + 12, x + 3, y + 19, true);
    oled_line(x, y + 6, x + 4, y + 6 + arm, true);
    oled_line(x, y + 6, x - 4, y + 9, true);
}
static void note_glyph(int x, int y)
{
    oled_disc(x, y, 1, true);
    oled_line(x + 1, y, x + 1, y - 5, true);
    oled_line(x + 1, y - 5, x + 3, y - 4, true);
}

bool scene_splash(int f)
{
    const int ground = 60, sx = 86;
    oled_hline_pat(0, ground + 1, OLED_W, 0x55, true);
    // the mic stand rises
    float r = ease_back(clampf((float)f / 12.0f, 0.0f, 1.0f));
    int top = ground - (int)(22.0f * r);
    oled_line(sx, ground, sx, top, true);
    oled_line(sx - 4, ground, sx + 4, ground, true);
    oled_line(sx, top, sx - 5, top + 3, true);
    oled_rrect(sx - 8, top + 1, 5, 5, 2, true);
    // the singer walks in and stops at the mic
    float w = ease_out(clampf((float)(f - 6) / 16.0f, 0.0f, 1.0f));
    int px = -10 + (int)(w * 82.0f);
    float open = f > 24 ? 0.5f + 0.5f * sin_turns((float)f * 0.09f) : 0.0f;
    if (f > 4) figure(px, ground, open, f > 22 ? -2 : 3);
    // the choir behind, rising one by one, singing back
    if (f > 44) {
        for (int k = 0; k < 2; k++) {
            float c = ease_back(clampf((float)(f - 44 - k * 6) / 10.0f, 0.0f, 1.0f));
            int rise = (int)((1.0f - c) * 24.0f);
            if (c > 0.0f) figure(px - 22 - k * 18, ground + rise, k ? open : 1.0f - open, k ? 1 : -1);
        }
    }
    // notes from the mouth: out to the right and up, each splitting into three (the harmonies)
    int my = ground - 20;
    for (int k = 0; k < 6; k++) {
        int t = f - 26 - k * 7;
        if (t < 0 || t > 34) continue;
        int nx = px + 8 + t, ny = my - 2 - t / 2 - (int)(sin_turns((float)t * 0.06f) * 2.0f);
        int spread = t > 10 ? (t - 10) / 2 : 0;
        for (int s = -1; s <= 1; s++) {
            if (!spread && s) continue;
            int y = ny + s * spread;
            if (y > 22 && y < ground - 2 && nx < OLED_W - 4) note_glyph(nx, y);
        }
    }
    // the reels roll in, the title drops between them
    if (f > 60) {
        float c = ease_out(clampf((float)(f - 60) / 12.0f, 0.0f, 1.0f));
        int off = (int)((1.0f - c) * 40.0f);
        oled_rect(0, 0, OLED_W, 22, false);
        reel(10 - off, 8, (float)f * 0.03f);
        reel(OLED_W - 11 + off, 8, (float)f * 0.03f);
        if (c > 0.99f) { oled_line(17, 2, OLED_W - 18, 2, true); oled_line(17, 14, OLED_W - 18, 14, true); }
    }
    if (f >= 66) {
        float t = ease_bounce(clampf((float)(f - 66) / 14.0f, 0.0f, 1.0f));
        int y = (int)(-14.0f + 17.0f * t);
        oled_rect(24, y - 1, 80, 13, false);
        oled_ftext_c(F_MED, 64, y + 1, "SING", 4);
        if (f >= 80) oled_ftext_c(F_TINY, 64, 17, "TUNE  STACK  LOOP", 1);
    }
    return f < 100;
}

// ---------------------------------------------------------------- screensaver: notes drifting up
void scene_saver(uint32_t f)
{
    for (int k = 0; k < 5; k++) {
        uint32_t t = (f + (uint32_t)k * 53u) % 240u;
        int x = 12 + (int)((uint32_t)(k * 29 + (int)(f / 240u) * 17) % 104u) + (int)(sin_turns((float)t * 0.01f + (float)k) * 6.0f);
        int y = 70 - (int)(t * 72u / 240u);
        if (y > 4 && y < 64) note_glyph(x, y);
    }
}
