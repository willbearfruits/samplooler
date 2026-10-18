// KIT's checks (inst_kit.c): every built-in break renders as it should (its length, its slices on their sixteenths,
// each kind of drum where it belongs in the spectrum), WAVs of each one to listen to (out/wreck-kit-<break>.wav),
// sampling from the mic, the USB sound card and WRECK itself (the take starts at the first sound or on the machine's
// step, REC's click never in it, whole beats, sliced at the hits within 5 ms; a resample at DRONE waits for a step
// still to come), a tap records nothing, the old break plays on until the swap, and what it all costs (a render tick,
// kit_feed, PSRAM).
#include "sim.h"
#include "wreck.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

// inst_kit.c's own, for these checks
size_t kit_test_psram(void);
void   kit_test_take(uint32_t *start, int32_t *rec_n, int *lost);
const uint8_t *kit_test_guess(void);
void   kit_test_pat(int k, const char **name, float *bpm, int *steps, float *swing, float *lay);
int    kit_test_lines(int k);
int    kit_test_counts(int *renders, int *takes);
int    kit_test_hit(int k, int drum, float vel, float *out, int n);
extern uint32_t (*kit_test_reading)(void);                        // stands in for beat_reading() when set
extern int (*wr_sim_usb_in)(int16_t *mono, int n);

#define SRF ((float)AUDIO_SR)
static int ptag(const char *t) { int p = param_by_tag(t); if (p < 0) note("no param '%s'", t); return p; }
static void set(const char *t, float v) { int p = ptag(t); if (p >= 0) param_set(p, v); }
static float get(const char *t) { int p = ptag(t); return p >= 0 ? pv[p] : 0.0f; }
static double now_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec * 1e6 + (double)t.tv_nsec * 1e-3; }

static void wav16(const char *fn, const int16_t *x, int n, int loops)
{
    FILE *f = fopen(fn, "wb"); if (!f) return;
    uint32_t sr = AUDIO_SR, br = sr * 2, ds = (uint32_t)n * 2u * (uint32_t)loops, rs = 36 + ds; uint16_t one = 1, bits = 16, ba = 2, fl16 = 16;
    uint32_t fl = fl16;
    fwrite("RIFF", 1, 4, f); fwrite(&rs, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fl, 4, 1, f);
    fwrite(&one, 2, 1, f); fwrite(&one, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&ds, 4, 1, f);
    for (int l = 0; l < loops; l++) fwrite(x, 2, (size_t)n, f);
    fclose(f);
}

// wait (playing) until the break named is in and nothing renders; false on a timeout
static bool wait_break(const char *name, float most_s)
{
    for (int i = 0; i < (int)(most_s * 100.0f); i++) {
        if (!kit_rendering() && !strcmp(kit_break()->name, name)) return true;
        sim_tick();
    }
    return false;
}

// where the energy of a stretch of the break is: below 200 Hz, 200 Hz to 5 kHz, above 5 kHz; and its zero crossings
typedef struct { float low, mid, high, zc; } bands_t;
static float split_lo = 200.0f, split_hi = 5000.0f;
static bands_t bandsf(const float *x, int n)
{
    biquad_t lo = { 0 }, hi1 = { 0 }, hi2 = { 0 };
    biquad_set(&lo, BQ_LP, split_lo, 0.7f, 0); biquad_set(&hi1, BQ_HP, split_hi, 0.7f, 0); biquad_set(&hi2, BQ_HP, split_hi, 0.7f, 0);
    float el = 0, eh = 0, et = 0, prev = 0; int zc = 0;
    for (int i = 0; i < n; i++) {
        float v = x[i], l = biquad_run(&lo, v), hp = biquad_run(&hi2, biquad_run(&hi1, v));
        el += l * l; eh += hp * hp; et += v * v;
        if ((v >= 0) != (prev >= 0)) zc++;
        prev = v;
    }
    bands_t b = { el / (et + 1e-12f), 0, eh / (et + 1e-12f), (float)zc / (float)n };
    b.mid = 1.0f - b.low - b.high; if (b.mid < 0) b.mid = 0;
    return b;
}
static bands_t bands(const int16_t *x, int n)
{
    biquad_t lo = { 0 }, hi1 = { 0 }, hi2 = { 0 };
    biquad_set(&lo, BQ_LP, 200.0f, 0.7f, 0); biquad_set(&hi1, BQ_HP, 5000.0f, 0.7f, 0); biquad_set(&hi2, BQ_HP, 5000.0f, 0.7f, 0);
    float el = 0, eh = 0, et = 0, prev = 0; int zc = 0;
    for (int i = 0; i < n; i++) {
        float v = (float)x[i] / 32768.0f, l = biquad_run(&lo, v), hp = biquad_run(&hi2, biquad_run(&hi1, v));
        el += l * l; eh += hp * hp; et += v * v;
        if ((v >= 0) != (prev >= 0)) zc++;
        prev = v;
    }
    bands_t b = { el / (et + 1e-12f), 0, eh / (et + 1e-12f), (float)zc / (float)n };
    b.mid = 1.0f - b.low - b.high; if (b.mid < 0) b.mid = 0;
    return b;
}

// ---------------------------------------------------------------- the built-in breaks
static void builtins(void)
{
    section("kit: the built-in breaks");
    const param_t *bp = param(ptag("kbrk"));
    int npat = kit_test_counts(&(int){ 0 }, &(int){ 0 });
    int lines_ok = 0;
    for (int k = 0; k < npat; k++) lines_ok += kit_test_lines(k);
    check(lines_ok == npat, "every break's lines are as long as the break (%d of %d)", lines_ok, npat);
    check(npat >= 6 && !strcmp(bp->labels[npat], "SAMPLED"), "%d built-in breaks, then SAMPLED", npat);
    int good_len = 0, good_slices = 0, good_peak = 0, good_spec = 0, rendered = 0, guess_ok = 0, guess_n = 0, worst_ticks = 0;
    for (int k = 0; k < npat; k++) {
        const char *name; float bpm, swing, lay; int steps;
        kit_test_pat(k, &name, &bpm, &steps, &swing, &lay);
        uint32_t ser = kit_break()->serial;
        set("kbrk", (float)k);
        int t0 = (int)(sim_t * 100.0f);
        if (!wait_break(name, 15.0f) || kit_break()->serial == ser) { note("%s: not rendered in 15 s", name); continue; }
        int ticks = (int)(sim_t * 100.0f) - t0;
        if (ticks > worst_ticks) worst_ticks = ticks;
        rendered++;
        const brk_t *b = kit_break();
        // its length and tempo
        int want_len = (int)((float)steps * 15.0f * SRF / bpm + 0.5f);
        if (b->len == want_len && b->steps == steps && b->bpm == bpm) good_len++;
        else note("%s: len %d (want %d), steps %d, bpm %.1f", name, b->len, want_len, b->steps, (double)b->bpm);
        // sane: never the int16 a NaN turns into, a peak at -6 dBFS, a break's loudness
        int bad = 0, pk = 0; double e = 0;
        for (int i = 0; i < b->len; i++) { int v = b->pcm[i]; if (v == -32768) bad++; if (abs(v) > pk) pk = abs(v); e += (double)v * v; }
        float fpk = (float)pk / 32768.0f, rms = (float)sqrt(e / b->len) / 32768.0f;
        if (!bad && fpk > 0.45f && fpk < 0.55f && rms > 0.04f && rms < 0.25f) good_peak++;
        else note("%s: peak %.3f rms %.3f, %d bad samples", name, (double)fpk, (double)rms, bad);
        // the slices: increasing, each on its sixteenth (late by its swing and a lay-back, loose by HUMAN 50 %)
        float sf = (float)b->len / (float)b->steps, worst = 0;
        bool order = b->nslices > 0 && b->slice[0] == 0;
        for (int j = 0; j < b->nslices; j++) {
            if (j && (b->slice[j] <= b->slice[j - 1] || b->step[j] <= b->step[j - 1])) order = false;
            float want = (float)b->step[j] * sf + ((b->step[j] & 1) ? swing * 0.5f * sf : 0.0f);
            float d = fabsf((float)b->slice[j] + 16.0f - want) / SRF * 1000.0f - lay;
            if (j && d > worst) worst = d;
        }
        if (order && worst < 15.0f) good_slices++;
        else note("%s: slices %s, %.1f ms off their sixteenths at worst", name, order ? "in order" : "OUT OF ORDER", (double)worst);
        // each kind where it belongs: kicks low, snares in the middle with noise, hats high
        float kl = 0, sm = 0, sz = 0, hh = 0; int nk = 0, ns = 0, nh = 0;
        for (int j = 0; j < b->nslices; j++) {
            int n = 960;                                           // the first 30 ms
            if (b->slice[j] + n > b->len) continue;
            bands_t q = bands(b->pcm + b->slice[j], n);
            if (b->kind[j] == KS_KICK) { kl += q.low; nk++; }
            if (b->kind[j] == KS_SNARE && b->peak[j] > 150) { sm += q.mid + q.high; sz += q.zc; ns++; }
            if (b->kind[j] == KS_HAT) { hh += q.high; nh++; }
        }
        if (nk) kl /= (float)nk;
        if (ns) { sm /= (float)ns; sz /= (float)ns; }
        if (nh) hh /= (float)nh;
        bool spec = (!nk || kl > 0.4f) && (!ns || (sm > 0.5f && sz > 0.08f)) && (!nh || hh > 0.4f);
        static float solo[AUDIO_SR / 4];
        int sn = AUDIO_SR / 4;
        float sk = 0, ss = 0, szc = 0, sh = 0, sr = 0, sc = 0;
        if (kit_test_hit(k, 0, 1.0f, solo, sn)) sk = bandsf(solo, 1600).low;
        split_lo = 120.0f; split_hi = 1000.0f;                       // a snare: little under 120 Hz, a noise over 1 kHz
        if (kit_test_hit(k, 1, 1.0f, solo, sn)) { bands_t q = bandsf(solo, 1600); ss = q.low < 0.25f && q.high > 0.25f ? 1.0f - q.low : 0.0f; szc = q.zc; }
        split_lo = 200.0f; split_hi = 5000.0f;
        if (kit_test_hit(k, 2, 0.7f, solo, sn)) sh = bandsf(solo, 1600).high;
        if (kit_test_hit(k, 3, 0.7f, solo, sn)) { bands_t q = bandsf(solo, 3200); sr = q.mid + q.high; }
        if (kit_test_hit(k, 4, 1.0f, solo, sn)) { bands_t q = bandsf(solo, 8000); sc = q.mid + q.high; }
        bool alone = sk > 0.7f && ss > 0.7f && szc > 0.08f && sh > 0.7f && sr > 0.9f && sc > 0.9f;
        if (!alone) note("%s alone: kick %.0f%% below 200 Hz, snare %.0f%% above 120 Hz (%.2f), hat %.0f%% above 5 kHz, ride %.0f%% and crash %.0f%% above 200 Hz",
                         name, (double)(sk * 100), (double)(ss * 100), (double)szc, (double)(sh * 100), (double)(sr * 100), (double)(sc * 100));
        if (spec && alone) good_spec++;
        note("%-8s %3d slices, %4d ticks; kicks %.0f%% below 200 Hz, snares %.0f%% above it (%.2f crossings a sample), hats %.0f%% above 5 kHz",
             name, b->nslices, ticks, (double)(kl * 100), (double)(sm * 100), (double)sz, (double)(hh * 100));
        // how well a recording's guess would do on it
        const uint8_t *g = kit_test_guess();
        for (int j = 0; j < b->nslices; j++) { guess_n++; if (g[j] == b->kind[j]) guess_ok++; }
        char fn[96]; snprintf(fn, sizeof fn, "out/%s-kit-%s.wav", sim_name, name);
        for (char *c = fn; *c; c++) if (*c == ' ') *c = '_';
        wav16(fn, b->pcm, b->len, 4);
    }
    check(rendered == npat, "every break renders (%d of %d), in %d ticks at most (%.1f s)", rendered, npat, worst_ticks, (double)worst_ticks / 100.0);
    check(good_len == npat, "each is its steps long at its tempo (%d of %d)", good_len, npat);
    check(good_peak == npat, "no NaN, printed to peak at -6 dBFS (%d of %d)", good_peak, npat);
    check(good_slices == npat, "the slices start at 0, in order, each on its sixteenth (%d of %d)", good_slices, npat);
    check(good_spec == npat, "kicks low, snares in the middle with noise, hats high: alone, and in the break (%d of %d)", good_spec, npat);
    metric("kit_guess_right", guess_n ? (double)guess_ok / guess_n : 0.0);
    note("a recording's guess of the kinds, tried on the built-in breaks: %d of %d right", guess_ok, guess_n);

    // HUMAN 0: every hit exactly where the pattern, its swing and its lay-back put it
    set("khum", 0); set("kbrk", 0);
    {
        const char *name; float bpm, swing, lay; int steps;
        kit_test_pat(0, &name, &bpm, &steps, &swing, &lay);
        wait_break(name, 15.0f);
        const brk_t *b = kit_break();
        float sf = (float)b->len / (float)b->steps, worst = 0;
        for (int j = 1; j < b->nslices; j++) {
            float want = (float)b->step[j] * sf + ((b->step[j] & 1) ? swing * 0.5f * sf : 0.0f) - 16.0f;
            float d = fabsf((float)b->slice[j] - want);
            if (d > worst) worst = d;
        }
        check(worst <= lay * 0.5f * SRF / 1000.0f + 2.0f, "HUMAN 0: %s's slices sit on their sixteenths (%.1f frames off at most: the backbeat's lay-back)", name, (double)worst);
    }
    set("khum", 50);
    wait_break(bp->labels[0], 15.0f);
}

// ---------------------------------------------------------------- a buffer BEAT still reads is never written
static uint32_t held_serial;
static uint32_t fake_reading(void) { return held_serial; }
static uint32_t sum_pcm(const brk_t *b) { uint32_t h = 2166136261u; for (int i = 0; i < b->len; i++) h = (h ^ (uint16_t)b->pcm[i]) * 16777619u; return h; }
static void held_buffer(void)
{
    section("kit: the buffer BEAT reads");
    set("kbrk", 1); wait_break("THINK", 15.0f);
    const brk_t *old = kit_break();
    held_serial = old->serial;                                    // BEAT still reads THINK, whatever it says
    kit_test_reading = fake_reading;
    uint32_t h0 = sum_pcm(old);
    set("kbrk", 0); wait_break("AMEN", 15.0f);                    // AMEN renders into the spare and comes in
    bool in = !strcmp(kit_break()->name, "AMEN");
    set("kbrk", 2); sim_run(3.0f);                                // the next one would need THINK's buffer: it waits
    bool waits = kit_rendering() > 0 && !strcmp(kit_break()->name, "AMEN") && sum_pcm(old) == h0;
    set("xsrc", 2);                                               // a take (a resample: it starts at once) waits too
    SI.rec = true; sim_run(1.0f); SI.rec = false; sim_tick(); sim_run(0.3f);
    bool take_waits = !strcmp(kit_break()->name, "AMEN") && sum_pcm(old) == h0;
    set("xsrc", 0);
    kit_test_reading = NULL;                                      // BEAT lets go of it
    bool goes = wait_break("APACHE", 15.0f);
    check(in && waits && take_waits && goes, "a break BEAT still reads is never written: a render and a take wait for it, then go on");
    set("kbrk", 0); wait_break("AMEN", 15.0f);
}

// ---------------------------------------------------------------- what it costs
void kit_feed(const float *in, int n, uint32_t f0);
static void cost(void)
{
    section("kit: cost");
    // a render, a tick at a time (the board stops at 2.5 ms a tick by itself; here it is a fixed 384 frames): the best
    // of three renders (the host is shared)
    ctl_t c; memset(&c, 0, sizeof c);
    double mx = 1e9, sum = 1e9; int n = 0;
    for (int r = 0; r < 3; r++) {
        set("kbrk", 1); set("khum", (float)(51 + r));             // a new render each time (the same work)
        const char *want = "THINK";
        double m1 = 0, s1 = 0; int n1 = 0;
        for (int i = 0; i < 4000; i++) {
            double a = now_us(); mod_kit.tick(&c); double d = now_us() - a;
            if (i > 8) { if (d > m1) m1 = d; s1 += d; n1++; }      // (the first ticks wait out the debounce)
            if (i > 10 && !kit_rendering() && !strcmp(kit_break()->name, want)) break;
        }
        if (s1 / n1 < (n ? sum / n : 1e9)) { sum = s1; n = n1; mx = m1; }
    }
    set("khum", 50);
    metric("kit_render_tick_us_max", mx);
    metric("kit_render_ms_total", sum / 1000.0);
    double mean = sum / (n > 0 ? n : 1);
    metric("kit_render_tick_us_mean", mean);
    check(mean < 40.0 && mx < 90.0, "a render tick takes %.0f us on the host (%.0f at most): about %.1f ms on the board, which stops itself at 2.5 ms; a whole break %.1f ms (%d ticks)",
          mean, mx, mean * 70.0 / 1000.0, sum / 1000.0, n);
    // kit_feed: all the audio task does for KIT
    float in[AUDIO_BLOCK];
    for (int i = 0; i < AUDIO_BLOCK; i++) in[i] = 0.3f * sim_noise();
    double best = 1e9;
    for (int r = 0; r < 5; r++) {
        double a = now_us();
        for (int k = 0; k < 20000; k++) kit_feed(in, AUDIO_BLOCK, (uint32_t)(k * AUDIO_BLOCK));
        double d = (now_us() - a) / 20000.0;
        if (d < best) best = d;
    }
    metric("kit_feed_us_per_block", best);
    check(best < 0.5, "kit_feed takes %.3f us per 64-frame block (budget 0.5)", best);
    size_t m = kit_test_psram();
    metric("kit_psram_kb", (double)m / 1024.0);
    check(m <= 1100 * 1024, "PSRAM: %.0f KB (two 8 s breaks, the ring, the room; budget 1100)", (double)m / 1024.0);
    set("kbrk", 0); wait_break("AMEN", 15.0f);
}

// ---------------------------------------------------------------- sampling
// the mic: a few clicks (short noise bursts) at known frames, and REC's own click, a spike, at the press and the release
#define NCLK 24
static uint32_t clk_at[NCLK]; static int nclk;
static uint32_t spike_at[2];
static float clk_amp = 0.4f;
static float burst(uint32_t f, uint32_t at, float amp)
{
    int32_t d = (int32_t)(f - at);
    if (d < 0 || d >= 320) return 0.0f;
    uint32_t h = f * 2654435761u; h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return amp * expf(-(float)d / 64.0f) * ((float)(h & 0xffff) / 32768.0f - 1.0f);
}
static float mic_clicks(float t, void *u)
{
    (void)u;
    uint32_t f = (uint32_t)(t * SRF + 0.5f);
    float y = 0.0f;
    for (int i = 0; i < nclk; i++) y += burst(f, clk_at[i], clk_amp);
    for (int i = 0; i < 2; i++) { int32_t d = (int32_t)(f - spike_at[i]); if (spike_at[i] && d >= 0 && d < 24) y += 0.95f * (d & 1 ? -1.0f : 1.0f) * (1.0f - (float)d / 24.0f); }
    return y;
}
static float mic_accents(float t, void *u)
{
    (void)u;
    uint32_t f = (uint32_t)(t * SRF + 0.5f);
    float y = 0.0f;
    for (int i = 0; i < nclk; i++) y += burst(f, clk_at[i], (i % 8) == 0 ? 0.6f : (i % 2) ? 0.25f : 0.4f);
    for (int i = 0; i < 2; i++) { int32_t d = (int32_t)(f - spike_at[i]); if (spike_at[i] && d >= 0 && d < 24) y += 0.95f * (d & 1 ? -1.0f : 1.0f) * (1.0f - (float)d / 24.0f); }
    return y;
}
// USB: the computer plays a break of clicks at 120 BPM, all the time
static uint32_t usb_frame;
static const float usb_acc[16] = { 0.8f, 0.15f, 0.35f, 0.2f, 0.7f, 0.15f, 0.35f, 0.25f, 0.8f, 0.2f, 0.45f, 0.2f, 0.7f, 0.15f, 0.35f, 0.3f };
static int usb_clicks(int16_t *mono, int n)
{
    uint32_t f0 = audio_frames();
    for (int i = 0; i < n; i++) {
        uint32_t f = f0 + (uint32_t)i, step = f / 4000u;                // a sixteenth at 120 BPM: 4000 frames
        mono[i] = (int16_t)(32767.0f * burst(f, step * 4000u, 0.5f * usb_acc[step & 15]));
    }
    usb_frame = f0;
    return n;
}

typedef struct { uint32_t press, release; } hold_t;
// hold REC for s seconds: returns the press's and the release's frames as KIT sees them
static hold_t hold_rec(float s, bool spikes)
{
    hold_t h;
    h.press = audio_frames() + TICK_FRAMES;                      // the tick that sees REC down ends there
    if (spikes) spike_at[0] = h.press - AUDIO_SR * 15 / 1000;    // its click comes 15 ms before the press is seen
    SI.rec = true;
    sim_run(s);
    h.release = audio_frames() + TICK_FRAMES;
    if (spikes) spike_at[1] = h.release - AUDIO_SR * 15 / 1000;
    SI.rec = false;
    sim_tick();
    return h;
}

// every click inside the take has a slice starting at most 5 ms before it (and none after it)
static int slices_on_clicks(const brk_t *b, uint32_t S, float *worst_ms)
{
    int ok = 0; *worst_ms = 0;
    for (int i = 0; i < nclk; i++) {
        int32_t at = (int32_t)(clk_at[i] - S);
        if (at < 0 || at >= b->len - 400) continue;
        int32_t best = 1 << 30;
        for (int j = 0; j < b->nslices; j++) { int32_t d = at - b->slice[j]; if (d >= 0 && d < best) best = d; }
        float ms = (float)best / SRF * 1000.0f;
        if (ms > *worst_ms) *worst_ms = ms;
        if (ms <= 5.0f) ok++;
    }
    return ok;
}
static int clicks_in_take(uint32_t S, int len) { int n = 0; for (int i = 0; i < nclk; i++) { int32_t at = (int32_t)(clk_at[i] - S); if (at >= 0 && at < len - 400) n++; } return n; }

static void sampling(void)
{
    section("kit: sampling");
    grid_t g; wr_grid_get(&g);
    int mbeat = (int)(4 * g.fpb / (uint32_t)wr_steps_per_beat(g.feel_q)), mstep = mbeat / 4;
    float mbpm = 60.0f * SRF / (float)mbeat;
    note("the machine at %.0f BPM: a beat is %d frames", (double)mbpm, mbeat);
    set("xsrc", 0); set("klen", 0); set("ksli", 0); set("kgat", -36);
    sim_run(0.3f);

    // ---- the mic: clicks after the press, REC's click at both ends
    uint32_t P = audio_frames() + TICK_FRAMES, c0 = P + AUDIO_SR / 5;   // the first sound 0.2 s after the press
    static const int on_grid[] = { 0, 2, 4, 6, 7, 10, 12, 15, 16, 20, 23, 24, 28 };
    nclk = 0;
    for (int i = 0; i < (int)(sizeof on_grid / sizeof on_grid[0]); i++) clk_at[nclk++] = c0 + (uint32_t)(on_grid[i] * mstep);
    clk_at[nclk++] = c0 + (uint32_t)(8 * mstep + mstep * 4 / 10);       // two off the grid: they keep their place
    clk_at[nclk++] = c0 + (uint32_t)(18 * mstep + mstep / 2);
    clk_at[nclk++] = c0 + (uint32_t)(32 * mstep);                        // the next bar's first: after the take
    SI.mic = mic_clicks; SI.mic_u = NULL;
    uint32_t ser0 = kit_break()->serial;
    float t_rec = sim_t;
    sim_gif_begin("kit-sample");
    // hold until just after the 32nd step (8 beats); watch the old break play on meanwhile
    SI.rec = true; spike_at[0] = P - AUDIO_SR * 15 / 1000;
    bool old_on = true; float rms_hold;
    int hold_ticks = (int)((float)(c0 + 32u * (uint32_t)mstep + 1600u - P) / (float)TICK_FRAMES);
    for (int i = 0; i < hold_ticks; i++) { sim_tick(); if (kit_break()->serial != ser0) old_on = false; }
    rms_hold = m_rms(t_rec + 0.4f, sim_t);
    bool rec_on = kit_recording();
    uint32_t R = audio_frames() + TICK_FRAMES; spike_at[1] = R - AUDIO_SR * 15 / 1000;
    SI.rec = false; sim_tick();
    sim_run(0.6f);
    sim_gif_end();
    const brk_t *b = kit_break();
    uint32_t S; int32_t rn; int lost; kit_test_take(&S, &rn, &lost);
    check(rec_on && old_on && rms_hold > 0.01f, "held, it records while the old break plays on (rms %.3f) until the swap", (double)rms_hold);
    check(b->serial != ser0 && !strcmp(b->name, "SAMPLED") && (int)get("kbrk") == kit_test_counts(&(int){ 0 }, &(int){ 0 }),
          "let go: the take is the break (%s), BREAK says SAMPLED", b->name);
    int32_t lead = (int32_t)(c0 - S);
    check(lead >= 0 && lead <= AUDIO_SR * 5 / 1000, "the take starts at the first sound (%.1f ms before it), not at REC's click", (double)lead * 1000.0 / SRF);
    check(b->len % mbeat == 0 && b->len / mbeat == 8 && fabsf(b->bpm - mbpm) < 0.01f && b->steps == 32,
          "whole beats at the machine's tempo: %d beats, %.0f BPM, %d steps", b->len / mbeat, (double)b->bpm, b->steps);
    check((int32_t)(spike_at[0] - S) < 0 && (int32_t)(S + (uint32_t)b->len - spike_at[1]) < -AUDIO_SR / 50,
          "REC's click is in it at neither end (it starts %.0f ms after the press's, ends %.0f ms before the release's)",
          (double)(S - spike_at[0]) * 1000.0 / SRF, (double)(spike_at[1] - S - (uint32_t)b->len) * 1000.0 / SRF);
    float worst; int on = slices_on_clicks(b, S, &worst), want = clicks_in_take(S, b->len);
    check(on == want && want >= 14, "every click in it has a slice within 5 ms before it (%d of %d, %.1f ms at worst)", on, want, (double)worst);
    int loud = 0;                                                 // (with REC's spike in it, they would be under 120)
    for (int j = 0; j < b->nslices; j++) if (b->peak[j] > 150) loud++;
    check(loud >= want - 2, "the clicks are its loudest hits (%d slices over 150 of 255): no spike in it", loud);
    {   // snapped to the grid where close, the two off it kept where they are
        int snapped = 0, kept = 0;
        for (int j = 0; j < b->nslices; j++) {
            if (b->slice[j] % mstep == 0 || b->slice[j] % mstep > mstep - 200) snapped++;
            else kept++;
        }
        note("slices: %d, %d on a sixteenth, %d between (lost %d frames)", b->nslices, snapped, kept, lost);
    }
    char fn[96]; snprintf(fn, sizeof fn, "out/%s-kit-take-mic.wav", sim_name); wav16(fn, b->pcm, b->len, 2);
    sim_run(2.0f); sim_wav("-kit-take-mic-played", sim_t - 2.6f, sim_t);
    SI.mic = NULL; spike_at[0] = spike_at[1] = 0;

    // ---- a tap: nothing recorded (and the machine stops; another tap starts it)
    uint32_t ser1 = kit_break()->serial;
    sim_rec(0.1f); sim_run(0.5f);
    bool same = kit_break()->serial == ser1 && !kit_recording();
    sim_rec(0.1f); sim_run(0.5f);
    check(same && perf.running, "a tap records nothing (the break is the same one)");

    // ---- USB: the computer plays a break all the time; the take starts on a hit
    set("xsrc", 1);
    wr_sim_usb_in = usb_clicks;
    sim_run(0.5f);
    uint32_t ser2 = kit_break()->serial;
    hold_t h = hold_rec(2.3f, false);
    sim_run(0.6f);
    b = kit_break(); kit_test_take(&S, &rn, &lost);
    int32_t off = (int32_t)(S % 4000u), d0 = off > 2000 ? 4000 - off : -off;   // from the take's start to the nearest hit
    check(b->serial != ser2 && (int32_t)(S - h.press) >= AUDIO_SR * 12 / 100 && d0 >= 0 && d0 <= AUDIO_SR * 5 / 1000,
          "USB: the take starts on a hit (%.1f ms before it), past REC's click", (double)d0 * 1000.0 / SRF);
    nclk = 0;
    for (uint32_t f = (S / 4000u + 1u) * 4000u; f < S + (uint32_t)b->len && nclk < NCLK; f += 4000u) clk_at[nclk++] = f;
    on = slices_on_clicks(b, S, &worst); want = clicks_in_take(S, b->len);
    for (int i = 0; i < nclk; i++) {
        int32_t at = (int32_t)(clk_at[i] - S), best = 1 << 30;
        for (int j = 0; j < b->nslices; j++) { int32_t d = at - b->slice[j]; if (d >= 0 && d < best) best = d; }
        if (at < b->len - 400 && best > 160) note("USB: the hit at %d (accent %.2f) has no slice (nearest %d frames before)", at, (double)usb_acc[(clk_at[i] / 4000u) & 15], best);
    }
    check(b->len % mbeat == 0 && on == want, "USB: whole beats (%d), a slice at every hit (%d of %d, %.1f ms at worst)", b->len / mbeat, on, want, (double)worst);
    snprintf(fn, sizeof fn, "out/%s-kit-take-usb.wav", sim_name); wav16(fn, b->pcm, b->len, 2);
    wr_sim_usb_in = NULL; set("xsrc", 0); nclk = 0;
    sim_run(0.5f);

    // ---- RESAMPLE: WRECK itself, from its step
    set("xsrc", 2);
    set("kbrk", 0); wait_break("AMEN", 15.0f);
    sim_run(1.0f);
    uint32_t ser3 = kit_break()->serial;
    h = hold_rec(1.6f, false);
    sim_run(0.6f);
    b = kit_break(); kit_test_take(&S, &rn, &lost);
    wr_grid_get(&g);
    int32_t pos = wr_pos(&g, S), sst = wr_step_start(&g, wr_step_of(&g, pos));
    double e = 0; for (int i = 0; i < b->len; i++) e += (double)b->pcm[i] * b->pcm[i];
    float trms = (float)sqrt(e / (b->len > 0 ? b->len : 1)) / 32768.0f;
    check(b->serial != ser3 && pos == sst && (int32_t)(h.press - S) < AUDIO_SR / 10,
          "RESAMPLE: the take starts on the machine's step (%d frames off), at the press", pos - sst);
    check(b->len % mbeat == 0 && b->len > 0 && trms > 0.02f && b->nslices >= 4, "RESAMPLE: whole beats (%d), the machine in it (rms %.3f), %d slices",
          b->len / mbeat, (double)trms, b->nslices);
    snprintf(fn, sizeof fn, "out/%s-kit-take-resample.wav", sim_name); wav16(fn, b->pcm, b->len, 2);

    // ---- RESAMPLE at DRONE: the step it starts on may be up to a beat after the press (the ring has not got there
    // yet): the take waits for it and holds what WRECK played from that step, not what the ring held before
    {
        set("xaxs", 90); sim_run(2.0f);
        wr_grid_get(&g);
        int dbeat = (int)(4 * g.fpb / (uint32_t)wr_steps_per_beat(g.feel_q));
        uint32_t ser4 = kit_break()->serial;
        {   // press well into a step (past the 35 ms a resample looks back), with the next step over 0.4 s away: the
            // take's start is still ahead when REC's 0.3 s hold confirms it
            int32_t stp = (int32_t)wr_step_frames(&g);
            for (;;) { int32_t to = wr_to_next_step(&g, audio_frames() + TICK_FRAMES); if (to > stp * 2 / 3 && to < stp - AUDIO_SR / 20) break; sim_tick(); }
        }
        h = hold_rec(4.5f, false);
        sim_run(0.6f);
        b = kit_break(); kit_test_take(&S, &rn, &lost);
        pos = wr_pos(&g, S); sst = wr_step_start(&g, wr_step_of(&g, pos));
        // the take against what was played from its start (sim_out is after the master: volume, DC, limiter)
        double xy = 0, xx = 0, yy = 0; int n = AUDIO_SR / 2;
        for (int i = 0; i < n && (int)(S + (uint32_t)i) < sim_n; i++) {
            double x = b->pcm[i] / 32768.0, y = sim_out[S + (uint32_t)i];
            xy += x * y; xx += x * x; yy += y * y;
        }
        double corr = xy / sqrt(xx * yy + 1e-18);
        check(b->serial != ser4 && pos == sst && (int32_t)(S - h.press) >= -AUDIO_SR / 10 && (int32_t)(S - h.press) <= dbeat / 4 + 2 * TICK_FRAMES && b->len % dbeat == 0 && corr > 0.9,
              "RESAMPLE at DRONE: the take starts on the step %.0f ms after the press (a step is %.0f ms), whole beats (%d), and it is what was played from there (correlation %.3f)",
              (double)(int32_t)(S - h.press) * 1000.0 / SRF, (double)(dbeat / 4) * 1000.0 / SRF, b->len / dbeat, corr);
        set("xaxs", 10); sim_run(1.5f);
        wr_grid_get(&g);
    }

    // ---- SLICES 16: equal parts
    set("ksli", 2);
    h = hold_rec(1.6f, false); sim_run(0.6f);
    b = kit_break();
    bool eq = b->nslices == 16;
    for (int j = 0; j < b->nslices && eq; j++) if (b->slice[j] != (int32_t)((int64_t)j * b->len / 16) || b->step[j] != j * b->steps / 16) eq = false;
    check(eq, "SLICES 16: sixteen equal slices (%d)", b->nslices);
    set("ksli", 0);

    // ---- LENGTH 2 BARS from the mic: a break at 120 BPM, REC let go just after its next downbeat
    set("xsrc", 0); set("klen", 2);
    sim_run(0.3f);
    P = audio_frames() + TICK_FRAMES; c0 = P + AUDIO_SR / 4;
    nclk = 0;
    for (int s = 0; s <= 32 && nclk < NCLK; s += 2) clk_at[nclk++] = c0 + (uint32_t)(s * 4000);
    clk_amp = 0.0f;                                               // (the accents: the downbeats loudest)
    SI.mic = mic_accents;
    SI.rec = true; spike_at[0] = P - AUDIO_SR * 15 / 1000;
    int ticks2 = (int)((float)(c0 + 32u * 4000u + 2400u - P) / (float)TICK_FRAMES);
    sim_run((float)ticks2 / 100.0f);
    spike_at[1] = audio_frames() + TICK_FRAMES - AUDIO_SR * 15 / 1000;
    SI.rec = false; sim_tick(); sim_run(0.6f);
    b = kit_break();
    check(b->steps == 32 && fabsf(b->bpm - 120.0f) < 0.5f, "LENGTH 2 BARS: the take's own tempo, from its length to its next downbeat: %.2f BPM (120)", (double)b->bpm);
    SI.mic = NULL; spike_at[0] = spike_at[1] = 0; nclk = 0;

    // put it back
    set("klen", 0); set("xsrc", 0);
    set("kbrk", 0); wait_break("AMEN", 15.0f);
    int r, t; kit_test_counts(&r, &t);
    note("%d renders and %d takes so far", r, t);
}

void test_mod_kit(void)
{
    builtins();
    held_buffer();
    cost();
    sampling();
}
