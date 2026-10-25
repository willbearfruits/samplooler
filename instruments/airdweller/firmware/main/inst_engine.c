// AIRDWELLER's audio task. PLACEHOLDER SOUND: the grid, the trigger queue, the scheduling, the loops' playback and the
// played report are the real ones (AIR's) and stay; the voices and the material below are stand-ins until the
// synthesized kit and the material arrive (inst_ad.h says what they are).
#include "inst_ad.h"
#include "dsp.h"
#include <string.h>

// ---------------------------------------------------------------- the contract's variables
loop_t eng_loops[NLOOP];
volatile int   eng_run = 1;
volatile int   eng_role, eng_pad_voice[NPAD] = { DV_HAT, DV_KICK, DV_SNAP, DV_CLANG }, eng_mat_chord, eng_cut, eng_voices_on;
volatile float eng_pad_tune[NPAD], eng_kick_hz = 55.0f, eng_drum_decay = 1.0f, eng_squeeze;
volatile float eng_mat_morph, eng_mat_hz = 220.0f, eng_mat_ring = 2.0f, eng_mat_bright = 0.5f, eng_bend, eng_bow, eng_creak, eng_mat_level = 1.0f, eng_duck;
volatile float eng_drive, eng_tone = 0.6f, eng_echo_s = 0.3f, eng_echo_fb, eng_echo_mix, eng_room, eng_room_size = 0.5f, eng_level = 0.8f;
volatile float eng_out_lvl, eng_mat_lvl, eng_mode_amp[8];

// ---------------------------------------------------------------- the queues
SPSC(trigq, trig_t, 32)
SPSC(playq, played_t, 64)
typedef struct { uint32_t at; int vel, voice; } strike_t;
SPSC(strkq, strike_t, 32)
static trigq_t trigs;
static playq_t played;
static strkq_t strikes;
void eng_trigger(const trig_t *t) { trigq_push(&trigs, t); }
bool eng_played_pop(played_t *p) { return playq_pop(&played, p); }
void eng_strike(uint32_t at, int vel, int voice) { strike_t s = { at, vel, voice }; strkq_push(&strikes, &s); }
#define NPEND 32
typedef struct { uint32_t at; int pad, vel, src, voice; bool strike; } pend_t;   // waiting for its frame
static pend_t pend[NPEND]; static int npend;


static uint32_t frame0;

// ---------------------------------------------------------------- the grid (a seqlock: the UI writes, any task reads)
static grid_t grid_v = { 19200, 0, 0.0f };
static volatile uint32_t grid_seq;
void eng_set_grid(const grid_t *g)
{
    __atomic_store_n(&grid_seq, grid_seq + 1, __ATOMIC_RELEASE);
    grid_v = *g;
    __atomic_store_n(&grid_seq, grid_seq + 1, __ATOMIC_RELEASE);
}
void eng_get_grid(grid_t *g)
{
    uint32_t s0, s1;
    do { s0 = __atomic_load_n(&grid_seq, __ATOMIC_ACQUIRE); *g = grid_v; s1 = __atomic_load_n(&grid_seq, __ATOMIC_ACQUIRE); } while (s0 != s1 || (s0 & 1));
}
int64_t eng_floor_div(int64_t a, int64_t b) { int64_t q = a / b; return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q; }
int64_t eng_tick_at(const grid_t *g, uint32_t frame)
{
    if (!g->fpb) return 0;
    int64_t rel = (int32_t)(frame - g->origin);
    return eng_floor_div(rel * PPB - (int64_t)(g->frac * (float)PPB), (int64_t)g->fpb);
}
uint32_t eng_frame_of(const grid_t *g, int64_t tick)
{
    int64_t f = eng_floor_div(tick * (int64_t)g->fpb + PPB / 2, PPB);
    return g->origin + (uint32_t)(int32_t)f + (uint32_t)(int32_t)(g->frac + 0.5f);
}
uint16_t eng_hit_tick(const loop_t *l, const hit_t *h)
{
    int t = h->tick;
    if ((t % (PPB / 2)) == PPB / 4) t += (int)(pv[P_SWING] * 0.01f * (float)(PPB / 4));
    int len = l->bars * BAR_TICKS;
    if (len > 0 && t >= len) t -= len;
    return (uint16_t)t;
}

// ---------------------------------------------------------------- PLACEHOLDER voices: a sine with a pitch drop each
#define NVO 10
typedef struct { bool on; int delay; float ph, f0, f1, kf, a, ka; } vo_t;
static vo_t vo[NVO];
static float mat_ph, mat_a;
static const float dv_hz[NDV] = { 55, 400, 900, 3000, 1800, 50, 2500, 300, 0 };
static void voice_start(int voice, float vel, int delay)
{
    if (voice < 0 || voice >= DV_OFF) return;
    int k = 0; float lo = 1e9f;
    for (int i = 0; i < NVO; i++) { if (!vo[i].on) { k = i; break; } if (vo[i].a < lo) { lo = vo[i].a; k = i; } }
    float f = voice == DV_KICK || voice == DV_SUB ? eng_kick_hz : dv_hz[voice];
    vo[k] = (vo_t){ true, delay, 0.0f, f * 6.0f, f, onepole_s(0.01f), vel * 0.3f, onepole_s(0.15f * eng_drum_decay) };
}
static void fire(const pend_t *p, int d)
{
    float v = (float)p->vel / 127.0f;
    if (p->strike) { mat_a = fmaxf(mat_a, v * 0.3f); return; }
    if (eng_role == ROLE_DRUM) voice_start(p->voice, v, d);
    else mat_a = fmaxf(mat_a, v * 0.3f);
    played_t pl = { frame0 + (uint32_t)d, (uint8_t)p->pad, (uint8_t)p->vel, (uint8_t)p->src, (uint8_t)p->voice };
    playq_push(&played, &pl);
}
static void schedule(uint32_t at, int pad, int vel, int src, int voice, bool strike)
{
    pend_t p = { at, pad, vel, src, voice, strike };
    int32_t d = (int32_t)(at - frame0);
    if (at == 0 || d < 0) { fire(&p, 0); return; }
    if (npend < NPEND) pend[npend++] = p;
}

// ---------------------------------------------------------------- the loops' playback (AIR's)
static void loop_block(int n)
{
    if (!eng_run) return;
    grid_t g; eng_get_grid(&g);
    if (!g.fpb) return;
    int64_t t_lo = eng_tick_at(&g, frame0 - 1) + 1, t_hi = eng_tick_at(&g, frame0 + (uint32_t)n - 1);
    if (t_hi < t_lo) return;
    for (int64_t t = t_lo; t <= t_hi; t++) {
        uint32_t at = eng_frame_of(&g, t);
        for (int k = 0; k < NLOOP; k++) {
            loop_t *l = &eng_loops[k];
            int bars = l->bars, nh = l->n;
            if (!bars || !l->on || !nh) continue;
            int len = bars * BAR_TICKS;
            int64_t since = t - l->start;
            int rel = (int)(since - eng_floor_div(since, len) * len);
            int32_t cycle = (int32_t)eng_floor_div(since, len);
            for (int i = 0; i < nh; i++) {
                const hit_t *h = &l->hits[i];
                if (eng_hit_tick(l, h) != rel || h->born == cycle) continue;
                schedule(at, h->pad, h->vel, SRC_LOOP, eng_pad_voice[h->pad], false);
            }
        }
    }
}

void eng_init(void) { }

void eng_audio(const float *in, float *out, int n)
{
    (void)in;
    frame0 = audio_frames();
    trig_t t;
    while (trigq_pop(&trigs, &t)) schedule(t.at, t.pad, t.vel, t.src, eng_pad_voice[t.pad], false);
    strike_t s;
    while (strkq_pop(&strikes, &s)) schedule(s.at, -1, s.vel, SRC_REMOTE, s.voice, true);
    loop_block(n);
    for (int i = 0; i < npend;) {
        int32_t d = (int32_t)(pend[i].at - frame0);
        if (d < n) { fire(&pend[i], d < 0 ? 0 : d); pend[i] = pend[--npend]; } else i++;
    }
    memset(out, 0, (size_t)n * sizeof(float));
    int on = 0;
    for (int k = 0; k < NVO; k++) {
        vo_t *v = &vo[k];
        if (!v->on) continue;
        on++;
        for (int i = 0; i < n; i++) {
            if (v->delay > 0) { v->delay--; continue; }
            v->f0 += v->kf * (v->f1 - v->f0);
            v->ph = wrap1(v->ph + v->f0 / (float)AUDIO_SR);
            out[i] += v->a * sin_turns(v->ph);
            v->a -= v->ka * v->a;
        }
        if (v->a < 1e-4f) v->on = false;
    }
    eng_voices_on = on;
    float b = eng_bow * 0.15f, mh = eng_mat_hz * fast_exp2(eng_bend / 12.0f);
    for (int i = 0; i < n; i++) {
        mat_ph = wrap1(mat_ph + mh / (float)AUDIO_SR);
        out[i] += (mat_a + b) * eng_mat_level * sin_turns(mat_ph);
        mat_a *= 0.99995f;
    }
    eng_mat_lvl = mat_a + b;
    float pk = 0.0f, g = eng_cut ? 0.0f : eng_level;
    for (int i = 0; i < n; i++) { out[i] *= g; float a = fabsf(out[i]); if (a > pk) pk = a; }
    eng_out_lvl = pk > eng_out_lvl ? pk : eng_out_lvl * 0.98f;
}
