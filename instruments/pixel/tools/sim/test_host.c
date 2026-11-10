// The host's checks (instrument.c): the clock (bars on time, ticks per step, swing), the hands (mutes, tilt, notes
// from a keyboard), a linked board, the editor on a board without a stick, a console change without a click, each
// console's cost, and a soak with everything going.
#include "sim.h"
#include "pixel.h"
#include "link.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void sim_menu_close(void);
void front_midi(uint8_t st, uint8_t d1, uint8_t d2);
void px_prof(float *render_us, float *tick_us);
void edit_test_state(int *row, int *slot, int *step);
void edit_test_home(void);

static int tag(const char *t) { int p = param_by_tag(t); if (p < 0) note("no param '%s'", t); return p; }
static void set(const char *t, float v) { int p = tag(t); if (p >= 0) param_set(p, v); }
static void quiet(void) { SI.mic = NULL; SI.mic_u = NULL; SI.jx = SI.jy = 0; SI.joy = true; SI.imu = true; SI.rec = false; sim_tilt(0, 0); }
static void program(int n) { front_midi(0xC0, (uint8_t)n, 0); sim_run(0.1f); }
static void push(float x, float y) { SI.jx = x; SI.jy = y; sim_run(0.05f); SI.jx = SI.jy = 0; sim_run(0.1f); }

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
// the longest stretch under thr inside t0..t1 ends at the next onset: skip a hit's own body (hold seconds)
static float next_onset(float after, float hold, float thr) { return onset(after + hold, thr); }

static void clock_checks(void)
{
    section("pixel: the clock");
    quiet(); sim_menu_close(); px_stop(); program(0); set("tmpo", 150); set("swng", 0); set("fram", 0); sim_run(0.6f);
    px_play_song(0); sim_run(6.4f + 0.05f);
    check(px_pos.running && px_pos.bars == 4 && px_pos.slot == 4 && px_pos.step == 0, "150 BPM: four bars take 6.4 s (bar %u, slot %d, step %d)", (unsigned)px_pos.bars, px_pos.slot, px_pos.step);
    check(px_pos.tps == 6 && fabsf(px_pos.tick_hz - 60.0f) < 0.1f, "six ticks a step at 60 Hz (%d, %.2f Hz)", px_pos.tps, (double)px_pos.tick_hz);
    set("fram", 1); sim_run(0.3f);
    check(px_pos.tps == 5 && fabsf(px_pos.tick_hz - 50.0f) < 0.1f, "FRAME 50 HZ: five ticks a step (%d, %.2f Hz)", px_pos.tps, (double)px_pos.tick_hz);
    set("fram", 0); set("tmpo", 100); sim_run(0.3f);
    check(px_pos.tps == 9 && fabsf(px_pos.tick_hz - 60.0f) < 0.1f && fabsf(px_pos.bpm - 100.0f) < 0.1f, "100 BPM: nine ticks a step, still 60 Hz (%d, %.2f Hz)", px_pos.tps, (double)px_pos.tick_hz);
    set("tmpo", 240); sim_run(0.3f);
    check(px_pos.tps == 4 && px_pos.tick_hz > 55.0f && px_pos.tick_hz < 70.0f, "240 BPM: four ticks a step (%.1f Hz)", (double)px_pos.tick_hz);
    px_stop(); sim_run(0.8f);

    // swing: two hats on the first two steps; the second comes later with SWING
    program(PX_NSONGS - 1);
    set("tmpo", 120); set("cons", (float)CON_NES); sim_run(0.5f);
    px_song.pat[0].drums[0] = DR_HAT; px_song.pat[0].drums[1] = DR_HAT;
    float gap[2];
    for (int k = 0; k < 2; k++) {
        set("swng", k ? 50.0f : 0.0f); sim_run(0.8f);
        float t0 = sim_t;
        px_play_pattern(0); sim_run(1.0f); px_stop(); sim_run(0.8f);
        float a = onset(t0, 0.02f), b = next_onset(a, 0.09f, 0.02f);
        gap[k] = (b - a) * 1000.0f;
    }
    check(fabsf(gap[0] - 125.0f) < 4.0f, "120 BPM, no swing: a step is 125 ms (%.1f)", (double)gap[0]);
    check(fabsf(gap[1] - 187.5f) < 4.0f, "SWING 50: the second step of each pair comes at 187.5 ms (%.1f)", (double)gap[1]);
    memset(&px_song.pat[0], 0, sizeof px_song.pat[0]);
    set("swng", 0); program(0); sim_run(0.3f);
}

static void hands_checks(void)
{
    section("pixel: the hands");
    quiet(); sim_menu_close(); px_stop(); program(0); sim_run(0.5f);
    px_play_song(0); sim_run(3.3f);
    float full = m_rms(sim_t - 3.0f, sim_t);
    push(0, 1); check(px_mute == (1u << PART_LEAD), "the stick up: the LEAD is muted");
    push(1, 0); push(0, -1);
    check(px_mute == ((1u << PART_LEAD) | (1u << PART_CHORDS) | (1u << PART_BASS)), "right and down: CHORDS and BASS too");
    sim_run(3.3f);
    float drums = m_rms(sim_t - 3.0f, sim_t);
    push(-1, 0); sim_run(1.5f);
    check(px_mute == 0x0F && m_rms(sim_t - 0.8f, sim_t) < 0.002f, "left: the DRUMS as well, and nothing sounds (rms %.4f)", (double)m_rms(sim_t - 0.8f, sim_t));
    check(drums > 0.005f && drums < full * 0.9f, "with only the drums it is quieter than all four (%.3f against %.3f)", (double)drums, (double)full);
    push(-1, 0); push(0, 1); push(1, 0); push(0, -1);
    check(px_mute == 0, "each pushed again: all four back");
    px_stop(); sim_run(0.8f);
    program(1); check(px_mute == 0, "another song: nothing muted");
    program(0);

    // the tilt: forward glitches, a roll bends; not while the editor is open; not with TILT OFF
    sim_tilt(0, 50); sim_run(0.6f);
    check(px_glitch_now() > 0.9f, "tipped forward 50 degrees: the glitch is full (%.2f)", (double)px_glitch_now());
    sim_tilt(0, 0); sim_run(0.6f);
    check(px_glitch_now() == 0.0f, "flat again: none");
    set("glit", 40); check(fabsf(px_glitch_now() - 0.4f) < 0.01f, "CONSOLE > GLITCH 40 does the same without a tilt"); set("glit", 0);
    // a key held on a keyboard, then the box rolled: the note bends up two semitones
    front_midi(0x90, 69, 100); sim_run(0.7f);
    float f0 = m_pitch_at(sim_t - 0.3f, sim_t, 300, 700);
    sim_tilt(50, 0); sim_run(0.7f);
    float f1 = m_pitch_at(sim_t - 0.3f, sim_t, 300, 700);
    check(fabsf(hz_to_midi(f0) - 69.0f) < 0.3f, "a key on a keyboard (A4) plays the lead (%.1f Hz)", (double)f0);
    check(fabsf(hz_to_midi(f1) - 71.0f) < 0.4f && fabsf(px_bend_now() - 2.0f) < 0.05f, "rolled right 50 degrees: two semitones up (%.1f Hz)", (double)f1);
    set("htlt", 0); sim_run(0.5f);
    check(px_bend_now() == 0.0f, "HANDS > TILT OFF: no bend");
    set("htlt", 1); sim_tilt(0, 0); sim_run(0.3f);
    front_midi(0x80, 69, 0); sim_run(0.8f);
    check(m_rms(sim_t - 0.3f, sim_t) < 0.002f, "the key let go: silence");
}

static void link_checks(void)
{
    section("pixel: linked to another board");
    quiet(); sim_menu_close(); px_stop(); program(0); sim_run(0.5f);
    int k = sim_peer_add("BAND", 2, 25.0, 9.0e9);
    sim_peer_use(k);
    {   uint32_t o; float f; link_origin(&o, &f); link_set_time(16000, o, 4, 0); link_set_harm(4, 1, 6, 0, 0, -1); }   // 120 BPM, E minor
    sim_peer_use(-1);
    sim_run(2.0f);
    check(!link_is_clock() && link_synced() && px_peers() == 1, "PIXEL takes the BAND as the clock");
    check(fabsf(px_pos.bpm - 120.0f) < 0.1f && px_key() == 4 && px_scale() == PX_SC_MINOR, "it plays at the BAND's 120 BPM in its key (E minor)");
    check((int)pv[tag("tmpo")] == 150 && (int)pv[tag("key ")] == 9 && !front_status()->edited, "the song's own tempo and key stay as written (not an edit)");
    sim_run(0.3f);
    while (px_pos.step > 8) sim_run(0.05f);                       // (not right before a bar line: there is a wait to see)
    sim_rec(0.1f); sim_run(0.05f);
    bool armed = px_pos.armed && !px_pos.running;
    sim_run(2.1f);
    check(armed && px_pos.running, "REC tapped: it waits for the flock's bar line, then plays");
    float gap = sim_peer_gap_ms(k);
    check(fabsf(gap) < 1.0f && px_pos.step < 16, "its beat is the BAND's (%.3f ms apart)", (double)gap);
    sim_run(1.0f);
    check(link_harm()->live == px_pos.degree - 1 && px_pos.degree > 0, "the chord that sounds is told to the flock (degree %d)", px_pos.degree);
    px_stop(); sim_run(0.5f);
    check(link_harm()->live == -1, "stopped: no chord told");
    sim_peer_power(k, false);
    sim_run(1.6f);
    check(link_is_clock() && px_peers() == 0, "the BAND switched off: PIXEL keeps its time alone");
    program(0); sim_run(0.3f);
    check(px_key() == 9, "a song loaded: its own key again");
}

static void no_stick(void)
{
    section("pixel: the editor without a stick");
    quiet(); sim_menu_close(); px_stop(); program(PX_NSONGS - 1); sim_run(0.3f);
    SI.joy = false;
    edit_test_home();
    sim_hold_knob(); sim_run(0.2f);
    int row, slot, step;
    sim_turn(1); sim_turn(1); sim_turn(1); sim_run(0.05f);
    edit_test_state(&row, &slot, &step);
    check(edit_active() && row == 1 && step == 3, "turning walks the cursor along the LEAD (row %d, step %d)", row, step);
    sim_tap(); sim_run(0.1f);
    uint8_t c0 = px_song.pat[0].lead[3];
    SI.pressed = true; sim_tick(); SI.enc = 2; sim_tick(); sim_tick(); SI.pressed = false; sim_run(0.05f);
    check(c0 && (px_song.pat[0].lead[3] & NC_ROW) == (c0 & NC_ROW) + 2, "a press puts a note, press and turn moves it up");
    for (int i = 0; i < 14; i++) sim_turn(1);
    edit_test_state(&row, &slot, &step);
    check(row == 2 && step == 1, "walking on past the bar's end comes to the next row, CHORDS (row %d, step %d)", row, step);
    sim_hold_knob(); sim_run(0.2f);
    check(!edit_active(), "the knob held leaves it");
    SI.joy = true;
    program(0);
}

static void console_change(void)
{
    section("pixel: a console change");
    quiet(); sim_menu_close(); px_stop(); program(0); set("cons", (float)CON_NES); sim_run(0.5f);
    px_play_song(0); sim_run(2.0f);
    float worst = 0;
    for (int c = 0; c < CON_N; c++) {
        // the biggest jump between two samples in a second of play, then around the change
        float base = 0, at = 0;
        for (int i = sim_n - AUDIO_SR; i < sim_n; i++) { float d = fabsf(sim_out[i] - sim_out[i - 1]); if (d > base) base = d; }
        int n0 = sim_n;
        set("cons", (float)((c + 1) % CON_N)); sim_run(0.12f);
        for (int i = n0; i < sim_n; i++) { float d = fabsf(sim_out[i] - sim_out[i - 1]); if (d > at) at = d; }
        sim_run(1.0f);
        float after = 0;
        for (int i = sim_n - AUDIO_SR; i < sim_n; i++) { float d = fabsf(sim_out[i] - sim_out[i - 1]); if (d > after) after = d; }
        float ref = base > after ? base : after, ratio = ref > 0 ? at / ref : 0;
        if (ratio > worst) worst = ratio;
    }
    check(worst < 1.5f, "the knob through all eight: no jump bigger than the music's own (worst x%.2f)", (double)worst);
    px_stop(); sim_run(0.8f); set("cons", (float)CON_NES);
}

static void cost(void)
{
    section("pixel: the cost");
    quiet(); sim_menu_close(); px_stop(); program(3); sim_run(0.3f);          // LAST BOSS: sixteenths everywhere
    px_play_song(0);
    float worst = 0; const char *who = "";
    for (int c = 0; c < CON_N; c++) {
        set("cons", (float)c); sim_run(0.5f);
        float best_r = 1e9f, best_t = 1e9f, r, t;
        for (int k = 0; k < 5; k++) { px_prof(&r, &t); sim_run(1.0f); px_prof(&r, &t); if (r < best_r) best_r = r; if (t < best_t) best_t = t; }
        char key[40]; snprintf(key, sizeof key, "us_per_block_%s", PX_CONSOLES[c]->name);
        metric(key, (double)(best_r + best_t));
        if (best_r + best_t > worst) { worst = best_r + best_t; who = PX_CONSOLES[c]->name; }
    }
    check(worst <= 14.0f, "the dearest console (%s) takes %.1f us of a 64-frame block here (14 at most: about half the board's 2 ms)", who, (double)worst);
    px_stop(); sim_run(0.8f); program(0);
}

// everything at once for a while on every console: songs, starts and stops, the knob, mutes, tilts, edits
static void soak(void)
{
    section("pixel: the soak");
    const char *e = getenv("PIXEL_SOAK");
    float per = e ? (float)atof(e) : 20.0f;
    if (per < 5.0f) per = 5.0f;
    quiet(); sim_menu_close(); px_stop(); program(0); sim_run(0.3f);
    uint32_t r = 12345;
    int ok = 0;
    for (int c = 0; c < CON_N; c++) {
        set("cons", (float)c);
        px_play_song(0);
        float t0 = sim_t, pk = 0;
        int step = 0;
        while (sim_t - t0 < per) {
            r = r * 1664525u + 1013904223u;
            uint32_t k = (r >> 8) % 40;
            if (k == 0) { program((int)((r >> 16) % (PX_NSONGS - 1))); set("cons", (float)c); }   // (a song brings its own console)
            else if (k == 1) { px_stop(); sim_run(0.3f); px_play_song((int)((r >> 16) % 8)); }
            else if (k == 2) push((float)((int)((r >> 16) % 3) - 1), (float)((int)((r >> 18) % 3) - 1));
            else if (k == 3) sim_tilt((float)((int)((r >> 16) % 100) - 50), (float)((int)((r >> 23) % 60)));
            else if (k == 4) sim_tilt(0, 0);
            else if (k == 5) { static const char *const T[] = { "lsnd", "bsnd", "csnd", "dkit", "carp", "cshp", "crhy", "lech" }; int p = tag(T[(r >> 16) % 8]); set(T[(r >> 16) % 8], (float)((r >> 20) % (uint32_t)(param(p)->max + 1))); }
            else if (k == 6) set("tmpo", (float)(60 + (r >> 16) % 181));
            else if (k == 7) set("swng", (float)((r >> 16) % 51));
            else if (k == 8) { px_song.pat[(r >> 16) % 8].lead[(r >> 20) % 16] = (uint8_t)(r >> 24); px_notes_edited(); }
            else if (k == 9) { px_song.pat[(r >> 16) % 8].drums[(r >> 20) % 16] = (uint8_t)((r >> 24) & 0x9F); px_notes_edited(); }
            else if (k == 10) set("glit", (float)((r >> 16) % 101));
            else if (k == 11) set("glit", 0);
            else if (k == 12) { front_midi(0x90, (uint8_t)(36 + (r >> 16) % 60), 100); sim_run(0.2f); front_midi(0x80, (uint8_t)(36 + (r >> 16) % 60), 0); }
            else if (k == 13) set("fram", (float)((r >> 16) & 1));
            sim_run(0.25f);
            float p = m_peak(sim_t - 0.25f, sim_t);
            if (p > pk) pk = p;
            step++;
        }
        px_mute = 0; sim_tilt(0, 0); set("glit", 0); set("tmpo", 150); set("swng", 0); set("fram", 0);
        program(0); set("cons", (float)c); px_stop(); sim_run(0.5f); px_play_song(0); sim_run(3.0f);
        float rr = m_rms(sim_t - 2.0f, sim_t);
        bool good = m_bad() == 0 && pk < 0.95f && rr > 0.02f && px_pos.running && px_pos.console == c;
        ok += good;
        note("%s: %.0f s, %d moves, peak %.3f, then the first song again at rms %.3f%s", PX_CONSOLES[c]->name, (double)(sim_t - t0), step, (double)pk, (double)rr, good ? "" : " BAD");
        px_stop(); sim_run(0.8f);
    }
    check(ok == CON_N, "%.0f s on each console with everything going: no NaN, under full scale, and it still plays the song after (%d of %d)", (double)per, ok, CON_N);
    set("cons", (float)CON_NES); program(0); sim_run(0.3f);
}

void test_host(void)
{
    clock_checks();
    hands_checks();
    link_checks();
    no_stick();
    console_change();
    cost();
    soak();
}
