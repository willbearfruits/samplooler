// NYLON's senses, UI task: the neck, the slaps, the pitch.
//
// The neck. The board is stuck on the guitar's top, at any angle. Up (the accelerometer, low-passed, the jolts
// left out) is compared with HOME (up as it was where you sit) about one axis: the axis the guitar turns about
// when you lift the neck, which is the top's normal. NECK SETUP learns it, which way is up, how far you lift
// (NECK RANGE), and which way and how far the top tips (LEAN, LEAN RANGE). Without it, a board standing up takes
// its own screen's normal as the axis (stuck on the top, screen out, a right-handed player) and a board lying flat
// its x axis (the bottom edge down lifts). Holding the knob sets HOME again where you are. LIFT is the angle about
// the axis, LEAN the angle about the one across it (the top tipped up towards your face > 0). Each becomes -1..1:
// STILL ZONE degrees do nothing, NECK RANGE (LEAN RANGE) is all the way.
//
// The slaps: the 1 kHz accelerometer's jolt (how far it jumps from the slow up), confirmed by an onset in the mic
// within 40 ms (a hard jolt needs none). Its direction says where: along the top's normal is the TOP (or the back),
// across it a SIDE.
//
// The pitch: NSDF (McLeod) on the last 48 ms of the mic at 16 kHz, 69 Hz to 1 kHz, every other tick, only for VOICE.
#include "inst_nylon.h"
#include "dsp.h"
#include <string.h>

neck_t NK;

static vec3_t g_lp = { 0, 0, 1 }, up = { 0, 0, 1 };
static float still_t;                          // seconds the box has been still
static bool  have_home, home_user;           // a home; one the player gave (kept in flash), not one taken by itself
static vec3_t H = { 0, 0, 1 }, A = { -1, 0, 0 };   // home (up there) and the lift's axis, both unit, A across H
static float jolt_last;

static inline float dot(vec3_t a, vec3_t b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline vec3_t cross(vec3_t a, vec3_t b) { return (vec3_t){ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
static inline vec3_t sub(vec3_t a, vec3_t b) { return (vec3_t){ a.x - b.x, a.y - b.y, a.z - b.z }; }
static inline vec3_t scl(vec3_t a, float k) { return (vec3_t){ a.x * k, a.y * k, a.z * k }; }
static inline float len3(vec3_t a) { return sqrtf(dot(a, a)); }
static inline vec3_t unit(vec3_t a) { float l = len3(a); return l > 1e-6f ? scl(a, 1.0f / l) : (vec3_t){ 0, 0, 1 }; }
static vec3_t across(vec3_t a, vec3_t h, bool *ok)    // a with its part along h taken out, unit
{
    vec3_t p = sub(a, scl(h, dot(a, h)));
    float l = len3(p);
    if (ok) *ok = l > 0.5f;
    return l > 1e-6f ? scl(p, 1.0f / l) : (vec3_t){ 1, 0, 0 };
}

// the axis when nobody has shown it: standing up, the screen's normal; flat, -x (the bottom edge down lifts). The
// core's screen axes have y towards the bottom edge, so they are a mirror image (x right, y down, z out): a
// right-handed guitar's neck lifted, the screen facing out of the top, turns about -z in them (found on the board,
// 2026-10-24: with +z lifting read as lowering)
static vec3_t auto_axis(vec3_t h)
{
    bool ok;
    if (fabsf(h.z) < 0.6f) { vec3_t a = across((vec3_t){ 0, 0, -1 }, h, &ok); if (ok) return a; }
    vec3_t a = across((vec3_t){ -1, 0, 0 }, h, &ok);
    if (ok) return a;
    return across((vec3_t){ 0, 1, 0 }, h, NULL);
}

static void store_frame(bool axis_set)
{
    param_set(P_HX, H.x); param_set(P_HY, H.y); param_set(P_HZ, H.z);
    param_set(P_AX, A.x); param_set(P_AY, A.y); param_set(P_AZ, A.z);
    param_set(P_AXSET, axis_set ? 1.0f : pv[P_AXSET]);
}

static void load_frame(void)
{
    vec3_t h = { pv[P_HX], pv[P_HY], pv[P_HZ] }, a = { pv[P_AX], pv[P_AY], pv[P_AZ] };
    if (len3(h) < 0.5f) { have_home = false; return; }
    home_user = true;
    H = unit(h);
    bool ok;
    A = across(a, H, &ok);
    if (!ok || len3(a) < 0.5f) A = auto_axis(H);
    have_home = true;
}

static void home_here(void)
{
    H = unit(g_lp);
    bool ok = false;
    if ((int)pv[P_AXSET] == 1) A = across((vec3_t){ pv[P_AX], pv[P_AY], pv[P_AZ] }, H, &ok);
    if (!ok) A = auto_axis(H);
    have_home = true;
}
void ear_set_home(void) { home_here(); home_user = true; store_frame(false); }

// ---------------------------------------------------------------- NECK SETUP
// Sit as you play and hold still (home); lift the neck as far as you would and hold it (the axis, which way is
// up, and NECK RANGE from how far); back to home; tip the top up towards your face and hold it (which way the
// lean goes, and LEAN RANGE). Any step that moves waits for the box to be still at its furthest point.
static int su_state; static float su_t, su_ang, su_max, su_lift, su_tip; static vec3_t su_h, su_peak;
static bool su_auto, su_asked;
#ifdef ESP_PLATFORM
bool ear_auto_setup = true;
#else
bool ear_auto_setup = false;                       // the simulator turns it on where it tests it (the core's checks hold the box every way)
#endif
void ear_setup_begin(void) { su_state = SU_STILL; su_t = 0.0f; su_max = su_lift = su_tip = 0.0f; su_auto = false; }
void ear_setup_cancel(void) { if (su_auto && pv[P_AXSET] < 0.5f) param_set(P_AXSET, 2.0f); su_state = SU_OFF; }   // not asked again
void ear_setup_skip(void) { if (su_state == SU_TIP) { su_state = SU_DONE; su_t = 0.0f; } }
int  ear_setup_state(void) { return su_state; }
float ear_setup_angle(void) { return su_ang; }
void ear_setup_result(float *lift, float *tip) { *lift = su_lift; *tip = su_tip; }
float ear_setup_progress(void)
{
    switch (su_state) {
    case SU_STILL: return clampf(still_t / 1.0f, 0.0f, 1.0f);
    case SU_LIFT: return clampf(su_ang / 15.0f, 0.0f, 1.0f);
    case SU_BACK: return clampf(1.0f - su_ang / 15.0f, 0.0f, 1.0f);
    case SU_TIP: return clampf(su_ang / 10.0f, 0.0f, 1.0f);
    default: return su_state >= SU_DONE ? 1.0f : 0.0f;
    }
}
bool ear_setup_wanted(const ctl_t *c)
{
    if (!ear_auto_setup || su_asked || su_state || pv[P_AXSET] > 0.5f || !c->has_imu || c->menu_open || c->ticks < 800) return false;
    if (!(fabsf(up.z) < 0.6f) || still_t < 2.0f) return false;    // standing up and resting: on a guitar
    su_asked = true;
    ear_setup_begin(); su_auto = true;
    return true;
}

static void setup_tick(float dt)
{
    su_t += dt;
    su_ang = acosf(clampf(dot(up, su_h), -1.0f, 1.0f)) * 57.29578f;
    switch (su_state) {
    case SU_STILL:
        su_ang = 0.0f;
        if (still_t >= 1.0f) { su_h = unit(g_lp); su_state = SU_HOME; su_t = 0.0f; }
        break;
    case SU_HOME: if (su_t > 0.6f) { su_state = SU_LIFT; su_t = 0.0f; su_max = 0.0f; } break;
    case SU_LIFT:                                                 // as far as you like, then still there
        if (su_ang > su_max) { su_max = su_ang; su_peak = up; }
        if (su_max > 8.0f && su_ang > su_max - 2.0f && still_t > 0.5f) {
            H = su_h;
            A = across(unit(cross(su_peak, su_h)), H, NULL);      // the axis that turns home into the lift: lifting is +
            have_home = home_user = true;
            store_frame(true);
            su_lift = su_max;
            param_set(P_RANGE, clampf(lroundf(0.85f * su_max), 8.0f, 45.0f));
            su_state = SU_BACK; su_t = 0.0f;
        }
        break;
    case SU_BACK: if (su_ang < 4.0f && still_t > 0.4f) { su_state = SU_TIP; su_t = 0.0f; su_max = 0.0f; } break;
    case SU_TIP:                                                  // the top tipped up towards the face, then still
        if (su_ang > su_max) { su_max = su_ang; su_peak = up; }
        if (su_max > 6.0f && su_ang > su_max - 2.0f && still_t > 0.4f) {
            vec3_t nt = dot(A, (vec3_t){ 0, 0, 1 }) >= 0.0f ? A : scl(A, -1.0f);
            float lean = atan2f(dot(su_peak, nt), dot(su_peak, H));
            param_set(P_LEAN, lean < 0.0f ? LEAN_FLIP : LEAN_NORMAL);
            param_set(P_LRANGE, clampf(lroundf(0.85f * su_max), 6.0f, 45.0f));
            su_tip = su_max;
            su_state = SU_DONE; su_t = 0.0f;
        } else if (su_t > 15.0f) { su_state = SU_DONE; su_t = 0.0f; }   // no tip: the lean as it was
        break;
    case SU_DONE: if (su_t > 2.5f) su_state = SU_OFF; break;
    }
    if (su_state && su_state < SU_TIP && su_t > 30.0f) ear_setup_cancel();
}

// ---------------------------------------------------------------- slaps
#define NSLAP 8
static slap_t slaps[NSLAP]; static int ns_r, ns_w;
typedef struct { uint32_t frame; float jolt; int top; bool on; } pend_t;
static pend_t pend;
static uint32_t ons_f[8]; static float ons_l[8]; static int ons_n;
static uint32_t last_slap;
bool ear_slap_pop(slap_t *s) { if (ns_r == ns_w) return false; *s = slaps[ns_r]; ns_r = (ns_r + 1) % NSLAP; return true; }
float ear_jolt(void) { return jolt_last; }
float ear_slap_thr(void) { return clampf(pv[P_SLAPTHR], 0.2f, 6.0f) * exp2f((50.0f - pv[P_SENSE]) / 50.0f); }

// ---------------------------------------------------------------- SLAP SETUP
// Strum as hard as you play for four seconds: the biggest jolt that makes. Then slap the top three times: the
// smallest of those. A slap is the geometric mean between them (SLAP SENSE then scales it, 50 % is exactly that);
// if the slaps were not clearly harder than the strumming, it says so.
static int ss_state, ss_n; static float ss_t, ss_heard, ss_strum, ss_slap, ss_peak, ss_gap, ss_thr;
void ear_slapset_begin(void) { ss_state = SS_STRUM; ss_t = ss_heard = ss_strum = 0.0f; ss_n = 0; ss_slap = 1e9f; ss_peak = ss_gap = 0.0f; }
int  ear_slapset_state(void) { return ss_state; }
void ear_slapset_cancel(void) { ss_state = SS_OFF; }
float ear_slapset_progress(void) { return ss_state == SS_STRUM ? clampf(ss_heard / 4.0f, 0.0f, 1.0f) : ss_state == SS_SLAP ? (float)ss_n / 3.0f : 1.0f; }
void ear_slapset_result(float *strum, float *slap, float *thr, bool *apart)
{
    *strum = ss_strum; *slap = ss_n ? ss_slap : 0.0f; *thr = ss_thr; *apart = ss_n && ss_slap > 1.5f * ss_strum;
}
static void slapset_tick(float dt, float jolt)
{
    ss_t += dt;
    switch (ss_state) {
    case SS_STRUM:                                                // only while it hears playing
        if (eng_env > 0.01f) { ss_heard += dt; if (jolt > ss_strum) ss_strum = jolt; }
        if (ss_heard >= 4.0f) { ss_state = SS_SLAP; ss_t = 0.0f; }
        else if (ss_t > 20.0f) ss_state = SS_OFF;
        break;
    case SS_SLAP: {
        float need = fmaxf(1.3f * ss_strum, 0.4f);
        if (ss_gap > 0.0f) ss_gap -= dt;
        if (jolt > need && ss_gap <= 0.0f) ss_peak = fmaxf(ss_peak, jolt);
        else if (ss_peak > 0.0f) {                                // the jolt is over: one slap
            if (ss_peak < ss_slap) ss_slap = ss_peak;
            ss_n++; ss_peak = 0.0f; ss_gap = 0.15f;
        }
        if (ss_n >= 3 || (ss_t > 15.0f && ss_n > 0)) {
            ss_thr = clampf(sqrtf(fmaxf(ss_strum, 0.2f) * ss_slap), 0.3f, 5.0f);
            param_set(P_SLAPTHR, ss_thr); param_set(P_SENSE, 50);
            ss_state = SS_DONE; ss_t = 0.0f;
        } else if (ss_t > 15.0f) ss_state = SS_OFF;
        break;
    }
    case SS_DONE: if (ss_t > 3.5f) ss_state = SS_OFF; break;
    }
}
static void slap_push(const slap_t *s) { int nw = (ns_w + 1) % NSLAP; if (nw == ns_r) return; slaps[ns_w] = *s; ns_w = nw; }

static bool heard_near(uint32_t f)
{
    for (int i = 0; i < ons_n; i++) { int32_t d = (int32_t)(ons_f[i] - f); if (d > -(int32_t)(AUDIO_SR * 40 / 1000) && d < (int32_t)(AUDIO_SR * 40 / 1000)) return true; }
    return false;
}

static void slaps_tick(const ctl_t *c, vec3_t g_now)
{
    onset_t o;
    while (eng_onset_pop(&o)) {                                  // the onsets of the last moments, for the confirmation
        if (ons_n == 8) { memmove(ons_f, ons_f + 1, 7 * sizeof ons_f[0]); memmove(ons_l, ons_l + 1, 7 * sizeof ons_l[0]); ons_n = 7; }
        ons_f[ons_n] = o.frame; ons_l[ons_n] = o.level; ons_n++;
    }
    float jmax = 0; int jk = -1;
    vec3_t dir = { 0, 0, 0 };
    if (c->nburst > 0) {
        for (int i = 0; i < c->nburst; i++) { float j = len3(sub(c->burst[i], g_now)); if (j > jmax) { jmax = j; jk = i; } }
        for (int i = 0; i <= jk; i++) { vec3_t d = sub(c->burst[i], g_now); if (len3(d) >= 0.5f * jmax) { dir = d; jk = i; break; } }
    } else { jmax = len3(sub(c->acc, g_now)); dir = sub(c->acc, g_now); jk = 0; }
    jolt_last = jmax;
    if (ss_state) { slapset_tick(c->dt, jmax); pend.on = false; return; }
    if (!c->has_imu || (int)pv[P_SLAP] == SLAP_OFF) { pend.on = false; return; }
    float thr = ear_slap_thr();
    uint32_t f = c->frames - (uint32_t)((c->nburst > 0 ? c->nburst - 1 - jk : 0) * (AUDIO_SR / 1000));
    if (jmax >= thr && (int32_t)(f - last_slap) > (int32_t)(AUDIO_SR * 90 / 1000) && !pend.on) {
        vec3_t nt = NK.upright ? A : (vec3_t){ 0, 0, 1 };
        float along = fabsf(dot(dir, nt)), all = len3(dir);
        pend = (pend_t){ f, jmax, along >= 0.6f * all, true };
    }
    if (pend.on) {
        int32_t age = (int32_t)(c->frames - pend.frame);
        if (heard_near(pend.frame) || pend.jolt >= 2.2f * thr) {
            slap_t s = { pend.frame, pend.jolt, pend.top };
            slap_push(&s); last_slap = pend.frame; pend.on = false;
        } else if (age > AUDIO_SR * 50 / 1000) pend.on = false;   // a jolt nobody heard: the guitar was moved
    }
}

// ---------------------------------------------------------------- the pitch
static bool trk_on; static float trk_hz, trk_clar, trk_last[3]; static int trk_k;
static UI_RAM float xb[1024], nsdf[232];          // UI-only buffers: PSRAM
void ear_track(bool on) { trk_on = on; if (!on) { trk_hz = 0.0f; trk_clar = 0.0f; } }
float ear_hz(void) { return trk_hz; }
float ear_clarity(void) { return trk_clar; }

// McLeod's peak picking: the key maxima after the first fall below zero, the first within 90 % of the best, refined
// by a parabola; its clarity (the NSDF there); -1 if none
static float nsdf_pick(const float *ns, int T0, int T1, float *clar)
{
    float best = 0; int bi = -1;
    int t = T0;
    while (t < T1 && ns[t] > 0.0f) t++;
    int cand[24], nc = 0;
    for (; t < T1 && nc < 24; t++) {
        if (ns[t] > 0.0f) {
            int k = t; float mx = ns[t];
            while (t < T1 && ns[t] > 0.0f) { if (ns[t] > mx) { mx = ns[t]; k = t; } t++; }
            cand[nc++] = k;
            if (mx > best) best = mx;
        }
    }
    *clar = best;
    for (int i = 0; i < nc; i++) if (ns[cand[i]] >= 0.9f * best) { bi = cand[i]; break; }
    if (bi <= T0) return -1.0f;
    float a = ns[bi - 1], b = ns[bi], c = bi + 1 <= T1 ? ns[bi + 1] : b;
    float den = a - 2.0f * b + c, off = fabsf(den) > 1e-9f ? 0.5f * (a - c) / den : 0.0f;
    *clar = b;
    return (float)bi + clampf(off, -0.5f, 0.5f);
}

// BLOOM's caught note: its period at 32 kHz, from 1024 samples a third of the way in (past the attack), lags 30..420
// (76 Hz to 1 kHz); 0 when it has none clear enough (a chord, a noise)
static UI_RAM float pb[1024 + 424], pn[424];
float ear_period(const int16_t *x, int n, float *clarity)
{
    const int W = 1024, T0 = 30, T1 = 420;
    int s0 = n / 3;
    if (!x || s0 + W + T1 + 2 > n) { if (clarity) *clarity = 0.0f; return 0.0f; }
    for (int i = 0; i < W + T1 + 2; i++) pb[i] = (float)x[s0 + i];
    float m0 = 0; for (int j = 0; j < W; j++) m0 += pb[j] * pb[j];
    float mt = 0; for (int j = 0; j < W; j++) mt += pb[j + T0] * pb[j + T0];
    if (m0 < (float)W) { if (clarity) *clarity = 0.0f; return 0.0f; }
    for (int t = T0; t <= T1; t++) {
        float r = 0;
        const float *a = pb, *b = pb + t;
        for (int j = 0; j < W; j++) r += a[j] * b[j];
        pn[t] = 2.0f * r / (m0 + mt + 1e-9f);
        float o = pb[t], i2 = pb[t + W];
        mt += i2 * i2 - o * o;
    }
    float clar, lag = nsdf_pick(pn, T0, T1, &clar);
    if (clarity) *clarity = clar;
    return lag > 0.0f && clar >= 0.85f ? lag : 0.0f;
}

static void track(void)
{
    uint32_t w = __atomic_load_n(&eng_ear_w, __ATOMIC_ACQUIRE);
    for (int i = 0; i < 1024; i++) xb[i] = eng_ear[(w - 1024u + (uint32_t)i) & (EAR_N - 1)];
    float e = 0; for (int i = 0; i < 1024; i++) e += xb[i] * xb[i];
    if (e < 1024.0f * 1e-6f) { trk_hz = 0.0f; trk_clar = 0.0f; trk_k = 0; return; }
    const int W = 768, T0 = 16, T1 = 231;
    float m0 = 0; for (int j = 0; j < W; j++) m0 += xb[j] * xb[j];
    float mt = 0; for (int j = 0; j < W; j++) mt += xb[j + T0] * xb[j + T0];
    for (int t = T0; t <= T1; t++) {
        float r = 0;
        const float *a = xb, *b = xb + t;
        for (int j = 0; j < W; j++) r += a[j] * b[j];
        nsdf[t] = 2.0f * r / (m0 + mt + 1e-9f);
        float o = xb[t], i2 = xb[t + W];                          // the shifted window's energy, slid one on
        mt += i2 * i2 - o * o;
    }
    float clar, lag = nsdf_pick(nsdf, T0, T1, &clar);
    if (lag <= 0.0f || clar < 0.75f) { trk_clar = clar; trk_hz = 0.0f; trk_k = 0; return; }
    float hz = 16000.0f / lag;
    trk_clar = clar;
    // a median of the last three: one wrong octave does not move it (a new note fills all three)
    if (trk_k == 0) trk_last[0] = trk_last[1] = trk_last[2] = hz;
    trk_last[trk_k++ % 3] = hz;
    float x0 = trk_last[0], x1 = trk_last[1], x2 = trk_last[2];
    float med = x0 > x1 ? (x1 > x2 ? x1 : (x0 > x2 ? x2 : x0)) : (x0 > x2 ? x0 : (x1 > x2 ? x2 : x1));
    trk_hz = med > 0.0f ? med : hz;
}

// ---------------------------------------------------------------- every tick
void ear_tick(const ctl_t *c)
{
    static bool loaded;
    if (!loaded) { load_frame(); loaded = true; }
    // up: the average of the tick's accelerometer readings, a jolt left out, low-passed
    vec3_t a = c->acc;
    if (c->nburst > 0) {
        vec3_t s = { 0, 0, 0 };
        for (int i = 0; i < c->nburst; i++) { s.x += c->burst[i].x; s.y += c->burst[i].y; s.z += c->burst[i].z; }
        a = scl(s, 1.0f / (float)c->nburst);
    }
    static int jolt_ticks = -1;
    if (jolt_ticks < 0) { if (len3(a) > 0.5f) g_lp = a; jolt_ticks = 0; }   // the first reading: start there
    vec3_t g_before = g_lp;
    vec3_t d = sub(a, g_lp);
    float dl = len3(d);
    if (dl > 0.25f && ++jolt_ticks < 4) d = (vec3_t){ 0, 0, 0 };     // a slap is left out; a real turn wins after 40 ms
    else if (dl <= 0.25f) jolt_ticks = 0;
    float k = 1.0f - expf(-c->dt / 0.12f);
    g_lp = (vec3_t){ g_lp.x + k * d.x, g_lp.y + k * d.y, g_lp.z + k * d.z };
    up = unit(g_lp);
    still_t = c->has_imu && c->gyro_mag < 10.0f && dl < 0.08f ? still_t + c->dt : 0.0f;
    // until the player gives one, home is where the box rests, taken again when it goes from lying flat to standing
    // up (from the table to the guitar) or back; lifting the neck turns it about its own normal and never does that
    if (!home_user && c->has_imu && still_t > 1.5f && (!have_home || (fabsf(H.z) < 0.6f) != (fabsf(up.z) < 0.6f))) home_here();
    if (su_state) setup_tick(c->dt);

    NK.ok = c->has_imu && have_home && pv[P_NECKON] >= 1.0f;
    NK.upright = fabsf(H.z) < 0.6f || (int)pv[P_AXSET] == 1;
    if (NK.ok) {
        vec3_t B = cross(H, A);
        float gu = dot(up, H);
        NK.neck = atan2f(dot(up, B), gu) * 57.29578f;
        vec3_t nt = NK.upright ? (dot(A, (vec3_t){ 0, 0, 1 }) >= 0.0f ? A : scl(A, -1.0f)) : scl(A, -1.0f);
        float lean = atan2f(dot(up, nt), gu) * 57.29578f;
        int lm = (int)pv[P_LEAN];
        NK.lean = lm == LEAN_OFF ? 0.0f : lm == LEAN_FLIP ? -lean : lean;
        float still = pv[P_STILL], range = fmaxf(pv[P_RANGE], still + 2.0f), lrange = fmaxf(pv[P_LRANGE], still + 2.0f);
        float an = fabsf(NK.neck) - still, al = fabsf(NK.lean) - still;
        NK.n = clampf((an > 0.0f ? an : 0.0f) / (range - still), 0.0f, 1.0f) * (NK.neck < 0.0f ? -1.0f : 1.0f);
        NK.l = clampf((al > 0.0f ? al : 0.0f) / (lrange - still), 0.0f, 1.0f) * (NK.lean < 0.0f ? -1.0f : 1.0f);
    } else { NK.neck = NK.lean = NK.n = NK.l = 0.0f; }

    slaps_tick(c, g_before);
    if (trk_on && (c->ticks & 1)) track();
}
