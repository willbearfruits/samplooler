// BEAT (inst_beat.h): the chopper.
//
// Voices read the break's slices (KIT's kit_break()) at a rate, forwards or backwards, with a 1 ms fade in and a 2 to
// 3 ms fade out at every cut; a new hit fades the last one out under it, so rolls and tails overlap and never click.
// SPEED: SLICE plays each slice at its own speed (cut by the next hit, or let ring); TIED plays the whole break at
// the machine's tempo (slowed with it: thunder at DRONE).
//
// What plays each step is planned a bar at a time, at the bar's first step:
//   AUTO   the break as it is, then the edits, more towards BREAKCORE (EDITS, ROLLS, REVERSE, DRILL, and the tilt's
//          chaos): a beat swapped for another beat of the break, kicks and snares shuffled, rolls rising or falling,
//          snare rushes into the bar line, reversed slices, pitch jumps, cut-short slices, stutters, a tape stop at
//          a phrase's end, drill'n'bass micro rolls and sweeps; bars of 7, 5 or 9 eighths (ODD BARS) against the
//          flock's 4/4; blast bars (BLAST) at HARDCORE; towards SLUDGE and DRONE only the big hits, pitched down
//          (DOOM PITCH, in SLICE) and let ring, and now and then a reversed swell or a thunder roll into the phrase
//   MINE   the step grid: 16 steps, each a slice of the break, the break's own, or off, with a trick
// Under it, the gabber kick (a swept sine clipped hard) on every beat (GABBER KICK: AUTO is at HARDCORE).
// The hands, read every block: chaos adds edits; the stick up rolls the slice now (faster and rising the further);
// the roll right stutters (the last bit again, shorter the further); the roll left plays backwards and smears; the
// stick down dives (an octave at full). A knock is a fill of the zone's kind: a snare rush (BREAKCORE), a blast
// (HARDCORE), a tom slam and a crash (SLUDGE), a thunder roll (DRONE); harder, longer.
//
// The grid's editor (the knob held in play, or BEAT > EDIT GRID): turn: the step; press and turn: its slice (the
// break's own, each slice of the break, off); tap: its trick; hold: done. REC tapped: the grid stops (each slice is
// heard as it is picked) or runs; REC held 1 s: back to the break's order. The stick: left and right the step, up and
// down the trick. The grid lives in seven hidden params (bgr0..bgr6, 10 bits a step), so it is saved and in sounds.
#include "wreck.h"
#include "ui.h"
#include "params.h"
#include <stdio.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_attr.h"          // the audio path in internal RAM by itself, whatever the linker fragment says
#else
#define IRAM_ATTR
#define DRAM_ATTR
#endif

// ---------------------------------------------------------------- params
enum {
    BP_LEVEL, BP_EDITS, BP_ROLLS, BP_REV, BP_DRILL, BP_PITCH, BP_SPEED, BP_ODD, BP_PAT, BP_GEDIT,
    BP_GABBER, BP_KTUNE, BP_KDIRT, BP_BLAST, BP_SWING, BP_DOOM, BP_SEND,
    BP_G0, BP_N = BP_G0 + 7
};
#define GWORDS 7               // the grid: 16 steps of 10 bits in seven 24-bit words
#define GSTEPS 16
#define P(i) MP(M_BEAT, (i))
static const char *const pages[] = { "BEAT", "BEAT 2" };
static const char *const speed_l[] = { "SLICE", "TIED" };
static const char *const odd_l[] = { "OFF", "7/8", "5/8", "9/8", "AUTO" };
static const char *const pat_l[] = { "AUTO", "MINE" };
static const char *const gab_l[] = { "OFF", "ON", "AUTO" };
#define HID (PF_INT | PF_HIDE | PF_NOCC)
static const param_t params[BP_N] = {
    [BP_LEVEL]  = { "LEVEL", "blvl", 0, PK_NUM, PF_INT, 0, 100, 5, 80, "%", NULL, "HOW LOUD THE BREAK IS" },
    [BP_EDITS]  = { "EDITS", "bedt", 0, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "HOW CHOPPED UP IT GETS" },
    [BP_ROLLS]  = { "ROLLS", "brol", 0, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "ROLLS AND SNARE RUSHES" },
    [BP_REV]    = { "REVERSE", "brev", 0, PK_NUM, PF_INT, 0, 100, 5, 30, "%", NULL, "SLICES PLAYED BACKWARDS" },
    [BP_DRILL]  = { "DRILL", "bdri", 0, PK_NUM, PF_INT, 0, 100, 5, 20, "%", NULL, "MICRO ROLLS AND SWEEPS" },
    [BP_PITCH]  = { "PITCH", "bpit", 0, PK_NUM, PF_INT | PF_SIGNED, -12, 12, 1, 0, "st", NULL, "PITCH OF THE BREAK" },
    [BP_SPEED]  = { "SPEED", "bspd", 0, PK_ENUM, 0, 0, 1, 1, 0, "", speed_l, "TIED: SLOWS WITH THE TEMPO" },
    [BP_ODD]    = { "ODD BARS", "bodd", 0, PK_ENUM, 0, 0, 4, 1, 4, "", odd_l, "ODD BARS AGAINST THE 4/4" },
    [BP_PAT]    = { "PATTERN", "bpat", 0, PK_ENUM, 0, 0, 1, 1, 0, "", pat_l, "MINE: THE GRID YOU DREW" },
    [BP_GEDIT]  = { "EDIT GRID", "bged", 0, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "CHOP IT YOURSELF: 16 STEPS" },
    [BP_GABBER] = { "GABBER KICK", "bgab", 1, PK_ENUM, 0, 0, 2, 1, 2, "", gab_l, "A HARD KICK ON EVERY BEAT" },
    [BP_KTUNE]  = { "KICK TUNE", "bktn", 1, PK_NUM, PF_INT, 35, 90, 1, 52, "Hz", NULL, "THE LOW NOTE OF THE KICK" },
    [BP_KDIRT]  = { "KICK DIRT", "bkdr", 1, PK_NUM, PF_INT, 0, 100, 5, 70, "%", NULL, "HOW HARD THE KICK CLIPS" },
    [BP_BLAST]  = { "BLAST", "bbla", 1, PK_NUM, PF_INT, 0, 100, 5, 30, "%", NULL, "BLAST BEATS AT HARDCORE" },
    [BP_SWING]  = { "SWING", "bswg", 1, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "OFFBEAT SIXTEENTHS LATE" },
    [BP_DOOM]   = { "DOOM PITCH", "bdwn", 1, PK_NUM, PF_INT, -12, 0, 1, -5, "st", NULL, "PITCHED DOWN TOWARDS DOOM" },
    [BP_SEND]   = { "SEND", "bsnd", 1, PK_NUM, PF_INT, 0, 100, 5, 20, "%", NULL, "THE BREAK INTO THE REVERB" },
    [BP_G0 + 0] = { "GRID 1", "bgr0", 0, PK_NUM, HID, 0, 16777215, 1, 0, "", NULL, NULL },
    [BP_G0 + 1] = { "GRID 2", "bgr1", 0, PK_NUM, HID, 0, 16777215, 1, 0, "", NULL, NULL },
    [BP_G0 + 2] = { "GRID 3", "bgr2", 0, PK_NUM, HID, 0, 16777215, 1, 0, "", NULL, NULL },
    [BP_G0 + 3] = { "GRID 4", "bgr3", 0, PK_NUM, HID, 0, 16777215, 1, 0, "", NULL, NULL },
    [BP_G0 + 4] = { "GRID 5", "bgr4", 0, PK_NUM, HID, 0, 16777215, 1, 0, "", NULL, NULL },
    [BP_G0 + 5] = { "GRID 6", "bgr5", 0, PK_NUM, HID, 0, 16777215, 1, 0, "", NULL, NULL },
    [BP_G0 + 6] = { "GRID 7", "bgr6", 0, PK_NUM, HID, 0, 16777215, 1, 0, "", NULL, NULL },
};
// BEAT's part of each sound (wreck.h has the list; WRECK_NOTES.md what each is at both ends)
static const char *const sounds[WR_NSOUNDS] = {
    "",                                                                                  // WRECK: as it comes
    "bedt=70 brol=85 brev=20 bdri=10 bodd=1 bgab=0 bbla=0 bdwn=-3 bsnd=25",             // SNARES: 7/8, rushes
    "bedt=70 brol=70 brev=25 bdri=95 bodd=0 bgab=0 bbla=0 bdwn=-7 bsnd=25",             // DRILL: micro rolls, sweeps
    "bedt=30 brol=30 brev=15 bdri=0 bodd=0 bgab=1 bkdr=95 bktn=48 bbla=20 bdwn=-7",     // GABBA: the kick everywhere
    "bedt=40 brol=40 brev=15 bdri=10 bodd=0 bgab=2 bkdr=60 bbla=90 bdwn=-5",            // GRIND: blasts
    "bedt=60 brol=60 brev=35 bdri=20 bodd=4 bgab=2 bbla=50 bdwn=-5 bsnd=25",            // BAROQUE
    "bedt=20 brol=20 brev=10 bdri=0 bodd=0 bgab=0 bbla=0 bswg=35 bdwn=-7 bsnd=35",      // WIZARD: a stoner swing
    "bedt=15 brol=10 brev=10 bdri=0 bodd=0 bgab=0 bbla=0 bswg=15 bdwn=-3 bsnd=15 blvl=70",   // EARTH: dry, sparse
    "bedt=80 brol=60 brev=50 bdri=40 bodd=4 bgab=0 bbla=10 bspd=1 bsnd=30 blvl=70",    // DRONE: noise, then rumble
    "bedt=40 brol=40 brev=25 bdri=10 bodd=0 bgab=2 bbla=20 bspd=1 bsnd=40",            // THUNDER: tied to the tempo
};

// ---------------------------------------------------------------- the break, mapped (audio task; again when it changes)
#define NSL KIT_SLICES_MAX
#define NKIND (KS_OTHER + 1)
typedef struct {
    const brk_t *b;
    uint32_t serial;
    const int16_t *pcm;
    int32_t len;
    int     steps, n;
    float   six;                           // the break's sixteenth, frames
    int32_t start[NSL], end[NSL], ring[NSL];   // a slice, where its sound ends (the next step's hit), and where letting
                                           // it ring ends (the next big hit)
    uint8_t kind[NSL], bstep[NSL], peak[NSL];
    int8_t  at[64], cover[64];             // the slice starting on each sixteenth of the break (-1), and the one sounding
    int8_t  by[NKIND][8]; uint8_t nby[NKIND];   // the loudest slices of each kind
    int8_t  kick, snare, crash, tom;       // the loudest of each (or a stand-in; crash and tom may be -1)
    int     pkmax;
} bmap_t;
static bmap_t M;

#ifndef ESP_PLATFORM
const brk_t *beat_test_break;              // tools/sim: a break of the test's own instead of KIT's
typedef struct { uint32_t frame; int16_t slice; int8_t lane, rev; uint8_t n; float rate, lvl; } beat_trig_t;
beat_trig_t beat_tlog[16384]; int beat_tlog_n;   // every hit started, for the tests
double beat_life_sum; int beat_life_n;     // how long the slices played (frames), for the tests
int beat_steal_n; float beat_steal_env;    // voices stolen while still sounding, and the loudest one's level (int16 x env)
#endif
static inline IRAM_ATTR const brk_t *the_break(void)
{
#ifndef ESP_PLATFORM
    if (beat_test_break) return beat_test_break;
#endif
    return kit_break();
}

static IRAM_ATTR void map_build(const brk_t *b)
{
    M.b = b; M.serial = b->serial; M.pcm = b->pcm; M.len = b->pcm ? b->len : 0;
    M.steps = clampi(b->steps, 1, 64); M.n = 0;
    memset(M.at, -1, sizeof M.at); memset(M.cover, -1, sizeof M.cover); memset(M.nby, 0, sizeof M.nby);
    M.kick = M.snare = M.crash = M.tom = -1; M.pkmax = 1;
    if (M.len < 512) { M.len = 0; return; }
    M.six = (float)M.len / (float)M.steps;
    int n = clampi(b->nslices, 0, NSL);
    if (n == 0) {                                                 // nothing found in it: a slice on every sixteenth
        n = M.steps;
        for (int j = 0; j < n; j++) {
            M.start[j] = (int32_t)((float)j * M.six); M.bstep[j] = (uint8_t)j; M.peak[j] = 128;
            M.kind[j] = (uint8_t)(j % 8 == 0 ? KS_KICK : j % 8 == 4 ? KS_SNARE : KS_HAT);
        }
    } else for (int j = 0; j < n; j++) {
        int32_t s = b->slice[j];
        M.start[j] = s < 0 ? 0 : s > M.len - 256 ? M.len - 256 : s;
        M.bstep[j] = (uint8_t)clampi(b->step[j], 0, M.steps - 1);
        M.kind[j] = (uint8_t)clampi(b->kind[j], 0, KS_OTHER);
        M.peak[j] = b->peak[j];
    }
    M.n = n;
    for (int j = 0; j < n; j++) if (M.peak[j] > M.pkmax) M.pkmax = M.peak[j];
    int big_thr = M.pkmax * 45 / 100;
    for (int j = 0; j < n; j++) {
        int32_t e = M.len - 2, r = M.len - 2;
        for (int q = j + 1; q < n; q++) if (M.bstep[q] > M.bstep[j] && M.start[q] > M.start[j]) { e = M.start[q]; break; }
        for (int q = j + 1; q < n; q++) {
            int k = M.kind[q];
            if (M.start[q] > M.start[j] + 64 && (k == KS_KICK || k == KS_SNARE || k == KS_CRASH || k == KS_TOM) && M.peak[q] >= big_thr) { r = M.start[q]; break; }
        }
        if (e < M.start[j] + 128) e = M.start[j] + 128 < M.len - 2 ? M.start[j] + 128 : M.len - 2;
        if (r < e) r = e;
        M.end[j] = e; M.ring[j] = r;
    }
    for (int j = n - 1; j >= 0; j--) M.at[M.bstep[j]] = (int8_t)j;
    int8_t c = -1;
    for (int s = 0; s < M.steps; s++) { if (M.at[s] >= 0) c = M.at[s]; M.cover[s] = c; }
    for (int s = 0; s < M.steps && M.cover[s] < 0; s++) M.cover[s] = (int8_t)(n - 1);   // the last hit rings over the top
    for (int j = 0; j < n; j++) {                                 // by kind, loudest first
        int k = M.kind[j], m = M.nby[k];
        if (m < 8) M.nby[k]++; else if (M.peak[j] <= M.peak[(int)M.by[k][7]]) continue; else m = 7;
        while (m > 0 && M.peak[(int)M.by[k][m - 1]] < M.peak[j]) { M.by[k][m] = M.by[k][m - 1]; m--; }
        M.by[k][m] = (int8_t)j;
    }
    M.kick = M.nby[KS_KICK] ? M.by[KS_KICK][0] : (M.at[0] >= 0 ? M.at[0] : 0);
    if (M.nby[KS_SNARE]) M.snare = M.by[KS_SNARE][0];
    else {                                                        // the loudest that is not the kick
        int best = -1;
        for (int j = 0; j < n; j++) if (j != M.kick && (best < 0 || M.peak[j] > M.peak[best])) best = j;
        M.snare = (int8_t)(best >= 0 ? best : M.kick);
    }
    M.crash = M.nby[KS_CRASH] ? M.by[KS_CRASH][0] : -1;
    M.tom = M.nby[KS_TOM] ? M.by[KS_TOM][0] : -1;
}

// ---------------------------------------------------------------- the voices
// One envelope, run as env = env * mul + add: an attack (a ramp up), a hold (flat, or dying away when let ring),
// a release (a ramp down). The int16 scale is in the level.
#define NV 12
typedef struct {
    const int16_t *pcm;
    float   pos, rate, glide;              // where it reads (frames), how fast (negative: backwards), rate *= glide
    float   env, mul, add, peak, hold_mul, rel;
    int32_t lo, hi, left, hold, life;      // the frames it may read; frames left in this phase; the hold's length
    uint32_t serial;
    int8_t  phase, lane;                   // 0 off, 1 attack, 2 hold, 3 release; lane 0 the main hits, 1 the layer
    int16_t slice;
} voice_t;
static voice_t V[NV];
static int8_t lane_v[2] = { -1, -1 };      // the voice each lane plays now
static float choke_rel = 64.0f;            // how long the last hit fades under the next (the smear makes it long)

static inline IRAM_ATTR float expm(float a) { return 1.0f + a * (1.0f + a * (0.5f + a * (1.0f / 6.0f))); }   // e^a, small a
static inline IRAM_ATTR uint32_t hash32(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x ? x : 0x9e3779b9u;
}

static IRAM_ATTR void v_off(voice_t *v)
{
#ifndef ESP_PLATFORM
    if (v->lane == 0) { beat_life_sum += v->life; beat_life_n++; }
#endif
    v->phase = 0;
    int k = (int)(v - V);
    for (int l = 0; l < 2; l++) if (lane_v[l] == k) lane_v[l] = -1;
}
static IRAM_ATTR void v_release(voice_t *v, float frames)
{
    if (!v->phase) return;
    if (frames < 8.0f) frames = 8.0f;
    if (v->phase == 3 && (float)v->left <= frames) return;
    v->phase = 3; v->left = (int32_t)frames; v->mul = 1.0f; v->add = -v->env / frames;
}
static IRAM_ATTR voice_t *v_alloc(void)
{
    int best = 0; float be = 1e30f;
    for (int k = 0; k < NV; k++) {
        if (!V[k].phase) return &V[k];
        float e = V[k].phase == 3 ? V[k].env : V[k].env + 1.0f;   // a voice fading out goes first, the quietest
        if (e < be) { be = e; best = k; }
    }
#ifndef ESP_PLATFORM
    beat_steal_n++;
    if (V[best].env > beat_steal_env) beat_steal_env = V[best].env;
#endif
    v_off(&V[best]);
    return &V[best];
}

// Play [lo, hi] of a buffer from pos at a rate (negative: backwards), at a level, for `gate` frames (0: to the end
// of the region), with fades of att and rel frames; ring_mul < 1 lets the hold die away; glide bends the rate.
static IRAM_ATTR voice_t *v_play(int lane, const int16_t *pcm, uint32_t serial, int32_t lo, int32_t hi, float pos, float rate,
                                 float lvl, int32_t gate, float att, float rel, float ring_mul, float glide, int slice)
{
    if (!pcm || hi <= lo + 8 || lvl <= 0.0f) return NULL;
    if (lane_v[lane] >= 0) { voice_t *o = &V[lane_v[lane]]; v_release(o, o->hold_mul < 1.0f && choke_rel < 160.0f ? 160.0f : choke_rel); }
    voice_t *v = v_alloc();
    if (att < 8.0f) att = 8.0f;
    v->pcm = pcm; v->serial = serial; v->lo = lo; v->hi = hi; v->pos = pos; v->rate = rate; v->glide = glide;
    v->peak = lvl * (1.0f / 32768.0f); v->env = 0.0f; v->mul = 1.0f; v->add = v->peak / att; v->left = (int32_t)att;
    v->hold = gate > 0 ? (gate > (int32_t)att ? gate - (int32_t)att : 1) : 0x3fffffff;
    v->hold_mul = ring_mul; v->rel = rel; v->phase = 1; v->lane = (int8_t)lane; v->slice = (int16_t)slice; v->life = 0;
    lane_v[lane] = (int8_t)(v - V);
    return v;
}

// every voice over m frames into acc; dm: the dive, a multiplier on every rate
static IRAM_ATTR void v_run(voice_t *v, float *acc, int m, float dm)
{
    int i = 0;
    while (v->phase && i < m) {
        int run = m - i;
        if (v->left < run) run = v->left;
        float r = v->rate * dm;
        if (v->phase != 3) {                                      // fade out before the region's end
            float room = r > 0.0f ? (float)v->hi - v->pos : v->pos - (float)v->lo;
            float lim = room / (fabsf(r) + 1e-6f) - v->rel;
            if (lim < 1.0f) { v_release(v, v->rel); continue; }
            if (lim < (float)run) run = (int)lim;
        }
        const int16_t *p = v->pcm;
        float pos = v->pos, e = v->env, mu = v->mul, ad = v->add, g = v->glide;
        int32_t lo = v->lo, hi = v->hi - 1;
        float *o = acc + i;
        for (int k = 0; k < run; k++) {
            int ip = (int)pos;
            ip = ip < lo ? lo : ip > hi ? hi : ip;
            float fr = pos - (float)ip;
            fr = fr < 0.0f ? 0.0f : fr > 1.0f ? 1.0f : fr;
            float a = (float)p[ip], b = (float)p[ip + 1];
            o[k] += (a + (b - a) * fr) * e;
            e = e * mu + ad;
            pos += r; r *= g;
        }
        v->pos = pos; v->env = e; v->rate = r / dm; v->left -= run; v->life += run; i += run;
        if (v->left <= 0) {
            if (v->phase == 1) { v->phase = 2; v->env = v->peak; v->mul = v->hold_mul; v->add = 0.0f; v->left = v->hold; }
            else if (v->phase == 2) v_release(v, v->rel);
            else v_off(v);
        } else if (v->phase == 2 && v->hold_mul < 1.0f && v->env < v->peak * 0.002f) v_release(v, 32.0f);
    }
}

// ---------------------------------------------------------------- the gabber kick
// A sine swept from six times KICK TUNE down to it, through a hard clip (KICK DIRT: how hard), with a body that
// dies away inside the clip (so it stays loud, then drops) and a gate around it. Two, so one fades as the next hits.
typedef struct { float ph, fe, fd, fk, pre, pre_mul, drive, lp, env, mul, add, peak; int32_t left, hold; int8_t phase; } kick_t;
static kick_t KK[2];
static int kk_cur;
#define KICK_LVL 0.21f

static volatile float kc_drive = 13.8f, kc_pre_mul = 0.9998f, kc_norm = 1.0f;   // from KICK DIRT (UI task)
static void kick_coefs(void)
{
    float dirt = P(BP_KDIRT) * 0.01f, tau = 0.06f + 0.1f * dirt;  // the body dies away inside the clip
    float drive = 1.1f + 26.0f * dirt * dirt;
    float clip = clampf(tau * logf(drive) / 0.45f, 0.0f, 1.0f);   // the longer it stays clipped (a square), the
    kc_norm = 1.0f / sqrtf(0.25f + 0.75f * clip);                 // louder it is: even that out
    kc_pre_mul = expf(-1.0f / (tau * (float)AUDIO_SR));
    kc_drive = drive;
}
static IRAM_ATTR void kick_release(kick_t *k, float frames)
{
    if (!k->phase || (k->phase == 3 && (float)k->left <= frames)) return;
    k->phase = 3; k->left = (int32_t)frames; k->mul = 1.0f; k->add = -k->env / frames;
}
static IRAM_ATTR void kick_start(float beat_frames, float lvl)
{
    kick_release(&KK[kk_cur], 96.0f);
    kk_cur ^= 1;
    kick_t *k = &KK[kk_cur];
    float fe = P(BP_KTUNE);
    k->ph = 0.0f; k->fe = fe; k->fd = fe * 5.0f; k->fk = expm(-1.0f / (0.026f * (float)AUDIO_SR));
    k->pre = 1.0f; k->pre_mul = kc_pre_mul; k->drive = kc_drive;
    k->peak = KICK_LVL * lvl * kc_norm;
    k->env = 0.0f; k->mul = 1.0f; k->add = k->peak / 16.0f; k->left = 16; k->phase = 1;
    float len = beat_frames * 0.85f, cap = 0.7f * (float)AUDIO_SR;
    k->hold = (int32_t)(len < cap ? len : cap) - 16 - 192;
    if (k->hold < 64) k->hold = 64;
}
static IRAM_ATTR void kick_run(kick_t *k, float *acc, int m, float dm)
{
    const float lpk = 0.62f, inv = dm / (float)AUDIO_SR;          // the low pass: about 5 kHz, the clip's fizz off
    int i = 0;
    while (k->phase && i < m) {
        int run = m - i;
        if (k->left < run) run = k->left;
        float ph = k->ph, fe = k->fe, fd = k->fd, fk = k->fk, pre = k->pre, pm = k->pre_mul, dr = k->drive, lp = k->lp;
        float e = k->env, mu = k->mul, ad = k->add;
        for (int j = 0; j < run; j++) {
            ph += (fe + fd) * inv; ph -= (float)(int)ph; fd *= fk;
            float x = sin_turns(ph) * pre * dr; pre *= pm;
            x = x > 1.0f ? 1.0f : x < -1.0f ? -1.0f : x;
            lp += lpk * (x - lp);
            acc[i + j] += lp * e;
            e = e * mu + ad;
        }
        k->ph = ph; k->fd = fd; k->pre = pre; k->lp = flush_denormal(lp); k->env = e; k->left -= run; i += run;
        if (k->left <= 0) {
            if (k->phase == 1) { k->phase = 2; k->env = k->peak; k->mul = 1.0f; k->add = 0.0f; k->left = k->hold; }
            else if (k->phase == 2) kick_release(k, 192.0f);
            else { k->phase = 0; k->lp = 0.0f; }
        }
    }
}

// ---------------------------------------------------------------- a step's plan
enum { F_REV = 1, F_RING = 2, F_DIVE = 4, F_RISE = 8, F_STOP = 16, F_CUT = 32 };
#define S_TIE  (-1)            // nothing new: what plays goes on
#define S_REST (-2)            // silence: what plays fades out
typedef struct {
    int8_t  slice, layer;      // the slice (or S_TIE, S_REST), and one played with it (a crash, a kick under a snare)
    uint8_t n, flags;          // hits in the step (2..64: a roll), F_*
    int8_t  pitch, rise;       // semitones: the first hit, and how far the roll goes up (down) by its last
    uint8_t lvl0, lvl1;        // the first and the last hit's level, /255
    uint8_t gate;              // F_CUT: the part of the step it plays, /255; a roll: the part of each hit (0: most)
    uint8_t span;              // F_STOP: the steps it takes
} plan_t;
static const plan_t PLAIN DRAM_ATTR = { S_TIE, -1, 1, 0, 0, 0, 255, 255, 0, 1 };

// the tricks of the grid's steps
enum { T_NONE, T_REV, T_X2, T_X3, T_X4, T_X8, T_X16, T_X64, T_UP, T_DOWN, T_FIFTH, T_DIVE, T_RISE, T_CUT, T_STOP, T_REVX4, T_N };
static const char *const trick_name[T_N] = { "PLAIN", "REVERSED", "ROLL X2", "ROLL X3", "ROLL X4", "ROLL X8", "ROLL X16", "ROLL X64",
                                             "UP AN OCTAVE", "DOWN AN OCTAVE", "UP A FIFTH", "DIVE", "RISE", "CUT SHORT", "TAPE STOP", "REVERSED ROLL" };
static IRAM_ATTR void trick_apply(int t, plan_t *p)
{
    static const uint8_t rolls[6] DRAM_ATTR = { 2, 3, 4, 8, 16, 64 };
    switch (t) {
    case T_REV:   p->flags |= F_REV; break;
    case T_X2: case T_X3: case T_X4: case T_X8: case T_X16: case T_X64:
        p->n = rolls[t - T_X2]; p->rise = (int8_t)(t == T_X16 ? 5 : 0); p->lvl0 = (uint8_t)(t >= T_X16 ? 170 : 255); break;
    case T_UP:    p->pitch = (int8_t)(p->pitch + 12); break;
    case T_DOWN:  p->pitch = (int8_t)(p->pitch - 12); break;
    case T_FIFTH: p->pitch = (int8_t)(p->pitch + 7); break;
    case T_DIVE:  p->flags |= F_DIVE; break;
    case T_RISE:  p->flags |= F_RISE; break;
    case T_CUT:   p->flags |= F_CUT; p->gate = 64; break;
    case T_STOP:  p->flags |= F_STOP; p->span = 2; break;
    case T_REVX4: p->flags |= F_REV; p->n = 4; break;
    default: break;
    }
}

// the grid's cells: 6 bits of slice (0 the break's own, 1 off, 2 + j slice j), 4 bits of trick
static IRAM_ATTR uint32_t cell_get(int k)
{
    int bit = k * 10, w = bit / 24, o = bit % 24;
    uint32_t v = (uint32_t)P(BP_G0 + w) >> o;
    if (o > 14 && w + 1 < GWORDS) v |= (uint32_t)P(BP_G0 + w + 1) << (24 - o);
    return v & 0x3FFu;
}
#define C_BRK 0
#define C_OFF 1
static IRAM_ATTR plan_t cell_plan(uint32_t c, int s)
{
    int f = (int)(c & 63), t = (int)((c >> 6) & 15);
    plan_t p = PLAIN;
    if (f == C_BRK) p.slice = M.at[s];
    else if (f == C_OFF) p.slice = S_REST;
    else p.slice = (int8_t)((f - 2) % (M.n > 0 ? M.n : 1));
    if (f == C_BRK && p.slice < 0 && t) p.slice = M.cover[s];    // a trick on the break's tie: on what sounds there
    trick_apply(t, &p);
    return p;
}

// ---------------------------------------------------------------- where a step is in its bar
typedef struct { int L, k, pbar, nbar; int32_t bar0; uint32_t barno; } where_t;
static const int8_t odd_lens[4] DRAM_ATTR = { 16, 14, 10, 18 };
static int32_t auto_ph = -1;
static int auto_l = 16;
static inline IRAM_ATTR float edit_weight(float ax) { return clampf(1.15f - 1.6f * ax, 0.05f, 1.0f); }   // the edits, by the axis

// ODD BARS AUTO: each 4/4 phrase (64 steps) is decided at its start: 4/4, or bars of 7, 5 or 9 eighths (the last cut
// short so the next phrase is on the flock's downbeat again)
static IRAM_ATTR int auto_len(int32_t ph)
{
    if (ph != auto_ph) {
        auto_ph = ph;
        uint32_t r = hash32((uint32_t)ph * 0x9E3779B1u ^ 0x5bd1e995u);
        float p = P(BP_EDITS) * 0.01f * edit_weight(perf.axis) * 0.55f + perf.chaos * 0.3f;
        if (perf.axis > 0.45f) p = 0.0f;
        auto_l = 16;
        if (rng_uni(&r) < p) { float u = rng_uni(&r); auto_l = u < 0.6f ? 14 : u < 0.8f ? 10 : 18; }
    }
    return auto_l;
}
static IRAM_ATTR void where_is(int32_t st, where_t *w)
{
    int odd = clampi((int)P(BP_ODD), 0, 4);
    if (st < 0) st = 0;
    if (odd >= 1 && odd <= 3) {                                   // a steady odd meter, running against the flock's
        int L = odd_lens[odd];
        w->L = L; w->k = (int)(st % L); w->bar0 = st - w->k; w->barno = (uint32_t)(st / L); w->pbar = (int)(w->barno % 4u); w->nbar = 4;
    } else {
        int32_t ph = st / 64, j = st % 64;
        int L = odd == 4 ? auto_len(ph) : 16;
        int b = (int)(j / L);
        w->bar0 = ph * 64 + b * L; w->k = (int)(j - b * L);
        w->L = (b + 1) * L <= 64 ? L : 64 - b * L;
        w->nbar = (64 + L - 1) / L; w->pbar = b; w->barno = (uint32_t)ph * 8u + (uint32_t)b;
    }
}
static inline IRAM_ATTR int brk_step(int k, uint32_t barno)            // the break's sixteenth for step k of a bar
{
    int nb = M.steps >= 16 ? M.steps / 16 : 1;
    return (int)(((barno % (uint32_t)nb) * 16u + (uint32_t)k) % (uint32_t)M.steps);
}
static inline IRAM_ATTR int pick(uint32_t *r, int n) { return n > 0 ? (int)(rng_next(r) % (uint32_t)n) : 0; }
static inline IRAM_ATTR int8_t kind_pick(uint32_t *r, int kind, int8_t fallback) { return M.nby[kind] ? M.by[kind][pick(r, M.nby[kind])] : fallback; }
static inline IRAM_ATTR bool gabber_now(void)
{
    int g = (int)P(BP_GABBER);
    return g == 1 || (g == 2 && perf.zone == Z_HARDCORE);
}

// ---------------------------------------------------------------- the bar's plan
static plan_t bar[32];
static volatile int32_t bar_at = -1;
static volatile int bar_len_v = 16;
static volatile uint32_t kicks_v = 0x1111u;
static volatile bool replan;

static IRAM_ATTR void blast_bar(int L)
{
    int8_t kk = M.kick, sn = M.snare;
    int8_t cr = M.crash >= 0 ? M.crash : (M.nby[KS_RIDE] ? M.by[KS_RIDE][0] : -1);
    for (int k = 0; k < L; k++) {
        plan_t *p = &bar[k];
        *p = PLAIN;
        p->slice = (k & 1) ? sn : kk;
        if (k % 4 == 0 && cr >= 0) p->layer = cr;
        p->lvl0 = p->lvl1 = (uint8_t)((k & 1) ? 230 : 255);
    }
}

static IRAM_ATTR void plan_bar(const where_t *w)
{
    int L = w->L > 32 ? 32 : w->L;
    bool mine = P(BP_PAT) >= 1.0f;
    for (int k = 0; k < L; k++) {
        int s = brk_step(k, w->barno);
        if (mine) bar[k] = cell_plan(cell_get(k % GSTEPS), s);
        else { bar[k] = PLAIN; bar[k].slice = M.at[s]; }
    }
    float ax = perf.axis, doom = smoothstep(0.45f, 0.68f, ax), deep = smoothstep(0.72f, 0.95f, ax);
    if (!mine && M.n) {
        uint32_t r2 = hash32((w->barno >> 1) * 0x9E3779B1u + M.serial), r = hash32(w->barno * 0x85EBCA6Bu + M.serial * 7u);
        float e = clampf(P(BP_EDITS) * 0.01f * edit_weight(ax) + perf.chaos * 0.5f, 0.0f, 1.2f);
        float rolls = P(BP_ROLLS) * 0.01f, rev = P(BP_REV) * 0.01f, drill = P(BP_DRILL) * 0.01f;
        bool last = w->pbar == w->nbar - 1;
        // a beat of the bar swapped for another beat of the break (jungle)
        if (L >= 8 && rng_uni(&r2) < e * 0.35f) {
            int c = pick(&r2, L / 4), src = pick(&r2, M.steps / 4 > 0 ? M.steps / 4 : 1);
            for (int i = 0; i < 4; i++) {
                int k = c * 4 + i;
                if (k < L) { bar[k] = PLAIN; bar[k].slice = M.at[(src * 4 + i) % M.steps]; }
            }
        }
        // kicks and snares shuffled
        int nsh = rng_uni(&r2) < e * 0.6f ? 1 + pick(&r2, 2) : 0;
        for (int q = 0; q < nsh; q++) {
            plan_t *p = &bar[pick(&r2, L)];
            if (p->slice < 0) continue;
            int kd = M.kind[p->slice];
            p->slice = kd == KS_KICK ? kind_pick(&r2, KS_SNARE, M.snare) : kd == KS_SNARE ? kind_pick(&r2, KS_KICK, M.kick)
                     : (rng_uni(&r2) < 0.5f ? M.snare : M.kick);
        }
        if (rng_uni(&r) < e * 0.4f) bar[pick(&r, L)].slice = (int8_t)pick(&r, M.n);   // a slice from anywhere
        // each step: rolls, reversed, pitch jumps, cut short, stutters, drill
        for (int k = 0; k < L; k++) {
            plan_t *p = &bar[k];
            if (p->slice < 0) {
                if (rng_uni(&r) < drill * e * 0.2f) {                // drill: ghost micro rolls in the gaps
                    p->slice = kind_pick(&r, KS_HAT, M.snare); p->n = (uint8_t)(4 << pick(&r, 3));
                    p->lvl0 = 50; p->lvl1 = 140; p->rise = (int8_t)(rng_uni(&r) < 0.5f ? 5 : -5);
                }
                continue;
            }
            int kd = M.kind[p->slice];
            if (rng_uni(&r) < rolls * e * 0.16f) {
                static const uint8_t rn[7] DRAM_ATTR = { 2, 2, 3, 4, 4, 6, 8 };
                p->n = rn[pick(&r, 7)];
                p->rise = (int8_t)(rng_uni(&r) < 0.75f ? 2 + pick(&r, 6) : -(2 + pick(&r, 5)));
                p->lvl0 = 150;
            }
            if (rng_uni(&r) < rev * e * (kd == KS_SNARE || kd == KS_CRASH ? 0.4f : 0.2f)) p->flags |= F_REV;
            if (rng_uni(&r) < e * 0.12f) { static const int8_t pj[6] DRAM_ATTR = { 12, 7, 5, -5, -7, -12 }; p->pitch = (int8_t)(p->pitch + pj[pick(&r, 6)]); }
            if (rng_uni(&r) < e * 0.10f) { p->flags |= F_CUT; p->gate = (uint8_t)(48 + pick(&r, 96)); }
            if (k > 0 && rng_uni(&r) < e * 0.07f) {                   // a stutter: the last step's slice, chopped
                if (bar[k - 1].slice >= 0) p->slice = bar[k - 1].slice;
                p->n = (uint8_t)(2 + pick(&r, 3)); p->gate = 140; p->flags &= (uint8_t)~F_REV;
            }
            if (rng_uni(&r) < drill * e * 0.3f) {                     // drill'n'bass: a micro roll, a sweep
                static const uint8_t dn[4] DRAM_ATTR = { 4, 8, 16, 32 };
                p->n = dn[pick(&r, 4)]; p->lvl0 = (uint8_t)(70 + pick(&r, 80)); p->lvl1 = 230;
                p->rise = (int8_t)(pick(&r, 25) - 12);
                if (rng_uni(&r) < 0.4f) p->flags |= rng_uni(&r) < 0.5f ? F_DIVE : F_RISE;
            }
        }
        // a snare rush into the bar line
        if (L >= 8 && rng_uni(&r) < e * (0.12f + 0.4f * rolls) * (last ? 1.8f : 1.0f)) {
            static const uint8_t r4[4] DRAM_ATTR = { 2, 3, 4, 8 }, r2n[2] DRAM_ATTR = { 4, 8 };
            int R = L >= 12 && rng_uni(&r) < 0.5f ? 4 : 2;
            for (int i = 0; i < R; i++) {
                plan_t *p = &bar[L - R + i];
                *p = PLAIN; p->slice = M.snare; p->n = R == 4 ? r4[i] : r2n[i];
                p->pitch = (int8_t)(i * (R == 4 ? 2 : 4)); p->rise = 2;
                p->lvl0 = (uint8_t)(110 + i * 35); p->lvl1 = (uint8_t)(150 + i * 30);
            }
        }
        // a tape stop at the phrase's end
        if (last && L >= 8 && rng_uni(&r2) < e * 0.3f) {
            int k0 = L - 4;
            plan_t *p = &bar[k0];
            if (p->slice < 0) p->slice = M.cover[brk_step(k0, w->barno)];
            p->n = 1; p->flags = F_STOP; p->span = 4; p->rise = 0; p->layer = -1;
            for (int k = k0 + 1; k < L; k++) bar[k] = PLAIN;
        }
        // blast bars (grind), in pairs
        float zb = perf.zone == Z_HARDCORE ? 1.0f : perf.zone == Z_BREAKCORE ? 0.35f : 0.0f;
        uint32_t r3 = hash32((w->barno >> 1) * 0xC2B2AE35u + 99u);
        if (rng_uni(&r3) < P(BP_BLAST) * 0.01f * zb * 0.7f) blast_bar(L);
        // towards doom: only the big hits; at DRONE only the one and the backbeat; a swell or a thunder roll into
        // the next phrase
        if (doom > 0.0f) {
            uint32_t rd = hash32(w->barno * 0x27D4EB2Fu + 5u);
            int thr = M.pkmax * 45 / 100;
            bool snare_kept = false;
            float keep = P(BP_SPEED) >= 1.0f ? 0.4f : 1.0f;             // TIED: the slowed break is the thunder, more stays
            for (int k = 0; k < L; k++) {
                plan_t *p = &bar[k];
                if (p->slice < 0) continue;
                int kd = M.kind[p->slice];
                bool big = (kd == KS_KICK || kd == KS_SNARE || kd == KS_CRASH || kd == KS_TOM) && M.peak[p->slice] >= thr;
                if (!big && rng_uni(&rd) < doom * keep) { *p = PLAIN; continue; }
                if (rng_uni(&rd) < doom) { p->n = 1; p->rise = 0; p->flags &= (uint8_t)~(F_CUT | F_DIVE | F_RISE); }
                if (deep > 0.0f && k != 0) {                          // DRONE: the one, some beats, the backbeat
                    bool backbeat = kd == KS_SNARE && k >= L / 2 && !snare_kept;
                    float drop = backbeat ? 0.15f : (k % 4 == 0 && big) ? 0.25f : 1.0f;
                    if (backbeat) snare_kept = true;
                    if (rng_uni(&rd) < deep * drop * keep) *p = PLAIN;
                }
            }
            if (deep > 0.5f && bar[0].slice < 0) bar[0].slice = M.kick;
            if (w->pbar == 0 && M.crash >= 0 && rng_uni(&rd) < doom) bar[0].layer = M.crash;
            if (last && doom > 0.3f && rng_uni(&rd) < (P(BP_EDITS) + P(BP_ROLLS)) * 0.004f) {
                if (deep > 0.3f) {                                    // a thunder roll
                    int8_t sl = M.tom >= 0 ? M.tom : M.kick;
                    for (int i = 0; i < 2 && L - 2 + i >= 0; i++) {
                        plan_t *p = &bar[L - 2 + i]; *p = PLAIN;
                        p->slice = sl; p->n = (uint8_t)(2 + i); p->pitch = -5; p->rise = 1; p->lvl0 = (uint8_t)(90 + 70 * i); p->lvl1 = (uint8_t)(160 + 90 * i);
                    }
                } else if (rng_uni(&rd) < rev + 0.2f) {               // a reversed swell into the one
                    plan_t *p = &bar[L - 1]; *p = PLAIN;
                    p->slice = M.crash >= 0 ? M.crash : M.snare; p->flags = F_REV;
                }
            }
        }
    }
    if (doom > 0.2f) for (int k = 0; k < L; k++) if (bar[k].slice >= 0 && bar[k].n == 1) bar[k].flags |= F_RING;
    // the kicks, for AMP: bit (step & 31) of each step of this bar (a bar is at most 18 steps, so the bits of the
    // next bar's steps are written before they are read)
    uint32_t km = kicks_v;
    bool gk = gabber_now();
    for (int k = 0; k < L; k++) {
        int32_t st = w->bar0 + k;
        uint32_t bit = 1u << (st & 31);
        int j = bar[k].slice, l = bar[k].layer;
        bool kick = (j >= 0 && M.kind[j] == KS_KICK) || (l >= 0 && M.kind[l] == KS_KICK) || (gk && st % 4 == 0);
        km = kick ? km | bit : km & ~bit;
    }
    kicks_v = km;
}

// ---------------------------------------------------------------- the step now
static int32_t cur_st = INT32_MIN, cur_s1, hit_base, st_len = 2400;
static plan_t cur;
static int hit_i;
static float hit_t0, hit_dt;
static where_t W;
static grid_t last_g;
static uint32_t now_frame, rr = 0x1234567u;
static float smear_now, hand_semis, dm_s = 1.0f;
static bool running_was, ed_stopped_was;
static volatile bool ed_stopped;          // the grid's editor has stopped it (UI writes)
static volatile bool ed_on;               // the grid's editor is open: the stick is its (UI writes)
static volatile beat_view_t view = { .slice = -1, .steps = 16 };
static volatile uint32_t reading_v;
// the last hit, for the stutter
static struct { const int16_t *pcm; uint32_t serial; int32_t lo, hi; float pos, rate, lvl; int slice; } last_hit;
// the hands
static bool hroll_on, stut_on;
static int hroll_slice = -1, hroll_n, stut_div, stut_j;
static float stut_pos0, stut_rate, stut_lvl;
static int32_t stut_lo, stut_hi;
static const int16_t *stut_pcm;
static uint32_t stut_serial;
// fills (a knock)
static volatile float fill_s;
static volatile uint32_t fill_seq;
static uint32_t fill_seen;
static int fill_left, fill_len, fill_j, fill_kind;
static float fill_str;
static bool fill_land;
// the editor's audition (the grid stopped)
static volatile uint32_t aud_seq, aud_cell;
static volatile int aud_step;
static uint32_t aud_seen;

static inline IRAM_ATTR float base_rate(void) { return P(BP_SPEED) >= 1.0f && M.six > 0.0f ? M.six / (float)st_len : 1.0f; }
static inline IRAM_ATTR float doom_semis(void) { return P(BP_SPEED) >= 1.0f ? 0.0f : P(BP_DOOM) * smoothstep(0.4f, 0.9f, perf.axis); }
static inline IRAM_ATTR int32_t hit_at(int i) { return (int32_t)(hit_t0 + (float)i * hit_dt + 0.5f); }

static IRAM_ATTR void tlog(const voice_t *v, int n)
{
#ifndef ESP_PLATFORM
    if (v && beat_tlog_n < (int)(sizeof beat_tlog / sizeof beat_tlog[0]))
        beat_tlog[beat_tlog_n++] = (beat_trig_t){ now_frame, v->slice, v->lane, (int8_t)(v->rate < 0.0f), (uint8_t)n, v->rate, v->peak * 32768.0f };
#else
    (void)v; (void)n;
#endif
}

// slice j as a hit: its region, backwards from as far into it as `span` frames reach (so the hit lands at the end)
static IRAM_ATTR voice_t *slice_play(int lane, int j, float rate, bool rev, float lvl, int32_t gate, float att, float rel,
                                     float ring_s, float glide, float span)
{
    if (j < 0 || j >= M.n || !M.len) return NULL;
    int32_t s0 = M.start[j], s1 = ring_s > 0.0f && !rev ? M.ring[j] : M.end[j];
    float ring_mul = ring_s > 0.0f ? expm(-1.0f / (ring_s * (float)AUDIO_SR)) : 1.0f;
    voice_t *v;
    if (rev) {
        float reach = span > 0.0f ? span * rate : (float)(s1 - s0);
        int32_t from = s0 + (int32_t)(reach < (float)(s1 - s0) ? reach : (float)(s1 - s0));
        if (from < s0 + 64) from = s0 + 64;
        v = v_play(lane, M.pcm, M.serial, s0, from, (float)from - 1.0f, -rate, lvl, gate, att, rel, ring_mul, glide, j);
    } else v = v_play(lane, M.pcm, M.serial, s0, s1, (float)s0, rate, lvl, gate, att, rel, ring_mul, glide, j);
    if (v && lane == 0) {
        last_hit.pcm = M.pcm; last_hit.serial = M.serial; last_hit.lo = v->lo; last_hit.hi = v->hi;
        last_hit.pos = v->pos; last_hit.rate = v->rate; last_hit.lvl = lvl; last_hit.slice = j;
    }
    return v;
}

static IRAM_ATTR void play_hit(int i)
{
    const plan_t *p = &cur;
    if (p->slice == S_REST) {
        if (i == 0) for (int l = 0; l < 2; l++) if (lane_v[l] >= 0) v_release(&V[lane_v[l]], 96.0f);
        return;
    }
    if (p->slice < 0 || !M.n) return;
    int n = p->n ? p->n : 1;
    float t = n > 1 ? (float)i / (float)(n - 1) : 0.0f;
    float semis = P(BP_PITCH) + (float)p->pitch + (float)p->rise * t + doom_semis() + hand_semis;
    float rate = base_rate() * fast_exp2(semis * (1.0f / 12.0f));
    float lvl = ((float)p->lvl0 + ((float)p->lvl1 - (float)p->lvl0) * t) * (1.0f / 255.0f);
    bool rev = (p->flags & F_REV) != 0;
    float sm = smear_now;
    if (sm > 0.1f && rng_uni(&rr) < sm * 1.6f) rev = true;
    float att = 32.0f + sm * 700.0f, rel = 64.0f, span = 0.0f, glide = 1.0f, ring_s = 0.0f;
    int32_t gate = 0;
    if ((p->flags & F_RING) || sm > 0.25f) ring_s = 0.12f + 0.6f * smoothstep(0.42f, 0.95f, perf.axis) + 0.2f * sm;
    if (n > 1) {
        gate = (int32_t)(hit_dt * (p->gate ? (float)p->gate * (1.0f / 255.0f) : 0.92f));
        span = hit_dt; ring_s = 0.0f;
    } else {
        int ties = 0;
        for (int k = W.k + 1; k < W.L && k < 32 && bar[k].slice == S_TIE; k++) ties++;
        span = (float)st_len * (float)(1 + ties) - hit_t0;
        if (p->flags & F_CUT) { gate = (int32_t)((float)st_len * (float)(p->gate ? p->gate : 128) * (1.0f / 255.0f)); rel = 96.0f; }
    }
    if (p->flags & F_DIVE) glide = expm(-0.6931f / (float)st_len);
    if (p->flags & F_RISE) glide = expm(0.6931f / (float)st_len);
    if (p->flags & F_STOP) {                                      // the tape stops over `span` steps: the rate falls to 3 %
        float stop = (float)st_len * (float)(p->span ? p->span : 1);
        glide = expm(-3.5f / stop);
        ring_s = 0.5f * stop * (1.0f / (float)AUDIO_SR);          // and the level dies with it (a hold at rate 0 would sit
    }                                                             // on one sample: a DC step until the next hit)
    voice_t *v = slice_play(0, p->slice, rate, rev, lvl, gate, att, rel, ring_s, glide, span);
    tlog(v, n);
    if (i == 0 && p->layer >= 0) tlog(slice_play(1, p->layer, rate, false, lvl * 0.8f, 0, 32.0f, 64.0f, ring_s, 1.0f, 0.0f), 1);
    if (v) {
        view.slice = p->slice; view.reversed = rev; view.roll = n > 1 ? n : 0; view.pitch = semis;
        view.hits = view.hits + 1;
    }
}

// a fill, step j of len: of the zone's kind
static IRAM_ATTR void fill_plan(plan_t *p, int j, int len)
{
    float f = len > 1 ? (float)j / (float)(len - 1) : 1.0f, s = fill_str;
    *p = PLAIN;
    switch (fill_kind) {
    case Z_BREAKCORE:                                             // a snare rush: faster, higher, louder
        p->slice = M.snare;
        p->n = (uint8_t)(f < 0.35f ? 2 : f < 0.65f ? 4 : f < 0.9f ? 8 : 16);
        p->pitch = (int8_t)(f * (5.0f + 7.0f * s)); p->rise = 2;
        p->lvl0 = (uint8_t)(140 + 100 * f); p->lvl1 = (uint8_t)(170 + 85 * f);
        break;
    case Z_HARDCORE:                                              // a blast
        p->slice = (j & 1) ? M.snare : M.kick;
        if (j % 4 == 0 && M.crash >= 0) p->layer = M.crash;
        p->lvl0 = p->lvl1 = 255;
        if (s > 0.5f) p->n = 2;                                   // hard: twice as fast
        break;
    case Z_SLUDGE:                                                // toms down the kit, then the crash
        if (M.nby[KS_TOM]) { p->slice = M.by[KS_TOM][j % M.nby[KS_TOM]]; p->pitch = (int8_t)(-j); }
        else { p->slice = (j & 1) ? M.kick : M.snare; p->pitch = (int8_t)(5 - (int)(12.0f * f)); }
        p->n = (uint8_t)(s > 0.6f && (j & 1) ? 2 : 1);
        p->flags = F_RING;
        break;
    default:                                                      // a thunder roll: low, swelling
        p->slice = M.tom >= 0 ? M.tom : M.kick;
        p->pitch = -9; p->rise = 2; p->n = (uint8_t)(2 + (int)(2.5f * f));
        p->lvl0 = (uint8_t)(70 + 150 * f); p->lvl1 = (uint8_t)(110 + 145 * f);
        break;
    }
}

static IRAM_ATTR void chaos_edit(plan_t *p)
{
    if (!M.n) return;
    switch (pick(&rr, 6)) {
    case 0: if (p->slice < 0) p->slice = M.cover[brk_step(W.k, W.barno)]; p->n = (uint8_t)(2 << pick(&rr, 3)); p->rise = (int8_t)(2 + pick(&rr, 6)); break;
    case 1: if (p->slice < 0) p->slice = M.cover[brk_step(W.k, W.barno)]; p->flags |= F_REV; break;
    case 2: { static const int8_t pj[6] DRAM_ATTR = { 12, 7, 5, -5, -7, -12 }; if (p->slice >= 0) p->pitch = (int8_t)(p->pitch + pj[pick(&rr, 6)]); } break;
    case 3: p->slice = (int8_t)pick(&rr, M.n); p->n = 1; break;
    case 4: if (p->slice < 0) p->slice = M.snare; p->n = 3; p->gate = 120; break;
    default: p->slice = kind_pick(&rr, KS_HAT, M.snare); p->n = 16; p->lvl0 = 60; p->lvl1 = 180; p->flags |= F_DIVE; break;
    }
}

static inline IRAM_ATTR int hroll_count(float r) { return r < 0.3f ? 2 : r < 0.5f ? 4 : r < 0.7f ? 8 : r < 0.88f ? 16 : 32; }

// the hits of `cur` in the step from `base`, skipping those already past `pos`
static IRAM_ATTR void schedule(int32_t base, int32_t pos, bool swing)
{
    hit_base = base;
    int n = cur.n ? cur.n : 1;
    hit_t0 = swing && (W.k & 1) ? P(BP_SWING) * 0.01f * 0.33f * (float)st_len : 0.0f;
    hit_dt = ((float)st_len - hit_t0) / (float)n;
    hit_i = 0;
    while (hit_i < n && hit_base + hit_at(hit_i) < pos - 64) hit_i++;
}
static inline IRAM_ATTR int cur_hits(void) { return cur.n ? cur.n : 1; }

static IRAM_ATTR void hroll_plan(void)
{
    float r = perf.roll;
    cur = PLAIN; cur.slice = (int8_t)hroll_slice; hroll_n = hroll_count(r); cur.n = (uint8_t)hroll_n;
    cur.lvl0 = cur.lvl1 = 220;
    cur.rise = (int8_t)(0.5f + 3.0f * r);
}

static IRAM_ATTR void new_step(const grid_t *g, int32_t st, int32_t pos)
{
    cur_st = st;
    int32_t s0 = wr_step_start(g, st);
    cur_s1 = wr_step_start(g, st + 1);
    st_len = cur_s1 - s0 > 0 ? cur_s1 - s0 : 1;
    where_is(st, &W);
    if (W.bar0 != bar_at || replan) { replan = false; plan_bar(&W); bar_at = W.bar0; bar_len_v = W.L > 32 ? 32 : W.L; }
    view.step = W.k; view.steps = W.L;
    if (ed_stopped) return;                                       // stopped by the grid: an audition plays out
    if (!perf.running || !M.n) { cur = PLAIN; cur.n = 0; hit_i = 0; return; }
    stut_j = 0;
    cur = bar[W.k < 32 ? W.k : 31];
    // a fill takes the step; then it lands
    if (fill_left > 0) { fill_plan(&cur, fill_j, fill_len); fill_j++; if (--fill_left == 0) fill_land = true; }
    else if (fill_land) {
        fill_land = false;
        cur = PLAIN; cur.slice = M.kick; cur.layer = M.crash >= 0 ? M.crash : M.snare;
        if (perf.zone >= Z_SLUDGE) cur.flags = F_RING;
    }
    // chaos: one more edit
    if (perf.chaos > 0.02f && rng_uni(&rr) < perf.chaos * 0.6f) chaos_edit(&cur);
    // the stick up: the slice now, rolled, rising while it is held
    if (hroll_on) {
        hroll_plan();
        cur.pitch = 0;
        hand_semis += 0.4f + 2.2f * perf.roll;
        float cap = 7.0f + 12.0f * perf.roll;
        if (hand_semis > cap) hand_semis = cap;
    }
    if (gabber_now() && st % 4 == 0) kick_start((float)st_len * 4.0f, 1.0f);
    if (fill_left > 0 && fill_kind == Z_HARDCORE && st % 2 == 0 && gabber_now() && st % 4) kick_start((float)st_len * 2.0f, 0.8f);
    schedule(s0, pos, true);
}

// ---------------------------------------------------------------- audio
void IRAM_ATTR beat_process(float *out, float *send, int n, uint32_t f0)
{
    static float acc[AUDIO_BLOCK];
    static smooth_t s_lvl = { 0.8f, 0.004f }, s_snd = { 0.2f, 0.004f };
    if (n > AUDIO_BLOCK) n = AUDIO_BLOCK;
    const brk_t *b = the_break();
    if (b != M.b || b->serial != M.serial) {                      // a new break: the old one's voices fade, then let go of it
        for (int k = 0; k < NV; k++) if (V[k].phase) v_release(&V[k], 320.0f);
        map_build(b);
        bar_at = -1; stut_on = false; hroll_on = false;
    }
    grid_t g; wr_grid_get(&g);
    if (g.feel_q != last_g.feel_q || g.fpb != last_g.fpb || g.origin != last_g.origin) { last_g = g; cur_st = INT32_MIN; }
    memset(acc, 0, sizeof acc);
    // stopped (REC tapped, or the grid's editor): no new hits, what plays fades
    bool run = perf.running && M.n;
    if (!run && running_was) { for (int k = 0; k < NV; k++) v_release(&V[k], 320.0f); kick_release(&KK[0], 320.0f); kick_release(&KK[1], 320.0f); }
    if (ed_stopped && !ed_stopped_was) { for (int k = 0; k < NV; k++) v_release(&V[k], 320.0f); kick_release(&KK[0], 320.0f); kick_release(&KK[1], 320.0f); cur.n = 0; }
    if (!ed_stopped && ed_stopped_was) cur_st = INT32_MIN;
    running_was = run; ed_stopped_was = ed_stopped;
    // the hands
    float roll = ed_on ? 0.0f : perf.roll, dive = ed_on ? 0.0f : perf.dive, stut = perf.stutter;
    smear_now = perf.smear;
    choke_rel = 64.0f + smear_now * smear_now * 2400.0f;           // smeared: each hit blurs into the next
    float dmt = fast_exp2(-(dive + smear_now * (2.0f / 12.0f)));
    dm_s += 0.3f * (dmt - dm_s);
    if (!hroll_on && roll > 0.08f && run) {                        // the stick up: roll the slice now, from the next sub-step
        hroll_on = true; hand_semis = 0.0f;
        hroll_slice = lane_v[0] >= 0 ? V[lane_v[0]].slice : last_hit.slice >= 0 ? last_hit.slice : M.snare;
        if (hroll_slice < 0 || hroll_slice >= M.n) hroll_slice = M.snare;
        if (cur_st != INT32_MIN) { hroll_plan(); schedule(hit_base, wr_pos(&g, f0), false); }
    } else if (hroll_on && roll < 0.05f) { hroll_on = false; hand_semis = 0.0f; cur.n = 0; }
    else if (hroll_on && hroll_count(roll) != hroll_n && cur_st != INT32_MIN) { hroll_plan(); schedule(hit_base, wr_pos(&g, f0), false); }
    if (stut > 0.08f && run) {                                     // the roll right: the last bit again
        int div = stut < 0.35f ? 2 : stut < 0.6f ? 4 : stut < 0.85f ? 8 : 16;
        int32_t pos = wr_pos(&g, f0), rel = pos - hit_base;
        if (!stut_on) {
            stut_on = true;
            float len = (float)st_len / (float)div;
            const voice_t *v = lane_v[0] >= 0 ? &V[lane_v[0]] : NULL;
            if (v) {
                stut_pcm = v->pcm; stut_serial = v->serial; stut_lo = v->lo; stut_hi = v->hi; stut_rate = v->rate;
                float back = len * fabsf(v->rate);
                stut_pos0 = v->rate > 0.0f ? v->pos - back : v->pos + back;
                stut_lvl = v->peak * 32768.0f;
            } else {
                stut_pcm = last_hit.pcm; stut_serial = last_hit.serial; stut_lo = last_hit.lo; stut_hi = last_hit.hi;
                stut_rate = last_hit.rate; stut_pos0 = last_hit.pos; stut_lvl = last_hit.lvl;
            }
            if (stut_pos0 < (float)stut_lo) stut_pos0 = (float)stut_lo;
            if (stut_pos0 > (float)stut_hi - 2.0f) stut_pos0 = (float)stut_hi - 2.0f;
            if (stut_lvl < 0.3f) stut_lvl = 0.3f;
        }
        if (div != stut_div || stut_j == 0) { stut_div = div; stut_j = rel > 0 ? (int)(((int64_t)rel * div + st_len - 1) / st_len) : 0; }
    } else if (stut_on && stut < 0.05f) stut_on = false;
    if (stut_on && stut_serial != M.serial) stut_on = false;
    // a knock: a fill, now
    if (fill_seq != fill_seen) {
        fill_seen = fill_seq;
        if (run && !ed_stopped) {
            fill_str = clampf(fill_s, 0.0f, 1.0f); fill_kind = perf.zone;
            static const float base_l[Z_N] DRAM_ATTR = { 4, 4, 2, 2 }, more_l[Z_N] DRAM_ATTR = { 12, 8, 4, 2 };
            fill_len = (int)(base_l[fill_kind] + more_l[fill_kind] * fill_str + 0.5f);
            fill_j = 0; fill_left = fill_len; fill_land = false;
            int32_t pos = wr_pos(&g, f0);
            if (cur_st != INT32_MIN && cur_s1 - pos > st_len / 3) {   // most of the step left: it starts in it
                fill_plan(&cur, 0, fill_len); fill_j = 1; fill_left--;
                schedule(hit_base, pos, false);
                if (hit_i >= cur_hits()) { hit_t0 = (float)(pos - hit_base); hit_dt = (float)(cur_s1 - pos) / (float)cur_hits(); hit_i = 0; }
            }
        }
    }
    // the editor's audition (the grid stopped): the cell, once, now
    if (aud_seq != aud_seen) {
        aud_seen = aud_seq;
        if (ed_stopped && M.n) {
            cur = cell_plan(aud_cell, brk_step(aud_step, 0));
            int32_t pos = wr_pos(&g, f0);
            hit_base = pos; hit_t0 = 0.0f; hit_dt = (float)st_len / (float)cur_hits(); hit_i = 0;
        }
    }

    int i = 0;
    while (i < n) {
        now_frame = f0 + (uint32_t)i;
        int32_t pos = wr_pos(&g, now_frame), st = wr_step_of(&g, pos);
        if (st != cur_st) new_step(&g, st, pos);
        int32_t to = cur_s1 - pos;
        if (to < 1 || to > st_len) to = 1;                         // (a jump in time: the next frame sorts it out)
        int nh = cur.n;
        while (hit_i < nh && pos >= hit_base + hit_at(hit_i)) { if (!stut_on || ed_stopped) play_hit(hit_i); hit_i++; }
        if (hit_i < nh) { int32_t d = hit_base + hit_at(hit_i) - pos; if (d < to) to = d; }
        if (stut_on && run && !ed_stopped) {
            for (;;) {
                int32_t at = hit_base + (int32_t)(((int64_t)stut_j * st_len) / stut_div);
                if (stut_j >= stut_div) break;
                if (pos >= at) {
                    float len = (float)st_len / (float)stut_div;
                    voice_t *v = v_play(0, stut_pcm, stut_serial, stut_lo, stut_hi, stut_pos0, stut_rate, stut_lvl,
                                        (int32_t)(len * 0.9f), 32.0f, 64.0f, 1.0f, 1.0f, last_hit.slice);
                    tlog(v, stut_div);
                    if (v) view.hits = view.hits + 1;
                    stut_j++;
                    continue;
                }
                if (at - pos < to) to = at - pos;
                break;
            }
        }
        if (to < 1) to = 1;
        int m = (int)(to < (int32_t)(n - i) ? to : (int32_t)(n - i));
        for (int k = 0; k < NV; k++) if (V[k].phase) v_run(&V[k], acc + i, m, dm_s);
        for (int k = 0; k < 2; k++) if (KK[k].phase) kick_run(&KK[k], acc + i, m, dm_s);
        i += m;
    }
    // level and send, smoothed; the meter for the screen
    float tl = P(BP_LEVEL) * 0.01f, ts = P(BP_SEND) * 0.01f * (0.7f + 0.6f * perf.axis), pk = 0.0f;
    for (int k = 0; k < n; k++) {
        float l = smooth_to(&s_lvl, tl), s = smooth_to(&s_snd, ts);
        float y = acc[k] * l;
        out[k] += y; send[k] += y * s;
        float a = fabsf(y);
        if (a > pk) pk = a;
    }
    float lv = view.level * 0.97f;
    view.level = pk * 1.6f > lv ? clampf(pk * 1.6f, 0.0f, 1.0f) : lv;
    // the oldest break a voice still reads
    uint32_t old = M.serial;
    for (int k = 0; k < NV; k++) if (V[k].phase && (int32_t)(V[k].serial - old) < 0) old = V[k].serial;
    if (stut_on && (int32_t)(stut_serial - old) < 0) old = stut_serial;
    reading_v = old;
}

uint32_t beat_reading(void) { return reading_v; }
uint32_t IRAM_ATTR beat_kicks(void) { return ed_stopped || !perf.running ? 0u : kicks_v; }
int32_t  beat_bar_start(void) { return bar_at < 0 ? 0 : bar_at; }
int      beat_bar_len(void) { return bar_len_v; }
void beat_view(beat_view_t *v) { memcpy(v, (const void *)&view, sizeof *v); }
void beat_fill(float strength) { fill_s = strength; fill_seq = fill_seq + 1; }

// ---------------------------------------------------------------- the grid's editor (UI task)
static bool ed_seen, ed_choosing;
static int ed_cur, ed_frames, ed_hint_ms, ed_rec_stage, ed_jx_ms, ed_jy_ms;
static float ed_cx = -1.0f, ed_cy = -1.0f;
static float pop[GSTEPS], hitfl[GSTEPS];
static uint32_t seen_hits, pseed = 0x5eed1234u;
static UI_RAM parts_t eparts;
#define CW  16                 // a cell's pitch across
#define RY0 19                 // the first row's top
#define RDY 16                 // the rows' pitch
#define CH  14                 // a cell's height

static int  cell_slice_field(uint32_t c) { return (int)(c & 63); }
static int  cell_trick(uint32_t c) { return (int)((c >> 6) & 15); }
static void cell_put(int k, uint32_t c)
{
    c &= 0x3FFu;
    int bit = k * 10, w = bit / 24, o = bit % 24;
    uint32_t lo = (uint32_t)P(BP_G0 + w);
    uint32_t nlo = (lo & ~((0x3FFu << o) & 0xFFFFFFu)) | ((c << o) & 0xFFFFFFu);
    if (nlo != lo) MP_SET(M_BEAT, BP_G0 + w, (float)nlo);
    if (o > 14 && w + 1 < GWORDS) {
        int sh = 24 - o;
        uint32_t hi = (uint32_t)P(BP_G0 + w + 1), m = 0x3FFu >> sh;
        uint32_t nhi = (hi & ~m) | (c >> sh);
        if (nhi != hi) MP_SET(M_BEAT, BP_G0 + w + 1, (float)nhi);
    }
}
static void audition(int k)
{
    if (!ed_stopped) return;
    aud_cell = cell_get(k); aud_step = k; aud_seq = aud_seq + 1;
}
static void cell_pop(int k, int count)
{
    pop[k] = 0.0f;
    int x = (k % 8) * CW + 7, y = RY0 + (k / 8) * RDY + 6;
    parts_burst(&eparts, (float)x, (float)y, count, 40.0f, 0.25f, PART_SPARK, &pseed);
}
static void slice_move(int d)                                     // press and turn: the break's own, each slice, off
{
    uint32_t c = cell_get(ed_cur);
    int f = cell_slice_field(c), n = M.n > 0 ? M.n : 1;
    if (n > 61) n = 61;
    int idx = f == C_BRK ? 0 : f == C_OFF ? n + 1 : 1 + (f - 2) % n;
    int ni = clampi(idx + d, 0, n + 1);
    if (ni == idx) return;
    int nf = ni == 0 ? C_BRK : ni == n + 1 ? C_OFF : 2 + (ni - 1);
    cell_put(ed_cur, (c & ~63u) | (uint32_t)nf);
    cell_pop(ed_cur, nf == C_OFF ? 2 : 4);
    audition(ed_cur);
}
static void trick_move(int d)
{
    uint32_t c = cell_get(ed_cur);
    int t = (cell_trick(c) + d + T_N) % T_N;
    cell_put(ed_cur, (c & 63u) | ((uint32_t)t << 6));
    cell_pop(ed_cur, t ? 5 : 2);
    audition(ed_cur);
}
static void editor_open(void)
{
    if (ed_on) return;
    ed_on = true; ed_frames = 0; ed_rec_stage = 0; ed_jx_ms = ed_jy_ms = 0; ed_choosing = false;
    ed_cx = ed_cy = -1.0f; ed_stopped = false;
    ed_hint_ms = ed_seen ? 2500 : 6000; ed_seen = true;           // until the first turn or press
    for (int k = 0; k < GSTEPS; k++) { pop[k] = 1.0f; hitfl[k] = 0.0f; }
    if (P(BP_PAT) < 1.0f) MP_SET(M_BEAT, BP_PAT, 1);              // you hear what you draw
    knob_grab(true);
    coach_done(LEARN_INST + 8);
}
static void editor_close(void)
{
    if (!ed_on) return;
    ed_on = false; ed_choosing = false; ed_stopped = false;
    knob_grab(false);
}
bool beat_hold(void) { editor_open(); return true; }
bool beat_editing(void) { return ed_on; }
#ifndef ESP_PLATFORM
int  beat_test_cursor(void) { return ed_cur; }                     // for tools/sim
uint32_t beat_test_cell(int k) { return cell_get(k); }
void beat_test_cell_put(int k, uint32_t c) { cell_put(k, c); }
bool beat_test_stopped(void) { return ed_stopped; }
#endif

static void editor_tick(const ctl_t *c)
{
    if (c->knob_hold) { editor_close(); return; }
    if (c->knob_tap) { trick_move(1); ed_hint_ms = 0; }
    if (c->knob) {
        if (c->knob_down) { ed_choosing = true; slice_move(c->knob); }
        else ed_cur = ((ed_cur + c->knob * (c->knob > 1 || c->knob < -1 ? 2 : 1)) % GSTEPS + GSTEPS) % GSTEPS;
        ed_hint_ms = 0;
    }
    if (!c->knob_down) ed_choosing = false;
    if (c->has_joy) {                                              // the stick: the step, the trick (repeating when held)
        int dx = c->jx > 0.6f ? 1 : c->jx < -0.6f ? -1 : 0, dy = c->jy > 0.6f ? 1 : c->jy < -0.6f ? -1 : 0;
        if (dx) { if (ed_jx_ms == 0 || (ed_jx_ms >= 350 && (ed_jx_ms - 350) % 120 < 10)) ed_cur = (ed_cur + dx + GSTEPS) % GSTEPS; ed_jx_ms += 10; ed_hint_ms = 0; }
        else ed_jx_ms = 0;
        if (dy) { if (ed_jy_ms == 0 || (ed_jy_ms >= 400 && (ed_jy_ms - 400) % 150 < 10)) trick_move(dy); ed_jy_ms += 10; ed_hint_ms = 0; }
        else ed_jy_ms = 0;
    }
    // REC: a tap stops or runs the grid; held 1 s, the steps go back to the break's order
    if (c->rec_press) ed_rec_stage = 0;
    if (c->rec && c->rec_ms >= 1000 && ed_rec_stage == 0) {
        for (int k = 0; k < GSTEPS; k++) if (cell_get(k)) { cell_put(k, 0); cell_pop(k, 2); }
        ed_rec_stage = 1; toast("THE BREAK'S ORDER", 900);
    }
    if (c->rec_release && c->rec_ms < 300 && ed_rec_stage == 0) {
        ed_stopped = !ed_stopped;
        toast(ed_stopped ? "GRID STOPS" : "GRID RUNS", 700);
    }
    if (ed_hint_ms > 0) ed_hint_ms -= 10;
}

static void tick(const ctl_t *c)
{
    if (ed_on) editor_tick(c);
    if (view.hits != seen_hits) {                                  // the step played flashes in the grid
        seen_hits = view.hits;
        if (P(BP_PAT) >= 1.0f) hitfl[view.step % GSTEPS] = 1.0f;
    }
}

// ---------------------------------------------------------------- the grid on the screen
static const char kind_letter[NKIND + 1] = "KSHRCTX";
static const char *const kind_name[NKIND] = { "KICK", "SNARE", "HAT", "RIDE", "CRASH", "TOM", "HIT" };

static void trick_glyph(int t, int cx, int y)                   // in a cell, 4 rows from y
{
    char b[4];
    switch (t) {
    case T_NONE: break;
    case T_REV: case T_REVX4:
        oled_line(cx - 3, y + 2, cx + (t == T_REVX4 ? 0 : 3), y + 2, true); oled_line(cx - 3, y + 2, cx - 1, y, true); oled_line(cx - 3, y + 2, cx - 1, y + 4, true);
        if (t == T_REVX4) oled_ftext(F_TINY, cx + 2, y, "4");
        break;
    case T_X2: case T_X3: case T_X4: case T_X8: case T_X16: case T_X64: {
        static const char *const nm[6] = { "2", "3", "4", "8", "16", "64" };
        snprintf(b, sizeof b, "%s", nm[t - T_X2]);
        oled_ftext_c(F_TINY, cx, y, b, 1);
        break; }
    case T_UP: case T_DOWN: {
        int tip = t == T_UP ? y : y + 4, tail = t == T_UP ? y + 4 : y, s = t == T_UP ? 1 : -1;
        oled_line(cx, tip, cx, tail, true); oled_line(cx, tip, cx - 2, tip + 2 * s, true); oled_line(cx, tip, cx + 2, tip + 2 * s, true);
        break; }
    case T_FIFTH: oled_ftext_c(F_TINY, cx, y, "+7", 1); break;
    case T_DIVE: oled_line(cx - 4, y, cx + 4, y + 4, true); break;
    case T_RISE: oled_line(cx - 4, y + 4, cx + 4, y, true); break;
    case T_CUT: oled_rect(cx - 4, y + 1, 4, 3, true); oled_pixel(cx + 1, y + 2, true); oled_pixel(cx + 3, y + 2, true); break;
    case T_STOP:
        for (int x = 0; x < 9; x++) { int yy = y + (x * x) / 16; oled_pixel(cx - 4 + x, yy > y + 4 ? y + 4 : yy, true); }
        break;
    default: break;
    }
}

static void draw_cell(int k, int x, int y, bool in_bar, float pp, float fl)
{
    uint32_t c = cell_get(k);
    int f = cell_slice_field(c), t = cell_trick(c), cx = x + 7;
    if (!in_bar) {                                                 // not in this bar (a short odd bar): dimmed
        oled_dither(x + 1, y, CH, CH, 1, true);
        return;
    }
    float e = ease_back(pp);
    if (f == C_OFF) {                                              // off: a dash
        int w = (int)lroundf(6.0f * e);
        oled_rect(cx - w / 2, y + 6, w > 0 ? w : 1, 2, true);
        if (t) trick_glyph(t, cx, y + 9);
    } else {
        int j = f == C_BRK ? (M.n ? M.at[k % (M.steps > 0 ? M.steps : 16)] : -1) : (M.n ? (f - 2) % M.n : -1);
        char lt[2] = { j >= 0 ? kind_letter[M.kind[j] < NKIND ? M.kind[j] : NKIND - 1] : '\x85', 0 };
        int w = (int)lroundf(14.0f * e), h = (int)lroundf(14.0f * e);
        if (w < 2) w = 2;
        if (h < 2) h = 2;
        if (f == C_BRK) oled_hline_pat(x + 3, y + 13, 10, 0x55, true);   // the break's own: a dotted line under it
        else oled_rframe(cx + 1 - w / 2, y + 7 - h / 2, w, h, 2);        // a slice picked: framed
        oled_ftext_c(F_SMALL, cx + 1, y + 2, lt, 0);
        trick_glyph(t, cx + 1, y + 8);
    }
    if (fl > 0.45f) oled_invert(x + 1, y, CH, CH);
}

void beat_draw_editor(const view_t *v)
{
    oled_rect(0, 16, OLED_W, OLED_H - 16, false);
    ed_frames++;
    for (int k = 0; k < GSTEPS; k++) {                             // pops grow, flashes fade
        if (pop[k] < 1.0f) pop[k] = fminf(1.0f, pop[k] + 1.0f / 6.0f);
        hitfl[k] *= 0.7f;
    }
    bool mine = P(BP_PAT) >= 1.0f;
    int L = view.steps > 0 ? view.steps : 16, head = mine && perf.running && !ed_stopped ? view.step % GSTEPS : -1;
    for (int r = 0; r < 2; r++) {
        float t = clampf((float)(ed_frames - 3 * r) / 8.0f, 0.0f, 1.0f);   // the rows drop in, the top one first
        int y = RY0 + r * RDY - (int)((1.0f - ease_out(t)) * 26.0f);
        if (y < 12) continue;
        for (int col = 0; col < 8; col++) {
            int k = r * 8 + col, x = col * CW;
            draw_cell(k, x, y, k < L || L > GSTEPS, pop[k], hitfl[k]);
            if (k % 4 == 0) oled_rect(x + 7, y - 2, 2, 1, true);    // the beats
        }
    }
    if (head >= 0 && ed_frames > 10) {                              // the playhead: a bar over its step
        int x = (head % 8) * CW, y = RY0 + (head / 8) * RDY - 2;
        oled_rect(x + 3, y, 10, 1, true);
    }
    float tx = (float)((ed_cur % 8) * CW), ty = (float)(RY0 + (ed_cur / 8) * RDY - 1);   // the cursor slides
    if (ed_cx < 0.0f) { ed_cx = tx; ed_cy = ty; }
    ed_cx += 0.55f * (tx - ed_cx); ed_cy += 0.55f * (ty - ed_cy);
    if (ed_frames > 12) {
        int x = (int)lroundf(ed_cx), y = (int)lroundf(ed_cy);
        if (ed_choosing || (v->frame / 5) % 3 != 2) { oled_frame(x, y, CW, CH + 2); oled_frame(x + 1, y + 1, CW - 2, CH); }
        else oled_frame(x, y, CW, CH + 2);
    }
    parts_step(&eparts, 0.04f, 60.0f, 3.0f);
    parts_draw(&eparts);
    // two lines at the bottom: how it works the first time, then the step's slice and trick
    oled_rect(0, 50, OLED_W, 14, false);
    oled_hline_pat(0, 50, OLED_W, 0x11, true);
    if (ed_hint_ms > 0) {
        oled_ftext_c(F_TINY, 64, 52, "TURN: STEP   PRESS+TURN: SLICE", 0);
        oled_ftext_c(F_TINY, 64, 58, "TAP: TRICK   HOLD: DONE", 0);
        return;
    }
    uint32_t c = cell_get(ed_cur);
    int f = cell_slice_field(c), t = cell_trick(c);
    char b[40];
    if (f == C_OFF) snprintf(b, sizeof b, "STEP %d  OFF", ed_cur + 1);
    else if (f == C_BRK) {
        int j = M.n ? M.at[ed_cur % (M.steps > 0 ? M.steps : 16)] : -1;
        if (j >= 0) snprintf(b, sizeof b, "STEP %d  BREAK: %s", ed_cur + 1, kind_name[M.kind[j] < NKIND ? M.kind[j] : NKIND - 1]);
        else snprintf(b, sizeof b, "STEP %d  BREAK", ed_cur + 1);
    } else {
        int j = M.n ? (f - 2) % M.n : 0;
        snprintf(b, sizeof b, "STEP %d  %s %d", ed_cur + 1, M.n ? kind_name[M.kind[j] < NKIND ? M.kind[j] : NKIND - 1] : "SLICE", j + 1);
    }
    oled_ftext_ex(F_TINY, 1, 52, b, 0);
    oled_ftext_ex(F_TINY, 1, 58, trick_name[t], 1);
    const char *st = ed_stopped ? "STOPPED" : !mine ? "AUTO" : L != 16 ? (L == 14 ? "MINE 7/8" : L == 10 ? "MINE 5/8" : L == 18 ? "MINE 9/8" : "MINE") : "MINE";
    if (ed_stopped) oled_rect(122, 58, 4, 5, true);               // stopped: a square; running: a play triangle
    else { oled_line(122, 58, 122, 62, true); oled_line(123, 59, 123, 61, true); oled_pixel(124, 60, true); }
    oled_ftext_r(F_TINY, 119, 58, st, 1);
}

// ---------------------------------------------------------------- the module
static void init(void)
{
    const brk_t *b = the_break();
    reading_v = b ? b->serial : 0;
    cur = PLAIN; cur.n = 0;
    last_hit.slice = -1;
    kick_coefs();
}
static void changed(int p)
{
    if (p < 0 || p == BP_KDIRT) kick_coefs();
    if (p < 0 || p == BP_PAT || p == BP_ODD || p == BP_BLAST || p == BP_GABBER || p >= BP_G0) replan = true;
    if (p < 0 && ed_on) editor_close();
}
static void action(int p)
{
    if (p == BP_GEDIT) { menu_leave(); editor_open(); }
}

const module_t mod_beat = { .name = "BEAT", .letter = 'b', .pages = pages, .npages = 2, .params = params, .nparams = BP_N,
                            .sounds = sounds, .init = init, .tick = tick, .changed = changed, .action = action };
