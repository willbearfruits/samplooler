// AIRDWELLER on the host: the host's own scenarios. The strokes (a hit is a stroke that stops: four ways against
// gravity however the box is held, the hand coming back, a sweep, a twist, no jolt, fast series, a hat and a snare
// together, back and forth, a shake's roll, the latency, the grid), the drag (pulled back slowly, a quick return, a
// twist), the loops, CUT, the roles (PAINT, PUMP, the hand on the material), NOTES (the tilt is the note, a flick, a held
// slide, a shake, MIDI), EASY (STEP in order whatever the way, a shake on free lines, GROOVE, NOTES), two boxes (who is
// LEFT, hits sent with their ticks, struck on the same tick, two DRUMs, SHAPE both ways), the loops' notes, every
// sound. Then the engine's checks (test_engine.c) and the screens (test_scene.c).
#include "sim.h"
#include "inst_ad.h"
#include "link.h"
#include "dsp.h"
#include "take.h"
#include <dirent.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

float st_mic(float t, void *u);
void  st_reset(void);
void  st_swing(float t, vec3_t dir, float peak);
void  st_stroke(float t, vec3_t dir, float peak, float dur, float ret);
void  st_hold(vec3_t dir, float dps);
void  st_flat_acc(int on);
void  st_strike(float t, vec3_t dir, float peak, float rise, float stop, float ret, float ret_s);
void  st_glide(float t, vec3_t dir, float dps, float dur);
void  st_shake(float t, vec3_t dir, float hz, float dps, float dur);
void  st_pose(float pitch, float roll);
void  st_motion(float t);
extern const vec3_t ST_FRONT, ST_BACK, ST_LEFT, ST_RIGHT, ST_TWIST;
extern int ad_pad_hits[NPAD], ad_drags;
extern int ad_strokes; extern float ad_st_peak, ad_st_rise, ad_st_fall; extern int ad_st_why;
extern uint32_t ad_hit_frame; extern int ad_hit_pad; extern float ad_hit_peak, ad_hit_rise, ad_hit_fall;
extern uint32_t ad_strike_at; extern int64_t ad_strike_tick; extern int ad_strikes; extern uint32_t ad_played_frame;
int64_t ad_now_tick(void);
void test_engine(void) __attribute__((weak));
void test_scene(void) __attribute__((weak));

extern nev_t ad_nev[64]; extern int ad_nevs;

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
// the drum notes since `from`, in order (up to max); and the shortest time between two of them
static int drum_seq(int from, int *out, int max, float *min_gap)
{
    int n = 0; float last = -1.0f; *min_gap = 1e9f;
    for (int i = from; i < midi_n; i++) if (midi_log[i].st == 0x99 && midi_log[i].d2 > 0) {
        if (n < max) out[n] = midi_log[i].d1;
        n++;
        if (last >= 0.0f && midi_log[i].t - last < *min_gap) *min_gap = midi_log[i].t - last;
        last = midi_log[i].t;
    }
    return n;
}
// the note events this box sent the engine since `from` (ad_nevs then), the newest 64 at most
static const nev_t *nev_at(int k) { return &ad_nev[k & 63]; }
static int nev_from(int from) { return ad_nevs - from > 64 ? ad_nevs - 64 : from; }
static int first_on(int from) { for (int k = nev_from(from); k < ad_nevs; k++) if (nev_at(k)->on && !nev_at(k)->legato) return k; return -1; }
static uint32_t frame_at(float t) { return (uint32_t)(t * (float)AUDIO_SR + 0.5f); }

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
    section("strokes: a hit is a stroke that stops");
    quiet();
    set("qant", 0);
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
    // the same four however the box is held: the directions are the room's
    static const struct { float p, r; } poses[] = { { 40, 0 }, { -35, 0 }, { 0, 30 }, { 75, 0 } };
    int okp = 0, tot = 0;
    for (int k = 0; k < 4; k++) {
        st_reset(); st_pose(poses[k].p, poses[k].r); run(0.5f);
        for (int p = 0; p < NPAD; p++) {
            int m0 = midi_n;
            st_swing(sim_t + 0.05f, dir[p], 650.0f);
            run(0.7f);
            bool good = D.last_pad == p && notes_since(m0, -1) == 1;
            if (!good) note("held at %.0f / %.0f, %s: pad %d, %d hits", (double)poses[k].p, (double)poses[k].r, way[p], D.last_pad, notes_since(m0, -1));
            okp += good; tot++;
        }
    }
    check(okp == tot, "tipped up 40, down 35, rolled 30 or nearly upright: each way still its own pad (%d of %d)", okp, tot);
    st_reset(); sim_tilt(0, 0); run(0.5f);
    // what is not a hit
    int m0 = midi_n;
    st_strike(sim_t + 0.05f, ST_LEFT, 650.0f, 0.07f, 0.015f, 0.8f, 0.0f);
    run(0.8f);
    check(notes_since(m0, -1) == 1, "a hit and the hand coming back at 0.8 of its speed: one hit (%d)", notes_since(m0, -1));
    m0 = midi_n;
    st_stroke(sim_t + 0.05f, ST_LEFT, 650.0f, 0.25f, 0.0f);
    run(0.8f);
    check(notes_since(m0, -1) == 0, "a fast sweep that slows down by itself: no hit (%d)", notes_since(m0, -1));
    m0 = midi_n;
    st_stroke(sim_t + 0.05f, ST_RIGHT, 250.0f, 0.5f, 0.0f);
    run(1.0f);
    check(notes_since(m0, -1) == 0, "a slow turn: no hit");
    m0 = midi_n;
    st_strike(sim_t + 0.05f, ST_TWIST, 700.0f, 0.07f, 0.015f, 0.3f, 0.0f);
    run(0.8f);
    check(notes_since(m0, -1) == 0, "a sharp twist of the wrist: no hit (%d)", notes_since(m0, -1));
    m0 = midi_n;
    st_flat_acc(1);
    st_swing(sim_t + 0.05f, ST_RIGHT, 700.0f);
    run(0.8f);
    st_flat_acc(0);
    check(notes_since(m0, -1) == 0, "CONFIRM: a fast turn with no jolt is no hit");
    // fast: eight kicks, each 50 ms up and stopped in 12 ms, the hand back at 0.7 of the speed in between
    int seq[16]; float gap;
    m0 = midi_n;
    float t0 = sim_t + 0.05f;
    for (int i = 0; i < 8; i++) st_strike(t0 + 0.16f * (float)i, ST_LEFT, 600.0f, 0.05f, 0.012f, 0.7f, 0.0f);
    run(1.6f);
    int nk = notes_since(m0, 36);
    check(nk == 8 && notes_since(m0, -1) == 8, "eight kicks in 1.2 s: eight hits, all kicks (%d of %d)", nk, notes_since(m0, -1));
    // a hat and a snare right after each other
    m0 = midi_n; t0 = sim_t + 0.05f;
    st_strike(t0, ST_FRONT, 600.0f, 0.05f, 0.012f, 0.0f, 0.0f);
    st_strike(t0 + 0.08f, ST_RIGHT, 600.0f, 0.05f, 0.012f, 0.0f, 0.0f);
    run(0.6f);
    int n = drum_seq(m0, seq, 16, &gap);
    check(n == 2 && seq[0] == 42 && seq[1] == 38, "a hat, then a snare 80 ms later: both (%d hits)", n);
    // back and forth, each stroke stopped hard: every one a hit, in turn
    m0 = midi_n; t0 = sim_t + 0.05f;
    for (int i = 0; i < 6; i++) st_strike(t0 + 0.1f * (float)i, (i & 1) ? ST_RIGHT : ST_LEFT, 600.0f, 0.05f, 0.012f, 0.0f, 0.0f);
    run(1.0f);
    n = drum_seq(m0, seq, 16, &gap);
    int alt = n == 6;
    for (int i = 0; alt && i < 6; i++) alt = seq[i] == ((i & 1) ? 38 : 36);
    check(alt, "left and right in turn, each stopped hard, 100 ms apart: six hits, kick and snare in turn (%d)", n);
    // a shake (a sine back and forth): the last pad rolls on the sixteenths
    st_swing(sim_t + 0.05f, ST_LEFT, 650.0f); run(0.6f);
    m0 = midi_n;
    st_shake(sim_t + 0.05f, ST_LEFT, 7.0f, 500.0f, 1.5f);
    run(1.8f);
    n = drum_seq(m0, seq, 16, &gap);
    int same = n > 0;
    for (int i = 0; same && i < n && i < 16; i++) same = seq[i] == 36;
    check(n >= 8 && same && gap > 0.11f, "a shake: the kick rolls, one to a sixteenth (%d hits in 1.5 s, at least %.0f ms apart)", n, (double)(gap * 1000.0f));
    run(0.5f);
    // the latency: the sound 12 ms after the stop (QUANTIZE OFF): the stop's half speed is 77.5 ms into this one
    t0 = sim_t + 0.05f;
    st_strike(t0, ST_LEFT, 650.0f, 0.07f, 0.015f, 0.0f, 0.0f);
    run(0.5f);
    uint32_t want = frame_at(t0 + 0.0775f + 0.012f);
    int32_t err = (int32_t)(ad_played_frame - want);
    check(abs(err) <= 48, "QUANTIZE OFF: it sounds 12 ms after the stop (%.1f ms off)", (double)err * 1000.0 / AUDIO_SR);
    // on the grid: stopped 15 ms before a sixteenth line, it waits for the line and sounds on it
    set("qant", 2);
    grid_t g; eng_get_grid(&g);
    uint32_t nowf = audio_frames();
    int64_t line = (eng_tick_at(&g, nowf) / (PPB / 4) + 3) * (PPB / 4);
    float line_t = sim_t + (float)(int32_t)(eng_frame_of(&g, line) - nowf) / (float)AUDIO_SR;
    m0 = midi_n;
    st_strike(line_t - 0.015f - 0.0775f, ST_LEFT, 650.0f, 0.07f, 0.015f, 0.0f, 0.0f);
    run(0.6f);
    eng_get_grid(&g);
    want = eng_frame_of(&g, line);
    check(notes_since(m0, -1) == 1 && ad_played_frame == want, "QUANTIZE 1/16: stopped just before a line, it sounds on it (frame %u, the line's %u)", (unsigned)ad_played_frame, (unsigned)want);
}

// ---------------------------------------------------------------- the drag
static void drag(void)
{
    section("drag: after a hit, the hand pulls the sound");
    quiet(); set("qant", 0); set("rech", 60); set("drag", 70); set("dbnd", 5);
    // a kick, then pulled back slowly at 60 degrees a second for 1.2 s (about 69 degrees)
    float t0 = sim_t + 0.05f;
    st_strike(t0, ST_LEFT, 650.0f, 0.07f, 0.015f, 0.0f, 0.0f);
    st_glide(t0 + 0.15f, ST_RIGHT, 60.0f, 1.2f);
    run(0.14f);
    int gid = eng_drag_gid;
    float a0 = eng_drag_amt;
    run(0.55f);
    float hold_mid = eng_drag_hold, amt_mid = eng_drag_amt;
    run(0.6f);
    float amt_end = eng_drag_amt;
    run(0.6f);
    check(gid != 0 && a0 < 0.05f, "a hit starts a gesture for the drag (%d), at nothing (%.2f)", gid, (double)a0);
    check(hold_mid > 0.9f && amt_mid > 0.3f && amt_mid < 0.8f, "pulled back slowly: held (%.2f), part way (%.2f)", (double)hold_mid, (double)amt_mid);
    run(0.2f); amt_end = eng_drag_amt;
    check(amt_end > 0.9f, "past REACH (60 degrees): a full drag (%.2f)", (double)amt_end);
    check(eng_drag_hold < 0.05f && eng_drag_amt > 0.9f && eng_drag_gid == gid, "still: it lets go (%.2f), the amount stays with that hit (%.2f)", (double)eng_drag_hold, (double)eng_drag_amt);
    // the hand coming back by itself, quickly: a new gesture, not dragged
    st_swing(sim_t + 0.05f, ST_LEFT, 650.0f);
    run(0.8f);
    check(eng_drag_gid != gid && eng_drag_amt < 0.1f && eng_drag_hold < 0.05f, "a hit and a quick return: a new gesture, no drag (%.2f)", (double)eng_drag_amt);
    // a twist while dragging bends it
    t0 = sim_t + 0.05f;
    st_strike(t0, ST_LEFT, 650.0f, 0.07f, 0.015f, 0.0f, 0.0f);
    st_glide(t0 + 0.15f, ST_TWIST, 90.0f, 0.6f);
    run(0.8f);
    check(eng_drag_bend > 4.0f, "twisted about 50 degrees while dragging: bent up to DRAG BEND (%.1f of 5 st)", (double)eng_drag_bend);
    set("drag", 0); run(0.05f);
    check(eng_drag_depth == 0.0f, "DRAG 0: the drag does nothing");
    set("drag", 70); run(1.0f);
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
    a[0] = 4; a[1] = (uint8_t)(role | hand << 3); a[2] = 0; a[3] = (uint8_t)kick; a[4] = peer_seq;
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

// ---------------------------------------------------------------- NOTES
// A minor from A2: the notes over SPAN 8 are A2 B2 C3 D3 E3 F3 G3 A3 (45 47 48 50 52 53 55 57)
static void notes_role(void)
{
    section("NOTES: the tilt is the note");
    quiet();
    set("role", ROLE_NOTES); set("skey", 9); set("sscl", 1); set("noct", 2); set("nspn", 8); set("nlen", 0.15f); set("qant", 0); set("easy", 0);
    run(0.3f);
    // the tilt when the stroke begins picks it (a sweep round does not tip the box)
    static const struct { float p, r; int want; const char *what; } T[] = {
        { -40, 0, 45, "tipped down 40: A2" }, { 0, 0, 52, "level: E3" }, { 33, 0, 57, "tipped up 33: A3" }, { 0, 45, 64, "level, rolled right: E4" },
    };
    int ok = 0;
    for (int k = 0; k < 4; k++) {
        st_reset(); st_pose(T[k].p, T[k].r); run(0.5f);
        int n0 = ad_nevs;
        st_swing(sim_t + 0.05f, ST_LEFT, 650.0f);
        run(0.5f);
        int f = first_on(n0);
        int got = f >= 0 ? nev_at(f)->note : -1;
        if (got != T[k].want) note("%s: got %d", T[k].what, got);
        ok += got == T[k].want;
    }
    check(ok == 4, "the tilt picks the note: down 40, level, up 33, rolled 45: A2 E3 A3 E4 (%d of 4)", ok);
    // a flick: NOTE LENGTH
    st_reset(); st_pose(0, 0); run(0.5f);
    int n0 = ad_nevs, m0 = midi_n;
    st_swing(sim_t + 0.05f, ST_LEFT, 650.0f);
    run(0.6f);
    int f = first_on(n0);
    int32_t len = -1;
    if (f >= 0) for (int k = f + 1; k < ad_nevs; k++) if (!nev_at(k)->on && nev_at(k)->gid == nev_at(f)->gid) { len = (int32_t)(nev_at(k)->at - nev_at(f)->at); break; }
    check(f >= 0 && abs(len - (int32_t)(0.15f * AUDIO_SR)) <= 2, "a flick: one note, NOTE LENGTH long (%.1f ms)", (double)len * 1000.0 / AUDIO_SR);
    int mn = 0;
    for (int i = m0; i < midi_n; i++) if (midi_log[i].st == 0x90 && midi_log[i].d2 > 0 && midi_log[i].d1 == 52) mn++;
    check(mn == 1, "and the note goes out on MIDI (channel 1, note 52: %d)", mn);
    // held: a hit, then the front edge brought up slowly: it holds and slides up the scale, and ends when still
    n0 = ad_nevs;
    float t0 = sim_t + 0.05f;
    st_strike(t0, ST_LEFT, 650.0f, 0.07f, 0.015f, 0.0f, 0.0f);
    st_glide(t0 + 0.15f, ST_BACK, 40.0f, 1.0f);
    run(1.3f);
    int slides = 0, last = -1, rising = 1;
    int32_t off_at = -1;
    f = first_on(n0);
    for (int k = nev_from(n0); f >= 0 && k < ad_nevs; k++) {
        const nev_t *e = nev_at(k);
        if (e->gid != nev_at(f)->gid) continue;
        if (e->on && e->legato) { slides++; if (last >= 0 && e->note <= last) rising = 0; last = e->note; }
        if (!e->on) off_at = (int32_t)(e->at - nev_at(f)->at);
    }
    run(0.6f);
    for (int k = nev_from(n0); f >= 0 && k < ad_nevs; k++) if (!nev_at(k)->on && nev_at(k)->gid == nev_at(f)->gid) off_at = (int32_t)(nev_at(k)->at - nev_at(f)->at);
    check(slides >= 3 && rising && last == 57, "held and tipped up: it slides up the scale (%d steps, up to %d)", slides, last);
    check(off_at > (int32_t)(1.1f * AUDIO_SR), "it ends only once the hand is still (after %.2f s)", (double)off_at / AUDIO_SR);
    // a shake, held level again: the note (E3) repeats on the sixteenths
    st_reset(); st_pose(0, 0); run(0.5f);
    st_swing(sim_t + 0.05f, ST_LEFT, 650.0f); run(0.6f);
    n0 = ad_nevs;
    st_shake(sim_t + 0.05f, ST_LEFT, 7.0f, 500.0f, 1.2f);
    run(1.5f);
    int reps = 0, same = 1;
    for (int k = nev_from(n0); k < ad_nevs; k++) if (nev_at(k)->on && !nev_at(k)->legato) { reps++; if (nev_at(k)->note != 52) same = 0; }
    check(reps >= 6 && same, "a shake: the note repeats (%d times)", reps);
    run(0.5f);
    set("role", ROLE_DRUM); set("qant", 2); run(0.3f);
}

// ---------------------------------------------------------------- EASY
static void easy(void)
{
    section("EASY: any swing, the next step");
    quiet();
    set("easy", EASY_STEP); set("patt", 0); set("qant", 2); run(0.3f);
    // eight swings any way at all: FOUR's steps in order (one box: KICK HAT SNAP HAT ...)
    const vec3_t dirs[8] = { ST_BACK, ST_TWIST, ST_LEFT, ST_FRONT, ST_RIGHT, ST_TWIST, ST_BACK, ST_LEFT };
    int seq[32]; float gap;
    int m0 = midi_n;
    for (int i = 0; i < 8; i++) { st_swing(sim_t + 0.05f, dirs[i], 650.0f); run(0.35f); }
    run(0.3f);
    int n = drum_seq(m0, seq, 32, &gap);
    static const int four[8] = { 36, 42, 38, 42, 36, 42, 38, 42 };
    int inorder = n == 8;
    for (int i = 0; inorder && i < 8; i++) inorder = seq[i] == four[i];
    check(inorder, "STEP: eight swings, back, twisted, left, forward...: FOUR's steps in order (%d)", n);
    // a shake: one step to each free sixteenth
    m0 = midi_n;
    st_shake(sim_t + 0.05f, ST_LEFT, 7.0f, 500.0f, 1.0f);
    run(1.4f);
    n = drum_seq(m0, seq, 32, &gap);
    check(n >= 6 && gap > 0.11f, "STEP: a shake steps on, one to a sixteenth (%d steps, at least %.0f ms apart)", n, (double)(gap * 1000.0f));
    // GROOVE: a swing sets the pattern going for a bar; still, it stops
    set("easy", EASY_GROOVE); run(3.0f);
    m0 = midi_n; run(1.0f);
    int before = notes_since(m0, -1);
    m0 = midi_n;
    st_swing(sim_t + 0.05f, ST_LEFT, 650.0f);
    run(2.0f);
    int during = notes_since(m0, -1);
    run(1.0f);
    m0 = midi_n; run(2.0f);
    int after = notes_since(m0, -1);
    check(before == 0 && during >= 6 && after == 0, "GROOVE: quiet (%d); one swing, the pattern for a bar (%d); still, it stops (%d)", before, during, after);
    // NOTES with STEP: BREAK's notes (0 2 4 7 of the scale) from where the tilt points (level: E3)
    set("role", ROLE_NOTES); set("easy", EASY_STEP); set("patt", 2); set("skey", 9); set("sscl", 1); set("noct", 2); set("nspn", 8);
    st_reset(); st_pose(0, 0); run(0.5f);
    int n0 = ad_nevs;
    for (int i = 0; i < 4; i++) { st_swing(sim_t + 0.05f, ST_LEFT, 650.0f); run(0.4f); }
    static const int brk[4] = { 52, 55, 59, 64 };
    int k = 0, okn = 1;
    for (int j = nev_from(n0); j < ad_nevs; j++) if (nev_at(j)->on && !nev_at(j)->legato) { if (k < 4 && nev_at(j)->note != brk[k]) okn = 0; k++; }
    check(okn && k == 4, "NOTES and STEP: the pattern's notes from the tilt's (E3 G3 B3 E4: %d notes)", k);
    set("easy", EASY_OFF); set("role", ROLE_DRUM); run(1.0f);
}

// ---------------------------------------------------------------- SHAPE
static void shape(void)
{
    section("SHAPE: one hand hits, the other drags");
    quiet();
    INST.action(P_ERASE_ALL);
    peer_k = sim_peer_add("AIRDWELL", 1, 6.0, 0.0);
    peer_bytes(ROLE_DRUM, HAND_LEFT, 36, 0, NULL, NULL);
    run(3.0f);
    // this box SHAPE: the other box's hit, then this hand pulled slowly; the drag goes back with the hit's number
    set("role", ROLE_SHAPE); run(0.3f);
    int64_t tk = (ad_now_tick() / (PPB / 4) + 2) * (PPB / 4);
    int v = DV_KICK;
    peer_bytes(ROLE_DRUM, HAND_LEFT, 36, 1, &tk, &v);
    run(0.1f);
    st_glide(sim_t + 0.05f, ST_RIGHT, 60.0f, 1.3f);
    run(0.6f);
    const link_peer_t *me = me_seen_by_peer();
    int s5 = me ? me->app[5] : -1, a6 = me ? me->app[6] : -1, h7 = me ? me->app[7] : -1;
    check(me && s5 == peer_seq && a6 > 50 && h7 > 200, "SHAPE: the other hand's hit (%d), pulled here: amount %d, held %d", s5, a6, h7);
    run(1.0f);
    me = me_seen_by_peer();
    check(me && me->app[6] > 230 && eng_drag_gid == 0, "pulled on: a full drag (%d); this box plays nothing of its own", me ? me->app[6] : -1);
    // this box DRUM, the other SHAPE: its drag reaches this box's last hit
    set("role", ROLE_DRUM); run(0.5f);
    st_swing(sim_t + 0.05f, ST_LEFT, 650.0f); run(0.5f);
    me = me_seen_by_peer();
    uint8_t mine = me ? me->app[4] : 0;
    uint8_t a[16] = { 4, (uint8_t)(ROLE_SHAPE | HAND_RIGHT << 3), 0, 36, peer_seq, mine, 200, 255, (uint8_t)(int8_t)8 };
    sim_peer_use(peer_k); link_set_app(a, 16); link_poke(); sim_peer_use(-1);
    run(0.3f);
    check(D.peer_shapes && eng_drag_amt > 0.75f && eng_drag_hold > 0.9f && fabsf(eng_drag_bend - 2.0f) < 0.3f,
          "the other hand (SHAPE) drags this box's last hit: amount %.2f, held %.2f, bent %.1f st", (double)eng_drag_amt, (double)eng_drag_hold, (double)eng_drag_bend);
    st_swing(sim_t + 0.05f, ST_LEFT, 650.0f); run(0.5f);
    check(!D.peer_shapes && eng_drag_amt < 0.1f, "a new hit here: that drag was for the old one (%.2f)", (double)eng_drag_amt);
    sim_peer_power(peer_k, false); run(2.5f);
}

// ---------------------------------------------------------------- the loops' notes
static void note_loops(void)
{
    section("NOTES loops: the notes come back");
    quiet();
    INST.action(P_ERASE_ALL);
    set("role", ROLE_NOTES); set("qant", 2); set("skey", 9); set("sscl", 1); set("noct", 2); set("nspn", 8);
    st_reset(); st_pose(0, 0); run(0.5f);
    SI.rec = true;
    for (int b = 0; b < 5; b++) { st_swing(sim_t + 0.05f, ST_LEFT, 650.0f); run(0.5f); }
    SI.rec = false;
    run(2.5f);
    int notes = 0;
    for (int i = 0; i < eng_loops[0].n; i++) notes += eng_loops[0].hits[i].pad >= NOTE_HIT;
    check(eng_loops[0].bars >= 1 && notes >= 3, "a loop of %d bar(s) holding %d notes", eng_loops[0].bars, notes);
    int n0 = ad_nevs;
    run(2.0f);
    int back = 0, right = 1;
    for (int k = nev_from(n0); k < ad_nevs; k++) if (nev_at(k)->on && nev_at(k)->gid >= 200) { back++; if (nev_at(k)->note != 52) right = 0; }
    check(back >= 3 && right, "they come back by themselves, the same note (%d in 2 s)", back);
    INST.action(P_ERASE_ALL);
    set("role", ROLE_DRUM); run(1.0f);
}

// ---------------------------------------------------------------- recorded takes (tools/motion_rec.py)
static const char *const pad_name[NPAD] = { "FRONT", "LEFT", "RIGHT", "BACK" };
// one take through the detector and held to its label; false if it has none
static bool take_check(const char *path, const char *name)
{
    take_t *t = take_load(path);
    if (!t) return false;
    quiet(); set("qant", 0); set("easy", 0); SI.mic = NULL; run(0.5f);
    for (int p = 0; p < NPAD; p++) ad_pad_hits[p] = 0;
    ad_drags = 0;
    take_run(t);
    run(0.5f);
    int total = 0; for (int p = 0; p < NPAD; p++) total += ad_pad_hits[p];
    note("%s: %.1f s, %d gaps; FRONT %d LEFT %d RIGHT %d BACK %d, dragged %d", name, (double)t->secs, t->gaps,
         ad_pad_hits[PD_FRONT], ad_pad_hits[PD_LEFT], ad_pad_hits[PD_RIGHT], ad_pad_hits[PD_BACK], ad_drags);
    char keys[8][24], only[24];
    int nk = take_keys(t, "label.hits", keys, 8);
    bool has_hits = take_get(t, "label.hits", only, sizeof only), labelled = true;
    if (take_get(t, "label.only", only, sizeof only)) {
        int want = -1; for (int p = 0; p < NPAD; p++) if (!strcmp(only, pad_name[p])) want = p;
        check(want >= 0 && total > 0 && ad_pad_hits[want] == total, "%s: only %s (%d of %d)", name, only, want >= 0 ? ad_pad_hits[want] : 0, total);
    } else if (has_hits) {
        bool ok = true; int named = 0;
        for (int k = 0; k < nk; k++) {
            char pth[64]; snprintf(pth, sizeof pth, "label.hits.%s", keys[k]);
            int want = (int)take_num(t, pth, -1), got = -1;
            for (int p = 0; p < NPAD; p++) if (!strcmp(keys[k], pad_name[p])) got = ad_pad_hits[p];
            ok &= got == want; named += got > 0 ? got : 0;
        }
        ok &= total - named <= (int)take_num(t, "label.others", 0);
        ok &= ad_drags >= (int)take_num(t, "label.drags", 0);
        check(ok, "%s: the hits it was recorded for (%d in all, %d dragged)", name, total, ad_drags);
    } else if (take_num(t, "label.any", 0) > 0) check(total > 0, "%s: something plays (%d)", name, total);
    else labelled = false;
    take_free(t);
    SI.mic = st_mic;
    return labelled;
}
// the model's own strokes written as a take (the recorder's format), for the round trip
static void write_model_take(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "{\"instrument\": \"AIRDWELLER\", \"name\": \"model\", \"label\": {\"hits\": {\"LEFT\": 3}, \"drags\": 1, \"others\": 0}}\n");
    st_reset();
    st_swing(0.10f, ST_LEFT, 650.0f);
    st_strike(0.60f, ST_LEFT, 650.0f, 0.07f, 0.015f, 0.0f, 0.0f); st_glide(0.75f, ST_RIGHT, 60.0f, 1.0f);
    st_swing(2.10f, ST_LEFT, 650.0f);
    vec3_t acc0 = SI.acc;
    for (int k = 0; k < 280; k++) {
        SI.nburst = 0;
        st_motion((float)k * 0.01f);
        for (int i = 0; i < 10; i++) {
            vec3_t a = SI.nburst ? SI.burst[i] : acc0, g = SI.nburst ? SI.gburst[i] : (vec3_t){ 0, 0, 0 };
            fprintf(f, "%d %.5f %.5f %.5f %.3f %.3f %.3f %d\n", k * TICK_FRAMES + i * (AUDIO_SR / 1000), (double)a.x, (double)a.y, (double)a.z, (double)g.x, (double)g.y, (double)g.z, TAKE_GYRO);
        }
    }
    SI.nburst = 0;
    fclose(f);
    st_reset();
}
// Every take recorded from a board under lab/takes/airdweller (or $TAKES) is played through the detector (DRUM, one
// box, QUANTIZE off) and held to its label: {"hits": {"LEFT": 10}, "others": 0} (exactly those, nothing else beyond
// "others"), {"only": "LEFT"} (at least one, all LEFT), {"hits": {}} (none), {"any": true} (at least one), "drags": n
// (at least n dragged). First, the round trip: the model's own strokes written as a take and played back.
static void takes(void)
{
    section("recorded takes");
    write_model_take("out/model.take");
    int before = ad_strokes;
    check(take_check("out/model.take", "the model's own take") && ad_strokes > before, "a take written in the recorder's format plays back through the detector");
    const char *dir = getenv("TAKES") ? getenv("TAKES") : "../../../../../lab/takes/airdweller";
    DIR *d = opendir(dir);
    int ntakes = 0;
    struct dirent *e;
    while (d && (e = readdir(d))) {
        size_t L = strlen(e->d_name);
        if (L < 6 || strcmp(e->d_name + L - 5, ".take")) continue;
        char path[512]; snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        ntakes++;
        take_check(path, e->d_name);
    }
    if (d) closedir(d);
    if (!ntakes) note("no takes recorded yet in %s: record them with tools/motion_rec.py guided --instrument airdweller", dir);
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
    drag();
    loops();
    cut();
    roles();
    notes_role();
    easy();
    pair();
    shape();
    note_loops();
    takes();
    sounds();
    if (test_engine) test_engine();
    if (test_scene) test_scene();
    (void)drain;
}
