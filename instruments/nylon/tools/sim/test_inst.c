// NYLON on the host, with a pretend classical guitar: six Karplus-Strong strings (E A D G B E, nylon: dark and
// about 2.6 s to silence), plucked by a script; a body that thumps when it is slapped (a sound in the mic and a
// jolt in the 1 kHz accelerometer) while the strings buzz the top a little; the guitar's pose (the neck's angle,
// the top tipped up, a left-handed guitar) as the accelerometer sees it from a board stuck on the top at any
// angle; and, for BLOOM ON WOOD, a speaker on the top that rings the strings.
//
// The checks: the neck (home by itself, lift and lean in degrees, a board turned on the top, a left-handed guitar
// fixed by NECK SETUP, the knob held sets home, a slap does not move it); the looper (armed until the first note,
// the loop as long as from that note to the tap, its note a loop later, no seam, a layer from the press, UNDO,
// stop, play, erase, a take too short, AT ONCE, TAPE and LEVEL on the neck); every mode doing what it says, and
// what the neck does to it; slaps against plucks; the pitch heard; the loop under every sound; the CPU.
#include "sim.h"
#include "inst_nylon.h"
#include "dsp.h"
#include <math.h>
#include <string.h>
#include <time.h>

const char *const inst_click_skip[] = { NULL };

// ---------------------------------------------------------------- the guitar
#define NSTR 6
static const float open_hz[NSTR] = { 82.41f, 110.0f, 146.83f, 196.0f, 246.94f, 329.63f };
typedef struct { float buf[2048]; int n, w; float frac, lp, loss, hz; } kstring_t;
static kstring_t str[NSTR];
static uint32_t gseed = 12345;
static float grnd(void) { gseed = gseed * 1664525u + 1013904223u; return (float)(int32_t)gseed / 2147483648.0f; }
static float couple;                         // how much of what the board plays rings the strings (a speaker on the top)
static float howl_g, howl_hz = 4300.0f;      // the speaker heard straight back by the mic through a resonance (feedback)
static biquad_t howl_bp; static float howl_set_hz;
static float body_t = -1, body_amp;          // a slap's thump
static float hum_amp, hum_hz = 110.0f;       // a steady tone instead (no attacks): 0 off
static int   hum_kind;                       // 0 a chord of sines, 1 one sine, 2 a sawtooth
static float hum_ph;

static float clk_t[32]; static int nclk; static bool rec_clicks = true;   // the REC button's clicks (rec_tap, rec_hold)
typedef struct { float t; int s, fret; float amp; } pluck_ev_t;
static pluck_ev_t pq[256]; static int npq;
typedef struct { float t, g; int top; bool heard; } slap_ev_t;
static slap_ev_t sq[16]; static int nsq;

static void pluck_now(int s, int fret, float amp)
{
    kstring_t *k = &str[s];
    k->hz = open_hz[s] * powf(2.0f, (float)fret / 12.0f);
    float d = (float)AUDIO_SR / k->hz - 0.5f;
    k->n = (int)d; k->frac = d - (float)k->n;
    float lp = 0;
    for (int i = 0; i < k->n + 2; i++) { float r = grnd(); lp += 0.5f * (r - lp); k->buf[i] = amp * lp; }
    k->w = 0; k->lp = 0;
    k->loss = powf(0.001f, 1.0f / (2.6f * k->hz));
}
static void pluck_at(float t, int s, int fret, float amp) { if (npq < 256) pq[npq++] = (pluck_ev_t){ t, s, fret, amp }; }
static void mute(void) { for (int s = 0; s < NSTR; s++) if (str[s].n > 1) str[s].loss = powf(0.001f, 1.0f / (0.08f * str[s].hz)); }
static float string_run(kstring_t *k, float drive)
{
    if (k->n < 2) return 0;
    int r1 = (k->w + 1) % (k->n + 1);
    float a = k->buf[k->w], b = k->buf[r1];
    float y = a + k->frac * (b - a);
    float v = 0.5f * (y + k->lp) * k->loss + drive;
    if (fabsf(v) < 1e-20f) v = 0.0f;
    k->lp = y;
    k->buf[k->w] = v;
    k->w = (k->w + 1) % (k->n + 1);
    return y;
}

// the pose: the neck's angle (40 is the classical position), the top tipped up towards the face, the board turned
// on the top by psi, a left-handed guitar's neck the other way
static float pose_neck = 40.0f, pose_lean = 0.0f, pose_psi = 30.0f; static bool pose_lefty;
typedef struct { vec3_t x, y, z; } frame_t;
static frame_t board_axes(void)
{
    float th = pose_neck * 0.0174533f, ph = pose_lean * 0.0174533f, ps = pose_psi * 0.0174533f;
    // the world: x right (seen from the audience), y up, z towards the audience
    vec3_t en = { (pose_lefty ? -1.0f : 1.0f) * cosf(th), sinf(th), 0 }, et = { 0, 0, 1 };
    vec3_t ew = { et.y * en.z - et.z * en.y, et.z * en.x - et.x * en.z, et.x * en.y - et.y * en.x };
    vec3_t *v[3] = { &en, &et, &ew };
    for (int i = 0; i < 3; i++) { vec3_t a = *v[i]; v[i]->y = a.y * cosf(ph) + a.z * sinf(ph); v[i]->z = -a.y * sinf(ph) + a.z * cosf(ph); }
    frame_t f;
    f.x = (vec3_t){ cosf(ps) * en.x + sinf(ps) * ew.x, cosf(ps) * en.y + sinf(ps) * ew.y, cosf(ps) * en.z + sinf(ps) * ew.z };
    f.z = et;
    f.y = (vec3_t){ f.z.y * f.x.z - f.z.z * f.x.y, f.z.z * f.x.x - f.z.x * f.x.z, f.z.x * f.x.y - f.z.y * f.x.x };
    return f;
}
static void pose(float neck, float lean)
{
    pose_neck = neck; pose_lean = lean;
    frame_t f = board_axes();
    // the world's up in the screen's axes, as the core gives them: x right, y towards the BOTTOM edge, z out (a
    // mirror of the board's right-handed frame, whose y points to the top edge)
    SI.acc = (vec3_t){ f.x.y, -f.y.y, f.z.y };
    SI.gyro = (vec3_t){ 0, 0, 0 };
}
// a step of a gesture: the pose, and the gyro turning at rate deg/s for the moment it takes (held: call pose())
static void move(float neck, float lean, float rate)
{
    pose(neck, lean);
    SI.gyro = (vec3_t){ 0, 0, rate };
}

// the burst: up, plus the slaps' jolts (from the schedule, so a burst read just before a slap's sample still has
// it), plus the strings buzzing the top
typedef struct { float t, g; vec3_t dir; } jolt_ev_t;
static jolt_ev_t jq[16]; static int njq;
static float buzz = 0.4f;
static void burst_fill(double t0)
{
    vec3_t u = SI.acc;
    for (int i = 0; i < 10; i++) {
        double t = t0 + 0.001 * (double)i;
        vec3_t j = { 0, 0, 0 };
        for (int k = 0; k < njq; k++) {
            float d = (float)(t - (double)jq[k].t);
            if (d < 0.0f || d >= 0.03f) continue;
            float a = jq[k].g * expf(-d / 0.006f) * cosf(6.2831853f * 90.0f * d);
            j.x += a * jq[k].dir.x; j.y += a * jq[k].dir.y; j.z += a * jq[k].dir.z;
        }
        float b = 0;
        for (int s = 0; s < NSTR; s++) b += fabsf(str[s].buf[str[s].w]);
        b *= buzz * sinf(6.2831853f * 173.0f * (float)fmod(t, 1.0));
        SI.burst[i] = (vec3_t){ u.x + j.x + b * 0.3f, u.y + j.y + b * 0.2f, u.z + j.z + b };
    }
    SI.nburst = 10;
    for (int k = 0; k < njq; ) { if (t0 - (double)jq[k].t > 0.05) jq[k] = jq[--njq]; else k++; }
}

static long last_tick = -1;
static float mic(float t, void *u)
{
    (void)u;
    for (int i = 0; i < npq; ) { if (t >= pq[i].t) { pluck_now(pq[i].s, pq[i].fret, pq[i].amp); pq[i] = pq[--npq]; } else i++; }
    for (int i = 0; i < nsq; ) {
        if (t >= sq[i].t) {
            if (sq[i].heard) { body_t = sq[i].t; body_amp = 0.25f + 0.1f * sq[i].g; }
            sq[i] = sq[--nsq];
        } else i++;
    }
    // the exact frame (a float t is half a sample off by now): the block's start, counted on
    static uint32_t blk = 0xffffffffu; static int in_blk;
    if (audio_frames() != blk) { blk = audio_frames(); in_blk = 0; } else in_blk++;
    long tk = (long)((blk + (uint32_t)in_blk) / (AUDIO_SR / 100));   // each tick's ten readings, from the tick's own start
    if (tk != last_tick) { last_tick = tk; burst_fill((double)tk * 0.01); }
    int idx = (int)(t * AUDIO_SR) - AUDIO_BLOCK - 1;
    float back = couple > 0 && idx >= 0 && idx < sim_n ? couple * sim_out[idx] : 0.0f;
    float x = 0;
    for (int s = 0; s < NSTR; s++) x += string_run(&str[s], back * 0.004f);
    x *= 0.6f;
    if (howl_g > 0.0f) {
        if (howl_set_hz != howl_hz) { biquad_set(&howl_bp, BQ_BP, howl_hz, 8.0f, 0.0f); howl_set_hz = howl_hz; }
        float o = idx >= 0 && idx < sim_n ? sim_out[idx] : 0.0f;
        x += howl_g * biquad_run(&howl_bp, o);
    }
    if (body_t >= 0 && t >= body_t && t < body_t + 0.25f) {
        float d = t - body_t;
        x += body_amp * (0.8f * sinf(6.2831853f * 105.0f * d) * expf(-d / 0.05f) + 0.5f * grnd() * expf(-d / 0.006f));
    }
    for (int k = 0; k < nclk; k++) {                              // a button's click: a 4 ms snap, a little ring of plastic
        float d = t - clk_t[k];
        if (d >= 0.0f && d < 0.012f) x += 0.45f * grnd() * expf(-d / 0.0012f) + 0.25f * sinf(6.2831853f * 3200.0f * d) * expf(-d / 0.003f);
    }
    if (nclk > 24) { memmove(clk_t, clk_t + 8, sizeof(float) * (size_t)(nclk - 8)); nclk -= 8; }
    if (hum_amp > 0) {
        hum_ph += hum_hz / (float)AUDIO_SR; if (hum_ph >= 1.0f) hum_ph -= 1.0f;
        if (hum_kind == 0) x += hum_amp * (sinf(6.2831853f * hum_ph) + 0.7f * sinf(6.2831853f * hum_ph * 1.4983f) + 0.5f * sinf(6.2831853f * 2.0f * hum_ph));
        else if (hum_kind == 1) x += hum_amp * sinf(6.2831853f * hum_ph);
        else x += hum_amp * (2.0f * hum_ph - 1.0f);
    }
    return x + 0.0005f * grnd();
}
static void slap_at(float t, int top, float g, bool heard)
{
    if (nsq < 16) sq[nsq++] = (slap_ev_t){ t, g, top, heard };
    if (njq < 16) jq[njq++] = (jolt_ev_t){ t, g, top ? (vec3_t){ 0, 0, -1 } : (vec3_t){ 0.8f, -0.6f, 0 } };
}
static void quiet(void) { for (int s = 0; s < NSTR; s++) str[s].n = 0; hum_amp = 0; npq = 0; nsq = 0; njq = 0; body_t = -1; couple = 0; howl_g = 0; }

static void run(float s) { int n = (int)(s * 100.0f + 0.5f); for (int i = 0; i < n; i++) sim_tick(); }
static void run_until(float t) { while (sim_t < t) sim_tick(); }
// the REC button is on the guitar's top: the mic hears it click going down and coming up
static void click_at(float t) { if (rec_clicks && nclk < 32) clk_t[nclk++] = t; }
static void rec_tap(void) { click_at(sim_t); SI.rec = true; run(0.08f); click_at(sim_t); SI.rec = false; sim_tick(); }
static void rec_hold(float s) { click_at(sim_t); SI.rec = true; run(s); click_at(sim_t); SI.rec = false; sim_tick(); }
// the largest step between two samples of what was played: a click is a jump the strings never make
static float max_step(float t0, float t1)
{
    float m = 0;
    for (int i = (int)(t0 * AUDIO_SR) + 1; i < (int)(t1 * AUDIO_SR) && i < sim_n; i++) { float d = fabsf(sim_out[i] - sim_out[i - 1]); if (d > m) m = d; }
    return m;
}
static int preset_index(const char *name) { for (int i = 0; i < INST.npresets; i++) if (!strcmp(INST.presets[i].name, name)) return i; return 0; }
static void load(const char *name) { front_midi(0xC0, (uint8_t)preset_index(name), 0); run(0.2f); }
static float amp_at(float t0, float t1, float f) { return 2.0f * sqrtf(m_goertzel(t0, t1, f)); }
// a band 40 Hz wide round f: a delay line shifter's output is f with sidebands its window's rate off
static float band_at(float t0, float t1, float f) { float e = 0; for (int k = -10; k <= 10; k++) { float a = amp_at(t0, t1, f + 2.0f * (float)k); e += a * a; } return sqrtf(e); }
static float mic_amp_at(float t0, float t1, float f)
{
    int a = (int)(t0 * AUDIO_SR), b = (int)(t1 * AUDIO_SR);
    double w = 2 * M_PI * f / AUDIO_SR, c = 2 * cos(w), s1 = 0, s2 = 0;
    for (int i = a; i < b && i < sim_n; i++) { double s0 = sim_mic[i] + c * s1 - s2; s2 = s1; s1 = s0; }
    double p = s1 * s1 + s2 * s2 - c * s1 * s2;
    return 2.0f * (float)sqrt(p / ((double)(b - a) * (b - a) + 1));
}
// the first attack in what was played after t0: a fast envelope jumping over a slow one
static float onset_after(float t0, float t1)
{
    float fast = 0, slow = 0;
    int a = (int)(t0 * AUDIO_SR), b = (int)((t0 + 0.05f) * AUDIO_SR);         // start both where the sound already is
    for (int i = a; i < b && i < sim_n; i++) slow += fabsf(sim_out[i]);
    slow /= (float)(b - a); fast = slow;
    for (int i = (int)(t0 * AUDIO_SR); i < (int)(t1 * AUDIO_SR) && i < sim_n; i++) {
        float a = fabsf(sim_out[i]);
        fast += (a > fast ? 0.3f : 0.003f) * (a - fast);
        slow += 0.0005f * (a - slow);
        if (i > (int)((t0 + 0.05f) * AUDIO_SR) && fast > 2.5f * slow + 0.01f) return (float)i / AUDIO_SR;
    }
    return -1;
}
static int notes_on(float t0, float t1, int ch, int *out, int max)
{
    int n = 0;
    for (int i = 0; i < midi_n && n < max; i++)
        if (midi_log[i].st == (0x90 | (ch - 1)) && midi_log[i].d2 > 0 && midi_log[i].t >= t0 && midi_log[i].t < t1) out[n++] = midi_log[i].d1;
    return n;
}
static void sit(void) { pose(40, 0); }

void inst_excite(void)
{
    quiet();
    hum_amp = 0.05f; hum_kind = 0; hum_hz = 110.0f;
    SI.mic = mic;
    sit();
}
extern bool ear_auto_setup;
// NECK SETUP as a player does it: sit still, lift the neck and hold it, back, tip the top up and hold it
static void do_setup(float lift, float tip)
{
    sit(); run(1.3f);                                              // still: home
    run(0.7f);
    for (int i = 1; i <= 10; i++) { move(40.0f + lift * (float)i / 10.0f, 0, 40.0f); run(0.04f); }
    pose(40.0f + lift, 0); run(0.8f);                              // held there
    for (int i = 9; i >= 0; i--) { move(40.0f + lift * (float)i / 10.0f, 0, -40.0f); run(0.03f); }
    pose(40, 0); run(0.6f);                                        // back home, still
    if (tip > 0.0f) {
        for (int i = 1; i <= 10; i++) { move(40.0f, tip * (float)i / 10.0f, 30.0f); run(0.04f); }
        pose(40.0f, tip); run(0.7f);
        pose(40, 0);
    }
    run(3.0f);                                                     // DONE shows, then play
}

// ---------------------------------------------------------------- the neck
static void neck_tests(void)
{
    section("neck");
    pose_psi = 30.0f; pose_lefty = false;
    // the first time on a guitar, standing up and resting: NECK SETUP starts by itself; stopped, it is not asked again
    ear_auto_setup = true;
    sit();
    for (int i = 0; i < 1500 && !ear_setup_state(); i++) sim_tick();
    check(ear_setup_state() == SU_STILL, "the first time it rests standing up, NECK SETUP starts by itself");
    sim_shot("setup_first");
    SI.rec = true; sim_tick(); SI.rec = false; run(0.2f);
    run(5.0f);
    check(ear_setup_state() == SU_OFF && (int)pv[P_AXSET] == 2, "REC stops it, and it does not ask again (%d)", (int)pv[P_AXSET]);
    ear_auto_setup = false;
    sit(); run(2.0f);
    check(NK.ok && fabsf(NK.neck) < 1.0f && NK.upright, "stuck on the top, turned 30 degrees, resting: home is here by itself (neck %.1f, upright %d)", (double)NK.neck, NK.upright);
    pose(60, 0); run(0.8f);
    float up = NK.neck, nu = NK.n;
    pose(25, 0); run(0.8f);
    float dn = NK.neck, nd = NK.n;
    check(fabsf(up - 20.0f) < 1.5f && fabsf(nu - 1.0f) < 0.02f, "the neck lifted 20 degrees reads %.1f, all the way (%.2f)", (double)up, (double)nu);
    check(fabsf(dn + 15.0f) < 1.5f && fabsf(nd + 12.0f / 17.0f) < 0.05f, "lowered 15 degrees reads %.1f (%.2f of the way down)", (double)dn, (double)nd);
    pose(41.5f, 0); run(0.8f);
    check(NK.n == 0.0f, "1.5 degrees off home is inside the STILL ZONE: nothing (%.2f)", (double)NK.n);
    pose(40, 15); run(0.8f);
    check(fabsf(NK.lean - 15.0f) < 1.5f && fabsf(NK.neck) < 1.5f, "the top tipped up 15 degrees: lean %.1f, the neck stays %.1f", (double)NK.lean, (double)NK.neck);
    sit(); run(0.8f);
    // a slap's jolt does not move the neck
    float before = NK.neck, worst = 0;
    slap_at(sim_t + 0.05f, 1, 2.5f, true);
    for (int i = 0; i < 30; i++) { sim_tick(); worst = fmaxf(worst, fabsf(NK.neck - before)); }
    check(worst < 2.0f, "a 2.5 g slap moves the neck reading %.2f degrees at most", (double)worst);
    run(0.5f);
    // a left-handed guitar: the screen's normal is the axis, but lifting reads down until NECK SETUP
    pose_lefty = true; sit(); run(0.5f);
    sim_hold_knob(); run(0.3f);
    pose(60, 0); run(0.8f);
    float lefty = NK.neck;
    check(lefty < -15.0f, "a left-handed guitar, home set with the knob: lifting reads %.1f before NECK SETUP", (double)lefty);
    sit(); run(0.5f);
    param_set(P_LEAN, LEAN_FLIP);                                  // a wrong lean, to see the setup put it right
    INST.action(P_SETUP); run(0.1f);
    check(ear_setup_state() == SU_STILL, "NECK SETUP waits for you to sit still");
    sim_gif_begin("setup");
    run(1.3f); sim_shot("setup_still");
    check(ear_setup_state() >= SU_HOME, "still a second: home taken (step %d)", ear_setup_state());
    run(0.7f);
    check(ear_setup_state() == SU_LIFT, "then: lift the neck (step %d)", ear_setup_state());
    for (int i = 1; i <= 10; i++) { move(40.0f + 2.2f * (float)i, 0, 50.0f); run(0.04f); }
    sim_shot("setup_lift");
    pose(62, 0); run(0.8f);
    check(ear_setup_state() == SU_BACK, "lifted 22 and held: back to where you sit (step %d)", ear_setup_state());
    for (int i = 9; i >= 0; i--) { move(40.0f + 2.2f * (float)i, 0, -50.0f); run(0.03f); }
    pose(40, 0); run(0.6f);
    check(ear_setup_state() == SU_TIP, "back home: tip the top up (step %d)", ear_setup_state());
    for (int i = 1; i <= 10; i++) { move(40.0f, 1.4f * (float)i, 35.0f); run(0.04f); }
    sim_shot("setup_tip");
    pose(40, 14); run(0.7f);
    sim_shot("setup_done");
    check(ear_setup_state() == SU_DONE, "tipped 14 and held: done (step %d)", ear_setup_state());
    pose(40, 0); run(3.0f);
    sim_gif_end();
    check(ear_setup_state() == SU_OFF && (int)pv[P_RANGE] == 19 && (int)pv[P_LRANGE] == 12 && (int)pv[P_LEAN] == LEAN_NORMAL,
          "it learned NECK RANGE 19 from a 22 lift (%d), LEAN RANGE 12 from a 14 tip (%d), and which way the lean goes (%d)", (int)pv[P_RANGE], (int)pv[P_LRANGE], (int)pv[P_LEAN]);
    pose(60, 0); run(0.8f);
    float l2 = NK.neck, ln2 = NK.n;
    pose(25, 0); run(0.8f);
    float l3 = NK.neck;
    pose(40, 12); run(0.8f);
    check(fabsf(l2 - 20.0f) < 1.5f && fabsf(l3 + 15.0f) < 1.5f && ln2 > 0.99f, "after it the left-handed lift reads %.1f (all the way), lowered %.1f", (double)l2, (double)l3);
    check(NK.lean > 10.5f && NK.l > 0.99f, "and the top tipped up 12 reads %.1f, all the way", (double)NK.lean);
    sit(); run(0.5f);
    // any way round on the top: NECK SETUP finds the axis
    pose_lefty = false;
    const float psis[4] = { 0.0f, 75.0f, 160.0f, 250.0f };
    int good = 0;
    for (int k = 0; k < 4; k++) {
        pose_psi = psis[k]; sit(); run(0.6f);
        INST.action(P_SETUP); run(0.1f);
        do_setup(18.0f, 10.0f);
        pose(58, 0); run(0.8f);
        float a = NK.neck;
        pose(30, 0); run(0.8f);
        float b = NK.neck;
        pose(40, 8); run(0.8f);
        if (fabsf(a - 18.0f) < 1.5f && fabsf(b + 10.0f) < 1.5f && NK.lean > 6.5f) good++;
        else note("turned %.0f: lifted %.1f, lowered %.1f, tipped %.1f", (double)psis[k], (double)a, (double)b, (double)NK.lean);
    }
    check(good == 4, "the board turned 0, 75, 160 or 250 degrees on the top: after NECK SETUP lifting 18 reads 18, lowering 10 reads -10, tipping reads up (%d of 4)", good);
    // standing straight up with the stick at the bottom, by the bridge, facing the room (the user's board, 24 Oct)
    pose_psi = 90.0f - 40.0f;                                      // the board's x along the strings' normal... upright at home
    sit(); run(0.6f); INST.action(P_SETUP); run(0.1f); do_setup(20.0f, 12.0f);
    pose(60, 0); run(0.8f);
    float v1 = NK.neck;
    pose(40, 0); run(0.8f);
    check(fabsf(v1 - 20.0f) < 1.5f, "the board standing up by the bridge: after NECK SETUP the lift reads %.1f", (double)v1);
    pose_psi = 30.0f; sit(); run(0.6f);
    INST.action(P_SETUP); run(0.1f); do_setup(20.0f, 12.0f);
    param_set(P_RANGE, 20); param_set(P_LRANGE, 15);
    // the knob held is home again, wherever you are
    pose(50, 0); run(0.6f);
    sim_hold_knob(); run(0.3f);
    float h0 = NK.neck;
    sit(); run(0.8f);
    check(fabsf(h0) < 1.0f && fabsf(NK.neck + 10.0f) < 1.5f, "sitting with the neck 10 up, the knob held: that is home (%.1f); back down reads %.1f", (double)h0, (double)NK.neck);
    pose(40, 0); run(0.3f); sim_hold_knob(); run(0.5f);
}

// ---------------------------------------------------------------- the looper
static float riff(float t1)                     // four notes half a second apart: A2, D3, E3, G3
{
    pluck_at(t1, 1, 0, 0.5f); pluck_at(t1 + 0.5f, 2, 0, 0.45f); pluck_at(t1 + 1.0f, 2, 2, 0.45f); pluck_at(t1 + 1.5f, 3, 0, 0.45f);
    return t1;
}
static void free_loop(void) { param_set(P_LBARS, 0); param_set(P_METRO, MET_OFF); param_set(P_START, ST_NOTE); }
static void loop_tests(void)
{
    section("looper");
    quiet(); load("CLEAN"); sit(); free_loop(); run(0.5f);
    sim_gif_begin("loop");
    rec_tap();
    check(loop_state() == L_ARMED, "REC tapped: armed, waiting for a note");
    run(0.6f);
    sim_shot("armed");
    check(loop_state() == L_ARMED, "the button's click going down and coming up does not start it, nor 0.6 s of a quiet room");
    float t1 = riff(sim_t + 0.5f);
    run_until(t1 + 0.2f);
    check(loop_state() == L_REC, "the first note starts the take");
    run_until(t1 + 1.2f); sim_shot("recording");
    run_until(t1 + 2.0f);
    float tclose = sim_t;
    rec_tap();
    run(0.1f);
    float L = loop_len_s();
    check(loop_state() == L_PLAY && fabsf(L - (tclose - t1)) < 0.015f, "REC tapped on the next one: it loops, %.3f s long (%.3f from the note to the tap)", (double)L, (double)(tclose - t1));
    mute();
    run_until(t1 + 3.0f * L + 0.3f);
    sim_shot("looping");
    float a2 = onset_after(t1 + 2.0f * L - 0.1f, t1 + 2.0f * L + 0.2f), a3 = onset_after(t1 + 3.0f * L - 0.1f, t1 + 3.0f * L + 0.2f);
    check(a2 > 0 && fabsf(a2 - (t1 + 2.0f * L)) < 0.012f && a3 > 0 && fabsf(a3 - (t1 + 3.0f * L)) < 0.012f,
          "the first note comes back a loop later and again: %.1f and %.1f ms off", (double)((a2 - t1 - 2.0f * L) * 1000.0f), (double)((a3 - t1 - 3.0f * L) * 1000.0f));
    {   // the seam: 20 ms before each pass's first note
        float worst = 0;
        for (int k = 2; k <= 3; k++) { float r = m_click(t1 + (float)k * L - 0.0205f, NULL, NULL); if (r > worst) worst = r; }
        check(worst < 3.0f, "no click where the loop goes round (%.2f)", (double)worst);
    }
    float live = max_step(t1 - 0.01f, tclose - 0.05f), pass = max_step(t1 + 2.0f * L - 0.1f, t1 + 3.0f * L - 0.1f);
    check(pass < 1.3f * live + 0.01f, "the button's clicks are nowhere in the loop: its biggest step %.3f, the strings' own %.3f", (double)pass, (double)live);
    float a110 = amp_at(t1 + 3.0f * L + 0.02f, t1 + 3.0f * L + 0.45f, 110.0f), live110 = amp_at(t1 + 0.02f, t1 + 0.45f, 110.0f);
    check(a110 > 0.6f * live110, "the loop plays the riff by itself (A2 at %.4f, %.4f when it was played)", (double)a110, (double)live110);
    // a layer from just after the let-go: B3 between the notes
    float P = t1 + 4.0f * L + 0.25f;
    run_until(P);
    rec_tap();
    pluck_at(P + 0.15f, 4, 0, 0.5f);
    run_until(P + 0.7f); mute();
    sim_shot("adding");
    run_until(P + 1.0f);
    rec_tap();
    run(0.1f);
    check(loop_state() == L_PLAY && loop_layers() == 2, "tap: adding; tap: playing, two layers (state %d, %d layers)", loop_state(), loop_layers());
    run_until(P + L + 0.6f);
    float b_before = amp_at(P - L + 0.16f, P - L + 0.55f, 246.94f), b_after = amp_at(P + L + 0.16f, P + L + 0.55f, 246.94f);
    check(b_after > 6.0f * b_before && b_after > 0.004f, "the next pass has the B3 where it was played (%.4f, before %.4f)", (double)b_after, (double)b_before);
    run_until(P + 2.0f * L + 0.5f);
    float pass2 = max_step(P + L - 0.1f, P + 2.0f * L - 0.1f);
    check(pass2 < 1.3f * live + 0.01f, "the four clicks of the two taps are not in the layer either (biggest step %.3f)", (double)pass2);
    sim_gif_end();
    // UNDO takes it off
    INST.action(P_UNDO); run(0.4f);
    float pu = P + 4.0f * L;
    run_until(pu + 0.6f);
    float b_undo = amp_at(pu + 0.16f, pu + 0.55f, 246.94f);
    check(loop_layers() == 1 && b_undo < 2.0f * b_before + 0.002f, "UNDO: one layer, the B3 gone (%.4f)", (double)b_undo);
    check(m_click(pu - 2.0f * L, NULL, NULL) < 3.0f && m_bad() == 0, "UNDO without a click");
    INST.action(P_UNDO); run(0.3f);
    check(loop_layers() == 1 && !loop_can_undo(), "a second UNDO: nothing more to take off");
    // hold: stop; tap: from the start; hold, hold: erased
    rec_hold(0.7f); run(0.2f);
    check(loop_state() == L_STOP, "REC held: stopped (state %d)", loop_state());
    float ts = sim_t;
    run(0.4f);
    check(m_rms(ts + 0.1f, sim_t) < 0.002f, "stopped: silence (rms %.4f)", (double)m_rms(ts + 0.1f, sim_t));
    float tp = sim_t; rec_tap(); run(0.4f);
    float ap = onset_after(tp - 0.05f, tp + 0.3f);
    check(loop_state() == L_PLAY && ap > 0 && ap - tp < 0.045f, "tapped: it plays from the start, the first note %.0f ms after the press", (double)((ap - tp) * 1000.0f));
    rec_hold(0.7f); run(0.1f); rec_hold(0.7f); run(0.2f);
    check(loop_state() == L_EMPTY, "held to stop, held again: erased (state %d)", loop_state());
    // a take shorter than a quarter second is a slip; held while recording, a take is dropped
    rec_tap(); pluck_at(sim_t + 0.9f, 1, 0, 0.5f); run(1.0f); rec_tap(); run(0.1f);
    check(loop_state() == L_EMPTY, "a take of 0.1 s: no loop (state %d)", loop_state());
    mute(); run(0.3f);
    rec_tap(); pluck_at(sim_t + 0.9f, 1, 0, 0.5f); run(1.6f); rec_hold(0.7f); run(0.1f);
    check(loop_state() == L_EMPTY, "REC held while recording: the take is dropped (state %d)", loop_state());
    // AT ONCE: no waiting for a note (50 ms after the tap: its click is before)
    mute(); param_set(P_START, ST_NOW); run(0.3f);
    float tn = sim_t; rec_tap();
    run(0.05f);
    check(loop_state() == L_REC, "START AT ONCE: recording from just after the let-go");
    run_until(tn + 1.5f);
    float tc = sim_t; rec_tap(); run(0.1f);
    check(loop_state() == L_PLAY && fabsf(loop_len_s() - (tc - tn - 0.135f)) < 0.015f, "a loop from 25 ms after one tap's let-go to 20 ms before the next (%.3f)", (double)loop_len_s());
    rec_hold(2.2f); run(0.2f);
    check(loop_state() == L_EMPTY, "REC held 2 s while playing: stopped, then erased");
    // UNDO needs the loop to fit twice: a 12 s loop cannot
    tn = sim_t; rec_tap(); run(12.0f); rec_tap(); run(0.2f);
    rec_tap(); pluck_at(sim_t + 0.15f, 1, 0, 0.4f); run(0.5f); rec_tap(); run(0.2f);
    check(loop_layers() == 2 && !loop_can_undo() && !loop_undo_fits(), "a 12 s loop with a layer: too long to undo (%.1f s; UNDO up to %.0f s)", (double)loop_len_s(), (double)(loop_max_s() / 2.0f));
    rec_hold(2.2f); run(0.2f);
    // and the longest take closes by itself
    mute(); tn = sim_t; rec_tap(); run(loop_max_s() + 0.5f);
    check(loop_state() == L_PLAY && loop_len_s() > loop_max_s() - 0.05f, "a take as long as the buffer closes by itself (%.2f s)", (double)loop_len_s());
    rec_hold(2.2f); run(0.2f);

    // ---- bars and the metronome
    section("bars and the metronome");
    quiet(); param_set(P_LBARS, 3); param_set(P_TEMPO, 120); param_set(P_BEATS, 4); param_set(P_METRO, MET_SOUND); param_set(P_START, ST_NOTE); run(0.3f);
    sim_gif_begin("bars");
    float tarm = sim_t; rec_tap();
    float g0 = tarm + 0.01f + 0.8f;                               // the grid's first beat: 0.8 s after the press was seen
    run_until(g0 + 1.6f);
    sim_shot("bars_armed");
    float tick1 = amp_at(g0 + 1.0f, g0 + 1.03f, 2100.0f) + amp_at(g0 + 1.5f, g0 + 1.53f, 1500.0f);
    check(loop_state() == L_ARMED && tick1 > 0.02f, "REC tapped: the metronome counts a bar in (%.3f), the bar's first tick higher", (double)tick1);
    float line = g0 + 2.0f;                                       // a bar of count-in, then the take; the riff starts 30 ms late
    riff(line + 0.03f); pluck_at(line + 2.03f, 1, 0, 0.5f); pluck_at(line + 4.03f, 1, 0, 0.5f); pluck_at(line + 6.03f, 1, 0, 0.5f);
    run_until(line - 0.05f);
    check(loop_state() == L_ARMED, "a bar of count-in: not recording yet");
    run_until(line + 1.0f); sim_shot("bars_rec");
    int of; int bar1 = loop_bar(&of);
    check(loop_state() == L_REC && bar1 == 1 && of == 4, "then it records from the bar line: bar %d of %d", bar1, of);
    run_until(line + 7.9f);
    check(loop_state() == L_REC, "still recording at 7.9 s");
    run_until(line + 8.1f);
    check(loop_state() == L_PLAY && fabsf(loop_len_s() - 8.0f) < 0.001f, "four bars at 120 and it loops by itself: %.4f s, to the sample", (double)loop_len_s());
    mute();
    run_until(line + 16.3f);
    sim_shot("bars_play");
    float on = onset_after(line + 15.8f, line + 16.2f);
    check(on > 0 && fabsf(on - (line + 16.03f)) < 0.012f, "the loop starts on the beat: the first note again two loops on, %.1f ms off", (double)((on - line - 16.03f) * 1000.0f));
    float ticks = amp_at(line + 8.0f, line + 8.03f, 2100.0f) + amp_at(line + 10.0f, line + 10.03f, 2100.0f) + amp_at(line + 9.0f, line + 9.03f, 1500.0f);
    check(ticks < 0.2f * tick1, "the metronome is not in the loop (%.4f where it ticked while recording)", (double)ticks);
    sim_gif_end();
    rec_hold(2.2f); run(0.3f);
    // FREE with the metronome: the close goes to the nearest bar line
    param_set(P_LBARS, 0); run(0.2f);
    tarm = sim_t; rec_tap(); g0 = tarm + 0.81f;
    line = g0 + 2.0f;
    pluck_at(line + 0.01f, 1, 0, 0.5f);
    run_until(line + 4.3f);                                       // tapped 0.3 s after two bars
    rec_tap(); run(0.2f);
    check(loop_state() == L_PLAY && fabsf(loop_len_s() - 4.0f) < 0.001f, "LENGTH FREE with the metronome: tapped 0.3 s late, two bars exactly (%.4f s)", (double)loop_len_s());
    rec_hold(2.2f); run(0.3f); mute();
    // COUNT-IN: ticks before the take, none in it (so a speaker the mic hears puts none in the loop)
    param_set(P_METRO, MET_COUNT); param_set(P_LBARS, 1); run(0.2f);
    tarm = sim_t; rec_tap(); g0 = tarm + 0.81f;
    run_until(g0 + 3.5f);
    float before = amp_at(g0 + 1.0f, g0 + 1.03f, 2100.0f) + amp_at(g0 + 1.5f, g0 + 1.53f, 1500.0f);
    float during = amp_at(g0 + 2.5f, g0 + 2.53f, 1500.0f) + amp_at(g0 + 3.0f, g0 + 3.03f, 1500.0f);
    check(loop_state() == L_REC && before > 0.02f && during < 0.1f * before, "METRONOME COUNT-IN: ticks before the take (%.3f), none during it (%.4f)", (double)before, (double)during);
    run_until(g0 + 4.1f);
    rec_hold(2.2f); run(0.3f);
    // AT ONCE with the metronome is the same: a bar of count-in, then it records from the bar line
    param_set(P_METRO, MET_SOUND); param_set(P_START, ST_NOW); param_set(P_LBARS, 1); run(0.2f);
    tarm = sim_t; rec_tap(); g0 = tarm + 0.81f;
    run_until(g0 + 1.0f);
    check(loop_state() == L_ARMED, "START AT ONCE with the metronome: a bar of count-in first");
    sim_shot("count_in");
    run_until(g0 + 2.1f);
    check(loop_state() == L_REC, "then it records");
    run_until(g0 + 4.1f);
    check(loop_state() == L_PLAY && fabsf(loop_len_s() - 2.0f) < 0.001f, "one bar, closed by itself (%.4f s)", (double)loop_len_s());
    rec_hold(2.2f); run(0.3f);
    // LIGHT: the beat on the LED, nothing in the sound
    param_set(P_METRO, MET_LIGHT); param_set(P_START, ST_NOTE); run(0.2f);
    tarm = sim_t; rec_tap(); g0 = tarm + 0.81f;
    int flashes = 0; uint8_t prev = 0;
    while (sim_t < g0 + 1.6f) { sim_tick(); if (sim_led[1] > 0 && prev == 0) flashes++; prev = sim_led[1]; }   // white (the armed blink is red)
    float lt = amp_at(g0 + 1.0f, g0 + 1.03f, 2100.0f) + amp_at(g0 + 1.5f, g0 + 1.53f, 1500.0f);
    check(flashes >= 3 && lt < 0.005f, "METRONOME LIGHT: the LED flashes the beats (%d), no tick in the sound (%.4f)", flashes, (double)lt);
    rec_tap(); run(0.3f);
    check(loop_state() == L_EMPTY, "REC tapped again: not armed, and its own click did not start a take (state %d)", loop_state());
    free_loop();
    nclk = 0;

    // TAPE: the neck plays the loop faster, slower, backwards
    section("looper on the neck");
    quiet(); load("TAPE LOOP"); free_loop(); param_set(P_START, ST_NOW); run(0.3f);
    hum_kind = 1; hum_hz = 110.0f; hum_amp = 0.1f;
    rec_tap(); run(1.5f); rec_tap(); run(0.1f);
    hum_amp = 0; run(0.5f);
    float f1 = m_pitch_at(sim_t - 0.4f, sim_t, 50, 500);
    pose(60, 0); run(1.0f);
    float f2 = m_pitch_at(sim_t - 0.4f, sim_t, 50, 500);
    check(fabsf(f1 - 110.0f) < 2.0f && fabsf(f2 - 220.0f) < 4.0f && fabsf(loop_rate_now() - 2.0f) < 0.02f, "TAPE: home plays 1x (%.1f Hz), the neck lifted 20 plays 2x (%.1f Hz)", (double)f1, (double)f2);
    pose(40.0f - 11.5f, 0); run(1.0f);
    check(fabsf(loop_rate_now()) < 0.03f, "half way down the tape stands still (%.2fx)", (double)loop_rate_now());
    pose(20, 0); run(1.0f);
    float f3 = m_pitch_at(sim_t - 0.4f, sim_t, 50, 500);
    check(fabsf(loop_rate_now() + 1.0f) < 0.02f && fabsf(f3 - 110.0f) < 3.0f, "all the way down: backwards at 1x (%.2fx, %.1f Hz)", (double)loop_rate_now(), (double)f3);
    sim_shot("tape_backwards");
    sit(); run(0.6f);
    load("CLEAN"); run(0.5f);                                     // BY THE NECK: LEVEL
    float lv0 = m_rms(sim_t - 0.4f, sim_t);
    pose(20, 0); run(0.8f);
    float lv1 = m_rms(sim_t - 0.4f, sim_t);
    check(lv1 < 0.05f * lv0, "CLEAN rides the loop's level: the neck down, it is gone (%.4f from %.4f)", (double)lv1, (double)lv0);
    sit(); run(0.5f);
    rec_hold(2.2f); run(0.2f);
    param_set(P_START, ST_NOTE);
}

// ---------------------------------------------------------------- the modes
static void cloud_tests(void)
{
    section("CLOUD");
    quiet(); load("CLEAN"); sit(); run(0.5f);
    float t0 = sim_t + 0.1f;
    pluck_at(t0, 1, 0, 0.5f); pluck_at(t0 + 0.02f, 3, 2, 0.4f); pluck_at(t0 + 0.04f, 4, 2, 0.4f);   // an A chord
    run(1.5f); mute(); run(1.1f);
    float clean_tail = m_rms(sim_t - 0.8f, sim_t);
    quiet(); load("CLOUD"); run(0.5f);
    sim_gif_begin("cloud");
    t0 = sim_t + 0.1f;
    pluck_at(t0, 1, 0, 0.5f); pluck_at(t0 + 0.02f, 3, 2, 0.4f); pluck_at(t0 + 0.04f, 4, 2, 0.4f);
    run(1.5f); mute(); run(0.3f);
    sim_shot("cloud");
    run(0.8f);
    float tail = m_rms(sim_t - 0.8f, sim_t);
    check(tail > 4.0f * clean_tail, "the strings stopped: the cloud goes on from what was played (rms %.4f, %.4f in CLEAN)", (double)tail, (double)clean_tail);
    uint32_t g0 = eng_grains; run(1.0f); uint32_t lo = eng_grains - g0;
    pose(60, 0); run(0.3f);
    g0 = eng_grains; run(1.0f); uint32_t hi = eng_grains - g0;
    sim_shot("cloud_up");
    check(hi > 2u * lo && lo > 5u, "the neck lifted: a thicker cloud (%u grains a second, %u at home)", (unsigned)hi, (unsigned)lo);
    sit(); run(0.5f);
    // a slap freezes it while the chord rings: then a new note does not go in
    t0 = sim_t + 0.1f;
    pluck_at(t0, 1, 0, 0.5f); pluck_at(t0 + 0.02f, 3, 2, 0.4f); pluck_at(t0 + 0.04f, 4, 2, 0.4f);
    run(1.0f);
    for (int k = 0; k < 6; k++) pluck_at(sim_t + 0.05f * (float)k, k, 2, 0.6f);   // a hard strum: not a freeze
    run(0.4f);
    check(!E.c_frozen, "a hard strum does not freeze it");
    SI.jx = 0.9f; sim_tick(); SI.jx = 0; run(0.3f);
    check(E.c_frozen && eng_frozen, "the stick pushed: frozen");
    sim_shot("frozen");
    mute(); run(0.2f);
    pluck_at(sim_t, 5, 3, 0.5f); run(0.8f); mute(); run(1.3f);   // a G4 while frozen
    float g4 = band_at(sim_t - 0.8f, sim_t, 392.0f) + band_at(sim_t - 0.8f, sim_t, 784.0f);
    float a2 = band_at(sim_t - 0.8f, sim_t, 110.0f) + band_at(sim_t - 0.8f, sim_t, 220.0f) + band_at(sim_t - 0.8f, sim_t, 277.2f);
    float fr = m_rms(sim_t - 0.8f, sim_t);
    check(a2 > 2.0f * g4 && fr > 4.0f * clean_tail, "frozen: the cloud is still the chord from before (A %.4f), not the G4 played since (%.4f); rms %.4f", (double)a2, (double)g4, (double)fr);
    SI.jy = -0.8f; sim_tick(); SI.jy = 0; run(0.3f);
    check(!E.c_frozen, "pushed again, any way: it flows again");
    param_set(P_CFREEZE, FRZ_SLAP); run(0.1f);
    slap_at(sim_t + 0.05f, 1, 2.5f, true); run(0.3f);
    check(E.c_frozen, "FREEZE BY A SLAP: a slap on the top freezes it");
    slap_at(sim_t + 0.05f, 0, 2.5f, true); run(0.3f);
    check(!E.c_frozen, "another slap lets it go");
    param_set(P_CFREEZE, FRZ_STICK);
    sim_gif_end();
    // HALO: the neck all the way up holds it
    load("HALO"); run(0.3f);
    pose(62, 0); run(0.5f);
    check(E.c_frozen, "HALO: the neck all the way up freezes it");
    sit(); run(0.5f);
    check(!E.c_frozen, "and down again lets it go");
}

static void bloom_tests(void)
{
    section("BLOOM");
    // the same A2 through CLEAN and BLOOM: what is left 4 s later
    quiet(); load("CLEAN"); sit(); run(0.5f);
    float t0 = sim_t + 0.1f; pluck_at(t0, 1, 0, 0.5f); run(4.6f);
    float clean = amp_at(t0 + 4.0f, t0 + 4.5f, 110.0f);
    quiet(); load("BLOOM"); run(0.5f);
    sim_gif_begin("bloom");
    t0 = sim_t + 0.1f; pluck_at(t0, 1, 0, 0.5f); run(1.5f); sim_shot("bloom"); run(3.1f);
    float bloom = m_rms(t0 + 4.0f, t0 + 4.5f), early = m_rms(t0 + 0.2f, t0 + 0.6f);
    check(bloom > 8.0f * clean && bloom > 0.1f * early, "an A2 4 s on: the string is gone (rms %.5f), the bloom holds it (%.4f, %.0f %% of the start)", (double)clean, (double)bloom, (double)(100.0f * bloom / early));
    // the neck up holds it for good; down chokes it
    pose(55, 0);
    quiet(); t0 = sim_t + 0.1f; pluck_at(t0, 1, 0, 0.5f); run(8.5f);
    float held = m_rms(t0 + 7.6f, t0 + 8.4f), at3 = m_rms(t0 + 3.0f, t0 + 3.8f);   // rms: the grains' phases make any one partial flutter
    check(held > 0.6f * at3, "the neck lifted: HOLD FOREVER, 8 s on it is still %.0f %% as loud as at 3 s", (double)(100.0f * held / at3));
    sim_shot("bloom_held");
    pose(22, 0); float tc = sim_t; run(0.9f);
    float choked = m_rms(tc + 0.6f, tc + 0.9f);
    check(choked < 0.1f * held, "the neck down: choked (%.5f, held %.4f)", (double)choked, (double)held);
    sim_gif_end();
    sit(); run(0.5f);
    // a slap chokes it too
    quiet(); t0 = sim_t + 0.1f; pluck_at(t0, 1, 0, 0.5f); run(1.8f);
    float s0 = eng_bloom_lvl; int caps = eng_bloom_caps;
    slap_at(sim_t, 1, 2.5f, true); run(0.9f);
    check(s0 > 0.5f && eng_bloom_lvl > 0.5f && eng_bloom_caps == caps, "a slap does not stop the held note (%.2f, %.2f after), and its thump is not taken as a note (%d caught, %d before)", (double)s0, (double)eng_bloom_lvl, eng_bloom_caps, caps);
    // HARMONIC: the octave comes in as it ages
    quiet(); load("HARMONICS"); run(0.3f);
    hum_kind = 1; hum_hz = 110.0f; t0 = sim_t; hum_amp = 0.15f; run(0.45f); hum_amp = 0; run(4.2f);   // a pure A2: any octave is the bloom's
    float h_oct = band_at(t0 + 3.6f, t0 + 4.4f, 220.0f) / fmaxf(band_at(t0 + 3.6f, t0 + 4.4f, 110.0f), 1e-6f);
    float h_early = band_at(t0 + 0.5f, t0 + 0.9f, 220.0f) / fmaxf(band_at(t0 + 0.5f, t0 + 0.9f, 110.0f), 1e-6f);
    check(h_oct > 0.5f && h_oct > 4.0f * h_early, "HARMONICS: the octave over an A2 grows as it is held (%.2f of the note, from %.2f)", (double)h_oct, (double)h_early);
    // a new note is caught
    quiet(); load("BLOOM"); run(0.3f);
    t0 = sim_t + 0.1f; pluck_at(t0, 1, 0, 0.5f); pluck_at(t0 + 1.5f, 2, 0, 0.5f); run(3.6f);
    float d3 = amp_at(sim_t - 0.4f, sim_t, 146.83f), a2 = amp_at(sim_t - 0.4f, sim_t, 110.0f);
    check(d3 > 2.0f * a2, "a new note, D3, takes over from the A2 (%.4f against %.4f)", (double)d3, (double)a2);
    // ON WOOD: a speaker on the top keeps the string itself ringing
    quiet(); load("BLOOM ON WOOD"); run(0.3f);
    t0 = sim_t + 0.1f; pluck_at(t0, 1, 0, 0.5f); run(4.0f);
    float alone = mic_amp_at(t0 + 3.4f, t0 + 3.9f, 110.0f);
    quiet(); couple = 1.0f; run(0.5f);
    t0 = sim_t + 0.1f; pluck_at(t0, 1, 0, 0.5f); run(4.0f);
    float rung = mic_amp_at(t0 + 3.4f, t0 + 3.9f, 110.0f);
    check(rung > 4.0f * alone && m_peak(t0, sim_t) < 0.9f, "BLOOM ON WOOD with a speaker on the top: the string itself still rings 3.5 s on (%.4f, alone %.5f)", (double)rung, (double)alone);
    quiet();
}

static void echo_tests(void)
{
    section("ECHO");
    quiet(); load("ECHO"); sit(); run(0.5f);
    sim_gif_begin("echo");
    float t0 = sim_t + 0.1f; pluck_at(t0, 3, 2, 0.5f); run(0.15f); mute(); run(2.0f);
    float et = pv[P_ETIME], dry = amp_at(t0, t0 + 0.12f, 220.0f), r1 = amp_at(t0 + et, t0 + et + 0.12f, 220.0f), r3 = amp_at(t0 + 3.0f * et, t0 + 3.0f * et + 0.12f, 220.0f), gap = amp_at(t0 + 1.5f * et, t0 + 1.5f * et + 0.1f, 220.0f);
    check(r1 > 0.25f * dry && r3 > 0.05f * r1 && r3 < 0.6f * r1 && gap < 0.5f * r1, "a short A3 (%.4f): repeats every %.2f s, dying (first %.4f, third %.4f, between %.4f)", (double)dry, (double)et, (double)r1, (double)r3, (double)gap);
    pose(60, 0); run(0.3f);
    t0 = sim_t + 0.1f; pluck_at(t0, 3, 2, 0.5f); run(0.15f); mute(); run(3.2f);
    sim_shot("echo_runaway");
    float late = amp_at(t0 + 3.0f, t0 + 3.2f, 220.0f) + amp_at(t0 + 3.0f, t0 + 3.2f, 110.0f) + m_rms(t0 + 2.8f, t0 + 3.2f);
    check(late > 0.01f && m_peak(t0, sim_t) < 0.99f, "the neck lifted: the repeats run away and go on 3 s later (%.4f), under full scale (%.2f)", (double)late, (double)m_peak(t0, sim_t));
    sit(); run(4.0f);
    float after = m_rms(sim_t - 0.5f, sim_t);
    check(after < 0.02f, "back home they die away (rms %.4f)", (double)after);
    sim_gif_end();
    // a sound change: the repeats ring out under CLEAN
    t0 = sim_t + 0.1f; pluck_at(t0, 3, 2, 0.5f); run(0.15f); mute(); run(0.05f);
    load("CLEAN");
    run(1.0f);
    float ring = m_rms(t0 + 2.0f * et - 0.02f, t0 + 2.0f * et + 0.08f), between = m_rms(t0 + 2.5f * et - 0.02f, t0 + 2.5f * et + 0.04f);
    check(ring > 0.004f && ring > 2.0f * between, "turned to CLEAN after a note: its repeats ring out (the second %.4f, between them %.4f)", (double)ring, (double)between);
    // ECHO SYNC: the time fits the loop
    load("DUB"); free_loop(); param_set(P_START, ST_NOW); run(0.2f);
    rec_tap(); run(2.0f); rec_tap(); run(0.3f);
    float L = loop_len_s();
    check(fabsf(E.e_time - L / 4.0f) < 0.002f, "DUB with a %.2f s loop: the echo is a quarter of it (%.3f s)", (double)L, (double)E.e_time);
    rec_hold(2.2f); run(0.2f); param_set(P_START, ST_NOTE);
}

static void wah_tests(void)
{
    section("WAH");
    quiet(); load("NECK WAH"); sit(); run(0.3f);
    hum_kind = 2; hum_hz = 110.0f; hum_amp = 0.12f;
    sim_gif_begin("wah");
    pose(22, 0); run(0.8f);
    float lo_lo = amp_at(sim_t - 0.5f, sim_t, 330.0f), lo_hi = amp_at(sim_t - 0.5f, sim_t, 2200.0f);
    sim_shot("wah_down");
    pose(58, 0); run(0.8f);
    float hi_lo = amp_at(sim_t - 0.5f, sim_t, 330.0f), hi_hi = amp_at(sim_t - 0.5f, sim_t, 2200.0f);
    sim_shot("wah_up");
    for (int i = 0; i < 20; i++) { pose(40.0f + 18.0f * sinf((float)i * 0.6f), 0); run(0.08f); }
    sim_gif_end();
    float r_dn = lo_hi / fmaxf(lo_lo, 1e-6f), r_up = hi_hi / fmaxf(hi_lo, 1e-6f);
    check(r_up > 10.0f * r_dn, "the neck is the pedal: 2.2 kHz over 330 Hz is %.2f lifted, %.3f lowered", (double)r_up, (double)r_dn);
    load("FUZZ WAH"); run(0.6f);
    check(m_rms(sim_t - 0.4f, sim_t) > 0.01f && m_peak(sim_t - 0.4f, sim_t) < 0.99f, "FUZZ WAH sounds, under full scale (peak %.2f)", (double)m_peak(sim_t - 0.4f, sim_t));
    quiet(); sit();
}

static void shimmer_tests(void)
{
    section("SHIMMER");
    quiet(); load("SHIMMER"); param_set(P_SHIM, 0); sit(); run(0.3f);
    hum_kind = 1; hum_hz = 220.0f; hum_amp = 0.1f;
    float t0 = sim_t; run(0.6f); hum_amp = 0; run(2.0f);
    float plain = band_at(t0 + 1.0f, t0 + 1.8f, 440.0f) / fmaxf(band_at(t0 + 1.0f, t0 + 1.8f, 220.0f), 1e-6f);
    load("SHIMMER"); run(2.0f);
    sim_gif_begin("shimmer");
    hum_amp = 0.1f; t0 = sim_t; run(0.6f); hum_amp = 0; run(2.0f);
    sim_gif_end();
    float shim = band_at(t0 + 1.0f, t0 + 1.8f, 440.0f) / fmaxf(band_at(t0 + 1.0f, t0 + 1.8f, 220.0f), 1e-6f);
    check(shim > 2.0f * plain, "SHIMMER: the room's tail rises an octave (440 over 220 is %.2f, %.2f without)", (double)shim, (double)plain);
    run(2.0f);
}

static void octave_tests(void)
{
    section("OCTAVE");
    quiet(); load("OCTAVES"); sit(); run(0.3f);
    hum_kind = 1; hum_hz = 220.0f; hum_amp = 0.1f;
    run(0.8f);
    float f = band_at(sim_t - 0.5f, sim_t, 220.0f), sub = band_at(sim_t - 0.5f, sim_t, 110.0f), up = band_at(sim_t - 0.5f, sim_t, 440.0f);
    check(sub > 0.2f * f && up > 0.15f * f, "an A3: its octave under (%.3f) and over (%.3f) beside it (%.3f)", (double)sub, (double)up, (double)f);
    pose(22, 0); run(0.8f);
    float sub2 = band_at(sim_t - 0.5f, sim_t, 110.0f);
    pose(58, 0); run(0.8f);
    float up2 = band_at(sim_t - 0.5f, sim_t, 440.0f);
    check(sub2 > 1.3f * sub && up2 > 1.5f * up, "the neck down brings the octave under up (%.3f), lifted the one over (%.3f)", (double)sub2, (double)up2);
    sit(); quiet();
}

static void wobble_tests(void)
{
    section("WOBBLE");
    quiet(); load("WOBBLE"); sit(); run(0.3f);
    hum_kind = 1; hum_hz = 440.0f; hum_amp = 0.1f;
    run(1.5f);
    float lo = 1e9f, hi = 0;
    for (int k = 0; k < 24; k++) { float p = m_pitch_at(sim_t - 1.2f + 0.05f * (float)k, sim_t - 1.2f + 0.05f * (float)k + 0.05f, 300, 600); if (p > 0) { lo = fminf(lo, p); hi = fmaxf(hi, p); } }
    float spread = (hi - lo) / 440.0f;
    pose(60, 0); run(1.5f);
    float lo2 = 1e9f, hi2 = 0;
    for (int k = 0; k < 24; k++) { float p = m_pitch_at(sim_t - 1.2f + 0.05f * (float)k, sim_t - 1.2f + 0.05f * (float)k + 0.05f, 300, 600); if (p > 0) { lo2 = fminf(lo2, p); hi2 = fmaxf(hi2, p); } }
    float spread2 = (hi2 - lo2) / 440.0f;
    check(spread > 0.004f && spread2 > 1.5f * spread, "an A4 wavers %.1f %%, %.1f %% with the neck lifted", (double)(100.0f * spread), (double)(100.0f * spread2));
    sit(); quiet();
}

static void slapset_tests(void)
{
    section("SLAP SETUP");
    quiet(); load("GOLPE"); sit(); run(0.3f);
    buzz = 1.6f;                                                   // a top that buzzes hard under the strumming hand
    INST.action(P_SLAPSET); run(0.1f);
    check(ear_slapset_state() == SS_STRUM, "SLAP SETUP: strum hard");
    sim_gif_begin("slapset");
    for (int k = 0; k < 30; k++) for (int st = 0; st < 6; st++) pluck_at(sim_t + 0.18f * (float)k + 0.012f * (float)st, st, 2, 0.7f);
    run(2.0f); sim_shot("slapset_strum");
    int got[16]; int n = notes_on(sim_t - 2.0f, sim_t, 10, got, 16);
    run(3.4f);
    check(ear_slapset_state() == SS_SLAP && n == 0, "four seconds of strumming heard, then: slap (step %d, no drums meanwhile: %d)", ear_slapset_state(), n);
    float strum = 0, sl, th; bool apart;
    mute(); run(0.3f);
    for (int k = 0; k < 3; k++) { slap_at(sim_t + 0.1f, 1, 5.0f, true); run(0.6f); }
    sim_shot("slapset_done");
    ear_slapset_result(&strum, &sl, &th, &apart);
    check(ear_slapset_state() == SS_DONE && apart && th > strum && th < sl && fabsf(pv[P_SLAPTHR] - th) < 0.01f,
          "three slaps: strumming made %.2f g, slaps %.2f g, a slap from %.2f g now", (double)strum, (double)sl, (double)th);
    run(4.0f);
    sim_gif_end();
    check(ear_slapset_state() == SS_OFF, "and back to playing");
    // with it: the hard strum plays no drum, a slap does
    float t0 = sim_t + 0.1f;
    for (int k = 0; k < 8; k++) for (int st = 0; st < 6; st++) pluck_at(t0 + 0.18f * (float)k + 0.012f * (float)st, st, 2, 0.7f);
    run(1.8f); mute();
    n = notes_on(t0, sim_t, 10, got, 16);
    t0 = sim_t + 0.1f; slap_at(t0, 1, 5.0f, true); run(0.4f);
    int m = notes_on(t0 - 0.02f, sim_t, 10, got, 16);
    check(n == 0 && m == 1, "after it, in GOLPE: strumming plays no drum (%d), a slap does (%d)", n, m);
    buzz = 0.4f; param_set(P_SLAPTHR, 1.2f);
    quiet();
}

static void golpe_tests(void)
{
    section("GOLPE");
    quiet(); load("GOLPE"); sit(); run(0.5f);
    sim_gif_begin("golpe");
    int got[16];
    float t0 = sim_t + 0.1f;
    slap_at(t0, 1, 2.0f, true); slap_at(t0 + 0.5f, 0, 2.0f, true); slap_at(t0 + 1.0f, 1, 2.0f, true); slap_at(t0 + 1.25f, 1, 2.0f, true); slap_at(t0 + 1.5f, 0, 2.0f, true);
    run(0.3f); sim_shot("golpe_top"); run(0.4f); sim_shot("golpe_side"); run(1.3f);
    int n = notes_on(t0 - 0.05f, sim_t, 10, got, 16);
    check(n == 5 && got[0] == 36 && got[1] == 38 && got[2] == 36 && got[3] == 36 && got[4] == 38, "slaps on the top, the side, top, top, side: kick snare kick kick snare (%d notes)", n);
    float when = -1; for (int i = 0; i < midi_n; i++) if (midi_log[i].st == 0x99 && midi_log[i].t >= t0 - 0.02f) { when = midi_log[i].t - t0; break; }
    check(when > -0.012f && when < 0.03f, "a drum within a tick or two of the slap (%.0f ms)", (double)(when * 1000.0f));
    // plucks are not slaps; moving the guitar is not a slap; a very hard knock nobody heard is
    t0 = sim_t + 0.1f;
    for (int k = 0; k < 6; k++) pluck_at(t0 + 0.12f * (float)k, k, 2, 0.6f);
    run(1.2f); mute();
    n = notes_on(t0, sim_t, 10, got, 16);
    check(n == 0, "a hard strum is not a drum (%d notes, the top buzzing %.2f g)", n, (double)ear_jolt());
    t0 = sim_t + 0.1f; slap_at(t0, 1, 1.2f, false); run(0.5f);
    n = notes_on(t0 - 0.02f, sim_t, 10, got, 16);
    check(n == 0, "a 1.2 g jolt with nothing heard (the guitar moved): no drum (%d)", n);
    t0 = sim_t + 0.1f; slap_at(t0, 1, 4.0f, false); run(0.5f);
    n = notes_on(t0 - 0.02f, sim_t, 10, got, 16);
    check(n == 1, "a 4 g knock the mic missed still plays (%d)", n);
    // the neck tunes the drums
    param_set(P_DTOP, DR_TOM); run(0.2f);
    t0 = sim_t + 0.1f; slap_at(t0, 1, 3.0f, true); run(0.6f);
    float fa = m_pitch_at(t0 + 0.12f, t0 + 0.4f, 60, 400);
    pose(60, 0); run(0.6f);
    t0 = sim_t + 0.1f; slap_at(t0, 1, 3.0f, true); run(0.6f);
    float fb = m_pitch_at(t0 + 0.12f, t0 + 0.4f, 60, 400);
    sim_shot("golpe_tuned");
    sim_gif_end();
    check(fa > 0 && fabsf(fb / fa - 1.498f) < 0.08f, "the neck lifted tunes the tom a fifth up (%.0f -> %.0f Hz)", (double)fa, (double)fb);
    sit(); run(0.5f);
    load("CAJON"); run(0.2f);
    t0 = sim_t + 0.1f; slap_at(t0, 1, 2.0f, true); slap_at(t0 + 0.4f, 0, 2.0f, true); run(1.0f);
    n = notes_on(t0 - 0.05f, sim_t, 10, got, 16);
    check(n == 2 && got[0] == 35 && got[1] == 40, "CAJON: the top low, the side high (%d notes)", n);
    // SLAPS: DRUMS plays them in any sound, OFF in none
    load("ECHO"); param_set(P_SLAP, SLAP_DRUM); run(0.2f);
    t0 = sim_t + 0.1f; slap_at(t0, 1, 2.0f, true); run(0.4f);
    n = notes_on(t0 - 0.05f, sim_t, 10, got, 16);
    check(n == 1, "SLAPS DRUMS: a slap in ECHO is a kick too (%d)", n);
    load("GOLPE"); param_set(P_SLAP, SLAP_OFF); run(0.2f);
    t0 = sim_t + 0.1f; slap_at(t0, 1, 2.0f, true); run(0.4f);
    n = notes_on(t0 - 0.05f, sim_t, 10, got, 16);
    check(n == 0, "SLAPS OFF: none in GOLPE (%d)", n);
    param_set(P_SLAP, SLAP_AUTO);
    quiet();
}

static void voice_tests(void)
{
    section("VOICE");
    quiet(); load("CLEAN"); sit(); run(0.3f);
    float t0 = sim_t + 0.1f; pluck_at(t0, 1, 0, 0.5f); run(1.0f);
    float clean55 = amp_at(t0 + 0.3f, t0 + 0.8f, 55.0f);
    quiet(); load("BASS"); run(0.3f);
    sim_gif_begin("voice");
    int got[8];
    t0 = sim_t + 0.1f; pluck_at(t0, 1, 0, 0.5f); run(0.5f); sim_shot("voice"); run(0.5f);
    float b55 = amp_at(t0 + 0.3f, t0 + 0.8f, 55.0f);
    int n = notes_on(t0, sim_t, 1, got, 8);
    check(b55 > 6.0f * clean55 && n >= 1 && got[0] == 33, "BASS under an A2: an A1 at 55 Hz (%.4f, %.5f without) and MIDI note %d", (double)b55, (double)clean55, n ? got[0] : -1);
    mute(); run(0.3f);
    t0 = sim_t + 0.1f; pluck_at(t0, 2, 2, 0.5f); run(0.8f);
    n = notes_on(t0, sim_t, 1, got, 8);
    check(n >= 1 && got[n - 1] == 40, "an E3: the note out is E2, 40 (%d)", n ? got[n - 1] : -1);
    mute(); run(0.4f);
    pose(22, 0); run(0.5f);
    t0 = sim_t + 0.1f; pluck_at(t0, 1, 0, 0.5f); run(0.8f);
    float gone = amp_at(t0 + 0.3f, t0 + 0.8f, 55.0f);
    check(gone < 0.2f * b55, "the neck down: the voice is gone (%.4f)", (double)gone);
    sim_gif_end();
    sit(); mute(); run(0.3f);
    load("BOW"); run(0.3f);
    t0 = sim_t + 0.1f; pluck_at(t0, 2, 0, 0.5f); run(0.8f);
    n = notes_on(t0, sim_t, 1, got, 8);
    check(n >= 1 && got[0] == 50, "BOW over a D3: note 50 (%d)", n ? got[0] : -1);
    quiet(); run(0.5f);
}

// ---------------------------------------------------------------- HIGH CUT and HOWL GUARD
static void howl_tests(void)
{
    section("high cut and howl guard");
    int before = guard_count();
    check(before == 0, "all the playing so far (plucks, strums, slaps, every mode, a sawtooth through the wah) set no notch (%d)", before);
    quiet(); load("CLEAN"); sit(); param_set(P_GUARD, 0); run(0.5f);
    hum_kind = 1; hum_hz = 12000.0f; hum_amp = 0.05f;
    param_set(P_HICUT, 14000); run(0.6f);
    float open_ = amp_at(sim_t - 0.4f, sim_t, 12000.0f);
    param_set(P_HICUT, 3000); run(0.6f);
    float cut = amp_at(sim_t - 0.4f, sim_t, 12000.0f);
    check(cut < 0.05f * open_, "HIGH CUT 3 kHz takes a 12 kHz whistle down %.0f dB", (double)(20.0f * log10f(cut / open_)));
    param_set(P_HICUT, 9000); quiet(); run(0.5f);
    // a speaker the mic hears too well at 4.3 kHz: without the guard it howls
    howl_hz = 4300.0f; howl_g = 2.5f;
    run(2.0f);
    float howl = m_rms(sim_t - 0.5f, sim_t);                         // it locks a little off 4.3 kHz, where the loop's phase comes round
    check(howl > 0.2f, "the speaker fed back into the mic near 4.3 kHz: a howl (rms %.2f)", (double)howl);
    howl_g = 0; run(1.0f);
    // a howl already at the limiter (harmonics and all) when the guard comes on: caught too
    howl_g = 2.5f; run(1.0f);
    param_set(P_GUARD, 1);
    { float t = sim_t, hz1[4]; float got = -1; while (sim_t < t + 1.0f) { sim_tick(); if (got < 0 && guard_active(hz1, 4) > 0) got = sim_t - t; }
      run(0.5f);
      check(got > 0 && got < 0.6f && m_rms(sim_t - 0.4f, sim_t) < 0.05f * howl, "a howl already at full level when the guard comes on: notched %.2f s later, then %.0f dB down", (double)got, (double)(20.0f * log10f(m_rms(sim_t - 0.4f, sim_t) / howl + 1e-9f))); }
    param_set(P_GUARD, 0); howl_g = 0; run(1.0f);
    // with the guard on from the start: a notch on it within half a second, and it stays down
    param_set(P_GUARD, 1); run(0.2f);
    float t0 = sim_t; howl_g = 2.5f;
    sim_gif_begin("guard");
    float found = -1, hz[4];
    while (sim_t < t0 + 1.5f) { sim_tick(); if (found < 0 && guard_active(hz, 4) > 0) found = sim_t - t0; }
    sim_shot("guard");
    run(1.5f);
    sim_gif_end();
    float after = m_rms(sim_t - 0.5f, sim_t);
    int nn = guard_active(hz, 4);
    check(found > 0 && found < 0.6f && nn >= 1 && fabsf(hz[0] - 4300.0f) < 0.1f * 4300.0f, "HOWL GUARD: a notch at %.0f Hz (where the loop locked, in the resonance at 4.3 kHz) %.2f s after it began", nn ? (double)hz[0] : 0.0, (double)found);
    check(after < 0.05f * howl && m_peak(sim_t - 0.5f, sim_t) < 0.2f, "and the howl stays down: %.0f dB under it (peak %.2f)", (double)(20.0f * log10f(after / howl + 1e-9f)), (double)m_peak(sim_t - 0.5f, sim_t));
    // the guitar still plays through it
    howl_g = 0; run(0.5f);
    float t1 = sim_t + 0.1f; pluck_at(t1, 1, 0, 0.5f); run(1.0f);
    check(m_rms(t1, t1 + 0.5f) > 0.01f, "a note still comes through (rms %.3f)", (double)m_rms(t1, t1 + 0.5f));
    param_set(P_GUARD, 0); run(0.3f);
    check(guard_active(hz, 4) == 0, "HOWL GUARD OFF: the notches are gone");
    param_set(P_GUARD, 1);
    quiet(); run(0.5f);
}

// ---------------------------------------------------------------- the loop under every sound
static void under_all(void)
{
    section("the loop under every sound");
    quiet(); load("CLEAN"); sit(); free_loop(); param_set(P_START, ST_NOW); run(0.3f);
    hum_kind = 1; hum_hz = 196.0f; hum_amp = 0.1f;
    rec_tap(); run(1.0f); rec_tap(); run(0.1f);
    hum_amp = 0; run(0.5f);
    float ref = amp_at(sim_t - 0.4f, sim_t, 196.0f);
    int kept = 0;
    sim_gif_begin("tour");
    for (int i = 0; i < INST.npresets; i++) {
        front_midi(0xC0, (uint8_t)i, 0); run(0.6f);
        sim_shot(INST.presets[i].name);
        float a = amp_at(sim_t - 0.4f, sim_t, 196.0f);
        if (a > 0.4f * ref) kept++; else note("%s: the loop at %.3f of %.3f", INST.presets[i].name, (double)a, (double)ref);
    }
    sim_gif_end();
    check(kept == INST.npresets, "a G3 loop plays on under all %d sounds (%d)", INST.npresets, kept);
    load("CLEAN"); rec_hold(2.2f); run(0.3f); param_set(P_START, ST_NOTE);
}

// ---------------------------------------------------------------- the CPU on the host
static double block_us(const char *sound, float neck, int (*setup)(void))
{
    quiet(); load(sound); pose(neck, 0); run(0.5f);
    if (setup) setup();
    hum_kind = 2; hum_hz = 110.0f; hum_amp = 0.1f; run(1.0f);
    float in[AUDIO_BLOCK], out[AUDIO_BLOCK];
    for (int i = 0; i < AUDIO_BLOCK; i++) in[i] = 0.1f * sinf((float)i * 0.3f);
    double best = 1e9;
    for (int rep = 0; rep < 3; rep++) {
        struct timespec a, b;
        clock_gettime(CLOCK_MONOTONIC, &a);
        for (int k = 0; k < 2000; k++) INST.audio(in, out, AUDIO_BLOCK);
        clock_gettime(CLOCK_MONOTONIC, &b);
        double us = ((double)(b.tv_sec - a.tv_sec) * 1e9 + (double)(b.tv_nsec - a.tv_nsec)) / 2000.0 / 1000.0;
        if (us < best) best = us;
    }
    quiet(); sit();
    return best;
}
static int dense_cloud(void) { param_set(P_CDENS, 80); param_set(P_CSIZE, 500); param_set(P_SHIM, 60); param_set(P_SPACE, 80); return 0; }
static int full_bloom(void) { param_set(P_BSUB, 100); param_set(P_SPACE, 80); param_set(P_SHIM, 60); pluck_at(sim_t, 1, 0, 0.5f); run(3.5f); return 0; }
static void cpu_tests(void)
{
    section("cpu");
    double c = block_us("CLOUD", 60, dense_cloud), b = block_us("HARMONICS", 55, full_bloom), e = block_us("DUB", 60, NULL), o = block_us("OCTAVES", 40, NULL), s = block_us("CATHEDRAL", 60, NULL), v = block_us("BOW", 40, NULL);
    metric("us_cloud", c); metric("us_bloom", b); metric("us_echo", e); metric("us_octave", o); metric("us_shimmer", s); metric("us_voice", v);
    double worst = fmax(fmax(c, b), fmax(fmax(e, o), fmax(s, v)));
    check(worst < 15.0, "the busiest block on the host: %.1f us (cloud %.1f, bloom %.1f, echo %.1f, octave %.1f, shimmer %.1f, voice %.1f; about 15 is the board's budget)", worst, c, b, e, o, s, v);
}

void inst_tests(void)
{
    SI.mic = mic;
    sit(); run(2.0f);
    neck_tests();
    loop_tests();
    cloud_tests();
    bloom_tests();
    echo_tests();
    wah_tests();
    shimmer_tests();
    octave_tests();
    wobble_tests();
    golpe_tests();
    slapset_tests();
    voice_tests();
    howl_tests();
    under_all();
    cpu_tests();
    quiet(); sit();
}
