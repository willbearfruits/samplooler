// PIXEL's own checks (first ones: the rest follow with the editor and the library).
#include "sim.h"
#include "pixel.h"
#include "link.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
void front_midi(uint8_t st, uint8_t d1, uint8_t d2);
void test_library(void);

// the console, the tempo, the swing, the frame rate, the key and the scale move the whole machine: a new chip or a
// new tempo is a new sound, not a control gliding
const char *const inst_click_skip[] = { "cons", "fram", "tmpo", "swng", "key ", "scal", "nlnk", "ngrp", "npow", NULL };

static int tag(const char *t) { int p = param_by_tag(t); if (p < 0) note("no param '%s'", t); return p; }
static void set(const char *t, float v) { int p = tag(t); if (p >= 0) param_set(p, v); }
static void quiet(void) { SI.mic = NULL; SI.mic_u = NULL; SI.jx = SI.jy = 0; SI.joy = true; SI.imu = true; sim_tilt(0, 0); }

void inst_excite(void) { if (!px_pos.running && !px_pos.armed) px_play_song(0); }

static void plays(void)
{
    section("pixel: it plays");
    quiet(); px_stop(); sim_run(1.5f);
    check(!px_pos.running && m_rms(sim_t - 0.3f, sim_t) < 0.001f, "stopped, it waits in silence");
    sim_gif_begin("play");
    sim_rec(0.1f); sim_run(4.0f);
    float r = m_rms(sim_t - 2.0f, sim_t), p = m_peak(sim_t - 2.0f, sim_t);
    check(px_pos.running && r > 0.02f && p < 0.99f, "REC tapped: the song plays (rms %.3f, peak %.3f)", (double)r, (double)p);
    sim_shot("play");
    sim_run(2.6f);
    sim_tilt(0, 40); sim_run(1.2f); sim_shot("glitch"); sim_tilt(0, 0); sim_run(1.0f);
    SI.jx = -1; sim_run(0.05f); SI.jx = 0; sim_run(1.5f); sim_shot("drums-muted");
    SI.jx = -1; sim_run(0.05f); SI.jx = 0; sim_run(0.5f);
    sim_gif_end();
    sim_rec(0.1f); sim_run(1.5f);
    check(!px_pos.running && m_rms(sim_t - 0.5f, sim_t) < 0.001f, "REC tapped again: it stops and falls silent");
}

static void consoles(void)
{
    section("pixel: the knob's consoles");
    quiet(); px_play_song(0); sim_run(0.2f);
    float t0 = sim_t;
    for (int c = 0; c < CON_N; c++) {
        set("cons", (float)c); sim_run(3.2f);
        float r = m_rms(sim_t - 2.0f, sim_t);
        check(px_pos.console == c && r > 0.02f, "%s plays the song (rms %.3f)", PX_CONSOLES[c]->name, (double)r);
    }
    sim_wav("-consoles", t0, sim_t);
    px_stop(); sim_run(1.0f); set("cons", (float)CON_NES);
}

// the hands in the editor
static void stick(float x, float y) { SI.jx = x; SI.jy = y; sim_run(0.03f); SI.jx = SI.jy = 0; sim_run(0.06f); }
static void drag(int d) { SI.pressed = true; sim_tick(); SI.enc = d; sim_tick(); sim_tick(); SI.pressed = false; sim_run(0.05f); }
static void load_song(int id) { int16_t want = (int16_t)id; (void)want; front_midi(0xC0, (uint8_t)id, 0); sim_run(0.1f); }

static void editor(void)
{
    section("pixel: the editor");
    quiet(); px_stop(); load_song(PX_NSONGS - 1); sim_run(0.5f);    // EMPTY
    check(px_song.order_n == 1 && !px_song.pat[0].lead[0], "the EMPTY song is one empty bar");
    sim_hold_knob(); sim_run(0.3f);
    check(edit_active(), "the knob held opens the editor");
    sim_gif_begin("write");
    // LEAD: a note on step 1, higher, longer, with a glide in its tail
    sim_tap(); sim_run(0.1f);
    px_pattern_t *p = &px_song.pat[0];
    int r0 = p->lead[0] & NC_ROW;
    check(r0 > 0, "a press puts a note under the cursor (row %d)", r0 - 1);
    sim_turn(2); sim_run(0.1f);
    check((p->lead[0] & NC_ROW) == r0 + 2, "a turn moves it up two rows");
    drag(1); drag(1); sim_run(0.1f);
    check((p->lead[1] & NC_TIE) && (p->lead[2] & NC_TIE) && !(p->lead[3]), "press and turn makes it three steps long");
    stick(1, 0); stick(1, 0);
    sim_turn(3); sim_run(0.1f);
    check((p->lead[2] & NC_ROW) == r0 + 5 && (p->lead[1] & NC_ROW) == r0 + 2 && (p->lead[2] & NC_TIE), "a turn on its tail makes it glide up from there");
    stick(1, 0); stick(1, 0);                                       // step 5: a second note, accented
    sim_tap(); sim_run(0.05f); sim_tap(); sim_run(0.1f);
    check((p->lead[4] & NC_ROW) && (p->lead[4] & NC_ACCENT), "two presses: an accented note");
    for (int i = 0; i < 4; i++) stick(1, 0);
    sim_turn(-1); sim_run(0.1f);
    check((p->lead[8] & NC_ROW) > 0, "a turn on an empty step puts a note there");
    sim_shot("edit-lead");
    // CHORDS
    stick(0, -1); for (int i = 0; i < 8; i++) stick(-1, 0);
    sim_tap(); sim_run(0.1f);
    check((p->chords[0] & CH_DEGREE) == 1, "CHORDS: a press puts the chord I");
    for (int i = 0; i < 8; i++) stick(1, 0);
    sim_turn(1); sim_turn(1); sim_turn(1); sim_turn(1); sim_run(0.1f);
    drag(1); sim_run(0.1f);
    check((p->chords[8] & CH_DEGREE) >= 4 && (p->chords[8] & CH_KIND) == CHK_SEVENTH, "a turn picks the chord, press and turn its kind (cell %02x)", p->chords[8]);
    sim_shot("edit-chords");
    // BASS
    stick(0, -1); for (int i = 0; i < 8; i++) stick(-1, 0);
    for (int i = 0; i < 4; i++) { sim_tap(); sim_run(0.05f); stick(1, 0); stick(1, 0); stick(1, 0); stick(1, 0); }
    check((p->bass[0] & NC_ROW) && (p->bass[4] & NC_ROW) && (p->bass[8] & NC_ROW) && (p->bass[12] & NC_ROW), "BASS: a note on each beat");
    sim_shot("edit-bass");
    // DRUMS: kicks on the beats, a snare on 2 and 4 (accented), hats
    stick(0, -1);
    for (int i = 0; i < 4; i++) { sim_tap(); sim_run(0.05f); for (int k = 0; k < 4; k++) stick(1, 0); }
    sim_turn(1); sim_run(0.05f);
    for (int k = 0; k < 4; k++) stick(1, 0);
    sim_tap(); sim_run(0.05f); sim_tap(); sim_run(0.05f);
    for (int k = 0; k < 8; k++) stick(1, 0);
    sim_tap(); sim_run(0.05f);
    sim_turn(1); sim_run(0.05f);
    for (int k = 0; k < 4; k++) stick(1, 0);
    for (int i = 0; i < 8; i++) { sim_tap(); sim_run(0.04f); stick(1, 0); stick(1, 0); }
    check((p->drums[0] & DR_KICK) && (p->drums[4] & DR_SNARE) && (p->drums[4] & DR_ACCENT) && (p->drums[12] & DR_SNARE) && (p->drums[2] & DR_HAT),
          "DRUMS: kick, snare (one accented) and hats written");
    sim_shot("edit-drums");
    // play the bar round and round, then stop
    sim_rec(0.1f); sim_run(3.4f);
    check(px_pos.running && px_pos.mode == PM_PATTERN && m_rms(sim_t - 1.5f, sim_t) > 0.02f, "REC tapped: the bar goes round (rms %.3f)", (double)m_rms(sim_t - 1.5f, sim_t));
    sim_shot("edit-playing");
    sim_rec(0.1f); sim_run(0.6f);
    check(!px_pos.running, "REC tapped again: it stops");
    // the SONG row: the bar once more, a pattern of its own for it, a third bar, and one taken away
    for (int k = 0; k < 4; k++) stick(0, 1);
    sim_tap(); sim_run(0.1f);
    check(px_song.order_n == 2 && px_song.order[1] == 0, "SONG: a press plays the bar once more (%d bars)", px_song.order_n);
    drag(1); sim_run(0.1f);
    check(px_song.order[1] == 1 && px_song.pat[1].drums[0] == px_song.pat[0].drums[0], "press and turn right: the bar gets a pattern of its own (B, a copy)");
    sim_turn(1); sim_run(0.05f);
    check(px_song.order[1] == 2, "a turn changes the bar's pattern (C)");
    sim_turn(-1); sim_run(0.05f);
    stick(1, 0); sim_tap(); sim_run(0.1f);
    check(px_song.order_n == 3, "a press on + adds a bar (%d)", px_song.order_n);
    sim_shot("edit-song");
    drag(-1); sim_run(0.1f);
    check(px_song.order_n == 2, "press and turn left: the bar leaves the song (%d)", px_song.order_n);
    sim_gif_end();
    // REC held a second empties the row's part in this bar
    stick(0, -1);                                                   // LEAD
    stick(-1, 0);
    SI.rec = true; sim_run(1.2f); SI.rec = false; sim_run(0.2f);
    bool empty = true; for (int i = 0; i < PX_STEPS; i++) if (px_song.pat[px_song.order[0]].lead[i]) empty = false;
    check(empty && !px_pos.running, "REC held a second: the LEAD is emptied in this bar, nothing starts");
    sim_hold_knob(); sim_run(0.3f);
    check(!edit_active(), "the knob held again leaves the editor");
    check(lib_notes_edited(), "the notes count as edited");
}

// every factory song reads without a complaint and plays; a few bars of each to listen to
static void songs(void)
{
    section("pixel: the factory songs");
    quiet(); px_stop(); sim_run(0.5f);
    for (int i = 0; i < PX_NSONGS; i++) {
        px_song_t t; char err[64] = "";
        int key = 0, scale = 0;
        sscanf(strstr(PX_PRESETS[i].set, "key =") ? strstr(PX_PRESETS[i].set, "key =") + 5 : "0", "%d", &key);
        sscanf(strstr(PX_PRESETS[i].set, "scal=") ? strstr(PX_PRESETS[i].set, "scal=") + 5 : "0", "%d", &scale);
        bool ok = px_song_parse(&t, PX_SONG_TEXT[i], key, scale, err, sizeof err);
        int notes = 0, used = 0;
        for (int p = 0; p < PX_PATTERNS; p++) { int n = 0; for (int k = 0; k < PX_STEPS; k++) n += (t.pat[p].lead[k] != 0) + (t.pat[p].bass[k] != 0) + (t.pat[p].chords[k] != 0) + (t.pat[p].drums[k] != 0); notes += n; used += n > 0; }
        check(ok, "%s reads (%d bars in the song, %d patterns, %d cells)%s%s", PX_PRESETS[i].name, t.order_n, used, notes, err[0] ? ": " : "", err);
        load_song(i);
        if (i == PX_NSONGS - 1) continue;                           // EMPTY has nothing to play
        float t0 = sim_t;
        px_play_song(0);
        float bar = 4.0f * 60.0f / pv[tag("tmpo")];
        sim_run(bar * 8.0f + 0.1f);
        px_stop(); sim_run(0.8f);
        float r = m_rms(t0 + 0.1f, t0 + bar * 8.0f), pk = m_peak(t0, sim_t);
        check(r > 0.03f && pk < 0.95f, "%s plays (rms %.3f, peak %.3f)", PX_PRESETS[i].name, (double)r, (double)pk);
        char sfx[24]; snprintf(sfx, sizeof sfx, "-song-%d", i + 1);
        sim_wav(sfx, t0, sim_t);
    }
    load_song(0);
}

void inst_tests(void)
{
    plays();
    consoles();
    songs();
    editor();
    test_library();
    check(m_bad() == 0, "no NaN or inf anywhere");
}
