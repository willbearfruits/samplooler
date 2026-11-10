// The screen while playing (pixel.h): the song as a little level that scrolls by. The lead's notes are platforms
// and a runner hops along them; the bass is blocks on the ground; a kick shakes the ground, a snare throws sparks,
// hats twinkle as stars; the chord's name sits in the sky. On the right the chip's own voices, each with the wave it
// makes now. Tilt the cartridge and the picture tears with the sound. Also the splash and the screensaver.
#include "pixel.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

int   px_beat_flash(void);
float px_out_env(void);

#define RUN_X   34                         // where the runner stands
#define STEP_W  6                          // a step's width in the level
#define SKY_Y   17
#define GROUND  57
#define VOX_X   100                        // the voices' column

static parts_t  parts;
static uint32_t seed = 0xC0FFEE11u;
static float    cam = 16.0f, run_y = GROUND - 9, run_vy;
static int      last_step = -1, shake, star_t[6], run_frame, land;
static uint32_t last_frame;

// the runner: 8 x 9, two strides and a jump
static const char *const RUN_A[9] = { "..####..", ".######.", ".#.##.#.", ".######.", "..####..", ".######.", "#.####.#", "..#..#..", ".##...#." };
static const char *const RUN_B[9] = { "..####..", ".######.", ".#.##.#.", ".######.", "..####..", ".######.", "#.####.#", "..#..#..", "..#..##." };
static const char *const RUN_J[9] = { "#.####.#", "#######.", ".#.##.#.", ".######.", "..####..", "..####..", "..####..", ".#....#.", "#......#" };
static const char *const RUN_I[9] = { "........", "..####..", ".######.", ".#.##.#.", ".######.", "..####..", ".######.", "..#..#..", ".##..##." };

// the bar and the step `ahead` steps from now: its pattern, or NULL where the song has nothing to show
static const px_pattern_t *pat_at(int ahead, int *step)
{
    int s = px_pos.step + ahead, bar = 0;
    while (s >= PX_STEPS) { s -= PX_STEPS; bar++; }
    while (s < 0) { s += PX_STEPS; bar--; }
    *step = s;
    if (px_pos.mode == PM_PATTERN) return &px_song.pat[px_pos.pattern & 15];
    int n = px_song.order_n ? px_song.order_n : 1;
    int slot = ((int)px_pos.slot + bar) % n; if (slot < 0) slot += n;
    return &px_song.pat[px_song.order[slot] & 15];
}

static int plat_y(int row) { return clampi(38 - (int)lroundf(((float)row - cam) * 2.0f), SKY_Y + 9, GROUND - 6); }

// a platform in the console's own style
static void platform(int x, int y, int w, bool accent)
{
    switch (px_pos.console) {
    case CON_BEEPER:  oled_rect(x, y, w, 1, true); break;
    case CON_ATARI:   oled_rect(x, y, w, 3, true); oled_rect(x, y + 1, w, 1, false); break;
    case CON_AY:      oled_rect(x, y, w, 2, true); break;
    case CON_GAMEBOY: oled_dither(x, y, w, 3, 2, true); oled_rect(x, y, w, 1, true); break;
    case CON_C64:     oled_rect(x, y, w, 3, true); break;
    case CON_AMIGA:   oled_dither(x, y, w, 3, 3, true); break;
    case CON_MEGA:    oled_rect(x, y, w, 1, true); oled_rect(x, y + 2, w, 1, true); break;
    default:          oled_rect(x, y, w, 2, true); oled_hline_pat(x, y + 2, w, 0x55, true); break;   // NES: a brick's edge
    }
    if (accent) oled_rect(x, y - 2, 2, 1, true);
}

// one voice of the chip, w x h: the wave it makes now
static void scope(int x, int y, int w, int h, const px_vview_t *v, uint32_t frame)
{
    if (h < 6) {                                                 // many voices: a bar each
        int l = v->kind == VW_OFF ? 0 : (int)(v->level * (float)(w - 2) + 0.5f);
        oled_pixel(x, y + h / 2, true);
        if (l > 0) { if (v->kind == VW_NOISE) oled_dither(x + 2, y, l, h - 1, 2, true); else oled_rect(x + 2, y, l, h - 1, true); }
        return;
    }
    int mid = y + h / 2, amp = v->kind == VW_OFF ? 0 : 1 + (int)(v->level * (float)(h / 2 - 1) + 0.5f);
    if (amp > h / 2) amp = h / 2;
    if (!amp) { oled_hline_pat(x, mid, w, 0x11, true); return; }
    float hz = v->hz > 20.0f ? v->hz : 200.0f;
    int per = clampi((int)(2200.0f / hz) + 4, 4, w);
    int ph = (int)(frame * 3) % per;
    int py = mid;
    for (int i = 0; i < w; i++) {
        int k = (i + ph) % per, yy = mid;
        float t = (float)k / (float)per;
        switch (v->kind) {
        case VW_PULSE:  yy = mid + (t < clampf(v->shape, 0.1f, 0.9f) ? -amp : amp); break;
        case VW_TRI:    yy = mid + (int)lroundf((float)amp * (4.0f * fabsf(t - 0.5f) - 1.0f)); break;
        case VW_SAW:    yy = mid + amp - (int)lroundf(2.0f * (float)amp * t); break;
        case VW_BUZZ:   yy = mid + amp - (int)lroundf(2.0f * (float)amp * t * t); break;
        case VW_NOISE:  yy = mid + (int)(rng_next(&seed) % (uint32_t)(2 * amp + 1)) - amp; break;
        case VW_WAVE:   yy = mid - (int)lroundf((float)amp * sin_turns(t) * (k & 2 ? 1.0f : 0.7f)); break;
        case VW_SAMPLE: yy = mid - (int)lroundf((float)amp * sin_turns(t) * sin_turns(t * 3.0f + 0.1f)); break;
        default:        yy = mid - (int)lroundf((float)amp * sin_turns(t + 0.25f * sin_turns(t * 2.0f) * v->shape)); break;   // VW_FM
        }
        if (i && v->kind != VW_NOISE) oled_line(x + i - 1, py, x + i, yy, true); else oled_pixel(x + i, yy, true);
        py = yy;
    }
}

// the cartridge is tilted: blocks of the picture land in the wrong place
static void tear(float g, int cover)
{
    int n = (int)(g * 10.0f), ymax = OLED_H - cover - 8;
    for (int k = 0; k < n; k++) {
        int x = (int)(rng_next(&seed) % 15) * 8, y = 16 + (int)(rng_next(&seed) % (uint32_t)((ymax - 16) / 8 + 1)) * 8;
        uint32_t r = rng_next(&seed);
        if (r & 1) { oled_invert(x, y, 8, 8); continue; }
        int sx = (int)((r >> 4) % 15) * 8, sy = 16 + (int)((r >> 9) % 5) * 8;      // a tile copied from somewhere else
        for (int j = 0; j < 8; j++) for (int i = 0; i < 8; i++) oled_pixel(x + i, y + j, oled_get(sx + i, sy + j));
    }
    if (g > 0.4f) {                                              // a band of rows slips sideways
        int y = 16 + (int)(rng_next(&seed) % (uint32_t)(ymax - 16)), d = 2 + (int)(g * 9.0f);
        for (int j = y; j < y + 4 && j < OLED_H - cover; j++) for (int i = OLED_W - 1; i >= d; i--) oled_pixel(i, j, oled_get(i - d, j));
    }
}

void scene_draw(const view_t *v)
{
    float dt = 0.04f;
    uint32_t df = v->frame - last_frame; last_frame = v->frame;
    if (df > 8) df = 1;
    bool run = px_pos.running;
    int sx = shake > 0 ? ((v->frame & 1) ? 1 : -1) : 0;
    if (shake > 0) shake--;

    // the header: the console, the song's name, the meter; the bar's sixteen steps along its last row
    ui_head(px_console()->name, run);
    for (int s = 0; s < PX_STEPS; s++) {
        int x = 16 + s * 6;
        if (run && s == px_pos.step) oled_rect(x, 14, 4, 2, true); else oled_pixel(x + 1, 15, s % 4 == 0);
    }

    // what happens on this step: the drums
    int st = run ? px_pos.step + 16 * px_pos.slot + 1024 * px_pos.pattern : -1;
    if (st != last_step) {
        last_step = st;
        if (run) {
            uint8_t d = px_song.pat[px_pos.pattern & 15].drums[px_pos.step];
            if ((px_mute >> PART_DRUMS) & 1) d = 0;
            if (d & DR_KICK) { shake = 2; parts_burst(&parts, RUN_X + 4, GROUND, 4, 26, 0.3f, PART_DOT, &seed); }
            if (d & DR_SNARE) parts_burst(&parts, VOX_X - 14, 30, 7, 46, 0.32f, PART_SPARK, &seed);
            if (d & DR_HAT) star_t[rng_next(&seed) % 6] = 3;
            if (d & DR_OPEN) for (int i = 0; i < 6; i++) star_t[i] = 5;
            if (d & DR_TOM) parts_emit(&parts, VOX_X - 24, 44, -20, -30, 0.4f, PART_RING, 2);
            run_frame ^= 1;
        }
    }

    // the level: the lead's notes as platforms, the bass as blocks on the ground. It scrolls with the song
    float frac = px_pos.tps ? (float)px_pos.tick / (float)px_pos.tps : 0.0f;
    int off = run ? (int)(frac * STEP_W) : 0;
    float target_y = GROUND - 9;
    bool on_note = false;
    if (run) {
        for (int a = -6; a <= 11; a++) {
            int s; const px_pattern_t *p = pat_at(a, &s);
            if (a < 0 && px_pos.bars == 0 && px_pos.step + a < 0) continue;     // nothing before the song began
            int x = RUN_X + a * STEP_W - off + sx;
            if (x < -STEP_W || x > VOX_X - 4) continue;
            int w = x + STEP_W > VOX_X - 3 ? VOX_X - 3 - x : STEP_W;
            uint8_t c = p->lead[s];
            if ((c & NC_ROW) && !((px_mute >> PART_LEAD) & 1)) {
                int y = plat_y((c & NC_ROW) - 1);
                platform(x, y, (c & NC_TIE) ? w : w - 1, (c & NC_ACCENT) && !(c & NC_TIE));
                if (a == 0) { target_y = (float)(y - 9); on_note = true; cam += 0.08f * ((float)((c & NC_ROW) - 1) - cam); }
            }
            c = p->bass[s];
            if ((c & NC_ROW) && !((px_mute >> PART_BASS) & 1)) {
                int h = 2 + ((c & NC_ROW) - 1) % 4;
                oled_rect(x, GROUND - h, (c & NC_TIE) ? w : w - 1, h, true);
            }
        }
    }
    oled_hline_pat(0, GROUND + sx, VOX_X - 3, shake ? 0xFF : 0x77, true);
    oled_hline_pat(0, GROUND + 2, VOX_X - 3, 0x11, true);

    // the sky: stars for the hats, the chord's name
    static const uint8_t star_x[6] = { 8, 27, 49, 66, 82, 93 }, star_y[6] = { 21, 26, 19, 24, 20, 27 };
    for (int i = 0; i < 6; i++) {
        int x = star_x[i], y = star_y[i];
        if (star_t[i] > 0) { star_t[i]--; oled_rect(x - 1, y, 3, 1, true); oled_rect(x, y - 1, 1, 3, true); }
        else if (((v->frame >> 3) + (uint32_t)i) % 7) oled_pixel(x, y, true);
    }
    if (run && px_pos.degree) { char b[8]; px_chord_name(px_pos.degree, px_scale(), b, sizeof b); oled_ftext_ex(F_TINY, 1, SKY_Y, b, 1); }

    // the runner: on the note that sounds, else on the ground; a hop at each new note
    if (run) {
        if (fabsf(target_y - run_y) > 0.5f) { run_vy = (target_y - run_y) * 0.55f; land = 2; }
        run_y += run_vy; run_vy *= 0.4f;
        if (fabsf(target_y - run_y) <= 0.5f) run_y = target_y;
    } else run_y += 0.3f * ((float)(GROUND - 9) - run_y);
    const char *const *spr = !run ? RUN_I : land > 0 && !on_note ? RUN_J : fabsf(run_vy) > 1.2f ? RUN_J : run_frame ? RUN_A : RUN_B;
    if (land > 0) land--;
    if (!run && (v->frame / 12) % 5 == 0) spr = RUN_A;                              // waiting: it shifts its feet
    ui_sprite(RUN_X - 1 + sx, (int)lroundf(run_y), spr, 9, false, true);

    parts_step(&parts, dt * (float)df, 120.0f, 0.9f);
    parts_draw(&parts);

    // the chip's voices, each with its wave; under them the four parts' lamps
    px_vview_t vv[PX_VOICES_MAX];
    int n = px_console()->view(vv, PX_VOICES_MAX);
    if (n > 0) {
        int top = SKY_Y + 1, room = 36, h = room / n; if (h > 11) h = 11;
        for (int i = 0; i < n; i++) scope(VOX_X, top + i * h, OLED_W - VOX_X, h - 1, &vv[i], v->frame);
    }
    oled_rect(VOX_X - 3, SKY_Y, 1, OLED_H - SKY_Y, false);
    oled_hline_pat(VOX_X - 3, SKY_Y, 1, 1, true);
    for (int y = SKY_Y; y < OLED_H; y += 2) oled_pixel(VOX_X - 3, y, true);
    static const char *const lamp[4] = { "L", "C", "B", "D" };
    static const uint8_t lamp_part[4] = { PART_LEAD, PART_CHORDS, PART_BASS, PART_DRUMS };
    for (int i = 0; i < 4; i++) {
        int x = VOX_X + 1 + i * 7, y = 56;
        bool muted = (px_mute >> lamp_part[i]) & 1;
        oled_rect(x, y, 6, 7, false);
        if (muted) { oled_hline_pat(x, y + 3, 6, 0x15, true); continue; }
        oled_ftext_ex(F_TINY, x + 1, y + 1, lamp[i], 0);
        oled_invert(x, y, 6, 7);
    }
    // where the song is
    char b[20];
    snprintf(b, sizeof b, "%c %d/%d", 'A' + (px_pos.pattern & 15), px_pos.slot + 1, px_song.order_n);
    if (!run) snprintf(b, sizeof b, "%d BPM", (int)(px_pos.bpm + 0.5f));
    oled_rect(40, 59, 50, 5, false);
    oled_ftext_ex(F_TINY, 44, 59, b, 1);
    if (px_pos.armed && (v->frame & 4)) oled_ftext_ex(F_TINY, 2, 59, "WAIT", 1);

    float g = px_glitch_now();
    if (g > 0.02f) tear(g, v->cover);
}

// ---------------------------------------------------------------- the splash and the screensaver
// PIXEL in blocks: each letter 5 x 5, each dot a square that falls into its place
static const char *const LOGO[5] = {
    "####. #####. #...#. #####. #....",
    "#...# ..#... .#.#.. #..... #....",
    "####. ..#... ..#... ####.. #....",
    "#.... ..#... .#.#.. #..... #....",
    "#.... #####. #...#. #####. #####",
};
static void logo(int x0, int y0, int sz, int f)                 // f: frames since it began (large: all in place)
{
    int k = 0;
    for (int r = 0; r < 5; r++) {
        const char *row = LOGO[r];
        for (int c = 0; row[c]; c++) {
            if (row[c] != '#') continue;
            int delay = c / 2 + (4 - r) * 2 + (k++ % 3), t = f - delay;
            if (t < 0) continue;
            float e = t >= 10 ? 1.0f : ease_bounce((float)t / 10.0f);
            int y = (int)lroundf(-8.0f + ((float)(y0 + r * sz) + 8.0f) * e);
            oled_rect(x0 + c * sz, y, sz - 1, sz - 1, true);
        }
    }
}

bool scene_splash(int f)
{
    logo(4, 14, 4, f);
    if (f > 34) oled_ftext_c(F_SMALL, 64, 42, "A CHIPTUNE MACHINE", 1);
    if (f > 44) {                                                // the runner crosses under it
        int x = -10 + (f - 44) * 4;
        ui_sprite(x, 52, (f / 2) & 1 ? RUN_A : RUN_B, 9, false, true);
        oled_hline_pat(0, 62, OLED_W, 0x77, true);
    }
    return f < 80;
}

void scene_saver(uint32_t f)
{
    int x = 2 + (int)((sin_turns((float)f * 0.0031f) * 0.5f + 0.5f) * 58.0f);
    int y = 18 + (int)((sin_turns((float)f * 0.0047f + 0.3f) * 0.5f + 0.5f) * 34.0f);
    logo(x, y, 2, 1000);
}
