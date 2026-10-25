// AIRDWELLER on the host: the host's own scenarios. The strokes (four ways, the return that is not a hit, a turn with
// no jolt, the grid), the hands' kits, the loops (a hold of two bars, replayed, toggled, stopped, erased), CUT (a hard
// stop, back in on the beat), the roles (PAINT strikes the material with the swings, PUMP takes the movement as
// energy), the hand on the material (tilt, twist, roll, shake), and two boxes: who is LEFT, the hits this box sends
// (their ticks, their voices, a loop's hits a quarter of a beat ahead), the other box's hits striking this one on the
// same tick, two DRUM boxes keeping their own drums, the other's kick tuning this box's material. Then the engine's
// checks (test_engine.c) and the screens (test_scene.c), when they are there.
#include "sim.h"
#include "inst_ad.h"
#include "link.h"
#include "dsp.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

float st_mic(float t, void *u);
void  st_reset(void);
void  st_swing(float t, vec3_t dir, float peak);
void  st_stroke(float t, vec3_t dir, float peak, float dur, float ret);
void  st_hold(vec3_t dir, float dps);
void  st_flat_acc(int on);
extern const vec3_t ST_FRONT, ST_BACK, ST_LEFT, ST_RIGHT, ST_TWIST;
extern uint32_t ad_strike_at; extern int64_t ad_strike_tick; extern int ad_strikes; extern uint32_t ad_played_frame;
int64_t ad_now_tick(void);
void test_engine(void) __attribute__((weak));
void test_scene(void) __attribute__((weak));

const char *const inst_click_skip[] = { "nlnk", "ngrp", "npow", NULL };   // the radio's switches, not the sound

static void run(float s) { int n = (int)(s * 100.0f + 0.5f); for (int i = 0; i < n; i++) sim_tick(); }
static int  P(const char *tag) { int p = param_by_tag(tag); if (p < 0) { printf("no param %s\n", tag); exit(1); } return p; }
static void set(const char *tag, float v) { param_set(P(tag), v); }
void inst_excite(void) { SI.mic = st_mic; st_reset(); }

// what the engine reported as sounding since the last call
static played_t got[256];
static int ngot;
static void drain(void) { ngot = 0; played_t p; while (eng_played_pop(&p)) if (ngot < 256) got[ngot++] = p; }
// the host pops the played queue itself every tick (sounded()); so the tests watch D.flash and the strike hooks instead,
// and count hits through MIDI: each sounded voice sends a note on channel 10
static int notes_since(int from, int note)
{
    int n = 0;
    for (int i = from; i < midi_n; i++) if (midi_log[i].st == 0x99 && midi_log[i].d2 > 0 && (note < 0 || midi_log[i].d1 == note)) n++;
    return n;
}
static int last_note_since(int from) { int nt = -1; for (int i = from; i < midi_n; i++) if (midi_log[i].st == 0x99 && midi_log[i].d2 > 0) nt = midi_log[i].d1; return nt; }

static void quiet(void)
{
    SI.mic = st_mic; st_reset(); sim_tilt(0, 0);
    set("role", ROLE_DRUM); set("hand", HAND_AUTO); set("pair", 1); set("qant", 2); set("swng", 0); set("tmpo", 120);
    for (int p = 0; p < NPAD; p++) param_set(P_FRONT + p, DV_AUTO);
    set("cnfm", 1); set("sens", 50); set("flip", 0); set("side", 0);
    run(0.5f);
}

// ---------------------------------------------------------------- the strokes
static void strokes(void)
{
    section("strokes: four ways, four pads");
    quiet();
    static const char *const way[NPAD] = { "forward", "left", "right", "back" };
    const vec3_t dir[NPAD] = { ST_FRONT, ST_LEFT, ST_RIGHT, ST_BACK };
    static const int gm[NPAD] = { 42, 36, 38, 49 };                   // ONE kit: HAT, KICK, SNAP, CLANG (GM notes)
    int ok = 0;
    for (int p = 0; p < NPAD; p++) {
        int m0 = midi_n;
        st_swing(sim_t + 0.05f, dir[p], 650.0f);
        run(0.6f);
        int nt = last_note_since(m0), n = notes_since(m0, -1);
        note("%s: pad %d, %d hit, note %d", way[p], D.last_pad, n, nt);
        ok += D.last_pad == p && n == 1 && nt == gm[p];
        run(0.3f);
    }
    check(ok == 4, "a swing each way is one hit on its pad, with its voice (%d of 4)", ok);
    check(D.pad_voice[PD_LEFT] == DV_KICK && D.pad_voice[PD_FRONT] == DV_HAT, "one box alone: the mixed kit (left KICK, forward HAT)");
    // the return is not a hit; a slow turn is not a hit; a turn with no jolt (CONFIRM) is not a hit
    int m0 = midi_n;
    st_stroke(sim_t + 0.05f, ST_LEFT, 650.0f, 0.12f, 0.7f);
    run(0.8f);
    check(notes_since(m0, -1) == 1, "a swing and the hand coming back: one hit (%d)", notes_since(m0, -1));
    m0 = midi_n;
    st_stroke(sim_t + 0.05f, ST_RIGHT, 250.0f, 0.5f, 0.0f);
    run(1.0f);
    check(notes_since(m0, -1) == 0, "a slow turn: no hit");
    m0 = midi_n;
    st_flat_acc(1);
    st_swing(sim_t + 0.05f, ST_RIGHT, 700.0f);
    run(0.8f);
    st_flat_acc(0);
    check(notes_since(m0, -1) == 0, "CONFIRM: a fast turn with no jolt is no hit");
    // on the grid: a hit placed just before a sixteenth line waits for it and sounds on it. The swing is timed so it is
    // caught (peak, then 15 % down, at the next 10 ms tick: 80 to 95 ms in; its meant moment HIT_LEAD before that) before
    // the line.
    grid_t g; eng_get_grid(&g);
    uint32_t nowf = audio_frames();
    int64_t line = (eng_tick_at(&g, nowf) / (PPB / 4) + 3) * (PPB / 4);
    float line_t = sim_t + (float)(int32_t)(eng_frame_of(&g, line) - nowf) / (float)AUDIO_SR;
    m0 = midi_n;
    st_swing(line_t - 0.110f, ST_LEFT, 700.0f);
    run(0.6f);
    eng_get_grid(&g);
    uint32_t want = eng_frame_of(&g, line);
    check(notes_since(m0, -1) == 1 && ad_played_frame == want, "QUANTIZE 1/16: a hit just before a line sounds on it (frame %u, the line's %u)", (unsigned)ad_played_frame, (unsigned)want);
}

// ---------------------------------------------------------------- the loops
static void loops(void)
{
    section("loops: hold REC and play, whole bars");
    quiet();
    set("era ", 0); INST.action(P_ERASE_ALL); run(0.2f);
    // two bars at 120: four seconds; a left swing on each beat
    SI.rec = true;
    for (int b = 0; b < 9; b++) { st_swing(sim_t + 0.05f, (b & 1) ? ST_RIGHT : ST_LEFT, 700.0f); run(0.5f); }
    SI.rec = false;
    run(3.5f);
    check(eng_loops[0].bars >= 1 && eng_loops[0].n >= 4 && eng_loops[0].on, "a loop of %d bars, %d hits, playing", eng_loops[0].bars, eng_loops[0].n);
    int m0 = midi_n;
    run(4.0f);
    int replayed = notes_since(m0, -1);
    check(replayed >= 6, "it replays by itself (%d hits in 4 s)", replayed);
    // the knob held: every loop stops; again: they go
    INST.hold();
    m0 = midi_n; run(2.0f);
    check(notes_since(m0, -1) == 0, "the knob held: the loops stop");
    INST.hold(); run(0.1f);
    m0 = midi_n; run(2.0f);
    check(notes_since(m0, -1) > 0, "held again: they go");
    // the stick: up toggles loop 1
    SI.joy = true; SI.jy = 1.0f; run(0.1f); SI.jy = 0.0f; run(0.1f);
    check(!eng_loops[0].on, "the stick up: loop 1 off");
    SI.jy = 1.0f; run(0.1f); SI.jy = 0.0f; run(0.1f); SI.joy = false;
    check(eng_loops[0].on, "up again: on");
    INST.action(P_ERASE_ALL); run(0.2f);
    check(eng_loops[0].bars == 0, "ERASE ALL: gone");
}

// ---------------------------------------------------------------- CUT
static void cut(void)
{
    section("CUT: a hard stop, back in on the beat");
    quiet();
    st_hold(ST_TWIST, 0);
    sim_rec(0.1f); run(0.05f);
    check(D.cut && eng_cut == 1, "a tap on REC: cut");
    run(1.0f);
    sim_rec(0.1f);
    int64_t t_tap = ad_now_tick();
    for (int i = 0; i < 100 && eng_cut; i++) sim_tick();
    int64_t t_back = ad_now_tick();
    check(!eng_cut && (t_back % PPB) < 4 && t_back - t_tap <= PPB + 4, "tapped again: back in on the next beat (tick %lld, %lld after the tap)", (long long)(t_back % PPB), (long long)(t_back - t_tap));
}

// ---------------------------------------------------------------- the roles
static void roles(void)
{
    section("roles: PAINT strikes, PUMP pumps");
    quiet();
    set("role", ROLE_PAINT); run(0.3f);
    int m0 = midi_n;
    float l0 = eng_mat_lvl;
    st_swing(sim_t + 0.05f, ST_LEFT, 700.0f);
    run(0.4f);
    check(notes_since(m0, -1) == 1 && eng_mat_lvl > l0 + 0.01f, "PAINT: a swing strikes the material (level %.3f -> %.3f)", (double)l0, (double)eng_mat_lvl);
    // the hand on the material: tilt, twist, roll
    set("matl", MAT_METAL); set("mrph", 100);
    sim_tilt(0, -45); run(1.0f);
    float mfw = eng_mat_morph;
    sim_tilt(0, 0); run(1.0f);
    check(mfw > 1.2f && eng_mat_morph < 0.1f, "tipped away 45 degrees: on towards rubber (morph %.2f), level: metal (%.2f)", (double)mfw, (double)eng_mat_morph);
    set("bend", 12);
    st_hold(ST_TWIST, 300.0f); run(0.5f);
    float bnd = eng_bend;
    st_hold(ST_TWIST, 0.0f); run(1.0f);
    check(bnd > 10.0f && fabsf(eng_bend) < 0.5f, "twisting bends it up to TWIST BEND (%.1f st), still it comes back (%.2f)", (double)bnd, (double)eng_bend);
    set("brit", 50);
    sim_tilt(30, 0); run(1.0f);
    float br = eng_mat_bright;
    sim_tilt(0, 0); run(1.0f);
    check(br > 0.9f && fabsf(eng_mat_bright - 0.5f) < 0.05f, "rolled right: brighter (%.2f), level: BRIGHT (%.2f)", (double)br, (double)eng_mat_bright);
    // its pitch: this box's own kick times RATIO when alone (key C: the kick is C2, 65.4 Hz; RATIO X4)
    set("tnto", TT_KICK); set("rato", 3); set("skey", 0); set("tune", 0); run(0.2f);
    check(fabsf(eng_mat_hz - 65.41f * 4.0f) < 0.5f, "TUNE TO THE KICK, X4: %.1f Hz (the kick %.1f)", (double)eng_mat_hz, (double)eng_kick_hz);
    // PUMP: swings are energy, not hits
    set("role", ROLE_PUMP); set("clmb", 2); set("fall", 6); set("rstl", -24);
    run(6.0f);
    float lv0 = eng_mat_level, b0 = eng_bow;
    m0 = midi_n;
    st_hold(ST_TWIST, 400.0f); run(2.5f);
    float lv1 = eng_mat_level, b1 = eng_bow;
    st_hold(ST_TWIST, 0.0f);
    check(notes_since(m0, -1) == 0, "PUMP: moving plays no hits");
    check(b1 > 0.9f && b0 < 0.05f && lv1 > 10.0f * lv0, "the movement pumps it: bow %.2f -> %.2f, level %.3f -> %.3f", (double)b0, (double)b1, (double)lv0, (double)lv1);
    run(7.0f);
    check(eng_bow < 0.05f, "still, it falls back (%.2f)", (double)eng_bow);
    // a shake scrapes (CREAK)
    set("crek", 100); set("shak", 100);
    for (int i = 0; i < 80; i++) { SI.acc = (vec3_t){ (i & 1) ? 1.6f : -1.6f, 0.3f, 1.0f }; sim_tick(); }
    SI.acc = (vec3_t){ 0, 0, 1 };
    check(eng_creak > 0.6f, "a shake: CREAK %.2f", (double)eng_creak);
    run(3.0f);
}

// ---------------------------------------------------------------- two boxes
static int peer_k = -1;
static uint8_t peer_seq;
static void peer_bytes(int role, int hand, int kick, int nhits, const int64_t *ticks, const int *voices)
{
    static uint8_t a[16];
    static int64_t pt[3]; static int pvo[3];
    for (int i = 0; i < nhits; i++) { pt[0] = pt[1]; pt[1] = pt[2]; pt[2] = ticks[i]; pvo[0] = pvo[1]; pvo[1] = pvo[2]; pvo[2] = voices[i]; peer_seq++; }
    a[0] = 3; a[1] = (uint8_t)(role | hand << 2); a[2] = 0; a[3] = (uint8_t)kick; a[4] = peer_seq;
    for (int k = 0; k < 3; k++) { uint16_t lo = (uint16_t)(pt[k] & 0xFFFF); a[5 + 3 * k] = (uint8_t)(lo & 0xFF); a[6 + 3 * k] = (uint8_t)(lo >> 8); a[7 + 3 * k] = (uint8_t)(pvo[k] << 4 | 15); }
    sim_peer_use(peer_k); link_set_app(a, 16); link_poke(); sim_peer_use(-1);
}
static const link_peer_t *me_seen_by_peer(void)
{
    static link_peer_t pr[LINK_PEERS_MAX];
    sim_peer_use(peer_k);
    int n = link_peers(pr, LINK_PEERS_MAX);
    sim_peer_use(-1);
    for (int i = 0; i < n; i++) if (!strncmp(pr[i].name, "AIRDWELL", 8)) return &pr[i];
    return NULL;
}

static void pair(void)
{
    section("two boxes: one in each hand");
    quiet();
    INST.action(P_ERASE_ALL);
    peer_k = sim_peer_add("AIRDWELL", 1, 6.0, 0.0);
    peer_bytes(ROLE_DRUM, HAND_AUTO, 36, 0, NULL, NULL);
    run(3.0f);
    const link_peer_t *me = me_seen_by_peer();
    link_peer_t pr[LINK_PEERS_MAX]; int np = link_peers(pr, LINK_PEERS_MAX);
    uint32_t peer_id = 0; for (int i = 0; i < np; i++) if (!strncmp(pr[i].name, "AIRDWELL", 8)) peer_id = pr[i].id;
    check(D.paired && me, "they find each other");
    int want = me && me->id < peer_id ? HAND_LEFT : HAND_RIGHT;
    check(D.hand == want, "HAND AUTO: the lower id is the LEFT hand (this box: %s)", D.hand == HAND_LEFT ? "LEFT" : "RIGHT");
    if (D.hand == HAND_LEFT) check(D.pad_voice[PD_LEFT] == DV_KICK && D.pad_voice[PD_BACK] == DV_SUB, "the left hand's kit: KICK, SNAP, WHIP, SUB");
    else check(D.pad_voice[PD_LEFT] == DV_CLANG && D.pad_voice[PD_FRONT] == DV_HAT, "the right hand's kit: CLANG, GLASS, HAT, CREAK");
    // this box drums: its hits go out with their ticks and voices
    uint8_t s0 = me ? me->app[4] : 0;
    st_swing(sim_t + 0.05f, ST_LEFT, 700.0f);
    run(0.6f);
    me = me_seen_by_peer();
    int64_t now = ad_now_tick();
    bool sent_ok = false;
    if (me) {
        uint16_t lo = (uint16_t)(me->app[11] | me->app[12] << 8);
        int64_t tick = now + (int16_t)(lo - (uint16_t)(now & 0xFFFF));
        sent_ok = (uint8_t)(me->app[4] - s0) == 1 && (me->app[13] >> 4) == D.pad_voice[PD_LEFT] && tick % (PPB / 4) == 0 && tick <= now && now - tick < PPB;
    }
    check(sent_ok, "a hit here goes to the other box: one more, its voice, its tick on a sixteenth");
    // a loop's hits go a little ahead
    SI.rec = true;
    for (int b = 0; b < 9; b++) { st_swing(sim_t + 0.05f, ST_LEFT, 700.0f); run(0.5f); }
    SI.rec = false;
    run(4.0f);
    int ahead = 0;
    for (int i = 0; i < 60; i++) {
        sim_tick();
        me = me_seen_by_peer();
        if (!me) continue;
        uint16_t lo = (uint16_t)(me->app[11] | me->app[12] << 8);
        int64_t nt = ad_now_tick();
        int64_t tick = nt + (int16_t)(lo - (uint16_t)(nt & 0xFFFF));
        if (tick > nt) ahead++;
    }
    check(ahead > 5, "a loop's hits reach the other box before they are due (%d of 60 ticks saw one ahead)", ahead);
    INST.action(P_ERASE_ALL); run(0.5f);
    // this box paints: the other box's hits strike it on their own tick
    set("role", ROLE_PAINT); run(0.5f);
    int64_t tk = (ad_now_tick() / (PPB / 4) + 4) * (PPB / 4);      // a sixteenth a beat ahead
    int v1 = DV_KICK, n0 = ad_strikes;
    peer_bytes(ROLE_DRUM, HAND_LEFT, 36, 1, &tk, &v1);
    grid_t g = { 0 };
    for (int i = 0; i < 30 && ad_strikes == n0; i++) { sim_tick(); eng_get_grid(&g); }   // the grid on the strike's own tick
    check(ad_strikes == n0 + 1 && ad_strike_tick == tk && ad_strike_at == eng_frame_of(&g, tk),
          "the other hand's kick strikes this box on its own tick (tick %lld of %lld, frame %u, the tick's %u)",
          (long long)ad_strike_tick, (long long)tk, (unsigned)ad_strike_at, (unsigned)eng_frame_of(&g, tk));
    run(0.3f);
    check(fabsf(eng_mat_hz - midi_hz(36) * 4.0f) < 0.5f, "TUNE TO THE KICK: the other hand's kick (C2) sets this box's material (%.1f Hz)", (double)eng_mat_hz);
    check(eng_duck > 0.4f, "and its hits duck this box (DUCK %.2f)", (double)eng_duck);
    // three hits at once arrive; an old number is not played again
    int64_t t3[3] = { tk + 24, tk + 48, tk + 72 }; int vv[3] = { DV_SNAP, DV_HAT, DV_CLANG };
    n0 = ad_strikes;
    peer_bytes(ROLE_DRUM, HAND_LEFT, 36, 3, t3, vv);
    run(0.5f);
    check(ad_strikes == n0 + 3, "three hits between two packets: all three strike (%d)", ad_strikes - n0);
    peer_bytes(ROLE_DRUM, HAND_LEFT, 36, 0, NULL, NULL);
    n0 = ad_strikes; run(0.5f);
    check(ad_strikes == n0, "the same bytes again: nothing new");
    // PAIR OFF: they do not strike
    set("pair", 0);
    int64_t t4 = (ad_now_tick() / (PPB / 4) + 4) * (PPB / 4);
    n0 = ad_strikes; peer_bytes(ROLE_DRUM, HAND_LEFT, 36, 1, &t4, &v1); run(0.4f);
    check(ad_strikes == n0, "PAIR OFF: the other hand's hits leave this box alone");
    set("pair", 1);
    // two DRUM boxes: each keeps its own drums
    set("role", ROLE_DRUM); run(0.3f);
    n0 = ad_strikes; t4 += PPB; peer_bytes(ROLE_DRUM, HAND_LEFT, 36, 1, &t4, &v1); run(0.4f);
    check(ad_strikes == n0, "two DRUM boxes: the other's hits do not sound here");
    sim_peer_power(peer_k, false); run(2.5f);
    check(!D.paired && D.hand == HAND_AUTO, "the other box gone: one box again");
}

// ---------------------------------------------------------------- every sound
static void sounds(void)
{
    section("every sound");
    quiet();
    int bad = 0;
    for (int i = 0; i < INST.npresets; i++) {
        front_midi(0xC0, (uint8_t)i, 0);
        run(0.3f);
        float t = sim_t;
        st_swing(sim_t + 0.05f, ST_LEFT, 700.0f);
        run(1.0f);
        float r = m_rms(t, sim_t), pk = m_peak(t, sim_t);
        bool ok = r > 0.002f && pk < 0.95f;
        note("%s: rms %.3f, peak %.2f%s", INST.presets[i].name, (double)r, (double)pk, ok ? "" : "  <-- off");
        bad += !ok;
    }
    check(bad == 0, "every sound, swung, plays at a sane level (%d not)", bad);
    front_midi(0xC0, 0, 0); run(0.5f);
}

void inst_tests(void)
{
    section("boot");
    SI.mic = st_mic; sim_tilt(0, 0); run(2.0f);
    check(m_bad() == 0 && D.role == ROLE_DRUM && D.hand == HAND_AUTO, "it starts as one box, a drummer");
    sim_shot("boot");
    strokes();
    loops();
    cut();
    roles();
    pair();
    sounds();
    if (test_engine) test_engine();
    if (test_scene) test_scene();
    (void)drain;
}
