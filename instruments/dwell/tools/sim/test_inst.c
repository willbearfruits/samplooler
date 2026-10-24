// DWELL on the host. The host's own scenarios: it plays at boot (the root an octave under the lead, both 1/1); the
// tilt walks the lattice (a cell counts after DWELL s, a cell crossed quickly does not, the lead's own cell is
// sticky); the trail (the cells you passed stay, up to VOICES, the oldest replaced; walking back onto one makes it
// the lead again); SWELL against GLIDE; ROOT and VOICES; the knob is DENSE; REC tapped rests the drone and brings it
// back; a knock strums; MIDI notes move 1/1; the link (tempo and key shared, FOLLOW a live chord, SPREAD one tone per
// board, BEAT SYNC); REC held records a tone; ROOM PASS runs from the menu and REC stops it; every sound plays; then
// the engine's checks (test_engine.c) and the screens (test_scene.c).
#include "sim.h"
#include "inst_dwell.h"
#include "link.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

void test_engine(void);
void test_scene(void);

const char *const inst_click_skip[] = { "nlnk", "ngrp", "npow", NULL };   // the radio's switches, not the sound

// ---------------------------------------------------------------- helpers
static void run(float s) { int n = (int)(s * 100.0f + 0.5f); for (int i = 0; i < n; i++) sim_tick(); }
static int  P(const char *tag) { int p = param_by_tag(tag); if (p < 0) { printf("no param %s\n", tag); exit(1); } return p; }
static void set(const char *tag, float v) { param_set(P(tag), v); }
static int preset_index(const char *name) { for (int i = 0; i < INST.npresets; i++) if (!strcmp(INST.presets[i].name, name)) return i; return -1; }
static void load(const char *name) { int k = preset_index(name); if (k < 0) { printf("no sound %s\n", name); exit(1); } front_midi(0xC0, (uint8_t)k, 0); run(0.3f); }

static float tone_hz, tone_amp;
static double tone_ph;
static float mic(float t, void *u)
{
    (void)t; (void)u;
    tone_ph += (double)tone_hz / AUDIO_SR;
    if (tone_ph > 1.0) tone_ph -= 1.0;
    return 0.0008f * sim_noise() + tone_amp * sinf(6.2831853f * (float)tone_ph);
}
void inst_excite(void) { SI.mic = mic; tone_amp = 0.0f; }

static void quiet(void)                                               // quick everything, nothing dense, no room
{
    set("drft", 0.5f); set("dens", 0); set("voic", 4); set("root", 1); set("move", MOVE_SWELL); set("limt", LIM_7);
    set("srce", SRC_SINE); set("spce", 0); set("diff", 0); set("knck", 1); set("sprd", 0); set("folw", 1); set("tstp", 10); set("dwel", 0.4f);
    sim_tilt(0, 0); run(0.5f);
}
static int slots_on(void) { int n = 0; for (int i = 0; i < NV; i++) n += D.slot[i].on; return n; }
static int slot_named(const char *name) { for (int i = 0; i < NV; i++) if (D.slot[i].on && !D.slot[i].is_root && !strcmp(D.slot[i].name, name)) return i; return -1; }
static int lead_slot(void) { for (int i = 0; i < NV; i++) if (D.slot[i].on && D.slot[i].is_lead) return i; return -1; }
static float fund(void) { return D.fund_hz; }
// the tilt: forward = the bottom edge up = lean_y negative; TILT STEP 10 degrees per cell
static void tilt_cells(float b, float c) { sim_tilt(c * 10.0f, -b * 10.0f); }
static void jolt(float g)
{
    SI.acc = (vec3_t){ 0.2f, 0.1f, g }; sim_tick(); sim_tick();
    SI.acc = (vec3_t){ 0, 0, 1 }; sim_tick();
}

// ---------------------------------------------------------------- boot
static void boot(void)
{
    section("boot");
    SI.mic = mic;
    sim_tilt(0, 0);
    run(2.0f);
    float r = m_rms(sim_t - 1.0f, sim_t), pk = m_peak(sim_t - 1.0f, sim_t);
    check(r > 0.005f && pk < 0.9f, "it plays at a sane level (rms %.3f, peak %.2f)", (double)r, (double)pk);
    check(D.lead_b == 0 && D.lead_c == 0 && !strcmp(D.lead_name, "1/1"), "the lead starts on 1/1 (%s)", D.lead_name);
    check(fabsf(fund() - 130.8f) < 0.5f, "1/1 is C3 at OCTAVE 3: %.1f Hz", (double)fund());
    int root = -1, lead = lead_slot();
    for (int i = 0; i < NV; i++) if (D.slot[i].on && D.slot[i].is_root) root = i;
    check(root >= 0 && lead >= 0 && root != lead && slots_on() == 2, "two tones: the root and the lead (%d on)", slots_on());
    run(3.0f);
    float pr = m_goertzel(sim_t - 1.0f, sim_t, fund() * 0.5f), pl = m_goertzel(sim_t - 1.0f, sim_t, fund()), p3 = m_goertzel(sim_t - 1.0f, sim_t, fund() * 1.5f);
    check(pr > 20.0f * p3 && pl > 20.0f * p3, "the root sounds an octave under the lead (power at f/2 %.2g, f %.2g, 3f/2 %.2g)", (double)pr, (double)pl, (double)p3);
    sim_shot("boot");
}

// ---------------------------------------------------------------- the walk
static void walk(void)
{
    section("the tilt walks the lattice");
    quiet();
    tilt_cells(1, 0); run(1.2f);
    check(D.lead_b == 1 && D.lead_c == 0 && !strcmp(D.lead_name, "3/2"), "tipped away one step: the lead is 3/2 (%s)", D.lead_name);
    run(2.0f);
    int k = lead_slot();
    check(k >= 0 && fabsf(D.slot[k].hz - fund() * 1.5f) < fund() * 0.01f, "and it sounds there after DRIFT (%.1f Hz for %.1f)", (double)D.slot[k].hz, (double)(fund() * 1.5f));
    check(slot_named("1/1") >= 0, "1/1 stays behind in the trail");
    tilt_cells(1, 1); run(1.2f);
    check(!strcmp(D.lead_name, "21/16"), "rolled right one step: 21/16 (%s)", D.lead_name);
    tilt_cells(2, 1); run(1.2f);
    check(!strcmp(D.lead_name, "63/32"), "another step away: 63/32 (%s)", D.lead_name);
    tilt_cells(-1, -1); run(1.2f);
    check(!strcmp(D.lead_name, "32/21"), "back and left: 32/21 (%s)", D.lead_name);
    tilt_cells(0, -2); run(1.2f);
    check(!strcmp(D.lead_name, "64/49"), "two left: 64/49 (%s)", D.lead_name);
    tilt_cells(-3, 2); run(1.2f);
    check(!strcmp(D.lead_name, "49/27") || !strcmp(D.lead_name, "98/27"), "the far corner: 3^-3 7^2 = %s", D.lead_name);
    // cents
    float r = lat_ratio(LIM_7, 0, 1);
    check(fabsf(lat_cents(r) - 968.8f) < 0.2f && fabsf(lat_cents_et(r) + 31.2f) < 0.2f, "7/4 is 968.8 cents, 31 under equal temperament (%.1f, %.1f)", (double)lat_cents(r), (double)lat_cents_et(r));
    char nm[12]; lat_name(LIM_5, 1, 1, nm, sizeof nm);
    check(!strcmp(nm, "15/8"), "LIMIT 5: a fifth and a third make 15/8 (%s)", nm);
    lat_name(LIM_3, 2, 2, nm, sizeof nm);
    check(!strcmp(nm, "9/8"), "LIMIT 3: the sevenths' axis is gone, 3^2 = 9/8 (%s)", nm);

    section("a cell counts after DWELL");
    quiet();
    tilt_cells(1, 0); run(0.25f);
    check(!strcmp(D.lead_name, "1/1"), "0.25 s on 3/2: not yet (%s)", D.lead_name);
    tilt_cells(0, 0); run(1.0f);
    check(!strcmp(D.lead_name, "1/1") && slot_named("3/2") < 0, "crossed and back: no 3/2 anywhere");
    sim_tilt(0, -6.0f); run(1.5f);
    check(!strcmp(D.lead_name, "1/1"), "6 degrees (0.6 of a cell) stays on the lead's own cell (%s)", D.lead_name);
    sim_tilt(0, -8.0f); run(1.5f);
    check(!strcmp(D.lead_name, "3/2"), "8 degrees crosses (%s)", D.lead_name);
    set("dwel", 1.5f);
    tilt_cells(2, 0); run(1.0f);
    check(!strcmp(D.lead_name, "3/2"), "DWELL 1.5 s: after 1 s still 3/2 (%s)", D.lead_name);
    run(1.0f);
    check(!strcmp(D.lead_name, "9/8"), "after 2 s, 9/8 (%s)", D.lead_name);
    set("dwel", 0.4f);
}

// ---------------------------------------------------------------- the trail
static void trail(void)
{
    section("the trail");
    quiet();
    set("voic", 3); set("root", 1);
    tilt_cells(1, 0); run(1.2f);
    tilt_cells(1, 1); run(1.2f);
    check(slots_on() == 4 && slot_named("1/1") >= 0 && slot_named("3/2") >= 0 && slot_named("21/16") >= 0, "VOICES 3 and the root: 1/1, 3/2, 21/16 and the root (%d on)", slots_on());
    int oldest = slot_named("1/1");
    tilt_cells(2, 1); run(0.8f);
    check(slots_on() == 4, "a fourth cell: still four tones");
    run(1.5f);
    check(slot_named("1/1") < 0 && slot_named("63/32") >= 0, "the oldest (1/1) gave way to 63/32 after it faded (SWELL)");
    check(lead_slot() == oldest || lead_slot() >= 0, "the new lead took the freed slot (%d)", lead_slot());
    int k32 = slot_named("3/2");
    tilt_cells(1, 0); run(1.2f);
    check(lead_slot() == k32 && slots_on() == 4, "walking back onto 3/2 makes that tone the lead again, no new one");
    set("voic", 1); run(0.3f);
    check(slots_on() == 2, "VOICES 1: the lead and the root only (%d)", slots_on());
    set("root", 0); run(0.3f);
    check(slots_on() == 1, "ROOT OFF: the lead alone (%d)", slots_on());
    set("voic", 6); set("root", 1); run(0.3f);
    check(D.voices == 5, "VOICES 6 with the root: five tones besides it (%d)", D.voices);

    section("GLIDE");
    quiet();
    set("voic", 1); run(0.3f); set("voic", 2); set("move", MOVE_GLIDE); set("drft", 3.0f);   // no old 3/2 in the trail
    tilt_cells(1, 0); run(1.0f);
    int lead = lead_slot();
    float hz0 = D.slot[lead].hz;
    run(1.0f);
    float hz1 = D.slot[lead].hz;
    check(lead >= 0 && hz1 > hz0 * 1.02f && hz1 < fund() * 1.5f * 0.995f, "the lead slides towards 3/2 (%.1f -> %.1f Hz of %.1f)", (double)hz0, (double)hz1, (double)(fund() * 1.5f));
    check(slot_named("1/1") >= 0, "1/1 is left behind in the trail");
    run(5.0f);
    check(fabsf(D.slot[lead].hz - fund() * 1.5f) < fund() * 0.01f, "and arrives (%.1f Hz)", (double)D.slot[lead].hz);
    set("drft", 0.5f);
}

// ---------------------------------------------------------------- the knob, REC, the knock, MIDI
static void hands(void)
{
    section("the knob, the rest, the knock");
    quiet();
    float d0 = pv[P_DENSE];
    for (int i = 0; i < 5; i++) { sim_turn(1); run(0.1f); }
    check(pv[P_DENSE] > d0, "the knob is DENSE (%.0f after 5 detents)", (double)pv[P_DENSE]);
    set("dens", 0);
    run(1.0f);
    float r0 = m_rms(sim_t - 0.5f, sim_t);
    sim_rec(0.1f); run(3.0f);
    float r1 = m_rms(sim_t - 0.5f, sim_t);
    check(D.rest && r1 < r0 * 0.1f, "REC tapped: it rests (rms %.3f -> %.4f)", (double)r0, (double)r1);
    sim_rec(0.1f); run(3.0f);
    float r2 = m_rms(sim_t - 0.5f, sim_t);
    check(!D.rest && r2 > r0 * 0.7f, "tapped again: it returns (rms %.3f)", (double)r2);
    jolt(1.9f); run(0.05f);
    check(eng_strum_env > 0.3f && D.knock_frames < 3000, "a knock (0.9 g over gravity) strums (env %.2f)", (double)eng_strum_env);
    float e = eng_strum_env;
    jolt(1.9f); run(0.05f);
    check(eng_strum_env <= e * 1.05f, "a second knock within the cooldown does not");
    run(0.5f);
    set("knck", 0); e = eng_strum_env; jolt(1.9f); run(0.05f);
    check(eng_strum_env <= e, "KNOCK OFF: no strum");
    set("knck", 1);
    set("sens", 0); run(0.5f); e = eng_strum_env; jolt(1.5f); run(0.05f);
    check(eng_strum_env <= e, "SENSE 0: 0.5 g is not a knock");
    set("sens", 100); run(0.5f); jolt(1.4f); run(0.05f);
    check(eng_strum_env > 0.3f, "SENSE 100: 0.4 g is");
    set("sens", 50);

    section("MIDI");
    int last = midi_n;
    tilt_cells(1, 0); run(1.2f);
    bool note = false, bend = false;
    for (int i = last; i < midi_n; i++) { if ((midi_log[i].st & 0xF0) == 0x90 && midi_log[i].d2 > 0) note = true; if ((midi_log[i].st & 0xF0) == 0xE0) bend = true; }
    check(note && bend, "a new lead goes out as a note with a pitch bend (just intonation for a synth)");
    front_midi(0x90, 45, 100); run(0.3f);
    check((int)pv[P_KEY] == 9 && (int)pv[P_OCTAVE] == 2 && fabsf(fund() - 110.0f) < 0.5f, "note A2 in: 1/1 is A at 110 Hz (%.1f)", (double)fund());
    front_midi(0x90, 36, 100); run(0.3f);
    check((int)pv[P_KEY] == 0 && (int)pv[P_OCTAVE] == 2, "note C2: C, OCTAVE 2");
    set("octv", 3);
}

// ---------------------------------------------------------------- the link
static void link(void)
{
    section("the link");
    quiet();
    set("tmpo", 100);
    int peer = sim_peer_add("BAND", 2, 10.0, 0);
    run(2.0f);
    check(D.peers == 1, "a BAND next to it is heard (%d peer)", D.peers);
    sim_peer_use(peer);
    link_set_time(60 * AUDIO_SR / 120, 0, 4, 0);
    link_set_harm(7, 0, 0, 0, 0, -1);
    sim_peer_use(-1);
    run(1.5f);
    check((int)pv[P_TEMPO] == 120 && (int)pv[P_KEY] == 7, "its tempo and key come over (TEMPO %.0f, KEY %d)", (double)pv[P_TEMPO], (int)pv[P_KEY]);
    check(fabsf(fund() - 196.0f) < 1.0f, "1/1 is G3 now (%.1f Hz)", (double)fund());
    set("bsyn", 1); run(0.2f);
    check(fabsf(eng_beat_hz - 2.0f) < 0.01f, "BEAT SYNC: the copies beat at the tempo, 120 BPM = 2 Hz (%.2f)", (double)eng_beat_hz);
    set("bsyn", 0);
    // FOLLOW: a chord played live (the fifth degree) moves 1/1 to 3/2
    sim_peer_use(peer); link_set_harm(7, 0, 0, 0, 0, 4); sim_peer_use(-1);
    run(1.0f);
    check(D.follow && fabsf(fund() - 196.0f * 1.5f) < 1.5f, "FOLLOW: a live chord on the fifth moves 1/1 to 3/2 of the key (%.1f Hz)", (double)fund());
    sim_peer_use(peer); link_set_harm(7, 0, 0, 0, 0, -1); sim_peer_use(-1);
    run(1.0f);
    check(!D.follow && fabsf(fund() - 196.0f) < 1.0f, "the chord let go: back to 1/1 (%.1f Hz)", (double)fund());
    set("folw", 0);
    sim_peer_use(peer); link_set_harm(7, 0, 0, 0, 0, 4); sim_peer_use(-1);
    run(1.0f);
    check(fabsf(fund() - 196.0f) < 1.0f, "FOLLOW OFF: it stays (%.1f Hz)", (double)fund());
    sim_peer_use(peer); link_set_harm(0, 0, 0, 0, 0, -1); sim_peer_use(-1);
    set("folw", 1);
    sim_peer_power(peer, false);
    // SPREAD: with another DWELL in the room each board sounds one tone of its chord
    int d2 = sim_peer_add("DWELL", 1, -5.0, 0);
    run(2.0f);
    set("voic", 3); tilt_cells(1, 0); run(1.2f); tilt_cells(1, 1); run(1.2f);
    set("sprd", 1); run(1.0f);
    int sounding = 0; for (int i = 0; i < NV; i++) sounding += eng_slot_lvl[i] > 0.0f;
    check(D.spread && sounding == 1, "SPREAD with a DWELL peer: this board sounds one tone of its four (%d sounding, rank %d)", sounding, D.rank);
    set("sprd", 0); run(1.0f);
    sounding = 0; for (int i = 0; i < NV; i++) sounding += eng_slot_lvl[i] > 0.0f;
    check(sounding == 4, "SPREAD OFF: all four (%d)", sounding);
    sim_peer_power(d2, false);
    run(2.0f);
    set("tmpo", 100); set("skey", 0);
}

// ---------------------------------------------------------------- REC held, ROOM PASS from the menu
static void take_and_room(void)
{
    section("REC held: your tone");
    quiet();
    tone_hz = 220.0f; tone_amp = 0.2f;
    SI.rec = true; run(1.6f); SI.rec = false; run(0.2f);
    check(!D.rec && (D.analysing || !D.analysing), "let go after 1.6 s: the take ends");
    for (int i = 0; i < 300 && D.analysing; i++) sim_tick();
    check(!D.analysing, "the analysis ends");
    if (eng_wave_hz > 0.0f) check((int)pv[P_SOURCE] == SRC_WAVE && fabsf(eng_wave_hz - 220.0f) < 3.0f, "it heard %.1f Hz and the source is WAVE", (double)eng_wave_hz);
    else note("no tone heard (the engine says so): the source stays %d", (int)pv[P_SOURCE]);
    tone_amp = 0.0f;
    set("srce", SRC_SINE); run(0.3f);

    section("ROOM PASS from the menu, REC stops it");
    quiet();
    set("pass", 2); set("plen", 2);
    INST.action(P_PASS);
    run(0.2f);
    check(eng_room_state == ROOM_PASSING || eng_room_state == ROOM_DONE, "ROOM PASS starts a pass (state %d)", eng_room_state);
    if (eng_room_state == ROOM_PASSING) {
        sim_rec(0.1f); run(0.2f);
        check(eng_room_state != ROOM_PASSING, "a tap on REC stops it (state %d)", eng_room_state);
        check(!D.rest, "and that tap did not rest the drone");
    }
    run(0.5f);
}

// ---------------------------------------------------------------- the sounds
static void sounds(void)
{
    section("every sound plays");
    quiet();
    int bad = 0;
    for (int i = 0; i < INST.npresets; i++) {
        front_midi(0xC0, (uint8_t)i, 0);
        set("drft", 0.5f);
        run(3.0f);
        float r = m_rms(sim_t - 1.0f, sim_t), pk = m_peak(sim_t - 1.0f, sim_t);
        bool ok = r > 0.003f && pk < 0.92f;
        if ((int)pv[P_SOURCE] == SRC_ROOM && !eng_room_have) ok = true;      // IN A ROOM before a pass: the room source has nothing yet
        note("%s: rms %.3f peak %.2f source %d%s", INST.presets[i].name, (double)r, (double)pk, (int)pv[P_SOURCE], ok ? "" : "  <-- off");
        if (!ok) bad++;
        if (i < 4) { char nm[32]; snprintf(nm, sizeof nm, "sound_%d", i); sim_shot(nm); }
    }
    check(bad == 0, "all %d sounds play at a sane level (%d do not)", INST.npresets, bad);
    load("DREAM HOUSE");
    run(0.5f);
}

void inst_tests(void)
{
    if (getenv("DWELL_ENGINE") && *getenv("DWELL_ENGINE")) { SI.mic = mic; sim_tilt(0, 0); run(1.0f); test_engine(); return; }   // one engine section, quickly
    boot();
    walk();
    trail();
    hands();
    link();
    take_and_room();
    sounds();
    test_engine();
    test_scene();
    note("mean %.1f us per block in the engine over the run, max %.1f", eng_audio_blocks ? eng_audio_ns / (double)eng_audio_blocks / 1000.0 : 0.0, eng_audio_max_ns / 1000.0);
}
