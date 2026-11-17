// Rev E (the Vessel) in PIXEL: no stick, a second knob on the left and a sensor that measures a hand's height.
// In the editor the left knob is the cursor along the steps and the right knob writes; in play the left knob is the
// tempo and the hand is the breakdown (the LEAD goes, then the CHORDS, then the BASS, and they come back when it
// leaves). No rev E board exists yet: these scenarios are how it is checked. Each one leaves the sim a rev C again.
#include "sim.h"
#include "pixel.h"
#include "link.h"
#include "oled.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void sim_menu_close(void);
void front_midi(uint8_t st, uint8_t d1, uint8_t d2);
void edit_test_state(int *row, int *slot, int *step);
void edit_test_home(void);

#define M_LEAD   (1u << PART_LEAD)
#define M_CHORDS (1u << PART_CHORDS)
#define M_BASS   (1u << PART_BASS)

static int  tag(const char *t) { int p = param_by_tag(t); if (p < 0) note("no param '%s'", t); return p; }
static void set(const char *t, float v) { int p = tag(t); if (p >= 0) param_set(p, v); }
static void program(int n) { front_midi(0xC0, (uint8_t)n, 0); sim_run(0.1f); }
static void calm(void) { SI.mic = NULL; SI.mic_u = NULL; SI.imu = true; SI.rec = false; SI.pressed = false; sim_tilt(0, 0); }
static void rev_c(void) { SI.joy = true; SI.jx = SI.jy = 0; SI.lknob = false; SI.lk = 0; SI.tof = false; SI.hand_mm = 0; }
static void push(float x, float y) { SI.jx = x; SI.jy = y; sim_run(0.05f); SI.jx = SI.jy = 0; sim_run(0.1f); }      // the stick in play
static void stick(float x, float y) { SI.jx = x; SI.jy = y; sim_run(0.03f); SI.jx = SI.jy = 0; sim_run(0.06f); }   // and in the editor
static void drag(int d) { SI.pressed = true; sim_tick(); SI.enc = d; sim_tick(); sim_tick(); SI.pressed = false; sim_run(0.05f); }
// rev E, the row: the right knob held down while the left one turns, then let go. On the board a press that short
// ends as a tap (input.c: 40 to 500 ms), so it does here.
static void row_turn(int d) { SI.pressed = true; sim_run(0.05f); sim_lturn(d); sim_run(0.05f); SI.pressed = false; SI.tap = true; sim_tick(); sim_run(0.05f); }

// the first moment after t0 (seconds) that the output rises over thr after having been under thr / 3; -1: never
static float onset(float t0, float thr)
{
    int i = (int)(t0 * AUDIO_SR);
    bool low = false;
    for (; i < sim_n; i++) {
        float a = fabsf(sim_out[i]);
        if (a < thr / 3.0f) low = true;
        else if (a > thr && low) return (float)i / (float)AUDIO_SR;
    }
    return -1.0f;
}
// a hat on every beat: the time from one to the next, the mean of n beats after t0, in ms
static float beat_ms(float t0, int n)
{
    float a = onset(t0, 0.02f), b = a;
    for (int k = 0; k < n && b >= 0; k++) b = onset(b + 0.09f, 0.02f);
    return a < 0 || b < 0 ? -1.0f : (b - a) * 1000.0f / (float)n;
}
// the level of three whole bars, from the next bar line (one bar alone moves a few per cent with the chip's phases)
static float bar_rms(void)
{
    int guard = 0;
    while (px_pos.step != PX_STEPS - 1 && guard++ < 1000) sim_tick();
    while (px_pos.step != 0 && guard++ < 1000) sim_tick();
    float t0 = sim_t, bars = 3.0f * 240.0f / px_pos.bpm;
    sim_run(bars);
    return m_rms(t0, t0 + bars);
}

// ---------------------------------------------------------------- the editor: move with the left, write with the right
static const int8_t WALK[] = { 17, -3, 34, -50 };                // detents, one at a time: over both bar lines and both ends
#define WALK_N 104
static void go(int slot, int step)                               // the cursor there, with the left knob alone
{
    int r, sl, st;
    for (int k = 0; k < 80; k++) {
        edit_test_state(&r, &sl, &st);
        if (sl == slot && st == step) return;
        sim_lturn(sl < slot || (sl == slot && st < step) ? 1 : -1);
    }
}
static void write_cell(const uint8_t *cell, int want)            // the right knob: a note on the empty step, then to its pitch
{
    sim_turn(1); sim_run(0.02f);
    int got = *cell & NC_ROW;
    if (got != want) { sim_turn(want - got); sim_run(0.02f); }
}

static void editor_two_knobs(void)
{
    section("pixel: rev E, the editor with two knobs");
    calm(); rev_c(); sim_menu_close(); px_stop(); program(PX_NSONGS - 1); sim_run(0.3f);    // EMPTY
    sim_rev_e(); sim_run(0.2f);
    const ctl_t *c = front_ctl();
    px_pattern_t *A = &px_song.pat[0], *B = &px_song.pat[1];
    int row, slot, step;
    edit_test_home();
    sim_hold_knob(); sim_run(0.2f);
    edit_test_state(&row, &slot, &step);
    check(edit_active() && c->has_lknob && !c->has_joy && row == 1 && step == 0, "rev E, the right knob held: the editor, on the LEAD's first step");
    sim_run(2.8f); sim_shot("reve-edit-hint");

    sim_lturn(1); sim_lturn(1); sim_lturn(1);
    edit_test_state(&row, &slot, &step);
    check(row == 1 && slot == 0 && step == 3, "three detents of the left knob: three steps along (row %d, step %d)", row, step);
    sim_lturn(-2);
    edit_test_state(&row, &slot, &step);
    check(row == 1 && step == 1, "two detents back on one tick: two steps back (step %d)", step);

    // the row: the right knob held, the left one turned. Up to SONG, a second bar with a pattern of its own, down again
    row_turn(-1);
    edit_test_state(&row, &slot, &step);
    bool none = true; for (int i = 0; i < PX_STEPS; i++) if (A->lead[i]) none = false;
    check(edit_active() && row == 0 && none, "the right knob held down and the left turned: the row above (SONG), and that press puts no note");
    sim_tap(); sim_run(0.1f); drag(1); sim_run(0.1f);
    check(px_song.order_n == 2 && px_song.order[0] == 0 && px_song.order[1] == 1, "on SONG the right knob adds a bar and gives it a pattern of its own (%d bars, A then %c)", px_song.order_n, 'A' + px_song.order[1]);
    sim_lturn(-1);
    edit_test_state(&row, &slot, &step);
    check(row == 0 && slot == 0, "on SONG the left knob goes from bar to bar (bar %d)", slot + 1);
    SI.pressed = true; sim_tick(); sim_lturn(1); sim_run(0.8f); SI.pressed = false; sim_tick(); sim_run(0.05f);
    edit_test_state(&row, &slot, &step);
    check(edit_active() && row == 1 && slot == 0, "held 0.8 s while choosing the row: LEAD again, and the editor stays open (that hold is not the way out)");

    // along the steps and over the bar lines, a detent at a time; every position kept to compare with the stick's
    static uint8_t at_e[WALK_N][2], at_c[WALK_N][2];
    go(0, 0);
    int n = 0, after16[2] = { -1, -1 }, after17back3[2] = { -1, -1 };
    for (int w = 0; w < (int)sizeof WALK; w++) for (int k = 0; k < abs(WALK[w]); k++) {
        sim_lturn(WALK[w] > 0 ? 1 : -1);
        edit_test_state(&row, &slot, &step);
        at_e[n][0] = (uint8_t)slot; at_e[n][1] = (uint8_t)step; n++;
        if (n == 16) { after16[0] = slot; after16[1] = step; }
        if (n == 20) { after17back3[0] = slot; after17back3[1] = step; }
    }
    check(after16[0] == 1 && after16[1] == 0, "sixteen detents from the bar's first step: the next bar's first step (bar %d, step %d)", after16[0] + 1, after16[1]);
    check(after17back3[0] == 0 && after17back3[1] == 14, "one more and three back: over the bar line again, to step 14 of the first bar (bar %d, step %d)", after17back3[0] + 1, after17back3[1]);

    // a phrase written with the two knobs: the left one moves, the right one puts the note, its pitch, its length, its accent
    go(0, 0); sim_turn(1); sim_run(0.02f);
    int r0 = A->lead[0] & NC_ROW;                                 // (the row a new note gets is wherever the earlier tests left it)
    go(0, 2); write_cell(&A->lead[2], r0 + 2);
    go(0, 4); write_cell(&A->lead[4], r0 + 4); drag(1);           // press and turn: two steps long
    go(0, 7); write_cell(&A->lead[7], r0 + 2);
    go(1, 0); write_cell(&B->lead[0], r0 + 5);                    // (nine detents on, over the bar line)
    go(1, 3); write_cell(&B->lead[3], r0 + 3); sim_tap(); sim_run(0.05f);   // a press on a note: its accent
    go(0, 15); write_cell(&A->lead[15], r0 + 1);                  // (four back, over the bar line)
    uint8_t wa[PX_STEPS] = { 0 }, wb[PX_STEPS] = { 0 };
    wa[0] = (uint8_t)r0; wa[2] = (uint8_t)(r0 + 2); wa[4] = (uint8_t)(r0 + 4); wa[5] = (uint8_t)((r0 + 4) | NC_TIE);
    wa[7] = (uint8_t)(r0 + 2); wa[15] = (uint8_t)(r0 + 1);
    wb[0] = (uint8_t)(r0 + 5); wb[3] = (uint8_t)((r0 + 3) | NC_ACCENT);
    int other = 0;
    for (int i = 0; i < PX_STEPS; i++) other += (A->bass[i] != 0) + (A->chords[i] != 0) + (A->drums[i] != 0) + (B->bass[i] != 0) + (B->chords[i] != 0) + (B->drums[i] != 0);
    check(r0 > 0 && !memcmp(A->lead, wa, PX_STEPS) && !memcmp(B->lead, wb, PX_STEPS) && other == 0,
          "a phrase written with the two knobs: the song holds exactly those notes (bar A steps 0 2 4-5 7 15, bar B steps 0 3; rows %d %d %d %d %d | %d %d accented; nothing else)",
          r0 - 1, r0 + 1, r0 + 3, r0 + 1, r0, r0 + 4, r0 + 2);
    if (memcmp(A->lead, wa, PX_STEPS) || memcmp(B->lead, wb, PX_STEPS)) {
        char t[2][80]; for (int b = 0; b < 2; b++) { int k = 0; for (int i = 0; i < PX_STEPS; i++) k += snprintf(t[b] + k, sizeof t[b] - (size_t)k, "%02x ", (b ? B : A)->lead[i]); }
        note("A: %s", t[0]); note("B: %s", t[1]);
    }
    go(0, 4); sim_run(0.3f); sim_shot("reve-edit");

    // the left knob's push is REC: the bar goes round, as REC does on any board
    sim_rec(0.1f); sim_run(2.2f);
    float r = m_rms(sim_t - 1.5f, sim_t);
    check(px_pos.running && px_pos.mode == PM_PATTERN && r > 0.01f, "the left knob pushed (REC): the bar goes round (rms %.3f)", (double)r);
    // a hand over the sensor does nothing in the editor
    px_song_t before = px_song;
    sim_hand(60, 0.6f);
    int r2, sl2, st2; edit_test_state(&r2, &sl2, &st2);
    check(c->hand > 0.9f && px_hand_mute == 0 && px_muted() == 0 && r2 == 1 && sl2 == 0 && st2 == 4 && !memcmp(&before, &px_song, sizeof before) && px_pos.running,
          "a hand at 60 mm over the sensor in the editor: nothing is muted, nothing moves, nothing is written");
    sim_hand(0, 0.6f);
    sim_rec(0.1f); sim_run(0.5f);
    // the rows below, with the same gesture
    row_turn(1); go(0, 0); sim_tap(); sim_run(0.1f);
    edit_test_state(&row, &slot, &step);
    bool chord = row == 2 && (A->chords[0] & CH_DEGREE);
    row_turn(2); go(0, 4); sim_tap(); sim_run(0.1f);
    edit_test_state(&row, &slot, &step);
    check(chord && row == 4 && (A->drums[4] & 0x1F) && !memcmp(A->lead, wa, PX_STEPS), "the rows below the same way: a chord put on CHORDS, a drum on DRUMS (the LEAD untouched)");
    sim_hold_knob(); sim_run(0.3f);
    check(!edit_active() && !px_pos.running, "the right knob held leaves the editor");

    // the same walk with the stick on a rev C: every position the same
    rev_c(); sim_run(0.3f);
    edit_test_home();
    sim_hold_knob(); sim_run(0.2f);
    n = 0; int differ = 0;
    for (int w = 0; w < (int)sizeof WALK; w++) for (int k = 0; k < abs(WALK[w]); k++) {
        stick(WALK[w] > 0 ? 1.0f : -1.0f, 0);
        edit_test_state(&row, &slot, &step);
        at_c[n][0] = (uint8_t)slot; at_c[n][1] = (uint8_t)step;
        if (at_c[n][0] != at_e[n][0] || at_c[n][1] != at_e[n][1]) differ++;
        n++;
    }
    check(edit_active() && n == WALK_N && differ == 0, "the stick's left and right on a rev C, the same %d moves: the cursor is where the left knob put it every time (%d differ)", n, differ);
    // and on that board (a stick, no left knob, no sensor) the new controls are nothing
    px_song_t keep = px_song;
    edit_test_state(&row, &slot, &step);
    SI.lk = 3; sim_tick(); SI.lk = -2; sim_tick(); SI.pressed = true; sim_tick(); SI.lk = 1; sim_tick(); SI.pressed = false; sim_tick();
    SI.hand_mm = 60; sim_run(0.5f); SI.hand_mm = 0;
    edit_test_state(&r2, &sl2, &st2);
    check(edit_active() && !c->has_lknob && !c->has_hand && r2 == row && sl2 == slot && st2 == step && !memcmp(&keep, &px_song, sizeof keep) && px_hand_mute == 0,
          "on the board with the joystick, in the editor: counts on the left knob's pins and a hand move nothing and write nothing");
    sim_hold_knob(); sim_run(0.3f);

    // a board with neither stick nor left knob (the Mini), and a rev E with SYSTEM > LEFT KNOB OFF: the one knob walks
    // the cursor and on into the next row, press and turn changes the note, as before
    for (int k = 0; k < 2; k++) {
        program(PX_NSONGS - 1); sim_run(0.2f);
        if (k) { sim_rev_e(); param_set(CP(CP_LKNOB), 0); } else { rev_c(); SI.joy = false; }
        sim_run(0.2f);
        edit_test_home();
        sim_hold_knob(); sim_run(0.2f);
        sim_turn(1); sim_turn(1); sim_turn(1); sim_run(0.05f);
        sim_lturn(2); sim_run(0.05f);                              // (the left knob's pins: ignored)
        edit_test_state(&row, &slot, &step);
        bool walked = row == 1 && step == 3;
        sim_tap(); sim_run(0.1f);
        uint8_t c0 = A->lead[3];
        SI.pressed = true; sim_tick(); SI.enc = 2; sim_tick(); sim_tick(); SI.pressed = false; sim_run(0.05f);
        bool wrote = c0 && (A->lead[3] & NC_ROW) == (c0 & NC_ROW) + 2;
        for (int i = 0; i < 14; i++) sim_turn(1);
        edit_test_state(&row, &slot, &step);
        check(edit_active() && !c->has_lknob && !c->has_joy && walked && wrote && row == 2 && step == 1,
              "%s: the knob walks the cursor (step 3), a press and press-and-turn write, and past the bar's end is the next row (row %d, step %d), as before",
              k ? "a rev E with SYSTEM > LEFT KNOB OFF" : "a board with neither stick nor left knob", row, step);
        sim_hold_knob(); sim_run(0.3f);
        if (k) param_set(CP(CP_LKNOB), 1);
    }
    rev_c(); program(0); sim_run(0.3f);
}

// ---------------------------------------------------------------- play: the left knob is the tempo
static void tempo_knob(void)
{
    section("pixel: rev E, the left knob is the tempo");
    calm(); rev_c(); sim_menu_close(); px_stop(); program(PX_NSONGS - 1); set("tmpo", 150); set("cons", (float)CON_NES); sim_run(0.5f);
    for (int b = 0; b < 4; b++) px_song.pat[0].drums[4 * b] = DR_HAT;            // a hat on every beat, to measure
    sim_rev_e(); sim_run(0.2f);
    int tm = tag("tmpo");
    uint32_t learned = meta.learned; meta.learned &= ~((1u << LEARN_LKNOB) | (1u << LEARN_HAND));
    px_play_song(0); sim_run(0.2f);
    float t0 = sim_t; sim_run(2.4f);
    float b150 = beat_ms(t0, 4);
    sim_lturn(1); sim_run(0.08f);
    check((int)pv[tm] == 151 && fabsf(px_pos.bpm - 151.0f) < 0.05f, "150 BPM, one detent to the right: TUNE > TEMPO is 151 and 151 plays (%.2f)", (double)px_pos.bpm);
    sim_shot("reve-tempo");
    check((meta.learned >> LEARN_LKNOB) & 1 && front_status()->edited, "the turn is an edit of the song, as in the menu, and the left knob's hint is done");
    sim_lturn(-3); sim_run(0.05f);
    check((int)pv[tm] == 148 && fabsf(px_pos.bpm - 148.0f) < 0.05f, "three detents back on one tick: three BPM, 148 (%.2f)", (double)px_pos.bpm);
    for (int i = 0; i < 22; i++) { sim_lturn(-1); sim_tick(); }
    sim_run(0.3f); t0 = sim_t; sim_run(2.8f);
    float b126 = beat_ms(t0, 4);
    check((int)pv[tm] == 126 && fabsf(b150 - 400.0f) < 1.5f && fabsf(b126 - 476.19f) < 1.5f,
          "22 more: 126 BPM. The beat that plays: %.1f ms at 150 (400.0), %.1f ms at 126 (476.2)", (double)b150, (double)b126);
    for (int i = 0; i < 40; i++) sim_lturn(4);
    sim_run(0.3f); t0 = sim_t; sim_run(1.6f);
    float b240 = beat_ms(t0, 4);
    sim_lturn(3); sim_run(0.05f);
    check((int)pv[tm] == 240 && fabsf(px_pos.bpm - 240.0f) < 0.05f && fabsf(b240 - 250.0f) < 1.5f, "turned far to the right: it stops at 240, and further turns leave it (a beat every %.1f ms)", (double)b240);
    for (int i = 0; i < 60; i++) sim_lturn(-4);
    sim_run(0.3f); t0 = sim_t; sim_run(5.4f);
    float b60 = beat_ms(t0, 4);
    sim_lturn(-3); sim_run(0.05f);
    check((int)pv[tm] == 60 && fabsf(px_pos.bpm - 60.0f) < 0.05f && fabsf(b60 - 1000.0f) < 1.5f, "far to the left: it stops at 60 (a beat every %.1f ms)", (double)b60);
    px_stop(); sim_run(0.8f);
    sim_lturn(30); sim_run(0.1f);
    check((int)pv[tm] == 90 && fabsf(px_pos.bpm - 90.0f) < 0.05f, "stopped, it sets the tempo too (90)");

    // on the board with the joystick: nothing
    rev_c(); sim_run(0.3f);
    px_play_song(0); sim_run(0.5f);
    SI.lk = 4; sim_tick(); SI.lk = -1; sim_tick();
    SI.hand_mm = 60; sim_run(0.6f);
    const ctl_t *c = front_ctl();
    check(!c->has_lknob && !c->has_hand && (int)pv[tm] == 90 && fabsf(px_pos.bpm - 90.0f) < 0.05f && px_hand_mute == 0 && px_muted() == 0,
          "on the board with the joystick, in play: counts on the left knob's pins and a hand at 60 mm change nothing (90 BPM, nothing muted)");
    SI.hand_mm = 0; px_stop(); sim_run(0.8f);
    memset(&px_song.pat[0], 0, sizeof px_song.pat[0]);
    meta.learned = learned;
    program(0); sim_run(0.3f);
}

// ---------------------------------------------------------------- play: the hand is the breakdown
static uint32_t tr_seed = 0x7E57u;
static int tremble(int mm, int amp, float seconds)               // a hand shaking amp mm either way round mm: how often the mutes changed
{
    int changes = 0; uint8_t was = px_hand_mute;
    for (int i = 0; i < (int)(seconds * 100.0f); i++) {
        tr_seed = tr_seed * 1664525u + 1013904223u;
        SI.hand_mm = mm - amp + (int)((tr_seed >> 12) % (uint32_t)(2 * amp + 1));
        sim_tick();
        if (px_hand_mute != was) { was = px_hand_mute; changes++; }
    }
    return changes;
}

static void breakdown(void)
{
    section("pixel: rev E, the hand is the breakdown");
    calm(); rev_c(); sim_menu_close(); px_stop(); program(PX_NSONGS - 1); set("tmpo", 150); set("cons", (float)CON_NES); sim_run(0.5f);
    // one bar, all four parts, going round
    px_pattern_t *p = &px_song.pat[0];
    static const uint8_t L[8] = { 15, 17, 19, 17, 15, 19, 22, 19 };
    memset(p, 0, sizeof *p);
    for (int i = 0; i < 8; i++) { p->lead[2 * i] = L[i]; p->drums[2 * i] = (uint8_t)(DR_HAT | (i % 4 == 0 ? DR_KICK : i % 4 == 2 ? DR_SNARE : 0)); }
    p->chords[0] = 1; p->chords[8] = 4;
    p->bass[0] = p->bass[4] = 8; p->bass[8] = p->bass[12] = 11;
    px_play_song(0); sim_run(1.0f);

    // the stick's own mutes on a rev C, to compare: all four, without the LEAD, without the CHORDS too, the DRUMS alone
    float ref[4], got[4];
    ref[0] = bar_rms();
    push(0, 1); sim_run(1.7f); ref[1] = bar_rms();
    push(1, 0); sim_run(1.7f); ref[2] = bar_rms();
    push(0, -1); sim_run(1.7f); ref[3] = bar_rms();
    bool stick_ok = px_mute == (M_LEAD | M_CHORDS | M_BASS);
    push(0, -1); push(1, 0); push(0, 1); sim_run(1.7f);
    stick_ok = stick_ok && px_mute == 0;

    sim_rev_e(); sim_run(0.3f);
    uint32_t learned = meta.learned; meta.learned &= ~(1u << LEARN_HAND);
    float hints_on = pv[CP(CP_HINTS)];
    param_set(CP(CP_HINTS), 0); coach_pause();                     // (no hint over the lamps in the pictures)
    got[0] = bar_rms();
    sim_hand(175, 0.5f);
    bool z1 = px_hand_mute == M_LEAD && px_mute == 0;
    sim_shot("reve-hand-lead");
    sim_run(1.7f); got[1] = bar_rms();
    sim_hand(125, 0.5f);
    bool z2 = px_hand_mute == (M_LEAD | M_CHORDS) && px_mute == 0;
    sim_run(1.7f); got[2] = bar_rms();
    sim_hand(60, 0.5f);
    bool z3 = px_hand_mute == (M_LEAD | M_CHORDS | M_BASS) && px_mute == 0;
    sim_shot("reve-hand-down");
    sim_run(1.7f); got[3] = bar_rms();
    check(stick_ok && z1 && fabsf(got[1] / ref[1] - 1.0f) < 0.05f && ref[1] < ref[0] * 0.9f,
          "a hand at 175 mm: the LEAD is out, as the stick's mute of it sounds (rms %.4f, the stick's %.4f; all four %.4f)", (double)got[1], (double)ref[1], (double)ref[0]);
    check(z2 && fabsf(got[2] / ref[2] - 1.0f) < 0.05f && ref[2] < ref[1] * 0.9f, "at 125 mm: the CHORDS too (rms %.4f, the stick's %.4f)", (double)got[2], (double)ref[2]);
    check(z3 && fabsf(got[3] / ref[3] - 1.0f) < 0.05f && ref[3] < ref[2] * 0.9f && got[3] > 0.005f,
          "at 60 mm: the BASS too, and the DRUMS play on alone (rms %.4f, the stick's %.4f)", (double)got[3], (double)ref[3]);
    check((meta.learned >> LEARN_HAND) & 1, "the hand's hint is done once it has been past half way");
    // back up through the zones, then away from fully down: the parts return in reverse
    sim_hand(125, 0.5f); bool u2 = px_hand_mute == (M_LEAD | M_CHORDS);
    sim_hand(175, 0.5f); bool u1 = px_hand_mute == M_LEAD;
    sim_hand(235, 0.5f); bool u0 = px_hand_mute == 0;
    check(u2 && u1 && u0, "the hand rising: at 125 mm the BASS is back, at 175 the CHORDS, at 235 the LEAD");
    sim_hand(60, 0.6f);
    uint8_t seq[8] = { px_hand_mute }; int nseq = 1, back_ms = -1;
    SI.hand_mm = 0;
    for (int i = 0; i < 200; i++) {
        sim_tick();
        if (px_hand_mute != seq[nseq - 1] && nseq < 8) seq[nseq++] = px_hand_mute;
        if (!px_hand_mute && back_ms < 0) back_ms = (i + 1) * 10;
    }
    sim_run(1.7f); float after = bar_rms();
    check(nseq == 4 && seq[0] == (M_LEAD | M_CHORDS | M_BASS) && seq[1] == (M_LEAD | M_CHORDS) && seq[2] == M_LEAD && seq[3] == 0 && px_mute == 0
          && fabsf(got[0] / ref[0] - 1.0f) < 0.05f && fabsf(after / ref[0] - 1.0f) < 0.05f,
          "the hand taken away from 60 mm: the BASS, the CHORDS, then the LEAD come back, all there %d ms later (rms %.4f, before the hand %.4f)", back_ms, (double)after, (double)got[0]);

    // the heights: down slowly a millimetre at a time, then up
    int down[3] = { 0, 0, 0 }, up[3] = { 0, 0, 0 };
    sim_hand(248, 0.5f);
    for (int mm = 247; mm >= 45; mm--) {
        uint8_t was = px_hand_mute; sim_hand(mm, 0.1f);
        if (px_hand_mute != was) { int k = px_hand_mute == M_LEAD ? 0 : px_hand_mute == (M_LEAD | M_CHORDS) ? 1 : 2; if (!down[k]) down[k] = mm; }
    }
    for (int mm = 46; mm <= 248; mm++) {
        uint8_t was = px_hand_mute; sim_hand(mm, 0.1f);
        if (px_hand_mute != was) { int k = px_hand_mute == (M_LEAD | M_CHORDS) ? 2 : px_hand_mute == M_LEAD ? 1 : 0; if (!up[k]) up[k] = mm; }
    }
    check(abs(down[0] - 190) <= 2 && abs(down[1] - 140) <= 2 && abs(down[2] - 90) <= 2 && abs(up[0] - 210) <= 2 && abs(up[1] - 160) <= 2 && abs(up[2] - 110) <= 2,
          "coming down the LEAD goes at %d mm, the CHORDS at %d, the BASS at %d; going up they are back at %d, %d, %d (19, 14, 9 cm and 2 cm higher)",
          down[0], down[1], down[2], up[2], up[1], up[0]);
    sim_hand(0, 1.0f);

    // a trembling hand at each border, come to from above and from below
    static const int border[3] = { 200, 150, 100 };
    int flips = 0, once[6], worst = 0;
    for (int b = 0; b < 3; b++) {
        sim_hand(border[b] + 30, 0.6f); flips += tremble(border[b], 8, 3.0f);
        sim_hand(border[b] - 30, 0.6f); flips += tremble(border[b], 8, 3.0f);
        sim_hand(border[b] + 30, 0.6f); once[2 * b] = tremble(border[b] - 10, 4, 3.0f);       // right at the height where it goes
        sim_hand(border[b] - 30, 0.6f); once[2 * b + 1] = tremble(border[b] + 10, 4, 3.0f);   // and where it comes back
        if (once[2 * b] > worst) worst = once[2 * b];
        if (once[2 * b + 1] > worst) worst = once[2 * b + 1];
    }
    check(flips == 0, "a hand trembling 8 mm either way at 20, 15 and 10 cm, come to from above and from below, 3 s each: no part goes or comes (%d changes)", flips);
    check(worst <= 1, "trembling 4 mm either way right at a height where a part goes (19, 14, 9 cm) or comes back (21, 16, 11): it changes once at most, never back and forth (%d %d, %d %d, %d %d)",
          once[0], once[1], once[2], once[3], once[4], once[5]);
    sim_hand(0, 1.0f);

    // a part muted before the hand came (as the stick leaves it on a board that has one) is still muted when it has gone
    px_mute = M_CHORDS;
    sim_hand(175, 0.5f); bool a = px_muted() == (M_LEAD | M_CHORDS) && px_mute == M_CHORDS;
    sim_hand(60, 0.5f);  bool b = px_muted() == (M_LEAD | M_CHORDS | M_BASS) && px_mute == M_CHORDS;
    sim_hand(0, 1.0f);
    check(a && b && px_hand_mute == 0 && px_mute == M_CHORDS && px_muted() == M_CHORDS, "the CHORDS muted before the hand came: still muted when it has gone, and the LEAD and the BASS are back");
    px_mute = 0;
    px_stop(); sim_run(0.8f);

    // every console: under a hand at 60 mm its drums play on alone, and all four parts are back after it
    program(0); sim_run(0.3f);
    px_play_song(0); sim_run(0.5f);
    int good = 0; float least = 1.0f; const char *who = "";
    for (int k = 0; k < CON_N; k++) {
        set("cons", (float)k); sim_run(1.2f);
        float t0 = sim_t; sim_run(3.2f); float full = m_rms(t0, sim_t);
        sim_hand(60, 0.6f);
        t0 = sim_t; sim_run(3.2f); float drums = m_rms(t0, sim_t);
        bool held = px_hand_mute == (M_LEAD | M_CHORDS | M_BASS);
        sim_hand(0, 1.0f);
        t0 = sim_t; sim_run(3.2f); float again = m_rms(t0, sim_t);
        bool ok = held && drums > 0.003f && drums < full * 0.9f && again > full * 0.8f && px_hand_mute == 0;
        good += ok;
        if (drums / full < least) { least = drums / full; who = PX_CONSOLES[k]->name; }
        if (!ok) note("%s: all four %.4f, under the hand %.4f, after %.4f", PX_CONSOLES[k]->name, (double)full, (double)drums, (double)again);
    }
    check(good == CON_N && m_bad() == 0, "on all eight consoles a hand at 60 mm leaves the drums playing alone and the four parts are back after it (%d of %d; the drums are least of the whole on %s, x%.2f)", good, CON_N, who, (double)least);
    px_stop(); sim_run(0.8f);

    // The first song, four bars, the chip fresh from its reset each time: once to warm up (what a run leaves behind in
    // the player is then the same for the next three), then left alone; with the hand going down and up; and on a rev C
    // with the stick muting and bringing back the same parts in the same steps of the song. The hand's mutes are the
    // stick's if the two come out the same, sample for sample.
    program(0); set("cons", (float)CON_NES); sim_run(0.5f);
    enum { RUN_TICKS = 644 };                                      // 6.44 s: four bars and a little
    static const int16_t hand_at[6] = { 99, 149, 199, 299, 349, 399 }, hand_mm[6] = { 175, 125, 60, 125, 175, 0 };
    static const float dir[6][2] = { { 0, 1 }, { 1, 0 }, { 0, -1 }, { 0, -1 }, { 1, 0 }, { 0, 1 } };   // LEAD, CHORDS, BASS off; BASS, CHORDS, LEAD on
    int n0[3] = { 0, 0, 0 }, pos[6] = { 0 }, npos = 0, pushed = 0;
    bool where[2] = { false, false }, link_same = false;
    for (int run = -1; run < 3; run++) {
        if (run == 2) { rev_c(); sim_run(0.3f); }
        set("cons", (float)CON_AY); sim_run(0.3f); set("cons", (float)CON_NES); sim_run(0.7f);
        px_play_song(0); sim_tick();
        uint32_t o0, o1; float f0, f1; link_origin(&o0, &f0);
        uint32_t fpb0 = link_fpb(), ver0 = link_time()->ver;
        if (run >= 0) n0[run] = sim_n;
        if (run == 1) sim_gif_begin("breakdown");
        for (int t = 0, hold = 0; t < RUN_TICKS; t++) {
            int here = (int)px_pos.bars * PX_STEPS + px_pos.step;  // the step of the song the last tick's sound was in
            if (run == 1) for (int k = 0; k < 6; k++) if (t == hand_at[k]) SI.hand_mm = hand_mm[k];
            if (run == 2) {
                if (hold > 0 && --hold == 0) SI.jx = SI.jy = 0;
                if (pushed < npos && px_pos.running && here == pos[pushed] && !hold) { SI.jx = dir[pushed][0]; SI.jy = dir[pushed][1]; hold = 5; pushed++; }
            }
            uint8_t was = px_hand_mute;
            sim_tick();
            if (run == 1 && px_hand_mute != was && npos < 6) pos[npos++] = (int)px_pos.bars * PX_STEPS + px_pos.step;
        }
        if (run == 1) sim_gif_end();
        if (run == 0 || run == 1) where[run] = px_pos.running && px_pos.bars == 4 && px_pos.slot == 4 && px_pos.step == 0;
        if (run == 1) { link_origin(&o1, &f1); link_same = o1 == o0 && f1 == f0 && link_fpb() == fpb0 && link_time()->ver == ver0; note("beat 0 at frame %u, %u frames a beat, the session's version %u", (unsigned)o1, (unsigned)link_fpb(), (unsigned)link_time()->ver); }
        px_stop(); sim_run(0.8f);
    }
    int N = RUN_TICKS * TICK_FRAMES, first = -1, differ = 0;
    float d_stick = 0, j_hand = 0, j_there = 0, j_alone = 0;
    for (int i = 0; i < N; i++) {
        float al = sim_out[n0[0] + i], h = sim_out[n0[1] + i], st = sim_out[n0[2] + i];
        if (fabsf(h - st) > d_stick) d_stick = fabsf(h - st);
        if (h != al) { differ++; if (first < 0) first = i; }
        if (i) { float ja = fabsf(al - sim_out[n0[0] + i - 1]); if (ja > j_alone) j_alone = ja; }
    }
    // a change takes effect where the next step starts (a step is 3200 frames here, the song began 256 frames in):
    // the biggest jump between two samples in the 0.3 s from there, for each of the six
    for (int k = 0; k < npos; k++) {
        int from = (pos[k] + 1) * 3200 + 256 - 64;
        for (int i = from; i < from + 9600 && i < N; i++) {
            float jh = fabsf(sim_out[n0[1] + i] - sim_out[n0[1] + i - 1]), ja = fabsf(sim_out[n0[0] + i] - sim_out[n0[0] + i - 1]);
            if (jh > j_hand) j_hand = jh;
            if (ja > j_there) j_there = ja;
        }
    }
    note("the hand's changes fell in steps %d %d %d %d %d %d of the song; the stick's pushes: %d", pos[0], pos[1], pos[2], pos[3], pos[4], pos[5], pushed);
    check(npos == 6 && pushed == 6 && px_mute == 0 && d_stick == 0.0f && differ > N / 4 && first > AUDIO_SR,
          "four bars broken down by the hand and four with the stick's mutes in the same steps: the same sound, sample for sample (largest difference %g; "
          "against the song left alone it differs in %d of %d samples, from %.2f s on)", (double)d_stick, differ, N, (double)first / AUDIO_SR);
    check(j_hand <= j_alone && j_hand <= j_there * 1.1f,
          "in the 0.3 s after each part goes or comes the biggest jump between two samples is %.4f (the song left alone: %.4f in the same places, %.4f over the four bars): no click of its own",
          (double)j_hand, (double)j_there, (double)j_alone);
    check(where[0] && where[1] && link_same, "6.44 s with the hand going down and up: the song is at bar 4, step 0 as when left alone, and the link's beat 0, tempo and version are untouched");
    param_set(CP(CP_HINTS), hints_on); meta.learned = learned;
    rev_c(); program(0); sim_run(0.3f);
}

// ---------------------------------------------------------------- with another board
static void linked(void)
{
    section("pixel: rev E, linked to another board");
    calm(); rev_c(); sim_menu_close(); px_stop(); program(0); sim_run(0.5f);
    sim_rev_e(); sim_run(0.2f);
    int tm = tag("tmpo");
    int k = sim_peer_add("BAND", 2, 25.0, 9.0e9);
    sim_peer_use(k);
    {   uint32_t o; float f; link_origin(&o, &f); link_set_time(16000, o, 4, 0); }      // 120 BPM
    sim_peer_use(-1);
    sim_run(2.0f);
    bool follows = !link_is_clock() && link_synced() && fabsf(px_pos.bpm - 120.0f) < 0.1f && (int)pv[tm] == 150;
    sim_lturn(1); sim_run(1.5f);
    sim_peer_use(k); uint32_t theirs = link_fpb(); sim_peer_use(-1);
    check(follows && (int)pv[tm] == 121 && fabsf(px_pos.bpm - 121.0f) < 0.05f && theirs == 15868u,
          "the BAND is the clock at 120 and the song says 150: one detent is one BPM from what plays, 121, here and on the BAND (%u frames a beat there)", (unsigned)theirs);
    while (px_pos.step > 8) sim_run(0.05f);
    px_play_song(0); sim_run(2.2f);
    sim_hand(175, 0.5f); sim_hand(60, 0.8f); sim_hand(130, 0.4f); sim_hand(0, 0.8f);
    float gap = sim_peer_gap_ms(k);
    check(px_pos.running && fabsf(gap) < 1.0f && px_hand_mute == 0, "the song played and broken down with the hand: its beat is still the BAND's (%.3f ms apart)", (double)gap);
    sim_peer_use(k); link_set_flags(LINKF_LOOP); sim_peer_use(-1);
    sim_run(0.6f);
    sim_lturn(4); sim_run(0.5f);
    sim_shot("reve-tempo-loop");
    sim_peer_use(k); theirs = link_fpb(); sim_peer_use(-1);
    check((int)pv[tm] == 121 && fabsf(px_pos.bpm - 121.0f) < 0.05f && theirs == 15868u, "the BAND keeps a loop: the left knob changes neither the tempo that plays nor the song's own (it says A LOOP HAS THE TEMPO)");
    px_stop();
    sim_peer_use(k); link_set_flags(0); sim_peer_use(-1);
    sim_peer_power(k, false);
    sim_run(1.6f);
    rev_c(); program(0); sim_run(0.3f);
    check(link_is_clock() && px_peers() == 0 && fabsf(px_pos.bpm - 150.0f) < 0.05f, "the BAND switched off, the song loaded again: alone at its own 150");
}

// the two hints, each alone on the screen (out/pixel/*reve-hint-*.pbm are the pictures to read them in)
// (a hint's strip begins with an "i" in a filled dot, 9 across, at the left of the bottom 18 rows: the dots lit there)
static int strip_lit(void) { int n = 0; for (int y = OLED_H - 13; y <= OLED_H - 5; y++) for (int x = 1; x <= 9; x++) n += oled_get(x, y); return n; }
static void hints(void)
{
    section("pixel: rev E, the hints");
    calm(); rev_c(); sim_menu_close(); px_stop(); sim_run(0.3f);
    sim_rev_e();
    uint32_t learned = meta.learned; float on = pv[CP(CP_HINTS)];
    int have = 0, lit[3] = { 0, 0, 0 };
    for (int i = 0; i < INST.nhints; i++) if (INST.hints[i].learn == LEARN_LKNOB || INST.hints[i].learn == LEARN_HAND) have++;
    static const uint8_t which[2] = { LEARN_LKNOB, LEARN_HAND };
    for (int k = 0; k < 3; k++) {
        sim_run(3.0f);                                             // idle long enough for the coach
        coach_pause();                                             // (whatever hint was up goes)
        param_set(CP(CP_HINTS), 0);
        if (k < 2) { param_set(CP(CP_HINTS), 1); meta.learned = ~(1u << which[k]); }   // (off and on: the coach starts again 0.8 s later)
        sim_run(1.6f);
        lit[k] = strip_lit();
        if (k < 2) sim_shot(k ? "reve-hint-hand" : "reve-hint-knob");
    }
    check(have == 2 && lit[0] >= 36 && lit[1] >= 36 && lit[2] < 24, "a hint for the left knob and one for the hand, and a rev E shows each (the hint's dot: %d and %d of 81 lit, %d with no hint up)", lit[0], lit[1], lit[2]);
    rev_c(); sim_run(3.0f); coach_pause(); param_set(CP(CP_HINTS), 1); meta.learned = ~((1u << LEARN_LKNOB) | (1u << LEARN_HAND)); sim_run(1.6f);
    int rc = strip_lit();
    check(rc < 24, "on the board with the joystick neither is shown (%d)", rc);
    param_set(CP(CP_HINTS), on); meta.learned = learned;
    sim_run(0.3f);
}

void test_rev_e(void)
{
    editor_two_knobs();
    tempo_knob();
    breakdown();
    linked();
    hints();
}
