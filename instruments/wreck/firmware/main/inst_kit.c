// KIT (inst_kit.h): the break. Built-in breaks are synthesized and "printed" into a buffer; sampled ones are recorded,
// trimmed and sliced. Two buffers of 8 s in PSRAM: the break that plays and a spare, written while the other plays on.
//
// The kit (all of it rendered in the UI task, a piece every tick, never in audio):
//   kick    a membrane: its fundamental gliding down from the strike, four overtones struck by the felt beater (a pulse
//           as long as the beater stays on the head), the beater's click, a knock in the low mids; a pillow in it
//   snare   seven head modes (a circular membrane's) struck by a stick pulse whose length says how hard, a rim/shell
//           ring (strong on a rimshot), the stick's crack; the wires are one shared rattle: band-passed noise driven by
//           the heads' motion, slapping on their swings, and buzzing a little when the kick and the toms sound
//   hats    six square oscillators at inharmonic ratios with noise, high-passed; closed, open (choked by the next
//           closed one), the foot's chick
//   ride    twelve cymbal modes struck by the stick (the bell rings the middle ones), the ping, a wash that builds
//   crash   metal and noise through a filter that closes as it rings, a burst at the strike, a little body
//   toms    membranes like the kick's, struck by a stick (bongos: high, short, slapped)
// played from a pattern like a drummer (ghosts, swing, the backbeat laid back, every hit a little different), then
// the room (early reflections, a short tail) and the print (a bus compressor, saturation, the band of a tape, a record
// or a 12-bit sampler, hiss and crackle). The render runs the whole break once before it (only its cymbals, but for
// the last second), so the start has the ring of the bar before, as a break cut from a record has, and it loops whole.
// The board renders for 2.5 ms a tick (TICK_US); the host a fixed number of frames (RENDER_TICK), as the tests measure.
#include "wreck.h"
#include <stdio.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_timer.h"
#define TICK_US    2500                          // the board: render for this long each tick (the screen keeps going)
#endif

#define SRF        ((float)AUDIO_SR)
#define ISR        (1.0f / (float)AUDIO_SR)
#define MAXLEN     (8 * AUDIO_SR)                // a break: 8 s at most (a take); two buffers
#define RING       16384u                        // 0.51 s of the source, always written: the hold test loses nothing
#define RMASK      (RING - 1u)
#define RING_SAFE  256u                          // the audio task may be writing the oldest frames
#define ENV_N      128
#define EV_MAX     512
#define ONS_MAX    192
#define CLICK_POST (AUDIO_SR * 12 / 100)         // REC's click: nothing from 120 ms after the press ...
#define CLICK_PRE  (AUDIO_SR * 4 / 100)          // ... nor from 40 ms before the release is ever in a take
#define ONSET_PRE  (AUDIO_SR * 3 / 1000)         // a take starts 3 ms before its first sound (the attack whole)
#define RES_PRE    (AUDIO_SR * 35 / 1000)        // RESAMPLE starts on the step at or after 35 ms before the press
#define SLICE_PRE  16                            // a built-in slice starts half a millisecond before its hit
#define MIN_GAP    (AUDIO_SR * 25 / 1000)        // two onsets at least 25 ms apart
#define TARGET_PK  0.5f                          // every break peaks at -6 dBFS
#define WSCALE     0.55f                         // the render writes the print at this level, then finishes it
#define RENDER_TICK 384                          // frames rendered per tick on the host (test_kit.c measures the cost);
#define FINISH_TICK 4096                         // frames finished per tick (the board stops at TICK_US instead)
#define PRE_FULL   AUDIO_SR                      // the render's run-up is the whole break; its last second in full
#define FADE       96                            // 3 ms
#define TAKE_BEATS 16                            // a take: 16 beats at most (64 steps: BEAT's grid)

// ---------------------------------------------------------------- params
enum { KP_BREAK, KP_PRINT, KP_ROOM, KP_TUNE, KP_TONE, KP_DECAY, KP_SWING, KP_GHOSTS, KP_HUMAN, KP_TAKE,
       KP_LENGTH, KP_SLICES, KP_GATE, KP_N };
enum { B_AMEN, B_THINK, B_APACHE, B_FUNKY, B_IMPEACH, B_BOOMBAP, B_BLAST, B_DOOM, NPAT, B_SAMPLED = NPAT };
enum { PR_CLEAN, PR_TAPE, PR_VINYL, PR_12BIT, PR_DUSTY };
static const char *const pages[] = { "KIT", "KIT 2" };
static const char *const break_l[NPAT + 1] = { "AMEN", "THINK", "APACHE", "FUNKY", "IMPEACH", "BOOM BAP", "BLAST", "DOOM", "SAMPLED" };
static const char *const print_l[] = { "CLEAN", "TAPE", "VINYL", "12-BIT", "DUSTY" };
static const char *const length_l[] = { "AUTO", "1 BAR", "2 BARS", "4 BARS" };
static const char *const slices_l[] = { "AUTO", "8", "16", "32" };
static const param_t params[KP_N] = {
    [KP_BREAK]  = { "BREAK", "kbrk", 0, PK_ENUM, 0, 0, NPAT, 1, 0, "", break_l, "THE BREAK IT CHOPS" },
    [KP_PRINT]  = { "PRINT", "kprt", 0, PK_ENUM, 0, 0, 4, 1, 1, "", print_l, "TAPE, A RECORD, A SAMPLER" },
    [KP_ROOM]   = { "ROOM", "krom", 0, PK_NUM, PF_INT, 0, 100, 5, 40, "%", NULL, "THE ROOM IT WAS PLAYED IN" },
    [KP_TUNE]   = { "TUNE", "ktun", 0, PK_NUM, PF_INT | PF_SIGNED, -12, 12, 1, 0, "st", NULL, "THE DRUMS UP OR DOWN" },
    [KP_TONE]   = { "TONE", "ktne", 0, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "DARK OR BRIGHT" },
    [KP_DECAY]  = { "DECAY", "kdcy", 0, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "MUFFLED OR RINGING DRUMS" },
    [KP_SWING]  = { "SWING", "kswg", 0, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "LATER OFFBEATS" },
    [KP_GHOSTS] = { "GHOSTS", "kgho", 0, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "SOFT SNARE NOTES" },
    [KP_HUMAN]  = { "HUMAN", "khum", 0, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "HOW LOOSE THE DRUMMER IS" },
    [KP_TAKE]   = { "TAKE", "ktak", 0, PK_NUM, PF_INT, 1, 9, 1, 1, "", NULL, "ANOTHER TAKE OF IT" },
    [KP_LENGTH] = { "LENGTH", "klen", 1, PK_ENUM, PF_SYS, 0, 3, 1, 0, "", length_l, "WHOLE BEATS, OR BARS" },
    [KP_SLICES] = { "SLICES", "ksli", 1, PK_ENUM, PF_SYS, 0, 3, 1, 0, "", slices_l, "AT ITS HITS, OR EQUAL PARTS" },
    [KP_GATE]   = { "GATE", "kgat", 1, PK_NUM, PF_INT | PF_SYS, -60, -12, 2, -36, "dB", NULL, "WHAT STARTS A TAKE" },
};
#define P(i) MP(M_KIT, (i))

// each sound's break (WRECK_NOTES.md): the Amen for WRECK and SNARES, the Think for DRILL ...
static const char *const sounds[WR_NSOUNDS] = {
    "kbrk=0 kprt=1 krom=40 ktne=55",                 // WRECK: the Amen, off tape
    "kbrk=0 kprt=0 krom=25 ktun=1 ktne=70",          // SNARES: the Amen, clean and crisp
    "kbrk=1 kprt=0 krom=20 ktne=65",                 // DRILL: the Think
    "kbrk=0 kprt=3 krom=25 ktne=60",                 // GABBA: the Amen through a 12-bit sampler
    "kbrk=6 kprt=3 krom=30",                         // GRIND: a blast
    "kbrk=2 kprt=2 krom=50",                         // BAROQUE: the Apache off a record
    "kbrk=4 kprt=2 krom=60 ktun=-3 ktne=40",         // WIZARD: a heavy break, dark
    "kbrk=5 kprt=1 krom=10 ktun=-2 kdcy=40",         // EARTH: a dry, sparse break
    "kbrk=1 kprt=4 krom=80 ktne=35",                 // DRONE: noise breaks: the Think, dusty, a big room
    "kbrk=7 kprt=1 krom=70",                         // THUNDER: big slow toms and crashes
};

// ---------------------------------------------------------------- the breaks
// Lines of sixteenths, one character a step. K kick: x X(accent) g(soft). S snare: x X g(ghost) r(rimshot) f(flam).
// H hats: x X closed, o O open, p the foot. R ride: x X, b the bell. C crash: x X. T1 T2 toms (or bongos): x X g.
typedef struct {
    const char *name;
    float bpm; int steps;
    float swing;               // its own: an offbeat sixteenth this much of half a step late
    float lay_ms;              // the backbeat laid back
    float kick_hz, kick_dec, snare_hz, snare_dec, wires, tom_hz[2], tom_dec, hat, ride_hz, crash_dec, room_t60;
    bool  bongos;
    const char *k, *s, *h, *r, *c, *t1, *t2;
} pat_t;
static const pat_t pats[NPAT] = {
    [B_AMEN] = { "AMEN", 136, 32, 0.0f, 3,  58, 0.075f, 235, 1.0f, 1.0f, { 150, 100 }, 0.16f, 1.0f, 440, 1.6f, 0.75f, false,
        .k = "x.x.......xx...." "x.x.......x.....",
        .s = "....X..x.x..X..x" "....X..x.x....X.",
        .r = "X.x.X.x.X.x.X.x." "X.x.X.x.X.x.X.x." },
    [B_THINK] = { "THINK", 115, 32, 0.05f, 1,  64, 0.06f, 215, 0.8f, 1.15f, { 160, 110 }, 0.14f, 1.15f, 470, 1.4f, 0.4f, false,
        .k = "x..x....x..x...." "x..x..x...x.....",
        .s = "....X.g..g..X.g." "....X..g.g..Xg.x",
        .h = "XxXxXxXxXxoxXxXx" "XxXxXxXxXxXxXxox" },
    [B_APACHE] = { "APACHE", 118, 32, 0.08f, 2,  56, 0.09f, 200, 1.0f, 0.95f, { 420, 290 }, 0.06f, 1.0f, 430, 1.8f, 0.7f, true,
        .k = "X......x..x....." "X......x..x.....",
        .s = "....X.......X..." "....X.......X..g",
        .h = "x.x.x.x.x.x.x.x." "x.x.x.x.x.x.x.o.",
        .c = "x..............." "................",
        .t1 = "Xgx.Xgx.Xgx.Xgx." "Xgx.Xgx.XxXxXxXx",
        .t2 = "...x...X...x...X" "...x...X........" },
    [B_FUNKY] = { "FUNKY", 100, 32, 0.12f, 2,  60, 0.065f, 225, 0.9f, 0.95f, { 160, 110 }, 0.14f, 1.0f, 450, 1.4f, 0.3f, false,
        .k = "x.x.......x..x.." "x.x....x..x.....",
        .s = "....X..g.g.gX..g" "....X.g..g.gX..g",
        .h = "XxXxXxXxXxXxXxox" "XxXxXxoxXxXxXxXx" },
    [B_IMPEACH] = { "IMPEACH", 98, 32, 0.1f, 4,  52, 0.11f, 185, 1.2f, 0.9f, { 140, 95 }, 0.2f, 0.95f, 420, 1.8f, 0.85f, false,
        .k = "X......xX.x....." "X......xX.x...x.",
        .s = "....X.......X..." "....X.......X..g",
        .h = "x.x.x.x.x.x.x.o." "x.x.x.x.x.x.x.o." },
    [B_BOOMBAP] = { "BOOM BAP", 90, 32, 0.3f, 5,  55, 0.1f, 195, 0.9f, 0.8f, { 140, 95 }, 0.18f, 0.9f, 430, 1.6f, 0.4f, false,
        .k = "X..x......X....." "X......x..X.....",
        .s = "....r.......r..." "....r.......r...",
        .h = "x.x.x.x.x.x.x.x." "x.x.x.x.x.x.x.x." },
    [B_BLAST] = { "BLAST", 110, 32, 0.0f, 0,  66, 0.05f, 245, 0.7f, 1.1f, { 170, 115 }, 0.15f, 1.1f, 470, 1.5f, 0.5f, false,
        .k = "x.x.x.x.x.x.x.x." "x.x.x.x.x.x.....",
        .s = ".x.x.x.x.x.x.x.x" "x.x.x.x.x.x.....",
        .r = "..x.x.x.x.x.x.x." "..x.x.x.x.x.....",
        .c = "X..............." "X...............",
        .t1 = "................" "............XX..",
        .t2 = "................" "..............XX" },
    [B_DOOM] = { "DOOM", 72, 32, 0.0f, 9,  47, 0.2f, 160, 1.6f, 0.8f, { 105, 70 }, 0.45f, 0.9f, 400, 3.0f, 1.7f, false,
        .k = "X.....x........." "X.....x.........",
        .s = "........r......." "........r......g",
        .r = "b...b...b...b..." "b...b...b...b...",
        .c = "X..............." "X.......x.......",
        .t1 = "............x..." "............xx..",
        .t2 = "..............X." "..............XX" },
};
static int32_t pat_len(const pat_t *p) { return (int32_t)((float)p->steps * 15.0f * SRF / p->bpm + 0.5f); }

// ---------------------------------------------------------------- the buffers and the break now
static int16_t *pcm[2], *ring;
static float   *roomb;
static int16_t  no_pcm[64];                      // out of memory: a silent break, so kit_break() is never empty
static brk_t    brk[2];
static uint32_t key_of[2];                       // the settings a buffer's built-in break was rendered with (0: none)
static uint8_t  env_c[2][ENV_N], guess_k[2][KIT_SLICES_MAX];
static volatile int cur = 1;
static uint32_t serial_n = 1;
static int      take_buf = -1;                   // the buffer holding the sampled break, while it is whole
static size_t   mem_psram;
static volatile uint32_t ring_w;                 // frames written to the ring (the audio task's)

const brk_t *kit_break(void) { return &brk[__atomic_load_n(&cur, __ATOMIC_ACQUIRE)]; }
static void publish(int b) { __atomic_store_n(&cur, b, __ATOMIC_RELEASE); }
// a buffer may be written once no voice of BEAT's reads the break it holds
uint32_t (*kit_test_reading)(void);              // tools/sim only: stands in for beat_reading()
static bool buf_free(int b)
{
    uint32_t r = kit_test_reading ? kit_test_reading() : beat_reading();
    return b != cur && (brk[b].serial == 0 || r != brk[b].serial);
}

// ---------------------------------------------------------------- the audio side: the ring (all kit_feed does)
void kit_feed(const float *in, int n, uint32_t f0)
{
    int16_t *r = ring;
    if (!r) return;
    for (int i = 0; i < n; i++) {
        float v = in[i] * 32767.0f;
        v = v > 32767.0f ? 32767.0f : v < -32767.0f ? -32767.0f : v;
        r[(f0 + (uint32_t)i) & RMASK] = (int16_t)v;
    }
    __atomic_store_n(&ring_w, f0 + (uint32_t)n, __ATOMIC_RELEASE);
}

// ================================================================ the render (UI task)
// It runs in spans of up to 32 frames, cut at every hit; each voice runs a whole span with its state in registers and
// drops the parts of itself that have died away (a beater's click, an overtone) at the span's start.
#define SPAN 32
static uint32_t rs = 1;                          // the render's dice
static inline float nz(void) { return rng_bi(&rs); }
static inline float kdec(float tau) { return expf(-1.0f / (tau * SRF)); }
static inline float att_c(float tau) { return 1.0f - expf(-1.0f / (tau * SRF)); }
static float uni(uint32_t *s) { return rng_uni(s); }
static inline float wrapf1(float p) { return p >= 1.0f ? p - 1.0f : p; }
// the sine of a phase already in 0..1 (sin_turns without its wrap)
static inline float sin01(float p) { float x = 2.0f * p - 1.0f, y = 4.0f * x * (1.0f - fabsf(x)); return -(0.225f * (y * fabsf(y) - y) + y); }
// a cubic soft clip: straight through the middle, flat from 1.5 on (no division)
static inline float sat3(float x) { x = x < -1.5f ? -1.5f : x > 1.5f ? 1.5f : x; return x - 0.148148f * x * x * x; }

enum { I_KICK, I_SNARE, I_HAT, I_OHAT, I_PHAT, I_RIDE, I_BELL, I_CRASH, I_TOM1, I_TOM2, I_N };
enum { EF_GHOST = 1, EF_RIM = 2, EF_GRACE = 4, EF_PRE = 8 };
typedef struct { int32_t at; uint8_t inst, step, flags, pad; float vel, r1, r2; } ev_t;
static UI_RAM ev_t ev[EV_MAX];

// the kit's voicing for this render: the break's, with TUNE, TONE and DECAY on it
static struct {
    float tune, tone, dec, human;
    float kick_hz, kick_dec, snare_hz, snare_dec, wires, tom_hz[2], tom_dec, hat, ride_hz, crash_dec;
    bool bongos;
} V;

// modes: two-pole resonators, kept as arrays so a bank runs as one loop
static void mode_set(float *c1, float *c2, float *sw, float f, float t60)
{
    float w = TWO_PI * clampf(f, 20.0f, 0.45f * SRF) * ISR, r = expf(-6.9078f / (t60 * SRF));
    *c1 = 2.0f * r * cosf(w); *c2 = -r * r; *sw = sinf(w);
}
// a half-sine pulse of tc frames: the stick on the head (shorter when harder); its area is 0.64 tc
static inline float pulse(int t, int tc) { return sin_turns(0.5f * (float)t / (float)tc); }

// ---- kicks and toms: a membrane. Its fundamental glides down from the strike (the head's tension rising with how
// far it moves), four overtones (a circular membrane's) ring from the strike, the beater's or the stick's click, a knock
#define NMEM 5
#define MEM_M 4
typedef struct {
    bool on, kick; int t, tc;
    float amp, f0, ga, kga, gb, kgb, ph, ae, kae, at, sat, out, snd, wire;
    float c1[MEM_M], c2[MEM_M], y1[MEM_M], y2[MEM_M], g[MEM_M];
    float ce, kce, ke, kke, carry, last;
    svf_t cbp, kbp;
} mem_t;
// ---- snare: seven head modes, three rim/shell modes
#define NSNARE 4
#define SN_M 10
static const float sn_r[7] = { 1.0f, 1.59f, 2.14f, 2.30f, 2.65f, 2.92f, 3.16f };
static const float sn_t[7] = { 0.30f, 0.20f, 0.15f, 0.13f, 0.11f, 0.09f, 0.08f };
typedef struct {
    bool on; int t, tc; float amp, ce, kce, carry, last;
    float c1[SN_M], c2[SN_M], y1[SN_M], y2[SN_M], g[SN_M];
    svf_t chp;
} snare_t;
// ---- cymbals: metal (square oscillators at inharmonic ratios) and noise
static const float metal_f[6] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
#define NRM 12
static const float ride_r[NRM] = { 1.0f, 1.47f, 2.09f, 2.56f, 3.18f, 3.73f, 4.35f, 5.11f, 6.02f, 7.1f, 8.37f, 9.88f };
static const float ride_t[NRM] = { 4.0f, 3.6f, 3.2f, 2.8f, 2.5f, 2.2f, 2.0f, 1.8f, 1.6f, 1.4f, 1.2f, 1.0f };
static const float ride_bow[NRM] = { 0.005f, 0.008f, 0.012f, 0.018f, 0.025f, 0.03f, 0.035f, 0.04f, 0.04f, 0.04f, 0.04f, 0.04f };
static const float ride_bell[NRM] = { 0.08f, 0.5f, 0.65f, 0.55f, 0.4f, 0.3f, 0.2f, 0.12f, 0.08f, 0.06f, 0.05f, 0.04f };

static struct {
    mem_t   mem[NMEM];
    snare_t snare[NSNARE];
    // the wires
    float w_env, w_burst, kw_burst, w_snap, kw_snap, w_att, w_rel; svf_t w_hp1, w_hp2, w_lp;
    // hats
    bool  h_on; int h_idle; float h_ph[6], h_df[6], hc, khc, ho, kho, kho_open, hpd, khpd, ht, kht, sl_ph;
    svf_t h_hp1, h_hp2, h_pbp, h_tbp;
    // ride
    bool  r_on; int r_idle, r_t, r_tc; float r_ph[6], r_df[6], r_wt, kr_wt, r_we, r_wa, r_pg, kr_pg;
    float rc1[NRM], rc2[NRM], ry1[NRM], ry2[NRM], rg[NRM], rsw[NRM];
    svf_t r_hp1, r_hp2, r_pbp;
    // crash
    bool  c_on; int c_idle, c_tt; float c_ph[6], c_df[6], c_t, kc_t, c_e, c_ea, c_f, kc_f, c_b, kc_b;
    float cc1[3], cc2[3], cy1[3], cy2[3], cg[3], csw[3];
    svf_t c_hp1, c_hp2, c_lp;
    // the room
    int   er_w; float er_lp, fd_d[4], fd_g[4], fd_c, lvl;
    // the print
    int   mode; float c_env, c_att, c_rel, thr, ratio, mk, drive, hold_ph, hold_dt, held, crk, pre_g, hiss;
    biquad_t bq_hp, bq_lp, bq_pk, bq_lp2, bq_lp3; bool use_pk, use_lp2, crush, vinyl; svf_t hiss_lp;
} K;
// a span's buses
static float s_mix[SPAN], s_send[SPAN], s_drive[SPAN], s_heads[SPAN];

// the room: early reflections (a multi-tap line), two diffusers, a four-line feedback delay network with damping
#define ER_N 2048                                 // the lines are powers of two, read with masks
#define AP1  113
#define AP2  337
#define AP1_N 128
#define AP2_N 512
static const int fd_len[4] = { 811, 1009, 1213, 1429 }, fd_n[4] = { 1024, 1024, 2048, 2048 };
#define ROOM_N (ER_N + AP1_N + AP2_N + 1024 + 1024 + 2048 + 2048)
static const int   er_tap[9] = { 137, 251, 360, 467, 609, 757, 941, 1149, 1361 };
static const float er_g[9] = { 0.55f, -0.45f, 0.38f, -0.32f, 0.27f, -0.22f, 0.18f, -0.15f, 0.12f };

static void kit_reset(const pat_t *p)
{
    memset(&K, 0, sizeof K);
    if (roomb) memset(roomb, 0, ROOM_N * sizeof(float));
    float tn = V.tune;
    // wires
    svf_set(&K.w_hp1, 1700.0f, 0.7f); svf_set(&K.w_hp2, 1700.0f, 0.7f); svf_set(&K.w_lp, 7000.0f + 4000.0f * V.tone, 0.6f);
    K.w_att = att_c(0.0005f); K.w_rel = att_c(0.025f);
    K.kw_burst = kdec(0.07f * sqrtf(V.snare_dec * V.dec)); K.kw_snap = kdec(0.006f);
    // hats
    float hs = 2.1f * V.hat * powf(tn, 0.3f);
    for (int i = 0; i < 6; i++) { K.h_df[i] = metal_f[i] * hs * ISR; K.r_df[i] = metal_f[i] * 3.3f * powf(tn, 0.2f) * ISR; K.c_df[i] = metal_f[i] * 1.45f * powf(tn, 0.3f) * ISR; }
    float hcut = clampf(6200.0f * (0.85f + 0.3f * V.tone) * V.hat, 3000.0f, 12000.0f);
    svf_set(&K.h_hp1, hcut, 0.75f); svf_set(&K.h_hp2, hcut, 0.75f);
    svf_set(&K.h_pbp, 2200.0f, 1.2f); svf_set(&K.h_tbp, 4200.0f, 1.0f);
    K.kho_open = kdec(0.4f * powf(V.dec, 0.6f)); K.kho = K.kho_open;
    K.khpd = kdec(0.012f); K.kht = kdec(0.0006f); K.khc = kdec(0.02f);
    // ride
    float rh = V.ride_hz * powf(tn, 0.25f), rd = sqrtf(V.dec);
    for (int i = 0; i < NRM; i++) mode_set(&K.rc1[i], &K.rc2[i], &K.rsw[i], rh * ride_r[i], ride_t[i] * rd);
    svf_set(&K.r_hp1, 3800.0f, 0.7f); svf_set(&K.r_hp2, 3800.0f, 0.7f); svf_set(&K.r_pbp, 5200.0f, 1.0f);
    K.kr_wt = kdec(1.4f * rd); K.r_wa = att_c(0.012f); K.kr_pg = kdec(0.004f);
    // crash
    svf_set(&K.c_hp1, 2400.0f, 0.7f); svf_set(&K.c_hp2, 2400.0f, 0.7f); svf_set(&K.c_lp, 12000.0f, 0.7f);
    K.kc_t = kdec(V.crash_dec * rd); K.c_ea = att_c(0.004f); K.kc_f = kdec(0.7f); K.kc_b = kdec(0.07f);
    static const float cmf[3] = { 330.0f, 505.0f, 770.0f };
    for (int i = 0; i < 3; i++) mode_set(&K.cc1[i], &K.cc2[i], &K.csw[i], cmf[i] * tn, 2.5f * rd);
    // the room
    float room = P(KP_ROOM) * 0.01f, t60 = p->room_t60 * (0.6f + 0.8f * room);
    for (int i = 0; i < 4; i++) K.fd_g[i] = powf(10.0f, -3.0f * (float)fd_len[i] / (t60 * SRF));
    K.fd_c = onepole_hz(4500.0f + 2000.0f * V.tone);
    K.lvl = room <= 0.0f ? 0.0f : 0.9f * powf(room, 1.2f);
    // the print
    K.mode = (int)P(KP_PRINT);
    float lpf = 0.7f + 0.6f * V.tone;
    K.c_att = att_c(0.004f); K.c_rel = att_c(0.08f);
    switch (K.mode) {
    default:
    case PR_CLEAN: K.thr = 0.5f;  K.ratio = 1.6f; K.drive = 1.0f; biquad_set(&K.bq_hp, BQ_HP, 25.0f, 0.7f, 0); biquad_set(&K.bq_lp, BQ_LP, 15000.0f * lpf, 0.7f, 0); break;
    case PR_TAPE:  K.thr = 0.3f;  K.ratio = 2.5f; K.drive = 1.7f; biquad_set(&K.bq_hp, BQ_HP, 30.0f, 0.7f, 0); biquad_set(&K.bq_lp, BQ_LP, 12500.0f * lpf, 0.7f, 0);
                   biquad_set(&K.bq_pk, BQ_PEAK, 90.0f, 1.0f, 2.5f); K.use_pk = true; K.hiss = 0.0006f; svf_set(&K.hiss_lp, 9000.0f, 0.6f); break;
    case PR_VINYL: case PR_DUSTY:
                   K.thr = 0.28f; K.ratio = 3.0f; K.drive = 1.4f; biquad_set(&K.bq_hp, BQ_HP, 45.0f, 0.7f, 0); biquad_set(&K.bq_lp, BQ_LP, 10500.0f * lpf, 0.7f, 0);
                   biquad_set(&K.bq_lp2, BQ_LP, 12000.0f * lpf, 0.6f, 0); K.use_lp2 = true;
                   biquad_set(&K.bq_pk, BQ_PEAK, 2200.0f, 0.8f, 2.0f); K.use_pk = true; K.vinyl = true;
                   svf_set(&K.hiss_lp, 6000.0f, 0.6f);
                   if (K.mode == PR_DUSTY) { K.crush = true; K.hold_dt = 22000.0f / SRF; K.drive = 1.8f; }
                   break;
    case PR_12BIT: K.thr = 0.25f; K.ratio = 3.5f; K.drive = 2.0f; biquad_set(&K.bq_hp, BQ_HP, 35.0f, 0.7f, 0); biquad_set(&K.bq_lp, BQ_LP, 12000.0f * lpf, 0.7f, 0);
                   K.crush = true; K.hold_dt = 24000.0f / SRF; break;
    }
    if (K.crush) biquad_set(&K.bq_lp3, BQ_LP, 11000.0f * lpf, 0.7f, 0);
    K.mk = 1.0f + (K.ratio - 1.0f) * 0.18f;
    K.pre_g = 0.9f;
}

// ---- the hits
static const float kick_r[MEM_M] = { 1.47f, 1.95f, 2.42f, 2.95f }, tom_r[MEM_M] = { 1.59f, 2.14f, 2.30f, 2.65f };
static mem_t *mem_voice(void)
{
    int k = 0;
    for (int i = 0; i < NMEM; i++) { if (!K.mem[i].on) { k = i; break; } if (K.mem[i].ae * K.mem[i].amp < K.mem[k].ae * K.mem[k].amp) k = i; }
    mem_t *v = &K.mem[k];
    float carry = v->on ? v->last + v->carry : 0.0f;              // a stolen voice fades, never cut
    memset(v, 0, sizeof *v);
    v->carry = carry; v->on = true; v->ae = 1.0f; v->ga = 1.0f; v->gb = 1.0f;
    return v;
}
static void mem_modes(mem_t *v, const float *ratio, const float *gain, float t60)
{
    float area = 0.64f * (float)v->tc, sw;
    for (int i = 0; i < MEM_M; i++) { mode_set(&v->c1[i], &v->c2[i], &sw, v->f0 * ratio[i], t60 * (1.0f - 0.15f * (float)i)); v->g[i] = gain[i] * sw / area; }
}
static void kick_hit(const ev_t *e)
{
    mem_t *v = mem_voice();
    float vel = e->vel, h = V.human, hard = vel * (0.5f + V.tone);
    v->kick = true;
    v->amp = powf(vel, 1.2f);
    v->f0 = V.kick_hz * V.tune * (1.0f + 0.025f * (e->r1 - 0.5f) * h);
    v->ga = 0.5f + 0.7f * vel; v->kga = kdec(0.005f); v->gb = 0.25f; v->kgb = kdec(0.035f);
    v->kae = kdec(V.kick_dec * V.dec * (1.0f + 0.12f * (e->r2 - 0.5f) * h)); v->at = 1.0f / 12.0f; v->sat = 1.6f;
    v->tc = clampi((int)(40.0f - 22.0f * hard), 12, 40);          // the felt beater: 0.4 to 1.2 ms on the head
    static const float kg[MEM_M] = { 0.35f, 0.22f, 0.15f, 0.1f };
    mem_modes(v, kick_r, kg, 0.16f * sqrtf(V.dec));
    v->ce = 0.9f * powf(vel, 1.5f) * (0.4f + V.tone); v->kce = kdec(0.0028f);
    svf_set(&v->cbp, 3000.0f * (0.7f + 0.6f * V.tone), 0.6f);
    v->ke = 0.28f * vel; v->kke = kdec(0.007f);
    svf_set(&v->kbp, 650.0f * V.tune, 1.1f);
    v->out = 0.55f; v->snd = 0.2f; v->wire = 0.06f;
    K.w_burst += 0.04f * vel;                                     // the snare's wires buzz with the kick
}
static void tom_hit(const ev_t *e)
{
    mem_t *v = mem_voice();
    float vel = e->vel, h = V.human, hard = vel * (0.5f + V.tone);
    bool bongo = V.bongos;
    v->amp = powf(vel, 1.3f);
    v->f0 = V.tom_hz[e->inst == I_TOM1 ? 0 : 1] * V.tune * (1.0f + 0.015f * (e->r1 - 0.5f) * h);
    v->ga = bongo ? 0.04f : 0.1f + 0.06f * vel; v->kga = kdec(bongo ? 0.02f : 0.06f); v->gb = 0.0f; v->kgb = 0.0f;
    float d = V.tom_dec * V.dec * (1.0f + 0.1f * (e->r2 - 0.5f) * h);
    v->kae = kdec(d); v->at = 1.0f / 8.0f; v->sat = bongo ? 0.8f : 1.0f;
    v->tc = clampi((int)(14.0f - 9.0f * hard), 4, 14);            // the stick
    static const float tg[MEM_M] = { 0.45f, 0.3f, 0.22f, 0.15f }, bg[MEM_M] = { 0.35f, 0.25f, 0.15f, 0.1f };
    mem_modes(v, tom_r, bongo ? bg : tg, (bongo ? 0.25f : 0.9f) * d * 6.9f * 0.35f);
    v->ce = (bongo ? 1.3f : 0.45f) * vel * (0.5f + V.tone); v->kce = kdec(bongo ? 0.007f : 0.0018f);  // a bongo's slap
    svf_set(&v->cbp, (bongo ? 2300.0f : 2600.0f) * (0.8f + 0.4f * V.tone), bongo ? 0.8f : 0.9f);
    v->ke = (bongo ? 0.1f : 0.25f) * vel; v->kke = kdec(0.01f);
    svf_set(&v->kbp, (bongo ? 900.0f : 420.0f) * V.tune, 1.2f);
    v->out = bongo ? 0.95f : 0.85f; v->snd = 0.6f; v->wire = 0.1f;
    K.w_burst += 0.06f * vel;
}

static void snare_hit(const ev_t *e)
{
    int k = 0;
    for (int i = 0; i < NSNARE; i++) { if (!K.snare[i].on) { k = i; break; } if (K.snare[i].amp * (float)(1 + K.snare[k].t) < K.snare[k].amp * (float)(1 + K.snare[i].t)) k = i; }
    snare_t *v = &K.snare[k];
    float carry = v->on ? v->last + v->carry : 0.0f, vel = e->vel, h = V.human;
    memset(v, 0, sizeof *v);
    v->carry = carry; v->on = true;
    bool rim = (e->flags & EF_RIM) != 0;
    v->amp = powf(vel, 1.4f) * (rim ? 1.15f : 1.0f);
    float hard = vel * (0.5f + 0.5f * V.tone);
    v->tc = clampi((int)(11.0f - 7.0f * hard + 2.0f * (e->r2 - 0.5f) * h), 3, 14);
    float area = 0.64f * (float)v->tc;
    float r = clampf(0.25f + 0.5f * (e->r1 - 0.5f) * (0.4f + h) + ((e->flags & EF_GHOST) ? 0.2f : 0.0f), 0.0f, 1.0f);   // strike: centre 0, edge 1
    float f1 = V.snare_hz * V.tune * (1.0f + 0.01f * (e->r1 - 0.5f) * h), d = V.snare_dec * V.dec;
    float bright = 0.6f + 0.8f * V.tone * vel, sw;
    for (int i = 0; i < 7; i++) {
        float g = i == 0 ? 1.0f - 0.45f * r : i == 1 ? 0.25f + 0.55f * r : i < 4 ? 0.15f + 0.32f * r : (0.1f + 0.25f * r) * bright;
        mode_set(&v->c1[i], &v->c2[i], &sw, f1 * sn_r[i], sn_t[i] * d);
        v->g[i] = g * sw / area;
    }
    static const float rim_f[3] = { 1050.0f, 1780.0f, 3150.0f }, rim_t[3] = { 0.07f, 0.05f, 0.035f };
    static const float rim_on[3] = { 1.6f, 1.1f, 0.8f }, rim_off[3] = { 0.08f, 0.05f, 0.03f };
    for (int i = 0; i < 3; i++) {
        mode_set(&v->c1[7 + i], &v->c2[7 + i], &sw, rim_f[i] * sqrtf(V.tune), rim_t[i]);
        v->g[7 + i] = (rim ? rim_on[i] : rim_off[i]) * sw / area;
    }
    v->ce = vel * vel * 0.9f * (0.5f + V.tone) * (rim ? 1.6f : 1.0f); v->kce = kdec(rim ? 0.003f : 0.0022f);   // the crack
    svf_set(&v->chp, 1200.0f, 0.7f);
    K.w_burst += 0.6f * powf(vel, 0.7f);                          // the wires snap (soft notes are mostly wire)
    K.w_snap += 0.9f * vel;
}

static void hat_hit(const ev_t *e)
{
    float vel = e->vel;
    K.h_on = true; K.h_idle = 0;
    if (e->inst == I_OHAT) { K.ho += vel; K.kho = K.kho_open; K.ht += 0.4f * vel; }
    else if (e->inst == I_PHAT) { K.hpd += 0.6f * vel; K.kho = kdec(0.012f); }
    else {
        K.hc += vel; K.khc = kdec(0.022f * (0.75f + 0.5f * vel) * powf(V.dec, 0.3f) * (1.0f + 0.15f * (e->r1 - 0.5f) * V.human));
        K.kho = kdec(0.012f); K.ht += 0.4f * vel;                 // a closed hat chokes the open one
    }
}

static void ride_hit(const ev_t *e)
{
    float vel = e->vel;
    bool bell = e->inst == I_BELL;
    K.r_on = true; K.r_idle = 0;
    K.r_tc = bell ? 6 : 4; K.r_t = 0;
    float area = 0.64f * (float)K.r_tc, s = 0.8f + 0.4f * e->r1;
    for (int i = 0; i < NRM; i++) K.rg[i] = (bell ? ride_bell[i] : ride_bow[i] * s) * vel * K.rsw[i] / area;
    K.r_wt += vel * (bell ? 0.12f : 0.25f);
    K.r_pg = vel * (bell ? 0.4f : 1.0f);
}

static void crash_hit(const ev_t *e)
{
    float vel = e->vel;
    K.c_on = true; K.c_idle = 0;
    K.c_t += vel; K.c_f = 1.0f; K.c_tt = 0;
    for (int i = 0; i < 3; i++) K.cg[i] = 0.006f * vel * K.csw[i];
    K.c_b += vel;
}

static bool is_cymbal(int inst) { return inst >= I_HAT && inst <= I_CRASH; }
static void hit(const ev_t *e)
{
    switch (e->inst) {
    case I_KICK: kick_hit(e); break;
    case I_SNARE: snare_hit(e); break;
    case I_TOM1: case I_TOM2: tom_hit(e); break;   // (bongos when the break says so)
    case I_HAT: case I_OHAT: case I_PHAT: hat_hit(e); break;
    case I_RIDE: case I_BELL: ride_hit(e); break;
    case I_CRASH: crash_hit(e); break;
    default: break;
    }
}

// ---- a span of each
static inline float metal(float *ph, const float *df, int n)
{
    float m = 0.0f;
    for (int i = 0; i < n; i++) { float p = wrapf1(ph[i] + df[i]); ph[i] = p; m += p < 0.5f ? 1.0f : -1.0f; }
    return m;
}

static void mem_span(mem_t *v, int n)
{
    float ph = v->ph, ga = v->ga, gb = v->gb, ae = v->ae, ce = v->ce, ke = v->ke, c = v->carry, y = 0.0f;
    const float f0 = v->f0 * ISR, amp = v->amp, kga = v->kga, kgb = v->kgb, kae = v->kae, sat = v->sat, so = v->out * amp, ss = v->snd * amp;
    const float wr = v->wire * amp;
    float y1[MEM_M], y2[MEM_M];
    memcpy(y1, v->y1, sizeof y1); memcpy(y2, v->y2, sizeof y2);
    int t = v->t;
    bool strike = t < v->tc || ce + ke > 1e-3f || t * v->at < 1.0f;
    if (strike) {                                                         // the strike: the pulse into the head, click, knock
        for (int i = 0; i < n; i++, t++) {
            float x = t < v->tc ? pulse(t, v->tc) : 0.0f;
            ph = wrapf1(ph + f0 * (1.0f + ga + gb));
            float body = sin01(ph) * ae, md = 0.0f;
            for (int m = 0; m < MEM_M; m++) { float o = v->c1[m] * y1[m] + v->c2[m] * y2[m] + v->g[m] * x; y2[m] = y1[m]; y1[m] = o; md += o; }
            float a = t * v->at; if (a < 1.0f) body *= a;
            float q = nz();
            y = sat3(sat * (body + md)) + 0.5f * x + svf_bp(&v->cbp, q, NULL) * ce + svf_bp(&v->kbp, q, NULL) * ke;
            ga *= kga; gb *= kgb; ae *= kae; ce *= v->kce; ke *= v->kke;
            s_mix[i] += y * so + c; s_send[i] += y * ss; s_drive[i] += wr * fabsf(body + md);
            c *= 0.93f;
        }
    } else if (fabsf(y1[0]) + fabsf(y1[1]) > 1e-3f) {                      // ringing with its overtones
        for (int i = 0; i < n; i++) {
            ph = wrapf1(ph + f0 * (1.0f + ga + gb));
            float md = 0.0f;
            for (int m = 0; m < MEM_M; m++) { float o = v->c1[m] * y1[m] + v->c2[m] * y2[m]; y2[m] = y1[m]; y1[m] = o; md += o; }
            float body = sin01(ph) * ae;
            y = sat3(sat * (body + md));
            ga *= kga; gb *= kgb; ae *= kae;
            s_mix[i] += y * so + c; s_send[i] += y * ss; s_drive[i] += wr * fabsf(body + md);
            c *= 0.93f;
        }
        t += n;
    } else {                                                              // the fundamental alone
        for (int i = 0; i < n; i++) {
            ph = wrapf1(ph + f0 * (1.0f + ga + gb));
            float body = sin01(ph) * ae;
            y = sat3(sat * body);
            ga *= kga; gb *= kgb; ae *= kae;
            s_mix[i] += y * so + c; s_send[i] += y * ss; s_drive[i] += wr * fabsf(body);
            c *= 0.93f;
        }
        t += n; memset(y1, 0, sizeof y1); memset(y2, 0, sizeof y2);
    }
    memcpy(v->y1, y1, sizeof y1); memcpy(v->y2, y2, sizeof y2);
    v->ph = ph; v->ga = ga; v->gb = gb; v->ae = ae; v->ce = ce > 1e-3f ? ce : 0.0f; v->ke = ke > 1e-3f ? ke : 0.0f;
    v->carry = flush_denormal(c); v->t = t; v->last = y * so;
    if (ae < 1e-3f && t > 600 && fabsf(y1[0]) + fabsf(y1[1]) < 1e-3f) v->on = false;
}

static void snare_span(snare_t *v, int n)
{
    float c = v->carry, y = 0.0f, ce = v->ce, amp = v->amp;
    const bool click = ce > 1e-4f;
    int t = v->t, i = 0;
    for (; i < n && (t < v->tc || click); i++, t++) {                     // the stick on the head, its click
        float x = t < v->tc ? pulse(t, v->tc) * (1.0f + 0.25f * nz()) : 0.0f;
        float hd = 0.0f, rm = 0.0f;
        for (int m = 0; m < 7; m++) { float o = v->c1[m] * v->y1[m] + v->c2[m] * v->y2[m] + v->g[m] * x; v->y2[m] = v->y1[m]; v->y1[m] = o; hd += o; }
        for (int m = 7; m < SN_M; m++) { float o = v->c1[m] * v->y1[m] + v->c2[m] * v->y2[m] + v->g[m] * x; v->y2[m] = v->y1[m]; v->y1[m] = o; rm += o; }
        y = (hd + rm) * amp;
        if (click) { y += svf_hp(&v->chp, nz()) * ce; ce *= v->kce; }
        s_heads[i] += hd * amp;
        s_mix[i] += (y + c) * 0.68f; s_send[i] += (y + c) * 0.8f;
        c *= 0.93f;
    }
    if (i < n) {                                                            // the head and the rim ringing
        float y1[SN_M], y2[SN_M];
        memcpy(y1, v->y1, sizeof y1); memcpy(y2, v->y2, sizeof y2);
        for (; i < n; i++, t++) {
            float hd = 0.0f, rm = 0.0f;
            for (int m = 0; m < 7; m++) { float o = v->c1[m] * y1[m] + v->c2[m] * y2[m]; y2[m] = y1[m]; y1[m] = o; hd += o; }
            for (int m = 7; m < SN_M; m++) { float o = v->c1[m] * y1[m] + v->c2[m] * y2[m]; y2[m] = y1[m]; y1[m] = o; rm += o; }
            y = (hd + rm) * amp;
            s_heads[i] += hd * amp;
            s_mix[i] += (y + c) * 0.68f; s_send[i] += (y + c) * 0.8f;
            c *= 0.93f;
        }
        memcpy(v->y1, y1, sizeof y1); memcpy(v->y2, y2, sizeof y2);
    }
    v->ce = ce > 1e-4f ? ce : 0.0f; v->carry = flush_denormal(c); v->t = t; v->last = y;
    if (t > 2000 && fabsf(v->y1[0]) + fabsf(v->y1[1]) + fabsf(v->y1[2]) < 1e-4f) v->on = false;
}

// the wires: driven by the heads (and a little by the kick and the toms), slapping on the heads' swings
static void wires_span(int n)
{
    float dmax = 0.0f;
    for (int i = 0; i < n; i++) { float d = s_drive[i] + fabsf(s_heads[i]); if (d > dmax) dmax = d; }
    if (K.w_burst < 1e-3f && K.w_env < 1e-3f && K.w_snap < 1e-3f && dmax < 1e-3f) { K.w_burst = K.w_env = K.w_snap = 0.0f; return; }
    float env = K.w_env, burst = K.w_burst, snap = K.w_snap;
    const float att = K.w_att, rel = K.w_rel, kb = K.kw_burst, ks = K.kw_snap, wg = V.wires;
    for (int i = 0; i < n; i++) {
        float d = s_drive[i] + fabsf(s_heads[i]);
        env += (d > env ? att : rel) * (d - env);
        float slap = 0.6f + 0.4f * clampf(fabsf(s_heads[i]) / (env * 1.5f + 1e-4f), 0.0f, 1.0f);
        float q = svf_lp(&K.w_lp, svf_hp(&K.w_hp2, svf_hp(&K.w_hp1, nz())));
        float y = q * ((burst + 1.6f * env) * slap + snap) * wg;
        burst *= kb; snap *= ks;
        s_mix[i] += y * 0.85f; s_send[i] += y * 0.7f;
    }
    K.w_env = flush_denormal(env); K.w_burst = flush_denormal(burst); K.w_snap = flush_denormal(snap);
}

static void hats_span(int n)
{
    if (!K.h_on) return;
    float hc = K.hc, ho = K.ho, hpd = K.hpd, ht = K.ht, sl = K.sl_ph;
    const float khc = K.khc, kho = K.kho, khpd = K.khpd, kht = K.kht;
    const bool foot = hpd > 1e-4f, tick = ht > 1e-4f;
    for (int i = 0; i < n; i++) {
        float q = nz(), src = 0.08f * metal(K.h_ph, K.h_df, 6) + 0.55f * q;
        float y = svf_hp(&K.h_hp2, svf_hp(&K.h_hp1, src));
        sl = wrapf1(sl + 9.0f * ISR);
        y *= hc + ho * (1.0f + 0.18f * sin01(sl));
        if (foot) { y += svf_bp(&K.h_pbp, q, NULL) * hpd * 0.8f; hpd *= khpd; }
        if (tick) { y += svf_bp(&K.h_tbp, q, NULL) * ht; ht *= kht; }
        hc *= khc; ho *= kho;
        s_mix[i] += y * 0.7f; s_send[i] += y * 0.5f;
    }
    K.hc = hc; K.ho = ho; K.hpd = foot ? hpd : 0.0f; K.ht = tick ? ht : 0.0f; K.sl_ph = sl;
    if (hc + ho + K.hpd + K.ht < 1e-4f) { K.h_idle += n; if (K.h_idle > 256) { K.h_on = false; K.hc = K.ho = 0.0f; } } else K.h_idle = 0;
}

static void ride_span(int n)
{
    if (!K.r_on) return;
    float we = K.r_we, wt = K.r_wt, pg = K.r_pg, md = 0.0f;
    const float wa = K.r_wa, kwt = K.kr_wt, kpg = K.kr_pg;
    const bool ping = pg > 1e-4f;
    float y1[NRM], y2[NRM];
    memcpy(y1, K.ry1, sizeof y1); memcpy(y2, K.ry2, sizeof y2);
    for (int i = 0; i < n; i++) {
        md = 0.0f;
        if (K.r_t < K.r_tc) {                                             // the stick
            float x = pulse(K.r_t, K.r_tc); K.r_t++;
            for (int m = 0; m < NRM; m++) { float o = K.rc1[m] * y1[m] + K.rc2[m] * y2[m] + K.rg[m] * x; y2[m] = y1[m]; y1[m] = o; md += o; }
        } else
            for (int m = 0; m < NRM; m++) { float o = K.rc1[m] * y1[m] + K.rc2[m] * y2[m]; y2[m] = y1[m]; y1[m] = o; md += o; }
        we += wa * (wt - we);
        float q = nz();
        float y = 0.45f * md + 0.6f * svf_hp(&K.r_hp2, svf_hp(&K.r_hp1, 0.08f * metal(K.r_ph, K.r_df, 4) + 0.5f * q)) * we;
        if (ping) { y += 1.5f * svf_bp(&K.r_pbp, q, NULL) * pg; pg *= kpg; }
        wt *= kwt;
        s_mix[i] += y * 0.45f; s_send[i] += y * 0.45f;
    }
    memcpy(K.ry1, y1, sizeof y1); memcpy(K.ry2, y2, sizeof y2);
    K.r_we = we; K.r_wt = wt; K.r_pg = ping ? pg : 0.0f;
    if (fabsf(md) + we < 1e-4f) {
        K.r_idle += n;
        if (K.r_idle > 2048) { K.r_on = false; memset(K.ry1, 0, sizeof K.ry1); memset(K.ry2, 0, sizeof K.ry2); K.r_wt = K.r_we = 0.0f; }
    } else K.r_idle = 0;
}

static void crash_span(int n)
{
    if (!K.c_on) return;
    svf_set(&K.c_lp, 4500.0f + 8500.0f * K.c_f * (0.7f + 0.6f * V.tone), 0.7f);   // the crash darkens as it rings
    float ce = K.c_e, ct = K.c_t, cb = K.c_b;
    const float ea = K.c_ea, kct = K.kc_t, kcb = K.kc_b;
    for (int i = 0; i < n; i++) {
        ce += ea * (ct + 1.5f * cb - ce); cb *= kcb;
        float src = 0.09f * metal(K.c_ph, K.c_df, 4) + 0.5f * nz();
        float y = svf_lp(&K.c_lp, svf_hp(&K.c_hp2, svf_hp(&K.c_hp1, src))) * ce;
        float x = K.c_tt < 4 ? 1.0f : 0.0f; K.c_tt++;
        for (int m = 0; m < 3; m++) { float o = K.cc1[m] * K.cy1[m] + K.cc2[m] * K.cy2[m] + K.cg[m] * x; K.cy2[m] = K.cy1[m]; K.cy1[m] = o; y += o; }
        ct *= kct;
        s_mix[i] += y * 0.45f; s_send[i] += y * 0.6f;
    }
    K.c_e = ce; K.c_t = ct; K.c_b = flush_denormal(cb);
    for (int i = 0; i < n; i++) K.c_f *= K.kc_f;
    if (ce < 1e-4f && K.c_tt > 8000) {
        K.c_idle += n;
        if (K.c_idle > 2048) { K.c_on = false; K.c_t = K.c_e = 0.0f; memset(K.cy1, 0, sizeof K.cy1); memset(K.cy2, 0, sizeof K.cy2); }
    } else K.c_idle = 0;
}

static void room_span(int n)
{
    float *er = roomb, *a1 = er + ER_N, *a2 = a1 + AP1_N, *fd[4];
    fd[0] = a2 + AP2_N; fd[1] = fd[0] + fd_n[0]; fd[2] = fd[1] + fd_n[1]; fd[3] = fd[2] + fd_n[2];
    const float lvl = K.lvl, fc = K.fd_c;
    unsigned w = (unsigned)K.er_w;                                         // one write count for every line
    for (int i = 0; i < n; i++, w++) {
        er[w & (ER_N - 1)] = s_send[i];
        float e = 0.0f;
        for (int k = 0; k < 9; k++) e += er_g[k] * er[(w - (unsigned)er_tap[k]) & (ER_N - 1)];
        K.er_lp += 0.6f * (e - K.er_lp); e = K.er_lp;
        float b = a1[(w - AP1) & (AP1_N - 1)], v = e + 0.6f * b, y1 = b - 0.6f * v; a1[w & (AP1_N - 1)] = v;
        b = a2[(w - AP2) & (AP2_N - 1)]; v = y1 + 0.6f * b; float y2 = b - 0.6f * v; a2[w & (AP2_N - 1)] = v;
        float d[4], s4 = 0.0f;
        for (int k = 0; k < 4; k++) { float o = fd[k][(w - (unsigned)fd_len[k]) & (unsigned)(fd_n[k] - 1)]; d[k] = K.fd_d[k] = K.fd_d[k] + fc * (o - K.fd_d[k]); s4 += d[k]; }
        s4 *= 0.5f;                                                        // Householder: s - d is unitary
        for (int k = 0; k < 4; k++) fd[k][w & (unsigned)(fd_n[k] - 1)] = 0.35f * y2 + K.fd_g[k] * (s4 - d[k]);
        s_mix[i] += lvl * (0.7f * e + 0.45f * (d[0] - d[1] + d[2] - d[3]));
    }
    K.er_w = (int)(w & 0xFFFFFFu);                                         // a multiple of every line's size
    for (int k = 0; k < 4; k++) K.fd_d[k] = flush_denormal(K.fd_d[k]);
}

static void print_span(int n)
{
    for (int i = 0; i < n; i++) {
        float x = s_mix[i] * K.pre_g;
        // the bus compressor
        float a = fabsf(x);
        K.c_env += (a > K.c_env ? K.c_att : K.c_rel) * (a - K.c_env);
        float g = K.c_env > K.thr ? (K.thr + (K.c_env - K.thr) / K.ratio) / K.c_env : 1.0f;
        x *= g * K.mk;
        // the tape (or the desk) saturating, the band of the medium
        x = soft_tanh(K.drive * x) / K.drive;
        x = biquad_run(&K.bq_hp, x);
        if (K.use_pk) x = biquad_run(&K.bq_pk, x);
        x = biquad_run(&K.bq_lp, x);
        if (K.use_lp2) x = biquad_run(&K.bq_lp2, x);
        if (K.vinyl) {                                            // dust and crackle
            if (rng_next(&rs) < 805306u) K.crk = (nz() > 0.0f ? 1.0f : -1.0f) * (0.006f + 0.03f * rng_uni(&rs)) * (0.4f + V.tone);   // 6 a second
            x += K.crk; K.crk *= -0.55f;
            x += svf_lp(&K.hiss_lp, nz()) * 0.0025f;
        } else if (K.hiss > 0.0f) x += svf_lp(&K.hiss_lp, nz()) * K.hiss;   // the tape's hiss
        if (K.crush) {                                            // a sampler: held at its rate, twelve bits
            K.hold_ph += K.hold_dt;
            if (K.hold_ph >= 1.0f) { K.hold_ph -= 1.0f; float q = x * 2048.0f; K.held = (float)(int)(q + (q >= 0.0f ? 0.5f : -0.5f)) * (1.0f / 2048.0f); }
            x = biquad_run(&K.bq_lp3, K.held);
        }
        s_mix[i] = x;
    }
}

// n frames of everything; light: only the cymbals (the long run-up, where only what rings for seconds matters)
static void render_span(int n, bool light)
{
    memset(s_mix, 0, sizeof s_mix); memset(s_send, 0, sizeof s_send);
    if (!light) {
        memset(s_drive, 0, sizeof s_drive); memset(s_heads, 0, sizeof s_heads);
        for (int i = 0; i < NMEM; i++) if (K.mem[i].on) mem_span(&K.mem[i], n);
        for (int i = 0; i < NSNARE; i++) if (K.snare[i].on) snare_span(&K.snare[i], n);
        wires_span(n);
    }
    hats_span(n); ride_span(n); crash_span(n);
    if (light) return;
    if (K.lvl > 0.0f) room_span(n);
    print_span(n);
}

// ---------------------------------------------------------------- a break's hits: the pattern played
static int nev;
static void add_ev(int32_t at, int inst, int step, int flags, float vel, uint32_t *d)
{
    if (nev >= EV_MAX) return;
    ev_t *e = &ev[nev++];
    e->at = at; e->inst = (uint8_t)inst; e->step = (uint8_t)step; e->flags = (uint8_t)flags; e->vel = clampf(vel, 0.03f, 1.0f);
    e->r1 = uni(d); e->r2 = uni(d);
}
static float gauss(uint32_t *d) { return (uni(d) + uni(d) + uni(d) - 1.5f) * 2.0f; }

// the slices of a built-in break: one at each step with hits, its kind the loudest hit's
static void build_slices(brk_t *b, int steps)
{
    static const float weight[I_N] = { 1.0f, 0.9f, 0.5f, 0.6f, 0.4f, 0.6f, 0.7f, 1.2f, 0.9f, 0.9f };
    static const uint8_t kind_of[I_N] = { KS_KICK, KS_SNARE, KS_HAT, KS_HAT, KS_HAT, KS_RIDE, KS_RIDE, KS_CRASH, KS_TOM, KS_TOM };
    int n = 0;
    for (int s = 0; s < steps && n < KIT_SLICES_MAX; s++) {
        int32_t at = 0x7fffffff; float best = -1.0f; int kind = KS_OTHER;
        for (int i = 0; i < nev; i++) {
            const ev_t *e = &ev[i];
            if (e->step != s || (e->flags & EF_PRE)) continue;
            if (e->at < at) at = e->at;
            float w = e->vel * weight[e->inst];
            if (w > best) { best = w; kind = kind_of[e->inst]; }
        }
        if (best < 0.0f) { if (s == 0) { at = 0; kind = KS_OTHER; } else continue; }
        at -= SLICE_PRE;
        if (n == 0 || at < 0) at = 0;
        if (n > 0 && at <= b->slice[n - 1]) at = b->slice[n - 1] + 1;
        b->slice[n] = at; b->kind[n] = (uint8_t)kind; b->step[n] = (uint8_t)s; b->peak[n] = 0;
        n++;
    }
    b->nslices = n;
}

static int32_t build_events(const pat_t *p, int32_t len, int32_t pre, uint32_t seed)
{
    uint32_t d = seed;
    float sf = (float)len / (float)p->steps, h = P(KP_HUMAN) * 0.01f, gh = P(KP_GHOSTS) * 0.01f;
    float sw = clampf(p->swing + P(KP_SWING) * 0.01f * 0.667f, 0.0f, 0.8f);
    const char *lines[7] = { p->k, p->s, p->h, p->r, p->c, p->t1, p->t2 };
    nev = 0;
    for (int l = 0; l < 7; l++) {
        const char *L = lines[l];
        if (!L) continue;
        for (int s = 0; s < p->steps && L[s]; s++) {
            char c = L[s];
            if (c == '.' || c == ' ') continue;
            int inst = I_KICK, fl = 0; float vel = 0.75f, bias = 0.0f;
            switch (l) {
            case 0: inst = I_KICK; vel = c == 'X' ? 1.0f : c == 'g' ? 0.45f : 0.85f; break;
            case 1: inst = I_SNARE;
                if (c == 'g') { if (gh < 0.1f) continue; vel = 0.2f * clampf(gh * 2.0f, 0.0f, 1.6f); fl = EF_GHOST; bias = -1.0f * h; }
                else if (c == 'r') { vel = 1.0f; fl = EF_RIM; bias = p->lay_ms * (0.5f + h); }
                else if (c == 'X') { vel = 1.0f; bias = p->lay_ms * (0.5f + h); }
                else vel = 0.72f;
                break;
            case 2: inst = c == 'o' || c == 'O' ? I_OHAT : c == 'p' ? I_PHAT : I_HAT; vel = c == 'X' || c == 'O' ? 0.9f : c == 'p' ? 0.6f : 0.62f; bias = -1.0f * h; break;
            case 3: inst = c == 'b' ? I_BELL : I_RIDE; vel = c == 'X' ? 0.85f : c == 'b' ? 0.8f : 0.62f; bias = -1.0f * h; break;
            case 4: inst = I_CRASH; vel = c == 'X' ? 1.0f : 0.8f; break;
            default: inst = l == 5 ? I_TOM1 : I_TOM2; vel = c == 'X' ? 1.0f : c == 'g' ? 0.4f : 0.75f; break;
            }
            float off = (s & 1 ? sw * 0.5f * sf : 0.0f) + (bias + h * 4.0f * gauss(&d)) * 0.001f * SRF;
            if (s == 0 && off < 0.0f) off = 0.0f;
            int32_t at = (int32_t)((float)s * sf + off + 0.5f);
            if (at >= len - 64) at = len - 64;
            float v = vel * (1.0f + h * 0.12f * gauss(&d));
            if (l == 1 && c == 'f') add_ev(at - (int32_t)(0.022f * SRF), inst, s, EF_GRACE, 0.35f * v, &d);
            add_ev(at, inst, s, fl, v, &d);
        }
    }
    // more ghosts: GHOSTS over half puts soft notes on the empty offbeats
    if (gh > 0.5f && p->s) {
        for (int s = 1; s < p->steps; s += 2) {
            if (p->s[s] != '.' || (p->k && p->k[s] != '.')) continue;
            if (uni(&d) > (gh - 0.5f) * 0.7f) continue;
            int32_t at = (int32_t)((float)s * sf + sw * 0.5f * sf + (h * 4.0f * gauss(&d) - h) * 0.001f * SRF + 0.5f);
            add_ev(at, I_SNARE, s, EF_GHOST, 0.12f + 0.12f * uni(&d), &d);
        }
    }
    // in time order
    for (int i = 1; i < nev; i++) { ev_t e = ev[i]; int j = i - 1; while (j >= 0 && ev[j].at > e.at) { ev[j + 1] = ev[j]; j--; } ev[j + 1] = e; }
    // the run-up: the break's last second played before it
    int c = 0;
    for (int i = 0; i < nev; i++) if (ev[i].at >= len - pre) c++;
    if (nev + c > EV_MAX) c = EV_MAX - nev;
    if (c > 0) {
        memmove(&ev[c], &ev[0], (size_t)nev * sizeof(ev_t));
        int k = 0;
        for (int i = c; i < nev + c && k < c; i++) if (ev[i].at >= len - pre) { ev[k] = ev[i]; ev[k].at -= len; ev[k].flags |= EF_PRE; k++; }
        nev += c;
    }
    return nev;
}

// ---------------------------------------------------------------- the jobs: a render, or a take's finishing
enum { J_IDLE, J_WAIT, J_RUN, J_FINISH };
static struct {
    int st, buf, pat, take;    // take: a sampled break being finished
    uint32_t key;
    int32_t len, pos, pre, fpos, rec_n;
    int evi, steps; float bpm, peak, gain;
    // finishing: a walk over the break, measuring each slice
    int sj, fade_in; float prev; biquad_t lo, hi;
    float ea[KIT_SLICES_MAX], el[KIT_SLICES_MAX], eh[KIT_SLICES_MAX], eb[KIT_SLICES_MAX];
    int na[KIT_SLICES_MAX], nb[KIT_SLICES_MAX], zc[KIT_SLICES_MAX]; int32_t pk[KIT_SLICES_MAX];
} J;
static uint32_t last_want;
static int debounce, renders, takes;
static bool no_take_said;

static uint32_t want_key(void)
{
    uint32_t h = 2166136261u;
    for (int i = KP_BREAK; i <= KP_TAKE; i++) { int v = (int)(P(i) + (P(i) >= 0.0f ? 0.5f : -0.5f)); h = (h ^ (uint32_t)(v + 1000)) * 16777619u; }
    return h | 1u;
}

static void render_start(int b, uint32_t key)
{
    memset(&J, 0, sizeof J);
    J.st = J_WAIT; J.buf = 1 - cur; J.pat = b < 0 || b >= NPAT ? B_AMEN : b; J.key = key;   // SAMPLED with no take: the first
}

static void render_prepare(void)
{
    const pat_t *p = &pats[J.pat];
    brk_t *b = &brk[J.buf];
    key_of[J.buf] = 0;
    if (take_buf == J.buf) take_buf = -1;                         // the sampled break is gone
    V.tune = exp2f(P(KP_TUNE) / 12.0f); V.tone = P(KP_TONE) * 0.01f; V.human = P(KP_HUMAN) * 0.01f;
    V.dec = 0.45f * powf(4.89f, P(KP_DECAY) * 0.01f);
    V.kick_hz = p->kick_hz; V.kick_dec = p->kick_dec; V.snare_hz = p->snare_hz; V.snare_dec = p->snare_dec; V.wires = p->wires;
    V.tom_hz[0] = p->tom_hz[0]; V.tom_hz[1] = p->tom_hz[1]; V.tom_dec = p->tom_dec; V.hat = p->hat; V.ride_hz = p->ride_hz;
    V.crash_dec = p->crash_dec; V.bongos = p->bongos;
    J.len = pat_len(p); J.steps = p->steps; J.bpm = p->bpm;
    J.pre = J.len;
    uint32_t seed = 0x9E3779B9u * (uint32_t)(J.pat + 1) + 0x85EBCA6Bu * (uint32_t)P(KP_TAKE);
    build_events(p, J.len, J.pre, seed);
    build_slices(b, p->steps);
    rs = seed ^ 0x5bd1e995u; if (!rs) rs = 1;
    kit_reset(p);
    J.pos = -J.pre; J.evi = 0; J.peak = 0.0f;
    while (J.evi < nev && ev[J.evi].at < J.pos) J.evi++;
    J.st = J_RUN;
}

// a tick's worth: n frames (the light run-up counts a quarter)
static void render_some(int n)
{
    int16_t *o = pcm[J.buf];
    int budget = 4 * n;
#ifdef ESP_PLATFORM
    int64_t t0 = esp_timer_get_time();
    budget *= 8;
#endif
    while (budget > 0 && J.pos < J.len) {
#ifdef ESP_PLATFORM
        if (esp_timer_get_time() - t0 > TICK_US) break;
#endif
        while (J.evi < nev && ev[J.evi].at <= J.pos) {
            const ev_t *e = &ev[J.evi++];
            if (J.pos >= -PRE_FULL || is_cymbal(e->inst)) hit(e);
        }
        bool light = J.pos < -PRE_FULL;
        int32_t span = SPAN, lim = light ? -PRE_FULL : J.pos < 0 ? 0 : J.len;
        if (J.evi < nev && ev[J.evi].at - J.pos < span) span = ev[J.evi].at - J.pos;
        if (lim - J.pos < span) span = lim - J.pos;
        if (span < 1) span = 1;
        render_span((int)span, light);
        budget -= light ? (int)span : 4 * (int)span;
        if (J.pos >= 0)
            for (int i = 0; i < span; i++) {
                float q = s_mix[i] * WSCALE * 32767.0f;
                q = q > 32767.0f ? 32767.0f : q < -32767.0f ? -32767.0f : q;
                o[J.pos + i] = (int16_t)q;
                float a = fabsf(q);
                if (a > J.peak) J.peak = a;
            }
        J.pos += span;
    }
}

// what a slice mostly is, from its first 40 ms (how much is under 180 Hz, over 5 kHz and between, how noisy) and how it
// rings after (40 to 150 ms). Measured on the built-in breaks: kicks low; snares mostly between, noisy; hats and rides
// high with little between (a hat right after a kick is still high over the kick's tail), the ride ringing on; toms
// between and tonal, or low and ringing long
static int guess_kind(int j)
{
    if (J.na[j] < 64) return KS_OTHER;
    float e = J.ea[j] + 1e-12f, low = J.el[j] / e, high = J.eh[j] / e, mid = 1.0f - low - high;
    float zc = (float)J.zc[j] / (float)J.na[j];
    float dec = J.nb[j] > 256 ? (J.eb[j] / (float)J.nb[j]) / (e / (float)J.na[j]) : 0.0f;
    float pk = (float)J.pk[j] / (TARGET_PK * 32767.0f);
    if (mid < 0.0f) mid = 0.0f;
    if (high > 0.5f || (high > 0.3f && mid < 0.2f)) return dec > 0.45f ? (pk > 0.7f ? KS_CRASH : KS_RIDE) : KS_HAT;
    if (low > 0.45f) return dec > 0.75f && zc < 0.08f ? KS_TOM : KS_KICK;
    if (zc > 0.15f && high > 0.12f) return KS_SNARE;
    if (high < 0.12f && zc < 0.15f) return KS_TOM;
    return high > 0.3f ? KS_HAT : KS_OTHER;
}

static void finish_some(int n)
{
    int16_t *o = pcm[J.buf];
    brk_t *b = &brk[J.buf];
#ifdef ESP_PLATFORM
    int64_t t0 = esp_timer_get_time();
    n *= 4;
#endif
    for (int k = 0; k < n && J.fpos < J.len; k++, J.fpos++) {
#ifdef ESP_PLATFORM
        if ((k & 1023) == 1023 && esp_timer_get_time() - t0 > TICK_US) break;
#endif
        int32_t i = J.fpos;
        float x;
        if (J.take) {
            x = i < J.rec_n ? (float)o[i] * J.gain : 0.0f;
            if (i < J.fade_in) x *= (float)i / (float)J.fade_in;
            if (i >= J.len - FADE) x *= (float)(J.len - 1 - i) * (1.0f / FADE);
        } else x = (float)o[i] * J.gain;
        x = x > 32767.0f ? 32767.0f : x < -32767.0f ? -32767.0f : x;
        o[i] = (int16_t)x;
        // the slice it is in, and what it sounds like
        while (J.sj + 1 < b->nslices && i >= b->slice[J.sj + 1]) J.sj++;
        int j = J.sj;
        int32_t ax = (int32_t)fabsf(x);
        if (ax > J.pk[j]) J.pk[j] = ax;
        float xf = x * (1.0f / 32767.0f);
        float lo = biquad_run(&J.lo, xf), hi = biquad_run(&J.hi, xf);
        int32_t d = i - b->slice[j];
        if (d < 1280) {
            J.ea[j] += xf * xf; J.el[j] += lo * lo; J.eh[j] += hi * hi; J.na[j]++;
            if ((xf >= 0.0f) != (J.prev >= 0.0f)) J.zc[j]++;
        } else if (d < 4800) { J.eb[j] += xf * xf; J.nb[j]++; }
        J.prev = xf;
        int c = (int)((int64_t)i * ENV_N / J.len);
        int e = (int)(ax * 255 / (int32_t)(TARGET_PK * 32767.0f));
        if (e > env_c[J.buf][c]) env_c[J.buf][c] = (uint8_t)(e > 255 ? 255 : e);
    }
}

static void finish_begin(void)
{
    J.st = J_FINISH; J.fpos = 0; J.sj = 0;
    biquad_set(&J.lo, BQ_LP, 180.0f, 0.7f, 0); biquad_set(&J.hi, BQ_HP, 5000.0f, 0.7f, 0);
    memset(env_c[J.buf], 0, ENV_N);
    float pk = J.peak > 1.0f ? J.peak : 1.0f;
    J.gain = TARGET_PK * 32767.0f / pk;
    if (J.take) J.gain = clampf(J.gain, 0.25f, 16.0f);
}

static void finish_end(void)
{
    brk_t *b = &brk[J.buf];
    for (int j = 0; j < b->nslices; j++) {
        int pk = (int)((float)J.pk[j] * 255.0f / (TARGET_PK * 32767.0f) + 0.5f);
        b->peak[j] = (uint8_t)(pk > 255 ? 255 : pk);
        guess_k[J.buf][j] = (uint8_t)guess_kind(j);
        if (J.take) b->kind[j] = guess_k[J.buf][j];
    }
    b->pcm = pcm[J.buf]; b->len = J.len; b->steps = J.steps; b->bpm = J.bpm;
    b->name = J.take ? break_l[B_SAMPLED] : pats[J.pat].name;
    b->serial = ++serial_n;
    key_of[J.buf] = J.take ? 0 : J.key;
    publish(J.buf);
    J.st = J_IDLE;
    if (J.take) {
        take_buf = J.buf; takes++;
        char t[40]; snprintf(t, sizeof t, "SAMPLED: %d BEATS, %d SLICES", J.steps / 4, b->nslices);
        toast(t, 1500);
        if ((int)P(KP_BREAK) != B_SAMPLED) param_set(mod_base[M_KIT] + KP_BREAK, (float)B_SAMPLED);
    } else renders++;
}

// ================================================================ sampling (UI task)
enum { RS_IDLE, RS_PRESSED, RS_ARMED, RS_REC, RS_FINISH };
static int rs_st, r_src, r_buf, r_lost;
static uint32_t r_press, r_S, r_scan, r_copied, r_min;
static int32_t r_n;
static bool r_quiet;
static UI_RAM float blk_pk[MAXLEN / 1024 + 1];                    // the take's peak per 1024 frames
static UI_RAM int32_t on_at[ONS_MAX];
static UI_RAM float on_pk[ONS_MAX];
static int n_on, on_pend;

// an onset detector: a fast envelope against a slow one, on the whole signal and on its highs
typedef struct { float ef, es, l, hf, hs; int since, armed; } det_t;
static det_t D;
#define DA_F 0.2684f        // 0.1 ms
#define DR_F 0.0052f        // 6 ms
#define DA_S 0.0026f        // 12 ms
#define DR_S 0.00052f       // 60 ms
#define DL2K 0.3247f        // 2 kHz
static void det_reset(void) { memset(&D, 0, sizeof D); D.armed = 1; D.since = MIN_GAP; }
static bool det_run(float x, float gate)
{
    float ax = fabsf(x);
    D.ef += (ax > D.ef ? DA_F : DR_F) * (ax - D.ef);
    D.es += (ax > D.es ? DA_S : DR_S) * (ax - D.es);
    D.l += DL2K * (x - D.l);
    float h = fabsf(x - D.l);
    D.hf += (h > D.hf ? DA_F : DR_F) * (h - D.hf);
    D.hs += (h > D.hs ? DA_S : DR_S) * (h - D.hs);
    float fl = gate * 0.3f, r1 = D.ef / (D.es + fl), r2 = D.hf / (D.hs + fl * 0.5f), r = r1 > r2 ? r1 : r2;
    D.since++;
    if (!D.armed) { if (r < 1.5f) D.armed = 1; return false; }
    if (r > 2.5f && D.ef > gate * 0.7f && D.since > MIN_GAP) { D.armed = 0; D.since = 0; return true; }
    return false;
}

static float gate_lin(void) { return exp2f(P(KP_GATE) * (1.0f / 6.0206f)); }
static inline float ring_at(uint32_t f) { return (float)ring[f & RMASK] * (1.0f / 32767.0f); }
static uint32_t ring_now(void) { return __atomic_load_n(&ring_w, __ATOMIC_ACQUIRE); }

static void take_begin(uint32_t S)
{
    r_S = S; r_copied = S; r_n = 0; n_on = 0; on_pend = 0;
    memset(blk_pk, 0, sizeof blk_pk);
    D.armed = 0; D.since = 0;                                     // its first hit is under way
    rs_st = RS_REC;
}

void kit_rec_press(int source)
{
    if (!pcm[0] || rs_st == RS_FINISH) return;
    rs_st = RS_PRESSED; r_src = source; r_press = audio_frames();
}

void kit_rec_confirm(void)
{
    if (rs_st != RS_PRESSED) return;
    if (J.st != J_IDLE && !J.take) J.st = J_IDLE;                 // a take comes first: the render starts again later
    r_buf = 1 - cur; r_lost = 0;
    if (take_buf == r_buf) take_buf = -1;
    key_of[r_buf] = 0;
    if (r_src == KSRC_RESAMPLE) {                                 // on the machine's step
        grid_t g; wr_grid_get(&g);
        uint32_t from = r_press - RES_PRE;
        det_reset();
        take_begin(from + (uint32_t)wr_to_next_step(&g, from));
    } else {                                                      // from the first sound past REC's click
        rs_st = RS_ARMED;
        r_min = r_press + CLICK_POST; r_scan = r_min;
        // a sound already going (the computer playing a record): the take starts on its next hit; else on the first sound
        float g = gate_lin(), pk = 0.0f;
        det_reset();
        for (uint32_t f = r_min - 640; f != r_min; f++) { float x = ring_at(f); det_run(x, g); if (f - (r_min - 320) < 320u && fabsf(x) > pk) pk = fabsf(x); }
        r_quiet = pk < g;
    }
}

// the ring into the take, a tick at a time
static void rec_pump(void)
{
    uint32_t w = ring_now(), oldest = w - RING + RING_SAFE;
    float g = gate_lin();
    if (rs_st == RS_ARMED) {
        if ((int32_t)(w - r_scan) <= 0) return;                   // (the ring has not reached the scan yet)
        if ((int32_t)(r_scan - oldest) < 0) r_scan = oldest;
        for (uint32_t f = r_scan; f != w; f++) {
            float x = ring_at(f);
            bool fired = det_run(x, g), on = r_quiet ? fabsf(x) > g : fired;
            if (on) {
                uint32_t S = f - ONSET_PRE;
                if ((int32_t)(S - r_min) < 0) S = r_min;
                take_begin(S);
                break;
            }
        }
        if (rs_st == RS_ARMED) { r_scan = w; return; }
    }
    if (rs_st != RS_REC || !buf_free(r_buf)) return;
    if ((int32_t)(w - r_copied) <= 0) return;                     // the take starts on a step still to come (a resample at
                                                                  // DRONE: up to a beat): nothing to copy yet
    if (r_n >= MAXLEN) { r_copied = w; return; }                  // full: 8 s
    if ((int32_t)(r_copied - oldest) < 0) {                       // fell behind the ring (the buffer was busy too long)
        int32_t miss = (int32_t)(oldest - r_copied);
        for (int32_t i = 0; i < miss && r_n < MAXLEN; i++) pcm[r_buf][r_n++] = 0;
        r_lost += miss; r_copied = oldest;
    }
    int16_t *o = pcm[r_buf];
    for (; r_copied != w && r_n < MAXLEN; r_copied++) {
        int16_t v = ring[r_copied & RMASK];
        o[r_n] = v;
        float x = (float)v * (1.0f / 32767.0f), a = fabsf(x);
        if (a > blk_pk[r_n >> 10]) blk_pk[r_n >> 10] = a;
        if (on_pend > 0) { if (a > on_pk[n_on - 1]) on_pk[n_on - 1] = a; on_pend--; }
        if (det_run(x, g) && r_n > AUDIO_SR / 50 && n_on < ONS_MAX) { on_at[n_on] = r_n; on_pk[n_on] = a; n_on++; on_pend = 320; }
        r_n++;
    }
}

// a take's slices: at its onsets (snapped to the nearest sixteenth when close), or in equal parts
static void take_slices(brk_t *b, int32_t L, int steps)
{
    float sl = (float)L / (float)steps;
    int mode = (int)P(KP_SLICES), n = 1;
    b->slice[0] = 0; b->step[0] = 0; b->kind[0] = KS_OTHER; b->peak[0] = 0;
    if (mode > 0) {
        int N = 4 << mode;                                        // 8, 16, 32
        if (N > steps) N = steps;
        for (int j = 1; j < N; j++) { b->slice[n] = (int32_t)((int64_t)j * L / N); b->step[n] = (uint8_t)(j * steps / N); n++; }
    } else {
        // the strongest onsets that fit
        int idx[ONS_MAX], m = 0;
        for (int i = 0; i < n_on; i++) if (on_at[i] - SLICE_PRE > MIN_GAP / 2 && on_at[i] < L - 256) idx[m++] = i;
        while (m > KIT_SLICES_MAX - 1) {
            int wk = 0;
            for (int i = 1; i < m; i++) if (on_pk[idx[i]] < on_pk[idx[wk]]) wk = i;
            for (int i = wk; i < m - 1; i++) idx[i] = idx[i + 1];
            m--;
        }
        for (int q = 0; q < m && n < KIT_SLICES_MAX; q++) {
            int32_t p = on_at[idx[q]] - SLICE_PRE;
            int k = (int)((float)p / sl + 0.5f);
            float gp = (float)k * sl, dist = fabsf((float)p - gp), tol = fminf(0.3f * sl, 0.03f * SRF);
            int32_t pos; int st;
            if (dist <= tol && k < steps) { pos = (int32_t)(gp + 0.5f); if (pos > p) pos = p; st = k; }   // never cut its attack
            else { pos = p; st = (int)((float)p / sl); }
            if (st >= steps) st = steps - 1;
            if (pos <= b->slice[n - 1] + MIN_GAP / 2) continue;
            if (st < b->step[n - 1]) st = b->step[n - 1];
            b->slice[n] = pos; b->step[n] = (uint8_t)st; b->kind[n] = KS_OTHER; b->peak[n] = 0;
            n++;
        }
    }
    b->nslices = n;
}

static void rec_finish(uint32_t release)
{
    // the take ends before REC's click (a resample has none)
    uint32_t cut = r_src == KSRC_RESAMPLE ? release : release - CLICK_PRE;
    rec_pump();
    int32_t avail = (int32_t)(cut - r_S);
    if (avail > r_n) avail = r_n;
    if (avail < AUDIO_SR / 10) { rs_st = RS_IDLE; toast(r_n ? "TOO SHORT: NO BREAK" : "BUSY: HOLD REC AGAIN", 1200); return; }
    grid_t g; wr_grid_get(&g);
    float mbeat = 4.0f * (float)g.fpb / (float)wr_steps_per_beat(g.feel_q);   // frames per beat of the machine
    int lm = (int)P(KP_LENGTH), bars = lm ? 1 << (lm - 1) : 0, beats; int32_t L; float bpm;
    if (!bars || r_src == KSRC_RESAMPLE) {                       // whole beats (or bars) at the machine's tempo
        beats = bars ? bars * 4 : (int)((float)avail / mbeat + 0.3f);
        int most = (int)((float)MAXLEN / mbeat);
        if (most > TAKE_BEATS) most = TAKE_BEATS;
        if (beats > most) beats = most;
        if (beats < 1) { rs_st = RS_IDLE; toast("TOO SHORT: LESS THAN A BEAT", 1200); return; }
        L = (int32_t)((float)beats * mbeat + 0.5f); bpm = 60.0f * SRF / mbeat;
    } else {                                                      // that many bars: the tempo from its length
        beats = bars * 4; L = avail;
        float first = fmaxf(blk_pk[0], fmaxf(blk_pk[1], blk_pk[2])), best = 0.0f; int32_t at = 0;   // its first 100 ms
        for (int i = 0; i < n_on; i++)                            // its next downbeat, if REC was let go just after it:
            if (on_at[i] > avail - avail / 8 && on_at[i] < avail && on_pk[i] >= 0.9f * best) {   // the strongest (the latest
                if (on_pk[i] > best) best = on_pk[i];             // of a few as strong)
                at = on_at[i];
            }
        if (at && best >= 0.5f * first) L = at - ONSET_PRE;
        bpm = (float)beats * 60.0f * SRF / (float)L;
    }
    if (L > MAXLEN) L = MAXLEN;
    int32_t keep = avail < L ? avail : L;
    float peak = 0.0f;
    for (int32_t i = 0; i < (keep >> 10); i++) if (blk_pk[i] > peak) peak = blk_pk[i];
    for (int32_t i = keep & ~1023; i < keep; i++) { float a = fabsf((float)pcm[r_buf][i] * (1.0f / 32767.0f)); if (a > peak) peak = a; }
    brk_t *b = &brk[r_buf];
    take_slices(b, L, beats * 4);
    memset(&J, 0, sizeof J);
    J.take = 1; J.buf = r_buf; J.len = L; J.rec_n = keep; J.steps = beats * 4; J.bpm = bpm;
    J.fade_in = r_src == KSRC_RESAMPLE ? 16 : FADE;
    J.peak = peak * 32767.0f;
    finish_begin();
    rs_st = RS_FINISH;
}

void kit_rec_release(bool confirmed)
{
    if (!confirmed) { if (rs_st == RS_PRESSED) rs_st = RS_IDLE; return; }   // a tap: nothing kept
    if (rs_st == RS_ARMED || rs_st == RS_PRESSED) { rs_st = RS_IDLE; toast("NOTHING HEARD: NO BREAK", 1200); return; }
    if (rs_st != RS_REC) return;
    rec_finish(audio_frames());
}

bool  kit_recording(void) { return rs_st == RS_ARMED || rs_st == RS_REC; }
bool  kit_armed(void) { return rs_st == RS_ARMED; }
float kit_rec_seconds(void)                                       // (a resample's start may still be ahead: 0 until then)
{
    if (rs_st != RS_REC) return 0.0f;
    int32_t n = (int32_t)(audio_frames() - r_S);
    return n > 0 ? (float)n / SRF : 0.0f;
}

// ================================================================ the module
int kit_rendering(void)
{
    if (J.st != J_IDLE && !J.take) {
        if (J.st == J_WAIT) return 1;
        if (J.st == J_RUN) return 1 + (int)(90.0f * (float)(J.pos + J.pre) / (float)(J.len + J.pre));
        return 91 + (int)(8.0f * (float)J.fpos / (float)J.len);
    }
    if (pcm[0] && rs_st == RS_IDLE && ((int)P(KP_BREAK) != B_SAMPLED || take_buf < 0) && key_of[cur] != want_key() && key_of[1 - cur] != want_key()) return 1;
    return 0;
}

static void render_logic(void)
{
    int b = (int)P(KP_BREAK);
    if (b == B_SAMPLED && take_buf >= 0) {
        if (J.st != J_IDLE) J.st = J_IDLE;
        if (take_buf != cur) publish(take_buf);                   // back to the sampled break, still whole
        return;
    }
    if (b == B_SAMPLED && !no_take_said) { toast("NO SAMPLED BREAK: HOLD REC", 1500); no_take_said = true; }
    if (b != B_SAMPLED) no_take_said = false;
    uint32_t k = want_key();
    if (key_of[cur] == k) { J.st = J_IDLE; last_want = k; return; }   // it is playing
    if (key_of[1 - cur] == k) { J.st = J_IDLE; last_want = k; publish(1 - cur); return; }   // ready in the spare
    if (J.st != J_IDLE && J.key == k) {
        if (J.st == J_WAIT) { if (buf_free(J.buf)) render_prepare(); return; }   // (a tick of its own)
        render_some(RENDER_TICK);
        if (J.pos >= J.len) finish_begin();
        return;
    }
    if (k != last_want) { last_want = k; debounce = 6; J.st = J_IDLE; }   // settings still moving: wait a moment
    if (debounce > 0) { debounce--; return; }
    render_start(b, k);
}

// the first break, printed whole at boot before the sound starts (about a second on the board), so WRECK plays it
// from the first beat: AMEN unless the settings say another (SAMPLED, with no take after a restart, is AMEN too)
static void render_now(void)
{
    int b = (int)P(KP_BREAK);
    uint32_t k = want_key();
    render_start(b, k);
    if (!buf_free(J.buf)) { J.st = J_IDLE; return; }
    render_prepare();
    while (J.pos < J.len) render_some(RENDER_TICK);
    finish_begin();
    while (J.fpos < J.len) finish_some(FINISH_TICK);
    finish_end();
    last_want = k;
}

static void tick(const ctl_t *c)
{
    (void)c;
    if (!pcm[0]) return;
    if (rs_st == RS_ARMED || rs_st == RS_REC) rec_pump();
    if (J.st == J_FINISH) {
        finish_some(FINISH_TICK);
        if (J.fpos >= J.len) { bool t = J.take; finish_end(); if (t) rs_st = RS_IDLE; }
        return;
    }
    if (rs_st != RS_IDLE) return;                                 // sampling first
    render_logic();
}

static void init(void)
{
    if (pcm[0]) return;
    pcm[0] = app_alloc((size_t)MAXLEN * sizeof(int16_t));
    pcm[1] = app_alloc((size_t)MAXLEN * sizeof(int16_t));
    ring = app_alloc(RING * sizeof(int16_t));
    roomb = app_alloc(ROOM_N * sizeof(float));
    mem_psram = 2 * (size_t)MAXLEN * sizeof(int16_t) + RING * sizeof(int16_t) + ROOM_N * sizeof(float) + sizeof ev + sizeof on_at + sizeof on_pk + sizeof blk_pk;
    // until the first break is rendered (a few ticks): a silent bar
    brk[1] = (brk_t){ .pcm = no_pcm, .len = 64, .steps = 16, .bpm = 120.0f, .nslices = 1, .serial = 1, .name = "-" };
    cur = 1;
    if (!pcm[0] || !pcm[1] || !ring || !roomb) {
        app_log("kit: no memory for the breaks");
        if (pcm[0]) app_free(pcm[0]);
        if (pcm[1]) app_free(pcm[1]);
        if (ring) app_free(ring);
        if (roomb) app_free(roomb);
        pcm[0] = pcm[1] = ring = NULL; roomb = NULL;
        return;
    }
    brk[1].pcm = pcm[1]; brk[1].len = 2 * AUDIO_SR; brk[1].nslices = 16;
    for (int j = 0; j < 16; j++) { brk[1].slice[j] = j * (2 * AUDIO_SR / 16); brk[1].step[j] = (uint8_t)j; brk[1].kind[j] = KS_OTHER; }
    render_now();
}

static void changed(int p) { if (p == KP_BREAK || p < 0) no_take_said = false; }

void kit_envelope(uint8_t *cols, int n)
{
    const uint8_t *e = env_c[cur];
    for (int c = 0; c < n; c++) {
        int a = c * ENV_N / n, z = (c + 1) * ENV_N / n, m = 0;
        if (z <= a) z = a + 1;
        for (int i = a; i < z && i < ENV_N; i++) if (e[i] > m) m = e[i];
        cols[c] = (uint8_t)m;
    }
}

// ---------------------------------------------------------------- for tools/sim/test_kit.c
size_t kit_test_psram(void) { return mem_psram; }
void   kit_test_take(uint32_t *start, int32_t *rec_n, int *lost) { *start = r_S; *rec_n = r_n; *lost = r_lost; }
const uint8_t *kit_test_guess(void) { return guess_k[cur]; }
void   kit_test_pat(int k, const char **name, float *bpm, int *steps, float *swing, float *lay)
{
    const pat_t *p = &pats[k < 0 ? 0 : k >= NPAT ? NPAT - 1 : k];
    *name = p->name; *bpm = p->bpm; *steps = p->steps; *swing = p->swing; *lay = p->lay_ms;
}
int    kit_test_lines(int k)                                      // every line of pattern k is its length
{
    const pat_t *p = &pats[k];
    const char *l[7] = { p->k, p->s, p->h, p->r, p->c, p->t1, p->t2 };
    for (int i = 0; i < 7; i++) if (l[i] && (int)strlen(l[i]) != p->steps) return 0;
    return 1;
}
int    kit_test_counts(int *r, int *t) { *r = renders; *t = takes; return NPAT; }
// one drum of break k alone, dry (no room, a CLEAN print), into out: 0 kick, 1 snare, 2 hat, 3 ride, 4 crash, 5 tom
int    kit_test_hit(int k, int drum, float vel, float *out, int n)
{
    if (J.st != J_IDLE || rs_st != RS_IDLE || !roomb) return 0;
    static const uint8_t inst[6] = { I_KICK, I_SNARE, I_HAT, I_RIDE, I_CRASH, I_TOM1 };
    const pat_t *p = &pats[k < 0 ? 0 : k >= NPAT ? NPAT - 1 : k];
    float keep_print = P(KP_PRINT), keep_room = P(KP_ROOM);
    pv[mod_base[M_KIT] + KP_PRINT] = PR_CLEAN; pv[mod_base[M_KIT] + KP_ROOM] = 0;
    V.tune = 1.0f; V.tone = 0.5f; V.human = 0.0f; V.dec = 1.0f;
    V.kick_hz = p->kick_hz; V.kick_dec = p->kick_dec; V.snare_hz = p->snare_hz; V.snare_dec = p->snare_dec; V.wires = p->wires;
    V.tom_hz[0] = p->tom_hz[0]; V.tom_hz[1] = p->tom_hz[1]; V.tom_dec = p->tom_dec; V.hat = p->hat; V.ride_hz = p->ride_hz;
    V.crash_dec = p->crash_dec; V.bongos = p->bongos;
    kit_reset(p);
    pv[mod_base[M_KIT] + KP_PRINT] = keep_print; pv[mod_base[M_KIT] + KP_ROOM] = keep_room;
    ev_t e = { 0, inst[drum % 6], 0, 0, 0, vel, 0.5f, 0.5f };
    hit(&e);
    int w = 0;
    while (w < n) { int m = n - w < SPAN ? n - w : SPAN; render_span(m, false); for (int i = 0; i < m; i++) out[w + i] = s_mix[i]; w += m; }
    return n;
}

const module_t mod_kit = { .name = "KIT", .letter = 'k', .pages = pages, .npages = 2, .params = params, .nparams = KP_N,
                           .sounds = sounds, .init = init, .tick = tick, .changed = changed };
