// BREATH: HUFF as a layer. Blow at the mic while it is live and a physical model plays; tilting walks a
// ladder of notes in the SONG's key and scale (bottom edge down = higher); the joystick bends. The host
// records the dry sound into BREATH's audio track (events = false) and runs the shared echo and reverb (we
// send into them). Six models on three engines, ported from ../huff (see its CLAUDE.md for the
// measurements):
//
//   JET    FLUTE, PAN PIPE, SHAKUHACHI: Perry Cook's STK Flute loop. Jet length 0.45 of the bore holds the
//          fundamental, 0.22 the octave: blowing hard overblows. Each model's sharpness (fitted in HUFF's sim,
//          cents at MIDI 76 plus a slope) comes off the bore delay. The pressure stays in a narrow band (the
//          jet's pitch moves with it); the loudness comes from the breath as a gain.
//   REED   CLARINET: STK's Clarinet, half-length bore, odd harmonics; blowing hard jumps a twelfth.
//   VESSEL OCARINA, BOTTLE: a sine with a little 2nd and 3rd, air noise rung through a band pass at the note.
//
// Breath: the mic's low band (four poles at 250 Hz: wind on a MEMS mic is rumble), a 30 ms RMS over a room
// floor; what we play ourselves (and, when the host offers it, everything the jam plays) is taken off in the
// same band. A knock is not a breath: a new breath has to stay up for 40 ms (its last 12 ms above 45 % of its
// first peak, on an 8 ms RMS), or it is dropped and the mic is ignored for 120 ms. Not live: fade out over 50 ms and stop
// (HOLD keeps the last note and breath going as a drone).
#include "band.h"
#include "ui.h"
#include "oled.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ---------------------------------------------------------------- params (16: the layer's limit)
enum {
    H_MODEL, H_OCT, H_NOTES, H_GLIDE, H_VIB, H_HARM, H_SEND, H_HOLD,
    H_SENSE, H_GATE, H_OVER, H_AIR, H_BRIGHT, H_BENDR, H_SETUP, H_RANGE,
    H_N
};
enum { M_OCARINA, M_FLUTE, M_PANPIPE, M_SHAKU, M_CLARINET, M_BOTTLE, M_N };
static const char *const pages[2] = { "BREATH", "BLOW" };
static const char *const model_l[M_N] = { "OCARINA", "FLUTE", "PAN PIPE", "SHAKUHACHI", "CLARINET", "BOTTLE" };
static const char *const harm_l[] = { "OFF", "3RD UP", "5TH UP", "OCTAVE UP", "OCTAVE DN", "DRONE" };
enum { HM_OFF, HM_THIRD, HM_FIFTH, HM_OCTUP, HM_OCTDN, HM_DRONE };
static const char *const onoff_l[] = { "OFF", "ON" };
static const char *const go_l[] = { "-", "GO" };

static const param_t params[H_N] = {
    [H_MODEL]  = { "MODEL", "hmdl", 0, PK_ENUM, 0, 0, M_N - 1, 1, M_OCARINA, "", model_l, "WHAT YOU ARE BLOWING INTO" },
    [H_OCT]    = { "OCTAVE", "hoct", 0, PK_NUM, PF_INT | PF_SIGNED, -2, 2, 1, 0, "", NULL, "SHIFT ALL NOTES BY OCTAVES" },
    [H_NOTES]  = { "NOTES", "hnts", 0, PK_NUM, PF_INT, 3, 15, 1, 10, "", NULL, "NOTES ON THE TILT LADDER" },
    [H_GLIDE]  = { "GLIDE", "hgld", 0, PK_NUM, PF_LOG, 0.002f, 1.0f, 1.25f, 0.025f, "s", NULL, "TIME TO SLIDE TO A NOTE" },
    [H_VIB]    = { "VIBRATO", "hvib", 0, PK_NUM, PF_INT, 0, 100, 5, 15, "%", NULL, "PITCH WOBBLE, ALWAYS ON" },
    [H_HARM]   = { "HARMONY", "hhrm", 0, PK_ENUM, 0, 0, 5, 1, HM_OFF, "", harm_l, "A SECOND VOICE" },
    [H_SEND]   = { "FX SEND", "hsnd", 0, PK_NUM, PF_INT, 0, 100, 5, 35, "%", NULL, "INTO THE ECHO AND REVERB" },
    [H_HOLD]   = { "HOLD", "hhld", 0, PK_ENUM, 0, 0, 1, 1, 0, "", onoff_l, "ON: A DRONE WHEN NOT LIVE" },
    [H_SENSE]  = { "SENSITIVITY", "hsns", 1, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "MORE: SOFT BREATH, LOUD" },
    [H_GATE]   = { "GATE", "hgat", 1, PK_NUM, PF_INT, 0, 100, 5, 25, "%", NULL, "MORE: IGNORE THE ROOM" },
    [H_OVER]   = { "OVERBLOW AT", "hovb", 1, PK_NUM, PF_INT, 50, 100, 5, 85, "%", NULL, "JUMP UP HERE. 100: NEVER" },
    [H_AIR]    = { "BREATHINESS", "hair", 1, PK_NUM, PF_INT, 0, 100, 5, 30, "%", NULL, "HOW MUCH AIR YOU HEAR" },
    [H_BRIGHT] = { "BRIGHTNESS", "hbrt", 1, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "DARK TO BRIGHT" },
    [H_BENDR]  = { "BEND RANGE", "hbnd", 1, PK_NUM, PF_INT, 1, 12, 1, 2, "st", NULL, "HOW FAR THE STICK BENDS" },
    [H_SETUP]  = { "SETUP", "hset", 1, PK_ENUM, PF_SYS | PF_NOCC, 0, 1, 1, 0, "", go_l, "TURN: QUIET 2 S, THEN BLOW" },
    [H_RANGE]  = { "RANGE", "hrng", 1, PK_NUM, PF_HIDE | PF_SYS | PF_NOCC | PF_LOG, 0.01f, 1.0f, 1.1f, 0.14f, "", NULL, "SET BY SETUP" },
};
#define P(i) LP(L_BREATH, (i))

// ---------------------------------------------------------------- the models
enum { E_VESSEL, E_JET, E_REED };
typedef struct {
    uint8_t eng;
    int8_t  shift;          // semitones above the ladder (bottle -12)
    float   noise;          // breath noise in the pressure (jet, reed); the air level (vessel)
    float   pole;           // jet: the bore end's low pass
    float   tune_a, tune_k; // jet: cents sharp at MIDI 76, per semitone above it (fitted in HUFF's sim)
    float   ob_st;          // overblow: 12 (jet, done by the jet), 19.02 (reed), 0 none
    float   h2, h3, q;      // vessel
    float   gain;           // about 0.12 rms at a normal breath
    float   tone;           // tone filter, harmonics, at BRIGHTNESS 50 %
    float   chiff_f, chiff; // chiff band (harmonics) and amount
} model_t;
static const model_t models[M_N] = {
    [M_OCARINA]  = { E_VESSEL, 0,   0.30f, 0,     0,     0,      0,      0.10f, 0.035f, 7.0f,  0.56f, 5.0f, 3.0f, 0.45f },
    [M_FLUTE]    = { E_JET,    0,   0.12f, 0.63f, 28.5f, -0.96f, 12.0f,  0, 0, 0,             0.17f, 8.0f, 4.0f, 0.35f },
    [M_PANPIPE]  = { E_JET,    0,   0.24f, 0.70f, 14.5f, -1.67f, 12.0f,  0, 0, 0,             0.17f, 5.0f, 3.0f, 0.5f },
    [M_SHAKU]    = { E_JET,    0,   0.30f, 0.66f, 22.6f, -1.32f, 12.0f,  0, 0, 0,             0.17f, 6.0f, 2.5f, 0.7f },
    [M_CLARINET] = { E_REED,   0,   0.12f, 0,     0,     0,      19.02f, 0, 0, 0,             0.36f, 9.0f, 5.0f, 0.2f },
    [M_BOTTLE]   = { E_VESSEL, -12, 0.75f, 0,     0,     0,      0,      0.02f, 0.01f, 22.0f, 0.8f, 3.0f, 2.0f, 0.3f },
};

// ---------------------------------------------------------------- UI -> audio, audio -> UI
static volatile float a_midi = 72.0f, a_midi_h = 79.0f;  // notes, MIDI, before bends
static volatile float a_bend, a_vib;                    // semitones
static volatile int   a_harm_on, a_mute, a_hold;
static volatile float a_cc_breath;
static volatile int   a_onset;                          // a new note while blowing: a small chiff
static volatile float v_breath, v_amp, v_benv;
static volatile int   v_over, v_knocks;
static float v_lvl;
float breath_reading(void) { return v_breath; }          // for tools/sim
int   breath_knocks(void) { return v_knocks; }
float breath_level(void) { return v_amp; }

// The jam's whole output, if the host offers it (a request in JAM_NOTES.md): the speaker plays it into the mic.
extern const float *host_last_out(void) __attribute__((weak));

// ---------------------------------------------------------------- the audio side
#define BORE_N 512
typedef struct {
    float bore_buf[BORE_N], jet_buf[256];
    dline_t bore, jet;
    float bore_last, refl_lp, jet_s, reed_z;
    dcb_t dcb;
    float ph;
    svf_t air_bp, chiff_bp;
    float m_s, ob_s, press_s, dyn, amp_s, chiff;
} voice_t;
static voice_t *vo;                       // two voices (the note, the harmony), PSRAM: 6 KB
static int    model_cur, model_want;
static float  duck = 1.0f, live_g, harm_g, bend_s, vib_s, vib_ph;
static float  tone_z, tone_z2, per0[2], dper[2];
static biquad_t out_hp;
static uint32_t seed = 0x2b8e1d33u;
// breath
static float  blp[4], olp[4], mlp[4], oenv, menv, lp_k, ms_k, msf_k, bms, bmsf, benv, bfloor = 0.01f;
static float  b_smooth, b_slow, b_held;
static int    over, bstate, arm, hold_off;    // the knock test: silent / arming / breathing, samples to watch, to ignore
static float  arm_peak, arm_late; static int arm_late_n;

static inline float jet_table(float x) { float y = x * (x * x - 1.0f); return clampf(y, -1.0f, 1.0f); }
static inline float reed_table(float x) { return clampf(0.7f - 0.3f * x, -1.0f, 1.0f); }

static void voice_reset(voice_t *v)
{
    memset(v, 0, sizeof *v);
    v->bore = (dline_t){ v->bore_buf, BORE_N - 1, 0 };
    v->jet = (dline_t){ v->jet_buf, 255, 0 };
    v->jet_s = 0.45f;
}

// The note's period for the engine at one end of a block: the bore delay (jet: with the model's tuning and the
// fold above B6; reed: half the period, with the overblow's twelfth) or the vessel's phase step.
static float period_for(const model_t *md, float m, float ob_st)
{
    float f = 440.0f * exp2f((m - 69.0f) / 12.0f);
    switch (md->eng) {
    case E_JET:
        if (f > 2000.0f) f *= 0.5f;                                     // past about B6 the jet loses its mode
        f *= exp2f(-(md->tune_a + md->tune_k * (m - 76.0f)) / 1200.0f);
        return clampf((float)AUDIO_SR / f - 1.5f, 4.0f, (float)(BORE_N - 4));
    case E_REED:
        f *= exp2f(ob_st / 12.0f);
        return clampf(0.5f * (float)AUDIO_SR / f - 1.5f, 2.0f, (float)(BORE_N - 4));
    default:
        return f / (float)AUDIO_SR;
    }
}

// One voice, one sample. per: the bore delay (jet, reed) or the phase step (vessel), from period_for().
static inline float voice_run(voice_t *v, const model_t *md, float per, float b, int ob, float air, float noise)
{
    float y;
    switch (md->eng) {
    case E_JET: {
        float p_t = b > 0.02f ? 0.9f + 0.22f * b : 0.0f;
        v->press_s += 0.004f * (p_t - v->press_s);
        v->dyn += 0.003f * ((b > 0.02f ? 0.15f + 0.85f * b : 0.0f) - v->dyn);
        v->jet_s += 0.0015f * ((ob ? 0.22f : 0.45f) - v->jet_s);
        float pressure = v->press_s * (1.0f + (md->noise * (0.4f + air) + 0.6f * v->chiff) * noise);
        v->refl_lp = (1.0f - md->pole) * v->bore_last + md->pole * v->refl_lp;
        float temp = dcb_run(&v->dcb, v->refl_lp);
        float jd = per * v->jet_s;
        float jo = dline_read(&v->jet, jd > 1.0f ? jd : 1.0f);
        dline_write(&v->jet, pressure - 0.5f * temp);
        float pd = jet_table(jo) + 0.5f * temp;
        float bo = dline_read(&v->bore, per);
        dline_write(&v->bore, pd);
        v->bore_last = flush_denormal(bo);
        v->refl_lp = flush_denormal(v->refl_lp);
        y = bo * v->dyn;
        v->amp_s = v->dyn;
        break;
    }
    case E_REED: {
        float p_t = b > 0.02f ? 0.55f + 0.33f * b : 0.0f;
        v->press_s += 0.004f * (p_t - v->press_s);
        float breath = v->press_s * (1.0f + (md->noise * (0.4f + air) + 0.5f * v->chiff) * noise);
        float last = v->bore_last;
        float fl = 0.5f * (last + v->reed_z); v->reed_z = last;
        float pd = -0.95f * fl - breath;
        float in = breath + pd * reed_table(pd);
        float bo = dline_read(&v->bore, per);
        dline_write(&v->bore, in);
        v->bore_last = flush_denormal(bo);
        v->dyn += 0.003f * ((b > 0.02f ? 0.3f + 0.7f * b : 0.0f) - v->dyn);
        y = bo * v->dyn;
        v->amp_s = v->dyn;
        (void)ob;
        break;
    }
    default: {
        float a_t = b > 0.02f ? b * (0.35f + 0.65f * b) : 0.0f;
        v->press_s += (a_t > v->press_s ? 0.0025f : 0.0015f) * (a_t - v->press_s);
        v->ph = wrap1(v->ph + per);
        float rich = 0.6f + 0.8f * v->press_s;
        float tone = sin_turns(v->ph) + md->h2 * rich * sin_turns(2.0f * v->ph) + md->h3 * rich * sin_turns(3.0f * v->ph);
        float nz = svf_bp(&v->air_bp, noise, NULL);
        y = v->press_s * ((1.0f - 0.6f * md->noise) * tone + md->noise * (1.2f + 2.0f * air) * nz);
        v->amp_s = v->press_s;
        break;
    }
    }
    if (v->chiff > 0.001f) { y += v->chiff * v->amp_s * 1.4f * svf_bp(&v->chiff_bp, noise, NULL); v->chiff *= 0.9985f; }
    else v->chiff = 0;
    return y;
}

// The low band (four one-poles) of a block, its peak.
static inline float low_peak(float *z, const float *x, int n, float k)
{
    float pk = 0;
    for (int i = 0; i < n; i++) {
        float y = x[i];
        for (int j = 0; j < 4; j++) { z[j] += k * (y - z[j]); y = z[j]; }
        if (fabsf(y) > pk) pk = fabsf(y);
    }
    for (int j = 0; j < 4; j++) z[j] = flush_denormal(z[j]);
    return pk;
}

static float breath_in(const float *mic, int n, bool live)
{
    if (!live) { b_smooth += 0.1f * (0.0f - b_smooth); bstate = 0; return b_smooth < 0.001f ? 0.0f : b_smooth; }
    for (int i = 0; i < n; i++) {
        float x = mic[i];
        for (int j = 0; j < 4; j++) { blp[j] += lp_k * (x - blp[j]); x = blp[j]; }
        bms += ms_k * (x * x - bms);
        bmsf += msf_k * (x * x - bmsf);                               // an 8 ms RMS, for telling a knock from a breath
    }
    for (int j = 0; j < 4; j++) blp[j] = flush_denormal(blp[j]);
    benv = 2.8f * sqrtf(bms);
    // what the speaker plays back into the mic: our own sound, and the jam's when the host shares it
    const float *mix = host_last_out ? host_last_out() : NULL;
    if (mix) { float pk = low_peak(mlp, mix, n, lp_k); menv = pk > menv ? pk : menv * 0.97f; }
    float thr = bfloor * (1.6f + 3.6f * P(H_GATE) * 0.01f) + 0.002f + 1.2f * (mix ? menv : oenv);
    if (benv < bfloor) bfloor += 0.2f * (benv - bfloor);
    else if (benv < thr) bfloor += 0.0005f * (benv - bfloor);
    float range = P(H_RANGE) * exp2f((50.0f - P(H_SENSE)) / 25.0f);
    float b = clampf((benv - thr) / range, 0.0f, 1.0f);
    // a knock is not a breath: over the first 40 ms of a new breath, the level in its last 12 ms must stay above
    // 45 % of its peak in the first 12 ms (a knock is a thump that has died away by then)
    float fe = sqrtf(bmsf);
    const int ARM = AUDIO_SR * 40 / 1000, EARLY = AUDIO_SR * 12 / 1000, LATE = AUDIO_SR * 28 / 1000;
    if (hold_off > 0) { hold_off -= n; b = 0; bstate = 0; }
    else if (bstate == 0) {                                         // silent
        if (b > 0.0f) { bstate = 1; arm = 0; arm_peak = fe; arm_late = 0; arm_late_n = 0; b = 0; }
    } else if (bstate == 1) {                                       // arming: is it staying up?
        arm += n;
        if (arm <= EARLY && fe > arm_peak) arm_peak = fe;
        if (arm > LATE) { arm_late += fe; arm_late_n++; }
        if (arm < ARM) b = 0;
        else if (b > 0.0f && arm_late >= 0.45f * arm_peak * (float)arm_late_n) bstate = 2;
        else { b = 0; bstate = 0; hold_off = AUDIO_SR * 120 / 1000; v_knocks++; }
    } else if (b <= 0.0f && b_smooth < 0.005f) bstate = 0;           // breathing, until it has really stopped
    // breath on a real mic is gusty: a fast onset, then about 50 ms up, 60 ms down, 20 ms when it stops
    float kb = b > b_smooth ? (b_smooth < 0.1f ? 0.16f : 0.04f) : (b < 0.05f ? 0.1f : 0.033f);
    b_smooth += kb * (b - b_smooth);
    return b_smooth;
}

static void audio(const float *mic, float *dry, float *send, int n, bool live)
{
    if (!vo) return;
    int hold = a_hold && !live;
    float target_g = live || hold ? 1.0f : 0.0f;
    if (!live && !hold && live_g <= 0.0f) { b_smooth = 0; v_breath = 0; v_amp = 0; return; }   // not live, faded: nothing to do
    float b = breath_in(mic, n, live);
    if (hold) b = b_held; else if (live) b_held = b;
    float ccb = a_cc_breath;
    if (live && ccb > b) b = ccb;
    if (a_mute) b = 0;
    float ob_at = P(H_OVER) * 0.01f;
    b_slow += 0.025f * (b - b_slow);
    if (!over && b_slow > ob_at && ob_at < 0.999f) over = 1; else if (over && b_slow < ob_at - 0.15f) over = 0;
    if (model_want != model_cur && duck <= 0.0f) { model_cur = model_want; voice_reset(&vo[0]); voice_reset(&vo[1]); }
    const model_t *md = &models[model_cur];
    int ob = over && md->ob_st > 0.0f;
    static float b_prev;
    float chiff = md->chiff * (0.6f + 0.8f * P(H_AIR) * 0.01f);
    if (b > 0.06f && b_prev <= 0.06f) vo[0].chiff = vo[1].chiff = 0.25f + chiff;
    if (a_onset) { a_onset = 0; if (b > 0.06f) vo[0].chiff = vo[1].chiff = 0.35f * chiff; }
    b_prev = b;

    // block-rate settings: the note's glide, the bend and the vibrato move once a block and the period is
    // interpolated across it (no exp2 per sample)
    float air = P(H_AIR) * 0.01f;
    float bright = P(H_BRIGHT) * 0.01f;
    float kn = (float)n;
    float glide_b = 1.0f - expf(-kn / (P(H_GLIDE) * 0.35f * (float)AUDIO_SR));
    float tgt = a_midi + (float)md->shift, tgt_h = a_midi_h + (float)md->shift;
    float bend = a_bend + 0.12f * (b - 0.5f);                      // a harder breath plays a little sharp
    float vs0 = vib_s * sin_turns(vib_ph);
    vib_s += (1.0f - expf(-kn * 0.0008f)) * (a_vib - vib_s);
    vib_ph = wrap1(vib_ph + kn * 5.5f / (float)AUDIO_SR);
    float vs1 = vib_s * sin_turns(vib_ph);
    float bend0 = bend_s;
    bend_s += (1.0f - expf(-kn * 0.0015f)) * (bend - bend_s);
    for (int k = 0; k < 2; k++) {
        float m0 = vo[k].m_s;
        vo[k].m_s += glide_b * ((k ? tgt_h : tgt) - vo[k].m_s);
        vo[k].ob_s += (1.0f - expf(-kn * 0.003f)) * ((ob ? md->ob_st : 0.0f) - vo[k].ob_s);
        float a = period_for(md, m0 + bend0 + vs0, vo[k].ob_s), z = period_for(md, vo[k].m_s + bend_s + vs1, vo[k].ob_s);
        per0[k] = a; dper[k] = (z - a) / kn;
        float f = 440.0f * exp2f((vo[k].m_s - 69.0f) / 12.0f);
        svf_set(&vo[k].air_bp, f, md->eng == E_VESSEL ? md->q : 2.0f);
        svf_set(&vo[k].chiff_bp, f * md->chiff_f, 1.5f);
    }
    float tone_k = onepole_hz(clampf(440.0f * exp2f((vo[0].m_s - 69.0f) / 12.0f) * md->tone * exp2f((bright - 0.5f) * 3.0f), 300.0f, 14000.0f));
    float hw = a_harm_on ? 0.6f : 0.0f;
    float sendg = P(H_SEND) * 0.01f;
    const float gstep = 1.0f / (0.05f * (float)AUDIO_SR);          // 50 ms fades in and out
    float vpk = 0;
    bool two = harm_g > 0.002f || hw > 0.0f;
    for (int i = 0; i < n; i++) {
        if (model_want != model_cur) { duck -= 1.0f / 256.0f; if (duck < 0) duck = 0; }
        else if (duck < 1.0f) { duck += 1.0f / 256.0f; if (duck > 1) duck = 1; }
        if (live_g < target_g) { live_g += gstep; if (live_g > target_g) live_g = target_g; }
        else if (live_g > target_g) { live_g -= gstep; if (live_g < target_g) live_g = target_g; }
        float fi = (float)i;
        float y = voice_run(&vo[0], md, per0[0] + dper[0] * fi, b, ob, air, rng_bi(&seed));
        if (two) {
            harm_g += 0.0005f * (hw - harm_g);
            y += harm_g * voice_run(&vo[1], md, per0[1] + dper[1] * fi, b, ob, air, rng_bi(&seed));
        }
        tone_z += tone_k * (y - tone_z);
        tone_z2 += tone_k * (tone_z - tone_z2);
        y = biquad_run(&out_hp, (0.35f * y + 0.65f * tone_z2) * md->gain * duck) * live_g;
        float ay = fabsf(y); if (ay > vpk) vpk = ay;
        dry[i] += y;
        send[i] += y * sendg;
    }
    float opk = low_peak(olp, dry, n, lp_k);                       // our own low band, for the next block's threshold
    oenv = opk > oenv ? opk : oenv * 0.97f;
    tone_z = flush_denormal(tone_z); tone_z2 = flush_denormal(tone_z2);
    v_lvl = vpk > v_lvl ? vpk : v_lvl * 0.97f;
    v_amp = clampf(v_lvl / 0.3f, 0.0f, 1.0f);
    v_breath = b; v_benv = benv; v_over = ob;
}

// ---------------------------------------------------------------- the UI side
#define MAXN 15
static int   note_midi[MAXN], nnotes, root_i, idx;
static float pos_s, midi_now = 72;
static int   held[8], nheld, sounding = -1, last_cc2 = -1, last_pb = -1;
static float bend_in, vib_env, breath_s, amp_s, cc_breath_t;
static int   over_was, over_flash, song_key = -1, song_scale_n = -1;
static const int8_t *song_scale_p;
// SETUP
static int   setup_step, setup_ms, setup_n, setup_req;
static float setup_quiet, setup_blow;

static void build_notes(void)
{
    nnotes = (int)P(H_NOTES);
    if (nnotes > MAXN) nnotes = MAXN;
    if (nnotes < 3) nnotes = 3;
    root_i = nnotes / 3;
    for (int k = 0; k < nnotes; k++) note_midi[k] = scale_note(k - root_i, 5 + (int)P(H_OCT));
    if (idx >= nnotes) idx = nnotes - 1;
    song_key = song.key; song_scale_p = song.scale; song_scale_n = song.scale_n;
}

static int harm_note(int i, int h)
{
    switch (h) {
    case HM_THIRD: return scale_note(i - root_i + 2, 5 + (int)P(H_OCT));
    case HM_FIFTH: return note_midi[i] + 7;
    case HM_OCTUP: return note_midi[i] + 12;
    case HM_OCTDN: return note_midi[i] - 12;
    case HM_DRONE: return note_midi[root_i] - 12;
    default:       return note_midi[i];
    }
}

static void changed(int p)
{
    if (p < 0 || p == H_OCT || p == H_NOTES) build_notes();
    if (p < 0 || p == H_MODEL) model_want = (int)P(H_MODEL);
    if (p == H_SETUP && P(H_SETUP) >= 1.0f && !setup_step) setup_req = 1;   // turned to GO: tick() decides
}

static void init(void)
{
    vo = app_alloc(2 * sizeof(voice_t));
    if (!vo) { app_log("breath: no memory"); return; }
    voice_reset(&vo[0]); voice_reset(&vo[1]);
    lp_k = onepole_hz(250.0f); ms_k = onepole_s(0.03f); msf_k = onepole_s(0.008f);
    biquad_set(&out_hp, BQ_HP, 60.0f, 0.707f, 0);
    model_cur = model_want = (int)P(H_MODEL);
    build_notes();
    idx = root_i;
    a_midi = midi_now = (float)note_midi[idx];
    vo[0].m_s = vo[1].m_s = a_midi;
    if (P(H_SETUP) != 0) LP_SET(L_BREATH, H_SETUP, 0);
}

static void setup_tick(void)
{
    setup_ms += 10;
    float e = v_benv;
    if (setup_step == 1) {
        if (setup_ms > 400) { setup_quiet += e; setup_n++; }
        if (setup_ms >= 2200) { setup_quiet /= (float)(setup_n ? setup_n : 1); setup_step = 2; setup_ms = 0; setup_n = 0; }
    } else {
        if (e > setup_quiet * 3.0f + 0.003f) { setup_blow += e; setup_n++; }
        if (setup_ms >= 3500) {
            a_mute = 0; setup_step = 0;
            LP_SET(L_BREATH, H_SETUP, 0);
            if (setup_n < 60) { toast("DID NOT HEAR YOU: TRY AGAIN", 2000); return; }
            float normal = setup_blow / (float)setup_n;
            float thr = setup_quiet * (1.6f + 3.6f * P(H_GATE) * 0.01f) + 0.002f;
            LP_SET(L_BREATH, H_RANGE, clampf((normal - thr) / 0.7f, 0.01f, 1.0f));
            LP_SET(L_BREATH, H_SENSE, 50);
            toast("BREATH SET", 1500);
        }
    }
}

static void tick(const ctl_t *c, bool live)
{
    a_hold = P(H_HOLD) >= 0.5f;
    if (song.key != song_key || song.scale != song_scale_p || song.scale_n != song_scale_n) build_notes();
    // SETUP starts when it was turned to GO in the menu, while BREATH is live. Layers have no action hook
    // (a request in JAM_NOTES.md), so it is a setting; turned any other way (the knob in play, MIDI) it just
    // goes back to '-'.
    if (setup_req) {
        setup_req = 0;
        if (live && c->menu_open) {
            menu_leave();
            setup_step = 1; setup_ms = 0; setup_quiet = setup_blow = 0; setup_n = 0;
            a_mute = 1;
        } else LP_SET(L_BREATH, H_SETUP, 0);
    }
    if (setup_step) {
        if (!live) { setup_step = 0; a_mute = 0; LP_SET(L_BREATH, H_SETUP, 0); }
        else { setup_tick(); coach_pause(); return; }
    }
    float b = v_breath;
    breath_s += 0.3f * (b - breath_s);
    amp_s += 0.3f * (v_amp - amp_s);
    if (live && b > 0.05f) ui_awake();
    if (v_over && !over_was) over_flash = 12;
    over_was = v_over;
    if (cc_breath_t > 0) { cc_breath_t -= 0.01f; if (cc_breath_t <= 0) a_cc_breath = 0; }
    if (!live) {                                                    // keep the note; let MIDI go quiet
        if (sounding >= 0 && !a_hold) { host_midi_note(L_BREATH, sounding, 0); sounding = -1; }
        nheld = 0;
        return;
    }
    // the note: the tilt walks the ladder (with hysteresis), MIDI notes in win while held
    float pos = c->has_imu ? (c->lean_y / 70.0f + 0.5f) * (float)nnotes - 0.5f : (float)root_i;
    pos = clampf(pos, 0.0f, (float)(nnotes - 1));
    pos_s += 0.4f * (pos - pos_s);
    if (pos > (float)idx + 0.65f) idx = (int)(pos + 0.35f);
    if (pos < (float)idx - 0.65f) idx = (int)(pos + 0.65f);
    idx = clampi(idx, 0, nnotes - 1);
    float m = (float)note_midi[idx];
    if (nheld > 0) m = (float)held[nheld - 1];
    if (m != midi_now) { if (fabsf(m - midi_now) >= 0.99f) { a_onset = 1; vib_env = 0; } midi_now = m; }
    a_midi = m;
    int hm = (int)P(H_HARM);
    a_harm_on = hm != HM_OFF;
    a_midi_h = hm == HM_DRONE ? (float)harm_note(idx, hm) : (float)harm_note(idx, hm) + (m - (float)note_midi[idx]);
    // bends and vibrato: the stick bends, leaning and pushing the stick up add vibrato, after a short delay
    float bend = bend_in + (c->has_joy ? c->jx * P(H_BENDR) : 0.0f);
    a_bend = bend;
    if (b > 0.05f) vib_env = fminf(1.0f, vib_env + 0.01f / 0.3f); else vib_env = 0;
    float depth = P(H_VIB) * 0.01f * vib_env;
    if (c->has_imu) depth += clampf((fabsf(c->lean_x) - 10.0f) / 30.0f, 0.0f, 1.0f);
    if (c->has_joy && c->jy > 0) depth += c->jy;
    a_vib = 0.5f * depth;
    // MIDI out: the note with the breath as velocity, the breath as CC 2, the bend
    int note = (int)lroundf(m) + models[(int)P(H_MODEL)].shift + (v_over ? ((int)P(H_MODEL) == M_CLARINET ? 19 : 12) : 0);
    if (b > 0.06f && sounding != note) {
        if (sounding >= 0) host_midi_note(L_BREATH, sounding, 0);
        host_midi_note(L_BREATH, note, 24 + (int)(b * 103.0f)); sounding = note;
    } else if (b < 0.02f && sounding >= 0) { host_midi_note(L_BREATH, sounding, 0); sounding = -1; }
    int cc = (int)(b * 127.0f);
    if (cc != last_cc2 && (abs(cc - last_cc2) >= 2 || cc == 0 || cc == 127)) { host_midi_cc(L_BREATH, 2, cc); last_cc2 = cc; }
    int pb = 8192 + (int)(clampf(bend / P(H_BENDR), -1.0f, 1.0f) * 8191.0f);
    if (abs(pb - last_pb) > 64 || (pb == 8192 && last_pb != 8192)) { last_pb = pb; midi_bend(pb); }
}

static void midi_in(uint8_t st, uint8_t d1, uint8_t d2)
{
    uint8_t k = st & 0xF0;
    if (k == 0x90 && d2 > 0) {
        for (int i = 0; i < nheld; i++) if (held[i] == d1) { memmove(&held[i], &held[i + 1], (size_t)(nheld - i - 1) * sizeof held[0]); nheld--; break; }
        if (nheld < 8) held[nheld++] = d1;
    } else if (k == 0x80 || k == 0x90) {
        for (int i = 0; i < nheld; i++) if (held[i] == d1) { memmove(&held[i], &held[i + 1], (size_t)(nheld - i - 1) * sizeof held[0]); nheld--; break; }
    } else if (k == 0xB0 && d1 == 2) { a_cc_breath = (float)d2 / 127.0f; cc_breath_t = 2.0f; }
    else if (k == 0xB0 && d1 == 123) nheld = 0;
    else if (k == 0xE0) bend_in = ((float)((d2 << 7) | d1) - 8192.0f) / 8192.0f * P(H_BENDR);
}

// ---------------------------------------------------------------- the screen (rows 16..63)
// The ocarina from above: twelve holes open as the note climbs the ladder (sub holes, right hand, left
// hand, thumbs underneath); air streams into the mouthpiece; notes float out of the window.
#define OC_CX 38
#define OC_CY 33
typedef struct { int8_t x, y, r; uint8_t under; } hole_t;
static const hole_t holes[12] = {
    { 41, 36, 1, 0 }, { 37, 36, 1, 0 },
    { 55, 33, 1, 0 }, { 51, 31, 2, 0 }, { 46, 29, 2, 0 }, { 41, 29, 2, 0 },
    { 31, 36, 1, 0 }, { 34, 29, 2, 0 }, { 29, 29, 2, 0 }, { 24, 31, 2, 0 },
    { 30, 44, 1, 1 }, { 46, 42, 1, 1 },
};
static UI_RAM parts_t air, notes_p;
static uint32_t pseed = 0x7f3a11c5u;
static float note_wob;
static const char *const note_spr[6] = { "...##", "...#.#", "...#..", ".###..", "####..", ".##..." };
static const char *const note2_spr[6] = { ".#####", ".#...#", ".#...#", "##..##", "##..##", "......" };

static bool oc_inside(int x, int y)
{
    float dx = (float)(x - OC_CX) / 20.0f;
    if (dx >= -1.0f && dx <= 1.0f) {
        float t = dx > 0 ? dx : 0, ry = 9.5f - 3.5f * t, cy = (float)OC_CY - 2.5f * t * t;
        float dy = ((float)y - cy) / ry;
        if (dx * dx + dy * dy <= 1.0f) return true;
    }
    if (x >= 4 && x <= 20) return fabsf((float)y - ((float)OC_CY + 1.0f)) <= 2.2f + 0.12f * (float)(x - 4);
    return false;
}

static void ocarina(int open, float glow)
{
    for (int y = 21; y <= 44; y++)
        for (int x = 3; x <= 60; x++)
            if (oc_inside(x, y) && (!oc_inside(x - 1, y) || !oc_inside(x + 1, y) || !oc_inside(x, y - 1) || !oc_inside(x, y + 1)))
                oled_pixel(x, y, true);
    oled_rect(5, OC_CY, 2, 2, true);
    oled_rect(18, OC_CY - 1, 3, 3, true);
    if (glow > 0.05f) { int g = 1 + (int)(glow * 3.0f); oled_hline_pat(16, OC_CY - 3 - g, 7, 0x55, true); }
    for (int i = 0; i < 12; i++) {
        const hole_t *h = &holes[i];
        bool is_open = i < open;
        if (h->under) { oled_rect(h->x - 1, h->y + (is_open ? 1 : 0), 3, is_open ? 1 : 2, true); continue; }
        if (h->r > 1) { if (is_open) oled_circle(h->x, h->y, 2, true); else oled_disc(h->x, h->y, 2, true); }
        else if (is_open) { oled_pixel(h->x - 1, h->y, true); oled_pixel(h->x + 1, h->y, true); oled_pixel(h->x, h->y - 1, true); oled_pixel(h->x, h->y + 1, true); }
        else oled_rect(h->x - 1, h->y - 1, 3, 3, true);
    }
}

static void ladder(int x, int y0, int y1)
{
    float step = nnotes > 1 ? (float)(y1 - y0) / (float)(nnotes - 1) : 0;
    for (int k = 0; k < nnotes; k++) {
        int y = y1 - (int)lroundf((float)k * step);
        if (k == idx) oled_rrect(x, y - 1, 9, 3, 1, true);
        else if (k == root_i) oled_rect(x + 1, y, 7, 1, true);
        else oled_rect(x + 2, y, 5, 1, true);
    }
    int by = y1 - (int)lroundf(pos_s * step);
    oled_line(x - 4, by - 2, x - 2, by, true); oled_line(x - 4, by + 2, x - 2, by, true);
    oled_pixel(x - 4, by - 1, true); oled_pixel(x - 4, by + 1, true); oled_pixel(x - 3, by, true);
}

static void draw_setup(void)
{
    oled_ftext_c(F_SMALL, 64, 20, setup_step == 1 ? "STAY QUIET A MOMENT" : "NOW BLOW, NORMALLY", 1);
    oled_ftext_c(F_TINY, 64, 29, setup_step == 1 ? "LISTENING TO THE ROOM" : "AS IF PLAYING A SOFT NOTE", 1);
    float lv = clampf(sqrtf(v_benv / 0.15f), 0.0f, 1.0f);
    oled_rframe(14, 38, 100, 8, 2); oled_rect(16, 40, (int)(lv * 96.0f), 4, true);
    float t = (float)setup_ms / (setup_step == 1 ? 2200.0f : 3500.0f);
    ui_arc(64, 54, 5, -90.0f, -90.0f + 360.0f * clampf(t, 0, 1));
    oled_ftext_ex(F_TINY, 2, 57, setup_step == 1 ? "SETUP 1/2" : "SETUP 2/2", 1);
}

static void draw(const view_t *v)
{
    if (setup_step) { draw_setup(); return; }
    float b = breath_s, amp = amp_s;
    int emit = b > 0.03f ? 1 + (int)(b * 3.0f) : 0;
    for (int i = 0; i < emit; i++)
        parts_emit(&air, -6.0f, (float)OC_CY + 1.0f + 1.5f * rng_bi(&pseed), 50.0f + 150.0f * b, 0, 0.3f, b > 0.55f ? PART_STREAK : PART_DOT, 1);
    parts_step(&air, 0.04f, 0, 0);
    for (int i = 0; i < air.n; i++) if (air.p[i].x >= 4.0f) air.p[i].life = 0;
    parts_draw(&air);
    static float acc;
    acc += amp * 0.7f;
    if (acc > 1.0f && notes_p.n < 8) {
        acc = 0;
        parts_emit(&notes_p, 16.0f, (float)(OC_CY - 12), 10.0f + 10.0f * rng_uni(&pseed), -16.0f - 10.0f * amp, 3.0f, PART_SQUARE, (uint8_t)(rng_next(&pseed) & 1));
    }
    parts_step(&notes_p, 0.04f, 0, 0);
    note_wob += 0.04f * 5.5f * 6.2831853f;
    for (int i = 0; i < notes_p.n; i++) {
        particle_t *q = &notes_p.p[i];
        if (q->y < 16.0f || q->x > 60.0f) { q->life = 0; continue; }
        int wx = (int)lroundf(sinf(note_wob + q->y * 0.25f) * a_vib * 4.0f);
        ui_sprite((int)q->x + wx, (int)q->y, q->size ? note2_spr : note_spr, 6, false, true);
    }
    int lo = note_midi[0], hi = note_midi[nnotes - 1];
    int open = hi > lo ? (int)lroundf((midi_now - (float)lo) / (float)(hi - lo) * 12.0f) : 0;
    ocarina(clampi(open, 0, 12), amp);
    if (over_flash > 0) { oled_circle(19, OC_CY, 13 - over_flash, true); over_flash--; }
    // the note, big; the model (or OCTAVE UP); the breath bar with the overblow mark; the ladder
    int mdl = (int)P(H_MODEL);
    char nm[8];
    ui_note_name((int)lroundf(midi_now) + models[mdl].shift + (v_over ? (mdl == M_CLARINET ? 19 : 12) : 0), nm, sizeof nm);
    int wob = a_vib > 0.05f && amp > 0.05f ? (int)lroundf(sinf(note_wob) * a_vib * 2.0f) : 0;
    const int tx = 64;
    oled_text2(tx + wob, 19, nm);
    if (fabsf(a_bend) > 0.08f) {
        int bx = tx + 12 * (int)strlen(nm) + 1, d = a_bend > 0 ? -1 : 1, nn = 1 + (int)clampf(fabsf(a_bend) * 2.0f, 0.0f, 4.0f);
        oled_line(bx + 2, 26, bx + 2, 26 + d * (2 + nn), true);
        oled_line(bx, 26 + d * nn, bx + 2, 26 + d * (2 + nn), true); oled_line(bx + 4, 26 + d * nn, bx + 2, 26 + d * (2 + nn), true);
    }
    if (v_over) oled_pill(F_TINY, tx, 34, mdl == M_CLARINET ? "TWELFTH UP" : "OCTAVE UP", true);
    else ui_text_fit(F_TINY, tx, 35, 44, model_l[mdl]);
    const int bw = 42;
    oled_hline_pat(tx, 43, bw, 0x11, true);
    oled_rect(tx, 42, (int)(clampf(b, 0, 1) * (float)bw), 3, true);
    float oba = P(H_OVER) * 0.01f;
    if (oba < 0.999f && models[mdl].ob_st > 0) oled_rect(tx + (int)(oba * (float)bw), 40, 1, 7, true);
    ladder(117, 18, 44);
    if (!v->cover) {
        oled_hline_pat(0, 47, OLED_W, 0x11, true);
        if (breath_s < 0.02f) oled_ftext_c(F_SMALL, 64, 52, a_hold ? "HOLD: IT PLAYS ON" : "BLOW AT THE MIC", 1);
        static const char *const keys[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        char k[24]; snprintf(k, sizeof k, "KEY %s  %d NOTES", keys[song.key % 12], nnotes);
        oled_ftext_c(F_TINY, 64, 59, k, 1);
    }
}

static void icon(int x, int y, bool lit)
{
    (void)lit;                                                    // the host inverts it
    static const char *const ic[9] = {
        "............", ".....####...", "...##....#..", "###..#.#..#.", "###.......#.",
        "...##....#..", ".....####...", "............", "............" };
    ui_sprite(x, y, ic, 9, false, true);
}

static void clear(void) { }

const layer_t layer_breath = {
    .name = "BREATH", .letter = 'h', .pages = pages, .npages = 2, .params = params, .nparams = H_N,
    .events = false, .init = init, .audio = audio, .tick = tick, .draw = draw, .icon = icon,
    .changed = changed, .clear = clear, .midi_in = midi_in,
};
