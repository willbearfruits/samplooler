// WRECK on a rev E (the Vessel): no stick; the left knob is the crush and the drive, a hand over the sensor the roll
// (or what WRECK > HAND says). No rev E has been built: this is how it is checked. Each thing the two parts do is
// measured against the stick doing it on a rev C, on the same two bars of the same break with BEAT's edits off (so
// the bars repeat): the level, how bright it is (the first difference's rms over the signal's), the crest; the rolls
// from BEAT's own log of the hits it started; the dive from the pitch of a break that is one tone. Then: nothing
// clicks or sticks when the hand comes down fast and leaves, the beat and the link are where they were, and on the
// board with the stick the new setting does nothing.
#include "sim.h"
#include "wreck.h"
#include "link.h"
#include <math.h>
#include <string.h>

// from inst_beat.c (tools/sim only)
extern const brk_t *beat_test_break;
typedef struct { uint32_t frame; int16_t slice; int8_t lane, rev; uint8_t n; float rate, lvl; } beat_trig_t;
extern beat_trig_t beat_tlog[16384];
extern int beat_tlog_n;
void front_midi(uint8_t st, uint8_t d1, uint8_t d2);
int  wr_zone(void);

static int tag(const char *t) { int p = param_by_tag(t); if (p < 0) note("no param '%s'", t); return p; }
static void set(const char *t, float v) { int p = tag(t); if (p >= 0) param_set(p, v); }
static float get(const char *t) { int p = tag(t); return p >= 0 ? pv[p] : 0.0f; }

static sim_in_t board_c;                                           // the board the sim boots as: a rev C with its stick
static void rev_c(void) { SI = board_c; SI.jx = SI.jy = 0; sim_run(0.3f); }
static void rev_e(void) { SI = board_c; sim_rev_e(); sim_run(0.3f); }
static void plain(void)                                            // the break as it is, the same every two bars
{
    set("bpat", 0); set("bedt", 0); set("brol", 0); set("brev", 0); set("bdri", 0); set("bbla", 0); set("bgab", 0);
    set("bodd", 0); set("bswg", 0); set("bpit", 0); set("bspd", 0);
}
static void shot(const char *label) { coach_pause(); sim_run(0.04f); sim_shot(label); }   // the scene with no hint over its bottom rows
static void lturn(int n)                                           // n detents as a hand turns them: one every 30 ms
{
    for (int i = 0; i < (n < 0 ? -n : n); i++) { sim_lturn(n < 0 ? -1 : 1); sim_run(0.02f); }
}

// ---------------------------------------------------------------- the same two bars
#define PHRASE 32                                                  // steps: the built-in breaks are two bars long
static int32_t step_now(void) { grid_t g; wr_grid_get(&g); return wr_step_at(&g, audio_frames()); }
static void to_phrase(void)                                        // run to the tick in which the next two bars start
{
    int32_t s = step_now();
    for (int i = 0; i < 4000; i++) { sim_tick(); int32_t n = step_now(); if (n / PHRASE != s / PHRASE) return; s = n; }
}
static int phrase_ticks(void) { grid_t g; wr_grid_get(&g); return (int)(wr_step_frames(&g) * (float)PHRASE / (float)TICK_FRAMES + 0.5f); }
typedef struct { float rms, bright, crest; } meas_t;
static meas_t meas(int a, int b)
{
    double e = 0, d = 0; float pk = 0;
    for (int i = a > 0 ? a : 1; i < b && i < sim_n; i++) {
        float x = sim_out[i], dx = x - sim_out[i - 1];
        e += (double)x * x; d += (double)dx * dx;
        if (fabsf(x) > pk) pk = fabsf(x);
    }
    float rms = (float)sqrt(e / (double)(b - a > 0 ? b - a : 1));
    return (meas_t){ rms, e > 0 ? (float)sqrt(d / e) : 0.0f, rms > 0 ? pk / rms : 0.0f };
}
static meas_t passage(void) { to_phrase(); int a = sim_n; for (int i = phrase_ticks(); i > 0; i--) sim_tick(); return meas(a, sim_n); }
// how far two passages are apart: the largest of the three differences, as a fraction
static float apart(meas_t a, meas_t b)
{
    float x = fabsf(a.rms / b.rms - 1.0f), y = fabsf(a.bright / b.bright - 1.0f), z = fabsf(a.crest / b.crest - 1.0f);
    return x > y ? (x > z ? x : z) : (y > z ? y : z);
}
static float max_step(float t0, float t1)                          // the biggest step between two samples
{
    int a = (int)(t0 * AUDIO_SR), b = (int)(t1 * AUDIO_SR); float w = 0;
    for (int i = a > 0 ? a : 1; i < b && i < sim_n; i++) { float d = fabsf(sim_out[i] - sim_out[i - 1]); if (d > w) w = d; }
    return w;
}

// a break that is one tone (880 Hz, the same in every slice): its pitch is the dive, and a click has nowhere to hide
#define TB_SIX 4000
static int16_t tb_pcm[16 * TB_SIX];
static brk_t TB;
static void tone_break(bool on)
{
    static uint32_t serial = 0x7E570000u;
    if (!on) { beat_test_break = NULL; sim_run(0.5f); return; }
    for (int i = 0; i < 16 * TB_SIX; i++) tb_pcm[i] = (int16_t)(0.5f * 32767.0f * sinf(6.2831853f * 880.0f * (float)i / (float)AUDIO_SR));
    for (int j = 0; j < 16; j++) {
        TB.slice[j] = j * TB_SIX; TB.step[j] = (uint8_t)j; TB.peak[j] = 200;
        TB.kind[j] = (uint8_t)(j % 4 == 0 ? KS_KICK : j % 4 == 2 ? KS_SNARE : KS_HAT);
    }
    TB.pcm = tb_pcm; TB.len = 16 * TB_SIX; TB.steps = 16; TB.bpm = 120.0f; TB.nslices = 16; TB.serial = ++serial; TB.name = "TEST";
    beat_test_break = &TB;
    sim_run(0.5f);
}

// ---------------------------------------------------------------- the left knob: crush to the left, drive to the right
static meas_t c_clean, c_crush, c_drive;                           // the stick's, on the rev C
static float noise, tol;                                           // how far the same two bars are apart played again; "the same"
static bool full(float v) { return v > 0.999f; }                   // (the hand's value comes to 1 by gliding)
static void left_knob(void)
{
    section("wreck: rev E, the left knob");
    rev_c();
    c_clean = passage();
    noise = 0;
    for (int k = 0; k < 3; k++) { float d = apart(passage(), c_clean); if (d > noise) noise = d; }
    tol = 2.0f * noise + 0.02f;                                    // "the same": within twice the bars' own spread, and 2 %
    SI.jx = -1.0f; sim_run(0.5f); float st_cr = perf.crush; fx_view_t fc; fx_view(&fc); c_crush = passage(); SI.jx = 0; sim_run(0.5f);
    SI.jx = 1.0f; sim_run(0.5f); float st_dr = perf.drive; fx_view_t fd; fx_view(&fd); c_drive = passage(); SI.jx = 0; sim_run(0.5f);
    note("rev C, the same two bars: clean rms %.4f bright %.3f crest %.2f (played three times more: %.1f %% apart at most); the stick left %.4f %.3f %.2f; right %.4f %.3f %.2f",
         (double)c_clean.rms, (double)c_clean.bright, (double)c_clean.crest, (double)(noise * 100.0f),
         (double)c_crush.rms, (double)c_crush.bright, (double)c_crush.crest, (double)c_drive.rms, (double)c_drive.bright, (double)c_drive.crest);
    check(st_cr == 1.0f && st_dr == 1.0f && apart(c_crush, c_clean) > 4.0f * tol && apart(c_drive, c_clean) > 4.0f * tol && apart(c_crush, c_drive) > 4.0f * tol,
          "the measure tells the stick's crush and drive from clean and from each other (%.0f %%, %.0f %%, %.0f %% apart; the same bars again: %.1f %%, so \"the same\" is within %.1f %%)",
          (double)(apart(c_crush, c_clean) * 100.0f), (double)(apart(c_drive, c_clean) * 100.0f), (double)(apart(c_crush, c_drive) * 100.0f), (double)(noise * 100.0f), (double)(tol * 100.0f));

    rev_e();
    const ctl_t *c = front_ctl();
    const wr_reve_t *e = wr_reve();
    float axis0 = get("xaxs");
    sim_run(0.5f);
    meas_t mid = passage();
    check(c->has_lknob && !c->has_joy && e->lk == 0 && perf.crush == 0.0f && perf.drive == 0.0f && apart(mid, c_clean) < tol,
          "rev E, the left knob in the middle: neither (crush %.2f, drive %.2f), the two bars as the rev C's clean ones (%.1f %% apart)",
          (double)perf.crush, (double)perf.drive, (double)(apart(mid, c_clean) * 100.0f));
    shot("reve_knob_middle");
    // one detent: a twelfth, and it glides there
    float was = perf.crush, big = 0;
    sim_lturn(-1);
    for (int i = 0; i < 10; i++) { float d = fabsf(perf.crush - was); if (d > big) big = d; was = perf.crush; sim_tick(); }
    check(e->lk == -1 && fabsf(perf.crush - 1.0f / 12.0f) < 1e-4f && big < 0.03f, "one detent left: crush %.3f (a twelfth), reached in steps of %.3f a tick at most", (double)perf.crush, (double)big);
    lturn(-11); sim_run(0.3f);
    shot("reve_knob_crush");
    fx_view_t fe; fx_view(&fe);
    float kn_cr = perf.crush;
    meas_t k_crush = passage();
    check(e->lk == -LK_FULL && kn_cr == 1.0f && st_cr == 1.0f && perf.drive == 0.0f && fabsf(fe.crush - fc.crush) < 0.005f && apart(k_crush, c_crush) < tol,
          "twelve detents left: crush %.2f, the stick fully left's %.2f (FX at %.3f and %.3f); the two bars %.1f %% apart from the stick's, %.0f %% from clean",
          (double)kn_cr, (double)st_cr, (double)fe.crush, (double)fc.crush, (double)(apart(k_crush, c_crush) * 100.0f), (double)(apart(k_crush, c_clean) * 100.0f));
    sim_lturn(-3); sim_run(0.2f);
    bool stops = e->lk == -LK_FULL && perf.crush == 1.0f;
    sim_run(5.0f);
    check(stops && e->lk == -LK_FULL && perf.crush == 1.0f, "turned further it stays at full, and five seconds later it is where it was left (crush %.2f)", (double)perf.crush);
    lturn(12); sim_run(0.5f);
    meas_t mid2 = passage();
    check(e->lk == 0 && perf.crush == 0.0f && perf.drive == 0.0f && apart(mid2, c_clean) < tol,
          "twelve detents back to the middle: clean again (%.1f %% apart from the rev C's clean bars)", (double)(apart(mid2, c_clean) * 100.0f));
    lturn(12); sim_run(0.3f);
    shot("reve_knob_drive");
    sim_run(0.3f);
    fx_view(&fe);
    float kn_dr = perf.drive;
    meas_t k_drive = passage();
    check(e->lk == LK_FULL && kn_dr == 1.0f && st_dr == 1.0f && perf.crush == 0.0f && fabsf(fe.drive - fd.drive) < 0.005f && apart(k_drive, c_drive) < tol,
          "twelve detents right of the middle: drive %.2f, the stick fully right's %.2f (FX at %.3f and %.3f); the two bars %.1f %% apart from the stick's, %.0f %% from clean",
          (double)kn_dr, (double)st_dr, (double)fe.drive, (double)fd.drive, (double)(apart(k_drive, c_drive) * 100.0f), (double)(apart(k_drive, c_clean) * 100.0f));
    lturn(-6); sim_run(0.5f);
    float half = perf.drive;
    // spun as fast as it goes (all twelve in one tick): the value still glides
    was = perf.drive - perf.crush; big = 0;
    sim_lturn(-12);
    for (int i = 0; i < 60; i++) { float v = perf.drive - perf.crush, d = fabsf(v - was); if (d > big) big = d; was = v; sim_tick(); }
    check(fabsf(half - 0.5f) < 1e-4f && e->lk == -6 && fabsf(perf.crush - 0.5f) < 1e-4f && perf.drive == 0.0f && big < 0.03f,
          "six detents: half (drive %.2f); spun twelve in one tick to the other side (crush %.2f): %.3f a tick at most", (double)half, (double)perf.crush, (double)big);
    // it is not the axis: the left knob leaves it, the right knob still turns it
    float axis1 = get("xaxs");
    sim_turn(1); sim_run(0.1f);
    float axis2 = get("xaxs");
    sim_turn(-1); sim_run(0.1f);
    check(axis1 == axis0 && axis2 == axis0 + 2.0f && get("xaxs") == axis0, "the left knob leaves the axis alone (%.0f); the knob still turns it (%.0f)", (double)axis1, (double)axis2);
    lturn(6); sim_run(0.5f);
}

// ---------------------------------------------------------------- the hand: the roll
static int roll_n(void) { beat_view_t v; beat_view(&v); return v.roll; }
typedef struct { int hits, n, slice; float rate; meas_t m; int on_tick; } roll_t;
// from the start of two bars: the control goes on (the hand, or the stick `wait` ticks later), two bars pass, the
// third and fourth are measured
static roll_t roll_take(bool hand, int wait)
{
    roll_t r = { 0 };
    to_phrase();
    if (hand) SI.hand_mm = 40;
    r.on_tick = -1;
    int pt = phrase_ticks();
    for (int i = 0; i < pt; i++) {
        if (!hand && i == wait) SI.jy = 1.0f;
        sim_tick();
        if (r.on_tick < 0 && perf.roll > 0.08f) r.on_tick = i;
    }
    beat_tlog_n = 0;
    int a = sim_n;
    for (int i = 0; i < pt; i++) sim_tick();
    r.m = meas(a, sim_n);
    for (int i = 0; i < beat_tlog_n; i++) if (beat_tlog[i].lane == 0) { r.hits++; r.n = beat_tlog[i].n; r.slice = beat_tlog[i].slice; r.rate = beat_tlog[i].rate; }
    if (hand) SI.hand_mm = 0; else SI.jy = 0;
    sim_run(1.0f);
    return r;
}
static int count_of(float r) { return r < 0.08f ? 0 : r < 0.3f ? 2 : r < 0.5f ? 4 : r < 0.7f ? 8 : r < 0.88f ? 16 : 32; }   // BEAT's own borders

static void hand_roll(void)
{
    section("wreck: rev E, the hand rolls");
    rev_e(); set("xhnd", HAND_ROLL); sim_run(0.5f);
    const ctl_t *c = front_ctl();
    beat_tlog_n = 0; sim_run(2.4f);
    int rolled = 0;
    for (int i = 0; i < beat_tlog_n; i++) if (beat_tlog[i].n > 1) rolled++;
    check(c->has_hand && !c->hand_seen && perf.roll == 0.0f && rolled == 0, "no hand: no roll (%d hits in two bars, %d of them rolled)", beat_tlog_n, rolled);
    roll_t he = roll_take(true, 0);
    rev_c();
    roll_t st = roll_take(false, he.on_tick);
    check(he.n == 32 && st.n == 32 && he.hits == st.hits && he.slice == st.slice && fabsf(he.rate / st.rate - 1.0f) < 0.001f && apart(he.m, st.m) < tol,
          "a hand at 4 cm rolls as the stick fully up does: %d and %d hits in two bars, %d and %d a step, slice %d and %d, %.2fx and %.2fx the pitch; the bars %.1f %% apart",
          he.hits, st.hits, he.n, st.n, he.slice, st.slice, (double)he.rate, (double)st.rate, (double)(apart(he.m, st.m) * 100.0f));
    note("the roll began %d ms after the hand came (the stick was pushed on that tick)", he.on_tick * 10);
    // the lower the hand, the more: each height against the stick pushed as far
    static const int mm[5] = { 215, 170, 125, 85, 40 };
    float val[5]; int got[5], want[5], stick[5];
    rev_e();
    for (int k = 0; k < 5; k++) { sim_hand(mm[k], 0.8f); val[k] = perf.roll; got[k] = roll_n(); want[k] = count_of(val[k]); if (k == 2) shot("reve_hand_roll"); }
    sim_hand(0, 0.6f);
    float gone = perf.roll;
    rev_c();
    for (int k = 0; k < 5; k++) { SI.jy = val[k]; sim_run(0.8f); stick[k] = roll_n(); }
    SI.jy = 0; sim_run(0.5f);
    bool more = true, same = true;
    for (int k = 0; k < 5; k++) { if (k && (val[k] <= val[k - 1] || got[k] <= got[k - 1])) more = false; if (got[k] != stick[k] || got[k] != want[k]) same = false; }
    check(more && same && got[0] == 2 && got[4] == 32 && full(val[4]) && gone == 0.0f,
          "the lower the hand, the faster: 215 mm %.2f %d a step, 170 mm %.2f %d, 125 mm %.2f %d, 85 mm %.2f %d, 40 mm %.2f %d (the stick as far up: %d %d %d %d %d); away: %.2f",
          (double)val[0], got[0], (double)val[1], got[1], (double)val[2], got[2], (double)val[3], got[3], (double)val[4], got[4],
          stick[0], stick[1], stick[2], stick[3], stick[4], (double)gone);
    // a hand that trembles where 8 and 16 a step meet (for a hand coming down: the core's value at 0.712, 101 mm): the
    // core's value crosses that line again and again, the roll's speed does not follow
    rev_e();
    sim_hand(101, 0.8f);
    const float line = 0.712f;
    int raw_flips = 0, flips = 0, was = roll_n();
    bool over = c->hand > line;
    uint32_t r = 99;
    for (int i = 0; i < 300; i++) {
        r = r * 1664525u + 1013904223u;
        SI.hand_mm = 101 + (int)((r >> 16) % 7u) - 3;              // 98 .. 104 mm
        sim_tick();
        int n = roll_n();
        if ((c->hand > line) != over) { raw_flips++; over = !over; }
        if (n && was && n != was) flips++;
        if (n) was = n;
    }
    sim_hand(0, 0.6f);
    check(raw_flips >= 4 && flips <= 1, "a hand trembling 3 mm each way at 101 mm for 3 s: the core's value crosses the border %d times, the roll changes speed %d time(s)",
          raw_flips, flips);
}

// ---------------------------------------------------------------- WRECK > HAND: the dive, the crush, the drive
static float fx_keep[3];
static void fx_bare(bool on)                                       // the drums' crush and drive off, so a tone stays a tone
{
    static const char *const t[3] = { "fcru", "frat", "fdrv" };
    for (int i = 0; i < 3; i++) { if (on) { fx_keep[i] = get(t[i]); set(t[i], 0); } else set(t[i], fx_keep[i]); }
    sim_run(0.5f);
}
static float pitch_now(void) { sim_run(0.5f); return m_pitch_at(sim_t - 0.3f, sim_t, 300.0f, 1200.0f); }

static void hand_modes(void)
{
    section("wreck: rev E, WRECK > HAND");
    // DIVE, on the tone
    rev_c(); fx_bare(true); tone_break(true);
    float f0 = pitch_now();
    SI.jy = -1.0f; float f_st = pitch_now(); float st_dv = perf.dive; SI.jy = 0; sim_run(0.3f);
    rev_e(); set("xhnd", HAND_DIVE);
    float f_e0 = pitch_now();
    sim_hand(40, 0.3f); float f_hd = pitch_now(); float h_dv = perf.dive, h_ro = perf.roll;
    sim_hand(145, 0.3f); float f_half = pitch_now(); float h_half = perf.dive;
    sim_hand(0, 0.5f); float f_back = pitch_now();
    check(f0 > 800.0f && fabsf(f_e0 / f0 - 1.0f) < 0.003f && fabsf(f_hd / f0 - 0.5f) < 0.003f && fabsf(f_st / f0 - 0.5f) < 0.003f && full(h_dv) && st_dv == 1.0f && h_ro == 0.0f
          && fabsf(f_half / f0 - exp2f(-h_half)) < 0.01f && h_half > 0.4f && h_half < 0.6f && fabsf(f_back / f0 - 1.0f) < 0.003f,
          "HAND: DIVE. A tone of %.0f Hz: a hand at 4 cm %.0f Hz (dive %.2f, no roll), the stick fully down %.0f Hz (%.2f): an octave; at 145 mm %.0f Hz (dive %.2f); away %.0f Hz",
          (double)f0, (double)f_hd, (double)h_dv, (double)f_st, (double)st_dv, (double)f_half, (double)h_half, (double)f_back);
    tone_break(false); fx_bare(false);
    // CRUSH and DRIVE, on the break: the two bars against the stick's
    set("xhnd", HAND_CRUSH);
    sim_hand(40, 0.5f); float h_cr = perf.crush, h_ro2 = perf.roll;
    meas_t m_cr = passage();
    sim_hand(0, 0.6f);
    check(full(h_cr) && h_ro2 == 0.0f && perf.crush == 0.0f && apart(m_cr, c_crush) < tol,
          "HAND: CRUSH. A hand at 4 cm: crush %.2f, no roll; the two bars %.1f %% apart from the stick fully left's (%.0f %% from clean); away: %.2f",
          (double)h_cr, (double)(apart(m_cr, c_crush) * 100.0f), (double)(apart(m_cr, c_clean) * 100.0f), (double)perf.crush);
    set("xhnd", HAND_DRIVE);
    sim_hand(40, 0.5f); float h_dr = perf.drive;
    shot("reve_hand_drive");
    meas_t m_dr = passage();
    sim_hand(0, 0.6f);
    check(full(h_dr) && perf.drive == 0.0f && perf.roll == 0.0f && apart(m_dr, c_drive) < tol,
          "HAND: DRIVE. A hand at 4 cm: drive %.2f; the two bars %.1f %% apart from the stick fully right's (%.0f %% from clean); away: %.2f",
          (double)h_dr, (double)(apart(m_dr, c_drive) * 100.0f), (double)(apart(m_dr, c_clean) * 100.0f), (double)perf.drive);
    // hand and knob on the same effect: the larger wins
    set("xhnd", HAND_CRUSH);
    lturn(-6); sim_run(0.5f);
    float a0 = perf.crush;
    sim_hand(40, 0.5f); float a1 = perf.crush;
    sim_hand(210, 0.5f); float a2 = perf.crush, a2h = wr_reve()->hand_v;
    sim_hand(0, 0.6f); float a3 = perf.crush;
    set("xhnd", HAND_DRIVE);
    sim_hand(40, 0.5f); float b_cr = perf.crush, b_dr = perf.drive;
    sim_hand(0, 0.6f);
    lturn(6); sim_run(0.5f);
    check(fabsf(a0 - 0.5f) < 1e-4f && full(a1) && fabsf(a2 - 0.5f) < 1e-4f && a2h > 0.05f && a2h < 0.4f && fabsf(a3 - 0.5f) < 1e-4f
          && fabsf(b_cr - 0.5f) < 1e-4f && full(b_dr) && perf.crush == 0.0f && perf.drive == 0.0f,
          "the knob half way to crush (%.2f) and the hand on CRUSH: a low hand wins (%.2f), a high one (worth %.2f) does not (%.2f), away %.2f; the hand on DRIVE: crush %.2f and drive %.2f together",
          (double)a0, (double)a1, (double)a2h, (double)a2, (double)a3, (double)b_cr, (double)b_dr);
    // the step grid open: the hand is left out, as the stick is; the knob stays where it is
    set("xhnd", HAND_CRUSH);
    lturn(-6); sim_hand(40, 0.5f);
    float g0 = perf.crush;
    sim_hold_knob(); sim_run(0.4f);
    bool open = beat_editing(); float g1 = perf.crush, g1h = wr_reve()->hand_v;
    sim_hold_knob(); sim_run(0.4f);
    bool shut = !beat_editing(); float g2 = perf.crush;
    sim_hand(0, 0.6f); lturn(6); set("bpat", 0); set("xhnd", HAND_ROLL); sim_run(0.5f);
    check(open && shut && full(g0) && fabsf(g1 - 0.5f) < 1e-4f && g1h == 0.0f && full(g2),
          "the step grid open with a hand at 4 cm on CRUSH and the knob half way: crush %.2f, then %.2f in the grid (the knob's; the hand is left out), %.2f once it is shut",
          (double)g0, (double)g1, (double)g2);
}

// ---------------------------------------------------------------- down fast, sits, leaves: no click, nothing stuck
// On the tone, with the drums' own crush and drive off. A cut with no fade would step by up to the tone's whole swing
// (0.74 here); what is allowed is a quarter over the biggest of the stick's own flick, the effect sitting and the tone
// alone. Half a second after the hand left, the sound is the tone's again: its pitch, no rolled hit, FX's crush and
// drive where they were, and its brightness (which any of the four would move by tens of per cent).
static float eff(int mode) { return mode == HAND_ROLL ? perf.roll : mode == HAND_DIVE ? perf.dive : mode == HAND_CRUSH ? perf.crush : perf.drive; }
static void hand_comes_and_goes(void)
{
    section("wreck: rev E, the hand comes down fast and leaves");
    static const char *const nm[HAND_N] = { "ROLL", "DIVE", "CRUSH", "DRIVE" };
    fx_bare(true); tone_break(true);
    rev_c(); sim_run(2.9f);
    float clean = max_step(sim_t - 2.4f, sim_t);                   // the tone's own biggest step (where one slice takes over from another)
    meas_t tone = meas(sim_n - (int)(2.4f * AUDIO_SR), sim_n);
    note("the tone alone: rms %.4f, bright %.4f, peak %.3f, so a cut with no fade could step by up to %.2f; its own biggest step is %.4f",
         (double)tone.rms, (double)tone.bright, (double)m_peak(sim_t - 2.4f, sim_t), (double)(2.0f * m_peak(sim_t - 2.4f, sim_t)), (double)clean);
    for (int m = 0; m < HAND_N; m++) {
        // the stick's own move, on the rev C: flicked all the way, held, let go
        rev_c(); sim_run(0.5f);
        float s0 = sim_t;
        if (m == HAND_ROLL) SI.jy = 1.0f; else if (m == HAND_DIVE) SI.jy = -1.0f; else if (m == HAND_CRUSH) SI.jx = -1.0f; else SI.jx = 1.0f;
        sim_run(1.2f);
        SI.jx = SI.jy = 0; sim_run(0.6f);
        float stick = max_step(s0, sim_t);
        // the hand, on the rev E
        rev_e(); set("xhnd", (float)m); sim_run(0.5f);
        fx_view_t f0; fx_view(&f0);
        float ta = sim_t;
        sim_hand(40, 1.2f);                                        // nothing, then 4 cm on the next reading
        float at_full = eff(m);
        float tl = sim_t;
        SI.hand_mm = 0;
        int gone_ms = -1;
        for (int i = 0; i < 100; i++) { sim_tick(); if (gone_ms < 0 && eff(m) == 0.0f) gone_ms = (i + 1) * 10; }
        float arrive = max_step(ta, ta + 0.4f), sit = max_step(ta + 0.5f, tl), leave = max_step(tl, tl + 0.5f);
        meas_t after = meas((int)((tl + 0.5f) * AUDIO_SR), (int)((tl + 1.0f) * AUDIO_SR));
        float a_bright = after.bright / tone.bright - 1.0f;      // (one tone in a room: its level over half a second
                                                                   // swings 20 % by itself, its brightness under 1 %)
        // half a second after the hand left: as it was before it came
        fx_view_t f1; fx_view(&f1);
        beat_tlog_n = 0; sim_run(0.6f);
        int rolled = 0;
        for (int i = 0; i < beat_tlog_n; i++) if (beat_tlog[i].n > 1) rolled++;
        float f = m_pitch_at(tl + 0.5f, tl + 0.8f, 300.0f, 1200.0f);
        float worst = arrive > leave ? arrive : leave, own = stick > sit ? stick : sit;
        if (clean > own) own = clean;
        bool back = rolled == 0 && roll_n() == 0 && fabsf(f / 880.0f - 1.0f) < 0.005f && fabsf(f1.crush - f0.crush) < 0.005f && fabsf(f1.drive - f0.drive) < 0.005f
                    && fabsf(a_bright) < 0.02f;
        check(full(at_full) && gone_ms > 0 && gone_ms < 500 && back && worst < own * 1.25f + 0.002f,
              "%s: the hand down at once, 1.2 s, gone. Biggest step between two samples arriving %.4f, leaving %.4f (sitting %.4f, the stick's own flick %.4f, the tone alone %.4f); "
              "gone %d ms after it left; from half a second after: %.0f Hz, %d rolled hits, the brightness %+.1f %% from the tone alone's",
              nm[m], (double)arrive, (double)leave, (double)sit, (double)stick, (double)clean, gone_ms, (double)f, rolled, (double)(a_bright * 100.0f));
    }
    set("xhnd", HAND_ROLL);
    tone_break(false); fx_bare(false);
}

// ---------------------------------------------------------------- the beat and the link stay where they were
static void workout(void (*each)(void))                            // everything the two parts do, about 9 s
{
    for (int m = 0; m < HAND_N; m++) {
        set("xhnd", (float)m);
        for (int k = 0; k < 2; k++) {
            SI.hand_mm = k ? 150 : 45; for (int i = 0; i < 40; i++) { sim_tick(); if (each) each(); }
            SI.hand_mm = 0; for (int i = 0; i < 30; i++) { sim_tick(); if (each) each(); }
        }
    }
    static const int turns[4] = { -12, 24, -18, 6 };
    for (int k = 0; k < 4; k++) for (int i = 0; i < (turns[k] < 0 ? -turns[k] : turns[k]); i++) {
        SI.lk = turns[k] < 0 ? -1 : 1;
        for (int j = 0; j < 4; j++) { sim_tick(); if (each) each(); }
    }
    set("xhnd", HAND_ROLL);
}
static int peer_k; static float gap_worst; static bool lost;
static void watch_link(void) { float g = fabsf(sim_peer_gap_ms(peer_k)); if (g > gap_worst) gap_worst = g; if (!link_synced()) lost = true; }
static void beat_stays(void)
{
    section("wreck: rev E, the tempo and the link");
    rev_e(); sim_run(0.5f);
    grid_t g0; wr_grid_get(&g0);
    uint32_t fpb0 = song.fpb, o0 = song.origin; float tempo0 = get("stmp"), axis0 = get("xaxs"); int zone0 = wr_zone();
    beat_tlog_n = 0;
    workout(NULL);
    sim_run(0.5f);
    grid_t g1; wr_grid_get(&g1);
    int plain_hits = 0, off = 0;                                   // every hit that is not part of a roll starts on a step's first frame
    for (int i = 0; i < beat_tlog_n; i++) {
        if (beat_tlog[i].lane || beat_tlog[i].n > 1) continue;
        int32_t pos = wr_pos(&g0, beat_tlog[i].frame);
        plain_hits++;
        if (pos != wr_step_start(&g0, wr_step_of(&g0, pos))) off++;
    }
    check(g1.feel_q == g0.feel_q && g1.fpb == g0.fpb && g1.origin == g0.origin && song.fpb == fpb0 && song.origin == o0 && get("stmp") == tempo0
          && get("xaxs") == axis0 && wr_zone() == zone0 && perf.running && plain_hits > 50 && off == 0,
          "alone, through every hand mode and the knob end to end: the tempo (%.0f BPM, x%d), beat 0 and the zone are where they were; %d plain hits, %d off its step's first frame",
          (double)tempo0, 1 << g1.feel_q, plain_hits, off);
    // next to a pretend board that is the clock
    peer_k = sim_peer_add("MANTRA", 2, 15.0, 6.0e9);
    sim_peer_use(peer_k);
    {   uint32_t o; float f; link_origin(&o, &f); link_set_time(16000, o, 4, 0); }   // 120 BPM
    sim_peer_use(-1);
    sim_run(2.5f);
    bool synced = !link_is_clock() && link_synced() && song.fpb == 16000;
    float gap0 = fabsf(sim_peer_gap_ms(peer_k));
    gap_worst = 0; lost = false;
    workout(watch_link);
    grid_t g2; wr_grid_get(&g2);
    check(peer_k >= 0 && synced && !lost && gap_worst < 1.0f && song.fpb == 16000 && g2.fpb == 16000 && g2.origin == song.origin && (int)get("stmp") == 120,
          "linked to a board at 120 BPM, the same again: in sync all the way, the beats %.3f ms apart at most (%.3f before), the grid on its beat",
          (double)gap_worst, (double)gap0);
    sim_peer_power(peer_k, false);
    sim_run(1.5f);
    set("stmp", 100); sim_run(0.5f);
}

// ---------------------------------------------------------------- the board with the stick: as before
static void stick_board(void)
{
    section("wreck: the board with the stick, with WRECK > HAND");
    rev_c();
    const ctl_t *c = front_ctl();
    const wr_reve_t *e = wr_reve();
    int ok = 0; float worst = 0;
    for (int m = 0; m < HAND_N; m++) {
        set("xhnd", (float)m); sim_run(0.2f);
        bool rest = perf.roll == 0.0f && perf.dive == 0.0f && perf.crush == 0.0f && perf.drive == 0.0f;
        SI.jy = 1.0f; sim_run(0.1f); bool up = perf.roll == 1.0f && perf.dive == 0.0f && perf.crush == 0.0f && perf.drive == 0.0f;
        SI.jy = -0.6f; sim_run(0.1f); bool dn = perf.roll == 0.0f && perf.dive == 0.6f && perf.crush == 0.0f && perf.drive == 0.0f;
        SI.jy = 0; SI.jx = -1.0f; sim_run(0.1f); bool lf = perf.roll == 0.0f && perf.dive == 0.0f && perf.crush == 1.0f && perf.drive == 0.0f;
        SI.jx = 0.3f; sim_run(0.1f); bool rt = perf.roll == 0.0f && perf.dive == 0.0f && perf.crush == 0.0f && perf.drive == 0.3f;
        SI.jx = 0; sim_run(0.5f);
        meas_t p = passage();
        float d = apart(p, c_clean);
        if (d > worst) worst = d;
        if (rest && up && dn && lf && rt && d < tol && !e->lknob && !e->hand && e->lk == 0 && e->hand_v == 0.0f) ok++;
        else note("HAND %d: rest %d up %d down %d left %d right %d, %.1f %% from clean", m, rest, up, dn, lf, rt, (double)(d * 100.0f));
    }
    set("xhnd", HAND_ROLL);
    check(ok == HAND_N && c->has_joy && !c->has_lknob && !c->has_hand,
          "with a stick, HAND on ROLL, DIVE, CRUSH or DRIVE changes nothing: the stick's four moves give what they gave, at rest all four are 0, the two bars are the clean ones (%.1f %% apart at most; %d of 4)",
          (double)(worst * 100.0f), ok);
}

// ---------------------------------------------------------------- the screen
static void show_hint(int learn, const char *label)                // only that hint left to learn: it comes up when idle
{
    uint32_t keep = meta.learned;
    coach_pause();
    param_set(CP(CP_HINTS), 0); param_set(CP(CP_HINTS), 1);        // (HINTS switched on: the coach starts over)
    meta.learned = ~(1u << learn);
    sim_run(4.0f);
    sim_shot(label);
    meta.learned = keep;
    coach_pause(); sim_run(0.3f);
}
static void screens(void)
{
    section("wreck: rev E, the screen");
    rev_e(); set("xhnd", HAND_ROLL); sim_run(0.5f);
    float t0 = sim_t;
    sim_gif_begin("rev-e");
    sim_run(1.0f);
    lturn(-12); sim_run(1.5f); lturn(24); sim_run(1.5f); lturn(-12); sim_run(1.0f);
    for (int mm = 240; mm >= 40; mm -= 8) sim_hand(mm, 0.08f);
    sim_run(1.0f);
    for (int mm = 40; mm <= 240; mm += 20) sim_hand(mm, 0.08f);
    sim_hand(0, 1.0f);
    sim_gif_end();
    sim_wav("-rev-e", t0, sim_t);
    // towards DRONE, the amp in: the knob on drive, the hand diving
    set("xhnd", HAND_DIVE); set("xaxs", 90); lturn(9); sim_run(2.5f);
    sim_hand(110, 0.8f);
    shot("reve_drone_dive");
    sim_hand(0, 0.6f); lturn(-9); set("xhnd", HAND_ROLL); set("xaxs", 10); sim_run(2.5f);
    show_hint(LEARN_LKNOB, "reve_hint_knob");
    show_hint(LEARN_HAND, "reve_hint_hand");
    check(m_bad() == 0, "the knob end to end and a hand down and up again (out/wreck-rev-e.wav, wreck-rev-e.gif): no NaN");
}

void test_rev_e(void)
{
    SI.mic = NULL; SI.mic_u = NULL; SI.jx = SI.jy = 0; SI.joy = true; SI.imu = true; sim_tilt(0, 0);
    board_c = SI;
    float keep_chg = get("xchg"), keep_tempo = get("stmp");
    front_midi(0xC0, 0, 0);                                        // the sound WRECK: the amp is silent at BREAKCORE
    set("xaxs", 10); set("xchg", 1); set("stmp", 100); set("xhnd", HAND_ROLL); plain();
    sim_run(16.0f);                                                // (the test before left it at DRONE: its hall rings for 10 s)
    left_knob();
    hand_roll();
    hand_modes();
    hand_comes_and_goes();
    beat_stays();
    stick_board();
    screens();
    SI = board_c;
    front_midi(0xC0, 0, 0);
    set("xhnd", HAND_ROLL); set("xchg", keep_chg); set("stmp", keep_tempo); set("xaxs", 10);
    sim_run(2.0f);
}
