// AIRDWELLER's screens on the host: every role, played; a pair; CUT; a loop recorded; the splash and the saver; the
// squeeze under the core's strip; what a frame costs. 0.2: NOTES (the bead follows the tilt, the name changes, a held
// slide), EASY (STEP's pill jumps with each swing, GROOVE's runs by itself), the drag (DRUM, PAINT, NOTES, and the other
// hand dragging this one's hit), SHAPE (the band pulled while the other hand hits). Shots go to out/airdweller/, clips
// to out/airdweller-*.gif.
#include "sim.h"
#include "inst_ad.h"
#include "link.h"
#include "oled.h"
#include "dsp.h"
#include "params.h"
#include <math.h>
#include <string.h>
#include <time.h>

#define STRIP_Y_CHECK 46                // the first row the core may cover

float st_mic(float t, void *u);
void  st_reset(void);
void  st_swing(float t, vec3_t dir, float peak);
void  st_hold(vec3_t dir, float dps);
void  st_pose(float pitch, float roll);
void  st_glide(float t, vec3_t dir, float dps, float dur);
void  st_strike(float t, vec3_t dir, float peak, float rise, float stop, float ret, float ret_s);
extern const vec3_t ST_FRONT, ST_BACK, ST_LEFT, ST_RIGHT, ST_TWIST;
bool  sim_menu_param(int p);
void  sim_menu_close(void);

static void run(float s) { int n = (int)(s * 100.0f + 0.5f); for (int i = 0; i < n; i++) sim_tick(); }
static int lit(int x0, int y0, int x1, int y1)
{
    int n = 0;
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) n += oled_get(x, y);
    return n;
}
static void role(int r) { param_set(param_by_tag("role"), (float)r); }

// the scene alone, as the core would call it (cover: the rows the core's strip takes)
static void draw_alone(int cover)
{
    static uint32_t f = 100000;
    view_t v = { .frame = ++f, .t = (float)f / 25.0f, .cover = cover, .out_level = 0.3f, .in_level = 0.0f };
    oled_clear();
    scene_draw(&v);
}
static double cost_us(int n)
{
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (int i = 0; i < n; i++) draw_alone(i % 7 == 0 ? 18 : 0);
    clock_gettime(CLOCK_MONOTONIC, &b);
    return ((double)(b.tv_sec - a.tv_sec) * 1e6 + (double)(b.tv_nsec - a.tv_nsec) * 1e-3) / n;
}

// a pretend second box: its link bytes as instrument.c's app_update() writes them
static int peer = -1;
static uint8_t pseq;
static struct { int64_t tick; uint8_t voice, vel; } psent[3];
static void peer_say(int prole, int phand, bool hit, int voice, int vel)
{
    if (hit) {
        grid_t g; eng_get_grid(&g);
        psent[0] = psent[1]; psent[1] = psent[2];
        psent[2].tick = eng_tick_at(&g, audio_frames()) + PPB / 8;   // a little ahead, as a loop's hits are sent
        psent[2].voice = (uint8_t)voice; psent[2].vel = (uint8_t)vel;
        pseq++;
    }
    uint8_t a[16] = { 4, (uint8_t)(prole | phand << 3), 0, 36, pseq };
    for (int k = 0; k < 3; k++) {
        uint16_t lo = (uint16_t)(psent[k].tick & 0xFFFF);
        a[5 + 3 * k] = (uint8_t)(lo & 0xFF); a[6 + 3 * k] = (uint8_t)(lo >> 8);
        a[7 + 3 * k] = (uint8_t)((psent[k].voice & 15) << 4 | (psent[k].vel >> 3));
    }
    sim_peer_use(peer); link_set_app(a, 16); link_poke(); sim_peer_use(-1);
}

// ---- 0.2's measures, on the frame as it is
static uint8_t frame_a[128 * 64];
static void grab(uint8_t *a) { for (int y = 0; y < 64; y++) for (int x = 0; x < 128; x++) a[y * 128 + x] = oled_get(x, y); }
static int changed_px(const uint8_t *a, int x0, int y0, int x1, int y1)
{
    int n = 0;
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) n += a[y * 128 + x] != oled_get(x, y);
    return n;
}
static void settle(int n) { for (int i = 0; i < n; i++) draw_alone(0); }   // the scene alone until its springs settle
// NOTES: the bead on the ladder: the longest run of rows with 4 or more of x 5..9 lit (the bead is 5 rows, a rung 1)
static int bead_y(void)
{
    int best = -1, bl = 0, n = 0;
    for (int y = 16; y <= 64; y++) {
        int c = 0;
        for (int x = 5; y < 64 && x <= 9; x++) c += oled_get(x, y);
        if (c >= 4) { n++; continue; }
        if (n > bl) { bl = n; best = y - 1 - (n - 1) / 2; }
        n = 0;
    }
    return bl >= 4 ? best : -1;
}
// EASY: the step's pill in the panel's strip (columns lit 7 rows or more of rows 27..34): its x
static int marker_x(void)
{
    int sx = 0, n = 0;
    for (int x = 84; x <= 127; x++) {
        int c = 0;
        for (int y = 27; y <= 34; y++) c += oled_get(x, y);
        if (c >= 7) { sx += x; n++; }
    }
    return n ? sx / n : -1;
}
// ... and whether it is filled (its middle column lit down its length) or hollow
static bool marker_filled(void)
{
    int x = marker_x(), c = 0;
    if (x < 0) return false;
    for (int y = 28; y <= 34; y++) c += oled_get(x, y);
    return c >= 6;
}
// columns of the scene (x 0..81) with anything lit in rows y0..y1
static int cols_lit(int y0, int y1)
{
    int n = 0;
    for (int x = 0; x <= 81; x++) { int c = 0; for (int y = y0; y <= y1 && !c; y++) c = oled_get(x, y); n += c; }
    return n;
}
// what the pretend box hears of this one (its app bytes: [4] the number of this box's last hit)
static const link_peer_t *me_seen(void)
{
    static link_peer_t pr[LINK_PEERS_MAX];
    sim_peer_use(peer);
    int n = link_peers(pr, LINK_PEERS_MAX);
    sim_peer_use(-1);
    for (int i = 0; i < n; i++) if (!strncmp(pr[i].name, "AIRDWELL", 8)) return &pr[i];
    return NULL;
}
// the pretend box as SHAPE, dragging this box's hit number `target`
static void peer_shape(uint8_t target, float amt, float hold, float bend)
{
    uint8_t a[16] = { 4, (uint8_t)(ROLE_SHAPE | HAND_LEFT << 3), 0, 36, pseq, target, (uint8_t)clampi((int)(amt * 255.0f), 0, 255),
                      (uint8_t)clampi((int)(hold * 255.0f), 0, 255), (uint8_t)(int8_t)clampi((int)lroundf(bend * 4.0f), -127, 127) };
    sim_peer_use(peer); link_set_app(a, 16); link_poke(); sim_peer_use(-1);
}
static void tag(const char *t, float v) { param_set(param_by_tag(t), v); }

// ---- 0.2: NOTES, EASY, the drag, SHAPE (the pretend box is there, LEFT)
static void scenes_02(void)
{
    float hints_was = pv[CP(CP_HINTS)];
    pv[CP(CP_HINTS)] = 0.0f; coach_pause();                                // the clips without the coach's tips over them
    INST.action(P_ERASE_ALL); run(1.0f);                                    // and without the loops recorded above
    // ---- NOTES: the tilt walks the bead up the ladder and the name follows; a flick lights its rung; a held slide
    role(ROLE_NOTES); tag("skey", 9); tag("sscl", 1); tag("noct", 2); tag("nspn", 8); tag("qant", 0);
    st_reset(); st_pose(-35.0f, 0.0f); run(0.6f);
    sim_shot("notes");
    check(lit(0, 16, 81, 63) > 120, "NOTES: the ladder and the name are drawn (%d px)", lit(0, 16, 81, 63));
    settle(12);
    int y_lo = bead_y(), note_lo = D.note;
    grab(frame_a);
    sim_gif_begin("notes");
    for (int i = 0; i <= 14; i++) {
        st_pose(-35.0f + 5.0f * (float)i, 0.0f); run(0.12f);
        if (i % 2 == 0) { st_swing(sim_t + 0.02f, ST_LEFT, 650.0f); run(0.1f); if (i == 6) sim_shot("notes_flick"); run(0.32f); }
    }
    settle(12);
    int y_hi = bead_y(), note_hi = D.note, name_px = changed_px(frame_a, 22, 16, 81, 45);
    check(y_lo > 0 && y_hi > 0 && y_hi < y_lo - 15, "NOTES: the bead climbs the ladder with the tilt (row %d tipped down 35, row %d up 35)", y_lo, y_hi);
    check(note_hi != note_lo && name_px > 40, "NOTES: the note's name changes with it (%d to %d, %d px of the name changed)", note_lo, note_hi, name_px);
    // held: a hit tipped up, then the front edge brought down slowly: it slides down the scale
    float t0 = sim_t + 0.05f;
    st_strike(t0, ST_LEFT, 650.0f, 0.07f, 0.015f, 0.0f, 0.0f);
    st_glide(t0 + 0.15f, ST_FRONT, 40.0f, 1.5f);
    int held_note0 = -1, held_on = 0, blob = 0, pill = 0, rails0 = lit(4, 17, 10, 62), rails = 0;
    for (int i = 0; i < 160; i++) {
        sim_tick();
        if (i == 40) held_note0 = D.note;
        if (i == 70) { sim_shot("notes_held"); held_on = D.note_on && D.hold > 0.3f; blob = lit(30, 17, 81, 48); pill = lit(30, 51, 76, 59); }
        if (i == 120) { sim_shot("notes_held_slid"); rails = lit(4, 17, 10, 62); }
    }
    run(1.0f);
    sim_gif_end();
    int blob_after = lit(30, 17, 81, 48);
    check(held_on && blob > 500 && pill > 30 && D.note != held_note0,
          "NOTES: a held note: its name in a filled blob (%d px), HELD under it (%d px); it slides with the tilt (%d to %d)", blob, pill, held_note0, D.note);
    check(rails > rails0 + 20 && blob_after < blob / 2, "NOTES: the rungs slid over fill in (%d px, %d before); once it ends the blob goes (%d px)", rails, rails0, blob_after);
    st_pose(10.0f, 0.0f); run(0.3f);
    st_swing(sim_t + 0.02f, ST_LEFT, 650.0f); run(0.05f);
    sim_menu_param(param_by_tag("nspn"));
    run(0.3f);
    sim_shot("notes_menu");
    draw_alone(18);
    check(lit(0, STRIP_Y_CHECK, 127, 63) == 0, "NOTES with the strip up: nothing in the bottom 18 rows");
    sim_menu_close();
    st_reset(); sim_tilt(0, 0); run(0.5f);

    // ---- EASY: STEP's pill jumps with each swing, a hollow box on the next; GROOVE's caret runs by itself
    role(ROLE_DRUM); tag("qant", 2); tag("easy", EASY_STEP); tag("patt", 0);
    run(0.5f);
    sim_gif_begin("easy");
    int mx[8], adv = 0, caret_step = 0;
    for (int i = 0; i < 8; i++) {
        st_swing(sim_t + 0.05f, i & 1 ? ST_FRONT : ST_LEFT, 650.0f);
        run(0.45f);
        mx[i] = marker_x();
        caret_step += lit(84, 26, 127, 26) > 0;
        if (i > 0 && mx[i] >= 0 && mx[i - 1] >= 0 && mx[i] != mx[i - 1]) adv++;
        if (i == 5) sim_shot("easy_step");
    }
    check(adv >= 6, "EASY STEP: the step's pill moves on with each swing (%d of 7: x %d %d %d %d %d %d %d %d)", adv, mx[0], mx[1], mx[2], mx[3], mx[4], mx[5], mx[6], mx[7]);
    sim_menu_param(param_by_tag("patt"));
    run(0.3f);
    sim_shot("easy_menu");
    sim_menu_close();
    draw_alone(18);
    check(lit(0, STRIP_Y_CHECK, 127, 63) == 0, "EASY with the strip up: nothing in the bottom 18 rows");
    tag("easy", EASY_GROOVE);
    run(1.0f);
    bool idle_filled = marker_filled();
    st_swing(sim_t + 0.05f, ST_LEFT, 650.0f);
    run(0.15f);
    int moves = 0, caret = 0, filled = 0, last = marker_x();
    for (int i = 0; i < 12; i++) {
        run(0.1f);
        int x = marker_x();
        moves += x >= 0 && x != last; last = x;
        caret += lit(84, 26, 127, 26) > 0;
        filled += marker_filled();
        if (i == 6) sim_shot("easy_groove");
    }
    float steps = 1.2f * D.bpm / 15.0f;                                    // the sixteenths in 1.2 s
    check(!idle_filled && moves >= (int)(0.6f * steps) && filled >= 10, "EASY GROOVE: one swing and the steps run by themselves (%d moves in 1.2 s at %.0f BPM), filled while it plays (%d of 12), hollow before", moves, (double)D.bpm, filled);
    run(3.0f);
    check(!marker_filled() && caret == 12 && caret_step == 0, "EASY GROOVE: still, it stops (hollow); its caret runs along the top (%d of 12), STEP has none (%d)", caret, caret_step);
    // NOTES with STEP: the strip over the ladder
    role(ROLE_NOTES); tag("easy", EASY_STEP); tag("patt", 2);
    st_reset(); st_pose(0.0f, 0.0f); run(0.5f);
    for (int i = 0; i < 6; i++) { st_swing(sim_t + 0.05f, ST_LEFT, 650.0f); run(0.4f); if (i == 3) sim_shot("easy_notes"); }
    run(0.5f);
    sim_gif_end();
    tag("easy", EASY_OFF); role(ROLE_DRUM); st_reset(); sim_tilt(0, 0); run(0.5f);

    // ---- the drag: a hit, then the hand pulled back slowly; DRUM, a twist; PAINT; then the other hand drags this one's hit
    sim_gif_begin("drag");
    t0 = sim_t + 0.05f;
    st_strike(t0, ST_LEFT, 650.0f, 0.07f, 0.015f, 0.0f, 0.0f);
    st_glide(t0 + 0.15f, ST_RIGHT, 60.0f, 1.3f);
    run(0.55f);
    int px_mid = lit(0, 16, 81, 45);
    float d_mid = D.drag;
    run(0.6f);
    sim_shot("drag_drum");
    int px_full = lit(0, 16, 81, 45);
    float d_full = D.drag;
    run(1.2f);
    check(d_full > d_mid + 0.3f && px_full > px_mid + 10, "the drag played: the strand out of the kick grows as it is pulled (drag %.2f: %d px, %.2f: %d px)", (double)d_mid, px_mid, (double)d_full, px_full);
    t0 = sim_t + 0.05f;
    st_strike(t0, ST_RIGHT, 650.0f, 0.07f, 0.015f, 0.0f, 0.0f);
    st_glide(t0 + 0.15f, ST_TWIST, 90.0f, 0.9f);
    run(0.8f);
    sim_shot("drag_drum_bent");
    run(1.2f);
    role(ROLE_PAINT); tag("matl", MAT_LATEX); run(0.5f);
    t0 = sim_t + 0.05f;
    st_strike(t0, ST_LEFT, 650.0f, 0.07f, 0.015f, 0.0f, 0.0f);
    st_glide(t0 + 0.15f, ST_BACK, 50.0f, 1.2f);
    run(1.0f);
    sim_shot("drag_paint");
    run(1.2f);
    // the other hand (SHAPE) takes this box's last hit and drags it (no loops: their hits would be the newest sent)
    role(ROLE_DRUM); tag("matl", MAT_METAL); st_reset(); sim_tilt(0, 0); run(0.3f);
    st_swing(sim_t + 0.05f, ST_FRONT, 650.0f);
    run(0.5f);
    const link_peer_t *me = me_seen();
    uint8_t mine = me ? me->app[4] : 0;
    int shaped = 0;
    for (int i = 0; i <= 25; i++) {
        peer_shape(mine, (float)i / 25.0f, 1.0f, 1.5f * (float)i / 25.0f);
        run(0.04f);
        shaped += D.peer_shapes;
        if (i == 22) sim_shot("drag_by_the_other");
    }
    run(0.4f);
    peer_shape(mine, 1.0f, 0.0f, 1.5f); run(0.8f);
    peer_say(ROLE_DRUM, HAND_LEFT, false, 0, 0); run(0.5f);
    sim_gif_end();
    check(shaped > 15, "the other hand dragging this box's hit shows (%d of 26 ticks)", shaped);

    // the strand against the pull itself, the scene alone: 0.2 then 0.8
    // (against the frame with nothing dragged: the pixels that changed)
    D.last_pad = PD_LEFT; D.dragging = 0; D.hold = 0.0f; D.drag = 0.0f; D.dbend = 0.0f; D.peer_shapes = 0; D.gest = 0; D.gyro = (vec3_t){ 0, 0, 0 };
    settle(25); grab(frame_a);
    D.dragging = 1; D.hold = 1.0f;
    D.drag = 0.2f; settle(25);
    int s1 = changed_px(frame_a, 0, 16, 81, 45);
    D.drag = 0.8f; settle(25);
    int s2 = changed_px(frame_a, 0, 16, 81, 45);
    sim_shot("drag_strand");
    check(s2 > s1 + 15, "the drag: the strand grows with D.drag (%d px changed at 0.2, %d at 0.8)", s1, s2);
    D.hold = 0.0f; settle(25);
    int s0 = changed_px(frame_a, 0, 16, 81, 45);
    check(s0 < 10, "let go: it springs back (%d px left changed)", s0);
    draw_alone(18);
    check(lit(0, STRIP_Y_CHECK, 127, 63) == 0, "the drag with the strip up: nothing in the bottom 18 rows");
    run(0.5f);

    // ---- SHAPE: the other hand hits, this one drags: the band
    role(ROLE_SHAPE); run(0.5f);
    sim_shot("shape");
    sim_gif_begin("shape");
    static const int hv[4] = { DV_KICK, DV_SNAP, DV_KICK, DV_WHIP };
    float pull_max = 0.0f; int px_max = 0;
    for (int i = 0; i < 4; i++) {
        peer_say(ROLE_DRUM, HAND_LEFT, true, hv[i], 112);
        run(0.3f);
        float t = sim_t;
        st_glide(t, i & 1 ? ST_LEFT : ST_RIGHT, 55.0f, 1.1f);
        if (i == 2) st_glide(t + 0.3f, ST_TWIST, 80.0f, 0.6f);
        for (int k = 0; k < 120; k++) {
            sim_tick();
            if (D.drag > pull_max) pull_max = D.drag;
            int n = lit(0, 16, 81, 45); if (n > px_max) px_max = n;
            if (i == 1 && k == 80) sim_shot("shape_pull");
            if (i == 2 && k == 80) sim_shot("shape_bent");
        }
    }
    sim_gif_end();
    check(pull_max > 0.6f && px_max > 80, "SHAPE: the band is drawn and pulled (drag up to %.2f, %d px)", (double)pull_max, px_max);
    D.paired = 0; D.drag = 0.0f; D.hold = 0.0f; D.dbend = 0.0f; settle(25);
    sim_shot("shape_alone");
    check(lit(0, 16, 81, 45) > 40, "SHAPE alone: the band hangs from a ghost (%d px)", lit(0, 16, 81, 45));
    D.paired = 1; D.hold = 1.0f; D.dbend = 0.0f;
    D.drag = 0.2f; settle(25);
    int c1 = cols_lit(22, 42);
    D.drag = 0.8f; settle(25);
    int c2 = cols_lit(22, 42);
    check(c2 > c1 + 15, "SHAPE: the grip goes further with D.drag (%d columns at 0.2, %d at 0.8)", c1, c2);
    sim_menu_param(param_by_tag("role"));
    peer_say(ROLE_DRUM, HAND_LEFT, true, DV_SNAP, 100); run(0.1f);
    st_glide(sim_t, ST_RIGHT, 60.0f, 0.8f); run(0.6f);
    sim_shot("shape_menu");
    draw_alone(18);
    check(lit(0, STRIP_Y_CHECK, 127, 63) == 0, "SHAPE with the strip up: nothing in the bottom 18 rows");
    sim_menu_close();
    run(0.5f);
    role(ROLE_DRUM); tag("skey", 0); st_reset(); sim_tilt(0, 0); run(0.5f);
    pv[CP(CP_HINTS)] = hints_was;
}

static void shake(float s)
{
    int n = (int)(s * 100.0f);
    for (int i = 0; i < n; i++) { SI.acc = (vec3_t){ (i & 1) ? 0.5f : -0.5f, 0.0f, (i & 2) ? 1.6f : 0.3f }; sim_tick(); }
    sim_tilt(0, 0);
}

void test_scene(void)
{
    section("screens");
    SI.mic = st_mic; st_reset(); sim_tilt(0, 0);
    role(ROLE_DRUM);
    run(0.5f);
    sim_shot("drum");
    check(lit(0, 16, 80, 45) > 120, "DRUM: the compass is drawn (%d px)", lit(0, 16, 80, 45));

    // ---- DRUM: swings, each pad
    sim_gif_begin("drum");
    const vec3_t *dirs[4] = { &ST_LEFT, &ST_RIGHT, &ST_FRONT, &ST_BACK };
    int popped = 0;
    for (int r = 0; r < 3; r++) for (int d = 0; d < 4; d++) {
        st_swing(sim_t + 0.05f, *dirs[d], 650.0f + 150.0f * (float)r);
        int before = lit(0, 16, 80, 63), most = before;
        bool shot = r != 1 || (d != 0 && d != 2);
        for (int i = 0; i < 30; i++) {
            sim_tick();
            int n = lit(0, 16, 80, 63);
            if (n > most) most = n;
            if (!shot && n > before + 40 && i % 4 == 3) { sim_tick(); sim_shot(d == 0 ? "drum_hit_left" : "drum_hit_front"); shot = true; }
        }
        if (most > before + 25) popped++;
        run(0.15f);
    }
    sim_gif_end();
    check(popped >= 9, "DRUM: a hit pops its pad (%d of 12 swings lit up the compass)", popped);

    // ---- DRUM: a loop of two bars, played while recording
    sim_gif_begin("loop");
    bool saw_wait = false, saw_rec = false;
    for (int i = 0; i < 300 && !(D.beat_in_bar == 2 && D.beat_phase > 0.3f); i++) sim_tick();   // REC down late in a bar: a count-in
    SI.rec = true;
    for (int i = 0; i < 520; i++) {
        if (i % 50 == 10) st_swing(sim_t + 0.02f, (i / 50) & 1 ? *dirs[1] : *dirs[0], 700.0f);
        sim_tick();
        if (D.rec == 1 && !saw_wait) { saw_wait = true; run(0.1f); sim_shot("drum_count_in"); }
        if (D.rec == 2 && !saw_rec && D.beat_in_bar == 2) { saw_rec = true; sim_shot("drum_recording"); }
    }
    SI.rec = false;
    for (int i = 0; i < 400; i++) { if (i % 50 == 10) st_swing(sim_t + 0.02f, ST_FRONT, 600.0f); sim_tick(); }
    sim_shot("drum_loop_on");
    sim_gif_end();
    check(saw_rec && (D.loop_bars[0] > 0 || D.loop_bars[1] > 0), "a loop recorded: REC on the header, an arc, then an orbit (%d bars)", D.loop_bars[0]);
    if (!saw_wait) note("no count-in seen (REC went down inside the bar's first beat)");

    // ---- CUT
    sim_gif_begin("cut");
    run(0.5f);
    sim_rec(0.1f);
    run(0.1f);
    sim_shot("cut");
    draw_alone(0);                                                     // the scene by itself: no hint or toast strip
    int cut_px = lit(0, 16, 127, 63);
    run(1.0f);
    check(D.cut && cut_px < 260, "CUT: the scene goes dark (%d px lit)", cut_px);
    sim_rec(0.1f);
    run(1.0f);
    sim_gif_end();
    check(!D.cut, "CUT again: back in");

    // ---- covered: the scene squeezes above the core's strip
    sim_menu_param(param_by_tag("role"));
    run(0.4f);
    sim_shot("drum_menu");
    draw_alone(18);
    check(lit(0, STRIP_Y_CHECK, 127, 63) == 0, "DRUM with the strip up: nothing of the scene in the bottom 18 rows");
    sim_menu_close();

    // ---- PAINT: struck by the other hand's kick, by this hand's swings; tilt morphs it, twist leans it
    role(ROLE_PAINT);
    run(0.3f);
    sim_gif_begin("paint");
    for (int m = 0; m < NMAT; m++) {
        param_set(param_by_tag("matl"), (float)m);
        for (int k = 0; k < 3; k++) {
            eng_strike(0, 110, DV_KICK); scene_hit(-1, 110, SRC_REMOTE);   // as peer_tick() does with a hit from the other box
            run(0.12f);
            if (k == 0) { char b[24]; snprintf(b, sizeof b, "paint_%s", mat_name[m]); sim_shot(b); }
            run(0.3f);
        }
        st_swing(sim_t + 0.05f, m & 1 ? ST_RIGHT : ST_LEFT, 750.0f);
        run(0.5f);
    }
    check(lit(0, 16, 80, 63) > 80, "PAINT: the material is drawn");
    param_set(param_by_tag("matl"), MAT_METAL);
    for (int i = 0; i <= 20; i++) { sim_tilt(0.0f, -4.5f * (float)i); if (i % 5 == 0) { eng_strike(0, 100, DV_SNAP); scene_hit(-1, 100, SRC_REMOTE); } run(0.1f); }
    sim_shot("paint_tilted");
    float morph_hi = D.morph;
    st_hold(ST_TWIST, 280.0f);
    eng_strike(0, 120, DV_KICK); scene_hit(-1, 120, SRC_REMOTE);
    run(0.6f);
    sim_shot("paint_twist");
    float bent = D.bend;
    st_hold(ST_TWIST, 0.0f);
    sim_tilt(0, 0);
    run(1.0f);
    sim_gif_end();
    check(morph_hi > 0.6f, "tilting moves through the materials (morph %.2f from METAL)", (double)morph_hi);
    check(fabsf(bent) > 1.0f, "twisting bends it (%.1f st)", (double)bent);
    draw_alone(18);
    check(lit(0, STRIP_Y_CHECK, 127, 63) == 0, "PAINT with the strip up: nothing in the bottom 18 rows");

    // ---- PUMP: moving fills it, stillness lets it fall, a shake rattles it
    role(ROLE_PUMP);
    param_set(param_by_tag("matl"), MAT_RUBBER);
    run(0.3f);
    sim_shot("pump_still");
    sim_gif_begin("pump");
    st_hold(ST_TWIST, 300.0f);
    run(3.0f);
    sim_shot("pump_half");
    run(3.0f);
    shake(0.6f);
    sim_shot("pump_shake");
    float e_full = D.energy;
    st_hold(ST_TWIST, 0.0f);
    run(3.0f);
    sim_shot("pump_falling");
    sim_gif_end();
    check(e_full > 0.5f, "PUMP: moving fills it (energy %.2f)", (double)e_full);

    // ---- a pair: another AIRDWELLER, in DRUM, on the left
    peer = sim_peer_add("AIRDWELL", 1, 5.0, 0.0);
    peer_say(ROLE_DRUM, HAND_LEFT, false, 0, 0);
    run(1.5f);
    check(D.paired, "the other box is heard (paired, this one is %s)", D.hand == HAND_LEFT ? "LEFT" : D.hand == HAND_RIGHT ? "RIGHT" : "ONE");
    role(ROLE_PAINT);
    param_set(param_by_tag("matl"), MAT_GLASS);
    run(0.3f);
    sim_gif_begin("pair");
    static const int pv_[8] = { DV_KICK, DV_HAT, DV_SNAP, DV_HAT, DV_KICK, DV_KICK, DV_SNAP, DV_WHIP };
    float flash_max = 0.0f;
    for (int i = 0; i < 16; i++) {
        peer_say(ROLE_DRUM, HAND_LEFT, true, pv_[i & 7], 90 + (i & 3) * 10);
        for (int k = 0; k < 25; k++) { sim_tick(); if (D.peer_flash > flash_max) flash_max = D.peer_flash; if (i == 4 && k == 3) sim_shot("pair_paint_struck"); }
        if (i == 7) { param_set(param_by_tag("matl"), MAT_LATEX); }
    }
    role(ROLE_DRUM);
    for (int i = 0; i < 12; i++) {
        peer_say(ROLE_PAINT, HAND_LEFT, true, pv_[i & 7], 100);
        if (i % 3 == 1) st_swing(sim_t + 0.1f, ST_RIGHT, 700.0f);
        for (int k = 0; k < 25; k++) { sim_tick(); if (i == 5 && k == 3) sim_shot("pair_drum"); }
    }
    sim_gif_end();
    check(flash_max > 0.5f, "the other box's hits reach the screen (peer flash %.2f)", (double)flash_max);
    scenes_02();
    sim_peer_power(peer, false);

    // ---- what a frame costs (host): 400 frames of each role
    double us[NROLE];
    for (int r = 0; r < NROLE; r++) {
        role(r);
        eng_strike(0, 120, DV_KICK); scene_hit(-1, 120, SRC_REMOTE); scene_hit(PD_LEFT, 120, SRC_LIVE);
        run(0.2f);
        us[r] = cost_us(400);
    }
    metric("scene_us_drum", us[0]); metric("scene_us_paint", us[1]); metric("scene_us_pump", us[2]);
    check(us[0] < 1000.0 && us[1] < 1000.0 && us[2] < 1000.0, "a frame costs %.0f / %.0f / %.0f us (DRUM / PAINT / PUMP) on the host", us[0], us[1], us[2]);
    // 0.2's at their busiest: a held note sliding, dragged and bent; the band pulled and humming; a drag with EASY's strip
    role(ROLE_NOTES); run(0.2f);
    D.note_on = 1; D.hold = 1.0f; D.drag = 0.6f; D.dragging = 1; D.dbend = 2.0f; scene_hit(0, 100, SRC_LIVE);
    double us_notes = cost_us(400);
    role(ROLE_SHAPE); run(0.2f);
    D.paired = 1; D.drag = 0.7f; D.hold = 1.0f; D.dbend = 3.0f; scene_hit(-1, 100, SRC_REMOTE);
    double us_shape = cost_us(400);
    role(ROLE_DRUM); tag("easy", EASY_STEP); run(0.2f);
    D.last_pad = PD_FRONT; D.dragging = 1; D.hold = 1.0f; D.drag = 0.7f; D.dbend = -3.0f;
    double us_drag = cost_us(400);
    tag("easy", EASY_OFF);
    metric("scene_us_notes", us[3]); metric("scene_us_shape", us[4]);
    metric("scene_us_notes_held", us_notes); metric("scene_us_shape_pulled", us_shape); metric("scene_us_drum_drag_easy", us_drag);
    check(us[3] < 1000.0 && us[4] < 1000.0 && us_notes < 1000.0 && us_shape < 1000.0 && us_drag < 1000.0,
          "0.2's frames cost %.0f / %.0f us (NOTES / SHAPE), busy %.0f / %.0f / %.0f us (a held slide / the band pulled / a drag with EASY)", us[3], us[4], us_notes, us_shape, us_drag);
    role(ROLE_DRUM);

    // ---- the splash and the saver, drawn alone
    static const int sf[] = { 6, 13, 30, 62, 90 };
    for (int i = 0; i < 5; i++) {
        for (int f = 0; f <= sf[i]; f++) { oled_clear(); scene_splash(f); }
        char b[24]; snprintf(b, sizeof b, "splash_%02d", sf[i]); sim_shot(b);
    }
    bool more = true; int last = 0;
    for (int f = 0; f < 100 && more; f++) { oled_clear(); more = scene_splash(f); last = f; }
    check(!more && last < 100, "the splash ends by itself (frame %d)", last);
    oled_clear(); scene_saver(500); int s0 = lit(0, 0, 127, 63); sim_shot("saver");
    uint8_t a[128 * 64]; for (int y = 0; y < 64; y++) for (int x = 0; x < 128; x++) a[y * 128 + x] = oled_get(x, y);
    oled_clear(); scene_saver(1500); int moved = 0;
    for (int y = 0; y < 64; y++) for (int x = 0; x < 128; x++) moved += a[y * 128 + x] != oled_get(x, y);
    check(s0 < 400 && moved > 40, "the saver: little lit (%d px), and it moves (%d px changed in 40 s)", s0, moved);
    param_set(CP(CP_SAVER), 1);
    SI.mic = NULL;
    run(61.0f);
    sim_gif_begin("saver");
    run(6.0f);
    sim_gif_end();
    sim_turn(1); run(0.2f);
    param_set(CP(CP_SAVER), param(CP(CP_SAVER))->def);
    SI.mic = st_mic;
}
