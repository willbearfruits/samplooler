// SNIFF's audio engine. Four layers into one dry bus and one reverb send:
//   PACKETS  16 voices, each a frame someone sent: modal plucks, bells and tines (struck two-pole
//            resonators with a noise exciter), drops and chirps (a sine gliding), beeps (a gated tone,
//            as long as the frame), static (noise in a band). Stolen with a 2 ms fade, never cut.
//            ACKs are ticks (a shared click), frames over the density budget are rain (shared drops).
//   ROOM     a chord of 8 detuned sine pairs whose band gains follow the room's radio shape (CSI) and
//            whose level follows how much it moves, plus a breath of wind when it moves a lot.
//   STORM    crackles at the rate broken frames arrive, a rumble from the raised noise floor, thunder claps.
//   HUNT     a Geiger beeper: rate and pitch from how near the target is.
// Then a tempo echo and a 4-line feedback delay network reverb (both in PSRAM), and a brightness filter.
// Every level is ramped per block, every filter set at block rate; nothing allocates.
#include "inst.h"
#include "spsc.h"
#include <string.h>

volatile snd_ctl_t sc;
volatile float snd_out_pkt, snd_out_room, snd_out_storm;
volatile int   snd_voices;
volatile uint32_t snd_beeps;

SPSC(strq, strike_t, 256)
static strq_t q;
static uint32_t seed = 0x51ff1234u;

static inline float nz(void) { return rng_bi(&seed); }

// ---------------------------------------------------------------- packet voices
#define NV 16
#define FADE_N 64                                 // a stolen voice fades over 2 ms
typedef struct { float c1, c2, y1, y2, g; } mode_t;
typedef struct {
    int     on, delay, age, quiet;
    uint8_t t;
    mode_t  m[3]; int nm;
    int     exc_n; float exc_lp, exc_k, exc_amp, imp;
    float   ph, f, f_end, glide, env, env_k, vib_ph, vib_d;
    int     gate;
    svf_t   bp;
    float   amp, wet, fade;
    bool    stealing; strike_t next;
} voice_t;
static voice_t vo[NV];

static void mode_set(mode_t *m, float f, float t60, float g)
{
    float w = TWO_PI * clampf(f, 20.0f, 0.45f * (float)AUDIO_SR) / (float)AUDIO_SR;
    float r = expf(-6.9078f / (fmaxf(t60, 0.005f) * (float)AUDIO_SR));
    m->c1 = 2.0f * r * cosf(w); m->c2 = -r * r; m->y1 = m->y2 = 0; m->g = g * sinf(w);
}

static void voice_start(voice_t *v, const strike_t *s, int delay)
{
    memset(v, 0, sizeof *v);
    v->on = 1; v->delay = delay; v->t = s->timbre; v->amp = s->amp; v->wet = s->wet; v->fade = 1.0f;
    float f = s->f, r = fmaxf(s->ring, 0.01f), b = clampf(s->bright, 0, 1);
    v->exc_k = 0.08f + 0.85f * b;
    v->imp = 1.0f;                                          // a unit impulse rings each mode at its gain; the noise is texture
    switch (s->timbre) {
    case T_PLUCK:
        v->nm = 3;
        mode_set(&v->m[0], f, r, 1.0f); mode_set(&v->m[1], 2.0f * f, r * 0.55f, 0.5f * b + 0.1f); mode_set(&v->m[2], 3.0f * f, r * 0.33f, 0.3f * b);
        v->exc_n = 48; v->exc_amp = 0.10f; break;
    case T_BELL:
        v->nm = 3;
        mode_set(&v->m[0], f, r, 1.0f); mode_set(&v->m[1], 2.76f * f, r * 0.6f, 0.55f); mode_set(&v->m[2], 5.40f * f, r * 0.3f, 0.3f * b + 0.05f);
        v->exc_n = 8; v->exc_amp = 0.05f; break;
    case T_KALIMBA:
        v->nm = 2;
        mode_set(&v->m[0], f, r, 1.0f); mode_set(&v->m[1], 5.93f * f, r * 0.09f, 0.5f * b + 0.15f);
        v->exc_n = 12; v->exc_amp = 0.08f; break;
    case T_DROP:
        v->f = f * 0.55f; v->f_end = f * 1.45f; v->glide = onepole_s(0.012f + 0.03f * r);
        v->env = 1.0f; v->env_k = expf(-6.9078f / (r * (float)AUDIO_SR)); break;
    case T_CHIRP:
        v->f = f * 2.3f; v->f_end = f; v->glide = onepole_s(0.025f);
        v->vib_d = 0.10f; v->env = 1.0f; v->env_k = expf(-6.9078f / (r * (float)AUDIO_SR)); break;
    case T_BEEP:
        v->f = v->f_end = f; v->gate = (int)(r * (float)AUDIO_SR); v->env = 0; break;
    case T_STATIC:
        svf_set(&v->bp, f, 1.5f + 5.0f * b);
        v->env = 1.0f; v->env_k = expf(-6.9078f / (r * (float)AUDIO_SR)); break;
    }
}

static inline float voice_run(voice_t *v)
{
    float e = v->imp, y = 0;
    v->imp = 0;
    if (v->exc_n > 0) { v->exc_lp += v->exc_k * (nz() - v->exc_lp); e += v->exc_lp * v->exc_amp * (float)v->exc_n * (1.0f / 48.0f); v->exc_n--; }
    switch (v->t) {
    case T_PLUCK: case T_BELL: case T_KALIMBA:
        for (int k = 0; k < v->nm; k++) {
            mode_t *m = &v->m[k];
            float o = m->c1 * m->y1 + m->c2 * m->y2 + e * m->g;
            m->y2 = m->y1; m->y1 = o; y += o;
        }
        y *= 0.7f;                                          // the modes' sum, back to about the strike's amp
        break;
    case T_DROP: case T_CHIRP: {
        v->f += v->glide * (v->f_end - v->f);
        float fm = v->f;
        if (v->vib_d > 0) { v->vib_ph = wrap1(v->vib_ph + 23.0f / (float)AUDIO_SR); fm *= 1.0f + v->vib_d * sin_turns(v->vib_ph); }
        v->ph = wrap1(v->ph + fm / (float)AUDIO_SR);
        y = sin_turns(v->ph) * v->env; v->env *= v->env_k;
        break;
    }
    case T_BEEP: {
        float want = v->gate > 0 ? 1.0f : 0.0f;
        v->env += (want - v->env) * 0.01f;                    // 3 ms edges
        if (v->gate > 0) v->gate--;
        v->ph = wrap1(v->ph + v->f / (float)AUDIO_SR);
        float s1 = sin_turns(v->ph);
        y = (s1 + 0.18f * sin_turns(3.0f * v->ph)) * v->env * 0.8f;
        break;
    }
    case T_STATIC:
        y = svf_bp(&v->bp, nz(), NULL) * v->env * 3.2f; v->env *= v->env_k;
        break;
    }
    return y * v->amp;
}

static void take_strike(const strike_t *s, uint32_t f0)
{
    int32_t d = (int32_t)(s->at - f0);
    int delay = d < 0 ? 0 : d > AUDIO_SR ? AUDIO_SR : d;
    int best = -1; float bq = 1e9f;
    for (int i = 0; i < NV; i++) {
        if (!vo[i].on) { voice_start(&vo[i], s, delay); return; }
        if (vo[i].stealing) continue;
        float qv = vo[i].amp * (vo[i].delay > 0 ? 10.0f : 1.0f) / (1.0f + 0.001f * (float)vo[i].age);
        if (qv < bq) { bq = qv; best = i; }
    }
    if (best < 0) return;                                   // every voice is already handing over: drop it
    vo[best].stealing = true; vo[best].next = *s; vo[best].next.at = s->at;
}

// ---------------------------------------------------------------- ticks (ACKs)
#define TICKQ 64
static uint32_t tick_at[TICKQ]; static float tick_amp[TICKQ]; static int tick_n;
static float tick_env, tick_hp_x, tick_hp_y;

// ---------------------------------------------------------------- reverb (4-line FDN) and echo
#define FDN_LEN 4096
static float *fdn_buf;                                      // 4 x FDN_LEN
static uint32_t fdn_w;
static const float fdn_base[4] = { 1103.0f, 1327.0f, 1583.0f, 1789.0f };
static float fdn_len[4], fdn_g[4], fdn_lp[4], fdn_size = -1;
#define ECHO_LEN 32768
static float *echo_buf; static uint32_t echo_w; static float echo_d = 9600, echo_lp;

void snd_init(void)
{
    if (!fdn_buf) fdn_buf = app_alloc(4 * FDN_LEN * sizeof(float));
    if (!echo_buf) echo_buf = app_alloc(ECHO_LEN * sizeof(float));
    for (int i = 0; i < 4; i++) fdn_len[i] = fdn_base[i];
    strq_clear(&q);
}

bool snd_strike(const strike_t *s) { return strq_push(&q, s); }

// ---------------------------------------------------------------- the rest of the state
static float ph_a[PAD_N], ph_b[PAD_N], pad_fs[PAD_N], pad_gs[PAD_N], motion_s, wind_lp;
static svf_t wind_bp;
static float crack_env, crack_hp_x, crack_hp_y, brown1, brown2, thunder_env;
static svf_t thunder_lp; static float thunder_fc = 1500.0f; static uint32_t thunder_seen;
static float rain_env; static svf_t rain_bp;
static float beep_ph, beep_env, beep_tone_ph, click_env;
static float lv_pkt, lv_room, lv_storm, lv_ticks, lv_hunt, lv_verb, lv_echo, bright_lp, bright_k = 1.0f;
static float pk_pkt, pk_room, pk_storm;

void snd_audio(const float *in, float *out, int n)
{
    (void)in;
    uint32_t f0 = audio_frames();
    strike_t s;
    int popped = 0;
    while (popped < 24 && strq_pop(&q, &s)) {
        popped++;
        if (s.timbre == T_TICK) {
            if (tick_n < TICKQ) { tick_at[tick_n] = s.at; tick_amp[tick_n] = s.amp; tick_n++; }
            continue;
        }
        take_strike(&s, f0);
    }

    // block-rate settings
    float t_pkt = clampf(sc.pkt, 0, 1), t_room = clampf(sc.room, 0, 1), t_storm = clampf(sc.storm, 0, 1), t_ticks = clampf(sc.ticks, 0, 1);
    float t_hunt = sc.hunt > 0.5f ? 1.0f : 0.0f, t_verb = clampf(sc.verb, 0, 1), t_echo = clampf(sc.echo, 0, 1);
    const float rk = 1.0f / (float)n;
    float d_pkt = (t_pkt - lv_pkt) * rk, d_room = (t_room - lv_room) * rk, d_storm = (t_storm - lv_storm) * rk, d_ticks = (t_ticks - lv_ticks) * rk;
    float d_hunt = (t_hunt - lv_hunt) * rk * 0.25f, d_verb = (t_verb - lv_verb) * rk, d_echo = (t_echo - lv_echo) * rk;
    float bk = onepole_hz(1200.0f * exp2f(clampf(sc.bright, 0, 1) * 4.0f));
    if (sc.bright > 0.97f) bk = 1.0f;
    bright_k += 0.2f * (bk - bright_k);
    float size = clampf(sc.size, 0, 1);
    if (fabsf(size - fdn_size) > 0.002f) {
        fdn_size = size;
        float rt = 0.4f + 6.0f * size * size;
        for (int i = 0; i < 4; i++) fdn_g[i] = powf(10.0f, -3.0f * fdn_base[i] * (0.6f + size) / (rt * (float)AUDIO_SR));
    }
    float lens[4];
    for (int i = 0; i < 4; i++) lens[i] = fdn_base[i] * (0.6f + size);
    float echo_t = clampf(sc.echo_s * (float)AUDIO_SR, 64.0f, (float)ECHO_LEN - 2.0f), efb = clampf(sc.efb, 0, 0.92f);
    for (int k = 0; k < PAD_N; k++) { float ft = clampf(sc.pad_f[k], 20.0f, 8000.0f); if (pad_fs[k] <= 0) pad_fs[k] = ft; }
    float pad_gt[PAD_N];
    for (int k = 0; k < PAD_N; k++) pad_gt[k] = clampf(sc.pad_g[k], 0, 2);
    float m_t = clampf(sc.motion, 0, 1);
    float crack_p = clampf(sc.crackle, 0, 3000) / (float)AUDIO_SR, rain_p = clampf(sc.rain, 0, 3000) / (float)AUDIO_SR;
    float rumble = clampf(sc.rumble, 0, 1);
    if (sc.thunder != thunder_seen) { thunder_seen = sc.thunder; thunder_env = 1.0f; thunder_fc = 1600.0f; }
    if (thunder_env > 0.001f) { thunder_fc += 0.03f * (90.0f - thunder_fc); svf_set(&thunder_lp, thunder_fc, 0.8f); }
    svf_set(&wind_bp, 700.0f + 1800.0f * motion_s, 0.9f);
    svf_set(&rain_bp, 2600.0f, 3.0f);
    float hunt_rate = clampf(sc.hunt_rate, 0.2f, 30.0f), hunt_f = clampf(sc.hunt_pitch, 100.0f, 6000.0f);

    int live = 0;
    float mx_p = 0, mx_r = 0, mx_s = 0;
    for (int i = 0; i < n; i++) {
        uint32_t now = f0 + (uint32_t)i;
        lv_pkt += d_pkt; lv_room += d_room; lv_storm += d_storm; lv_ticks += d_ticks; lv_hunt += d_hunt; lv_verb += d_verb; lv_echo += d_echo;
        if (lv_hunt < 0) lv_hunt = 0;
        // packet voices
        float dry = 0, send = 0;
        for (int k = 0; k < NV; k++) {
            voice_t *v = &vo[k];
            if (!v->on) continue;
            if (v->delay > 0) { v->delay--; continue; }
            float y = voice_run(v);
            if (v->stealing) {
                v->fade -= 1.0f / (float)FADE_N;
                if (v->fade <= 0) { strike_t nx = v->next; int32_t d = (int32_t)(nx.at - now); voice_start(v, &nx, d < 0 ? 0 : d > AUDIO_SR ? AUDIO_SR : d); continue; }
                y *= v->fade;
            }
            dry += y * (1.0f - v->wet); send += y * v->wet;
        }
        // ticks
        for (int k = 0; k < tick_n; ) {
            if ((int32_t)(tick_at[k] - now) <= 0) { tick_env += tick_amp[k]; if (tick_env > 1.2f) tick_env = 1.2f; tick_at[k] = tick_at[--tick_n]; tick_amp[k] = tick_amp[tick_n]; }
            else k++;
        }
        float tn = nz(), th = tn - tick_hp_x + 0.6f * tick_hp_y; tick_hp_x = tn; tick_hp_y = th;
        float tick = th * tick_env * 0.35f * lv_ticks; tick_env *= 0.93f;
        // rain
        if (rng_uni(&seed) < rain_p) rain_env += 0.35f * rng_uni(&seed);
        float rain = svf_bp(&rain_bp, nz(), NULL) * rain_env * 0.5f; rain_env *= 0.985f;
        float pkt = (dry + tick + rain) * lv_pkt;
        bright_lp += bright_k * (pkt - bright_lp);
        pkt = bright_lp;
        float pkt_send = send * lv_pkt;
        // room pad
        motion_s += 0.0002f * (m_t - motion_s);
        float pad = 0;
        if (lv_room > 0.0005f) {
            float det = 1.0015f + 0.004f * motion_s;
            for (int k = 0; k < PAD_N; k++) {
                if ((i & 15) == 0) { pad_fs[k] += 0.02f * (clampf(sc.pad_f[k], 20.0f, 8000.0f) - pad_fs[k]); pad_gs[k] += 0.004f * (pad_gt[k] - pad_gs[k]); }
                float f = pad_fs[k] / (float)AUDIO_SR;
                ph_a[k] = wrap1(ph_a[k] + f); ph_b[k] = wrap1(ph_b[k] + f * det);
                pad += pad_gs[k] * (sin_turns(ph_a[k]) + sin_turns(ph_b[k]));
            }
            pad *= 0.045f * (0.15f + 0.85f * motion_s);
            float w = svf_bp(&wind_bp, nz(), NULL);
            wind_lp += 0.3f * (w - wind_lp);
            pad += wind_lp * 0.12f * motion_s * motion_s;
            pad *= lv_room;
        }
        // storm
        float storm = 0;
        if (lv_storm > 0.0005f) {
            if (rng_uni(&seed) < crack_p) { float a = rng_uni(&seed); crack_env += a * a * a + 0.05f; }
            float cn = nz(), ch = cn - crack_hp_x + 0.3f * crack_hp_y; crack_hp_x = cn; crack_hp_y = ch;
            float crack = ch * crack_env * 0.5f; crack_env *= 0.82f;
            brown1 += 0.025f * (nz() - brown1); brown2 += 0.025f * (brown1 - brown2);
            float rum = brown2 * 7.0f * rumble;
            float thu = 0;
            if (thunder_env > 0.0005f) { thu = svf_lp(&thunder_lp, nz()) * thunder_env * 1.6f; thunder_env *= 0.99993f; }
            storm = (crack + rum + thu) * lv_storm;
        }
        // hunt
        float hunt = 0;
        if (lv_hunt > 0.0005f) {
            beep_ph += hunt_rate / (float)AUDIO_SR;
            if (beep_ph >= 1.0f) { beep_ph -= 1.0f; beep_env = 1.0f; click_env = 1.0f; snd_beeps++; }
            beep_tone_ph = wrap1(beep_tone_ph + hunt_f / (float)AUDIO_SR);
            hunt = (sin_turns(beep_tone_ph) * beep_env * 0.30f + nz() * click_env * 0.15f) * lv_hunt;
            beep_env *= 0.9993f; click_env *= 0.8f;
        }
        // space: echo and reverb
        float mix = pkt + pad * 0.6f + storm + hunt;
        float verb_in = pkt_send + pad * 0.5f + storm * 0.25f + hunt * 0.15f + pkt * 0.15f;
        float wet = 0;
        if (echo_buf) {
            echo_d += 0.0004f * (echo_t - echo_d);
            float e = dline_read(&(dline_t){ echo_buf, ECHO_LEN - 1, echo_w }, echo_d);
            echo_lp += 0.35f * (e - echo_lp);
            echo_buf[echo_w & (ECHO_LEN - 1)] = flush_denormal((pkt + pkt_send) * lv_echo + echo_lp * efb);
            echo_w++;
            mix += echo_lp * 0.8f;
            verb_in += echo_lp * 0.3f;
        }
        if (fdn_buf) {
            float y[4];
            for (int k = 0; k < 4; k++) {
                fdn_len[k] += 0.0002f * (lens[k] - fdn_len[k]);
                dline_t dl = { fdn_buf + k * FDN_LEN, FDN_LEN - 1, fdn_w };
                y[k] = dline_read(&dl, fdn_len[k]);
                fdn_lp[k] += 0.55f * (y[k] - fdn_lp[k]);
            }
            float a = fdn_lp[0], b = fdn_lp[1], c = fdn_lp[2], d = fdn_lp[3];
            float h[4] = { 0.5f * (a + b + c + d), 0.5f * (a - b + c - d), 0.5f * (a + b - c - d), 0.5f * (a - b - c + d) };
            for (int k = 0; k < 4; k++) fdn_buf[k * FDN_LEN + (fdn_w & (FDN_LEN - 1))] = flush_denormal(verb_in * 0.35f + fdn_g[k] * h[k]);
            fdn_w++;
            wet = (y[0] + y[1] + y[2] + y[3]) * 0.5f * lv_verb;
        }
        out[i] = mix + wet;
        float ap = fabsf(pkt), ar = fabsf(pad), as = fabsf(storm);
        if (ap > mx_p) mx_p = ap;
        if (ar > mx_r) mx_r = ar;
        if (as > mx_s) mx_s = as;
    }
    for (int k = 0; k < NV; k++) {
        voice_t *v = &vo[k];
        if (!v->on) continue;
        live++;
        v->age += n;
        if (v->delay > 0 || v->stealing || v->exc_n > 0 || v->gate > 0) { v->quiet = 0; continue; }
        float e = 0;
        if (v->t == T_PLUCK || v->t == T_BELL || v->t == T_KALIMBA) { for (int j = 0; j < v->nm; j++) e += fabsf(v->m[j].y1) + fabsf(v->m[j].y2); }
        else e = v->env;
        if (e * v->amp < 2e-5f) { if (++v->quiet > 2) v->on = 0; } else v->quiet = 0;
    }
    snd_voices = live;
    for (int k = 0; k < 4; k++) fdn_lp[k] = flush_denormal(fdn_lp[k]);
    echo_lp = flush_denormal(echo_lp); bright_lp = flush_denormal(bright_lp); wind_lp = flush_denormal(wind_lp);
    brown1 = flush_denormal(brown1); brown2 = flush_denormal(brown2);
    pk_pkt = mx_p > pk_pkt ? mx_p : pk_pkt * 0.97f; pk_room = mx_r > pk_room ? mx_r : pk_room * 0.97f; pk_storm = mx_s > pk_storm ? mx_s : pk_storm * 0.97f;
    snd_out_pkt = pk_pkt; snd_out_room = pk_room; snd_out_storm = pk_storm;
}
