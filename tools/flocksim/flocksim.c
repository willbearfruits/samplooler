// The Samplooler in a pretend room (see run.sh). The board under test is the firmware's flock.c + recorder_f32.c on
// the link's built-in state, its clock its audio frames (frame 0 at 1 s of true time, no drift). Pretend boards
// (WRECK, MANTRA) are bare link states as tools/linktest makes them, each with a crystal of its own; packets wait
// a little before the air and on arrival. The harness plays main.c's part: every 10 ms five 64-frame blocks of
// audio, flock_tick(), the loop cue (a loop that began playing is reported to the flock), the key and scale news.
#include "flock.h"
#include "link.h"
#include "recorder.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void recorder_debug_grid(uint32_t *origin, uint32_t *fpb, int *mode, uint32_t *anchor);

#define SR  REC_SAMPLE_RATE
#define NP  3
typedef struct { link_state_t *st; uint8_t mac[6]; double off_us, ppm, epoch; bool on; } peer_t;
static peer_t P[NP];
static link_state_t *board_st;                 // the Samplooler's link state (a fresh one per scenario)
static int npeer, cur = -1;                    // -1: the Samplooler
static bool board_on;
static const uint8_t board_mac[6] = { 0x90, 0xDA, 0x72, 0x76, 0x00, 0xD0 };
static double loss = 0.0;
static uint32_t seed = 0x5eed;
static double rnd(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return (double)(seed % 1000000u) / 1e6; }

static double now_true(void) { return (double)recorder_frames() * 31.25 + 1e6; }
static double clk(int k, double t) { return k < 0 ? t : P[k].off_us + t * (1.0 + P[k].ppm * 1e-6); }

typedef struct { double t; int to; uint8_t from[6]; int n; uint8_t b[256]; } air_t;
static air_t air[512];
static int nair;

void app_log(const char *fmt, ...) { va_list a; va_start(a, fmt); fprintf(stderr, "      "); vfprintf(stderr, fmt, a); fputc('\n', stderr); va_end(a); }
bool plat_link_start(int power) { (void)power; if (cur < 0) board_on = true; else P[cur].on = true; return true; }
void plat_link_stop(void) { if (cur < 0) board_on = false; else P[cur].on = false; }
bool plat_link_send(const void *b, int n)
{
    double ta = now_true() + rnd() * 2500.0;
    for (int k = -1; k < npeer; k++) {
        if (k == cur) continue;
        if (!(k < 0 ? board_on : P[k].on)) continue;
        if (rnd() < loss || nair >= 512) continue;
        air_t *a = &air[nair++];
        a->t = ta + 150.0 + rnd() * 400.0; a->to = k; memcpy(a->from, cur < 0 ? board_mac : P[cur].mac, 6);
        a->n = n; memcpy(a->b, b, (size_t)n);
    }
    return true;
}
int64_t plat_link_us(void) { return (int64_t)clk(cur, now_true()); }
int64_t plat_link_epoch(void) { return cur < 0 ? 1000000 : (int64_t)P[cur].epoch; }
void plat_link_mac(uint8_t mac[6]) { memcpy(mac, cur < 0 ? board_mac : P[cur].mac, 6); }

static void use(int k) { cur = k; link_use(k < 0 ? board_st : P[k].st); }
static int peer_add(const char *name, int prio, double ppm, double off_us)
{
    if (npeer < 0 || npeer >= NP) return -1;
    int k = npeer++;
    peer_t *p = &P[k];
    p->st = link_new_state();
    for (int i = 0; i < 6; i++) p->mac[i] = (uint8_t)(0x40 + 9 * k + i);
    p->off_us = off_us; p->ppm = ppm;
    p->epoch = clk(k, now_true()) + 0.5e6;
    int was = cur;
    use(k);
    link_init(name, prio);
    link_boot_time(19200, 0, 4);
    link_boot_harm(0, 0, 1, 0, 0);
    link_enable(true, 1, 1);
    use(was);
    return k;
}
static double peer_frame(int k) { return (clk(k, now_true()) - P[k].epoch) / 31.25; }
// where board k is in the session's beats now (k = -1: the Samplooler)
static double beats_at(int k)
{
    int was = cur; use(k);
    uint32_t o; float f; link_origin(&o, &f);
    double fr = k < 0 ? (double)recorder_frames() : peer_frame(k);
    double rel = fmod(fr - (double)o - (double)f, 4294967296.0);
    if (rel < 0) rel += 4294967296.0;
    if (rel > 2147483648.0) rel -= 4294967296.0;
    double b = rel / (double)link_time()->fpb;
    use(was);
    return b;
}

// ---------------------------------------------------------------- main.c's part
static bool  rec_down, snap_setting;
static int   key = 0, scale = 0, last_state = -1;
static uint32_t loop_seen = 0xFFFFFFFFu;
static float in_ph;
static float blk_in[64], blk_out[64];
static float gap_worst;                        // the loop's beat against the grid, worst lately (ms)
static int   gap_n;

static float tick_peak;                        // the loudest sample of the last tick's output

static void tick(void)
{
    // the air: what has landed, then every board's tick
    double t = now_true();
    for (int i = 0; i < nair;) {
        if (air[i].t <= t + 10000.0) {
            int k = air[i].to;
            use(k);
            link_rx(air[i].from, air[i].b, air[i].n, (int64_t)clk(k, air[i].t), -45);
            air[i] = air[--nair];
        } else i++;
    }
    for (int k = 0; k < npeer; k++) { use(k); link_tick(0.01f); }
    use(-1);
    flock_tick(0.01f, snap_setting);
    int k2, s2;
    if (flock_harm_news(&k2, &s2)) { key = k2; scale = s2; }
    recorder_set_record(rec_down);
    for (int b = 0; b < 5; b++) {
        for (int i = 0; i < 64; i++) { in_ph += 220.0f / SR; if (in_ph >= 1) in_ph -= 1; blk_in[i] = 0.3f * (1.0f - 2.0f * in_ph); }
        recorder_process(blk_in, blk_out, 64);
        if (b == 0) tick_peak = 0;
        for (int i = 0; i < 64; i++) if (fabsf(blk_out[i]) > tick_peak) tick_peak = fabsf(blk_out[i]);
    }
    int s = recorder_state();
    if (s != last_state) { if (s != REC_PLAYING) flock_loop_gone(); last_state = s; }
    if (s == REC_PLAYING && recorder_loop_start() != loop_seen) { loop_seen = recorder_loop_start(); flock_loop_made(recorder_loop_frames(), loop_seen); }
}
static void run_ms(int ms) { for (int i = 0; i < ms / 10; i++) tick(); }
static void take(double seconds) { rec_down = true; run_ms((int)(seconds * 1000.0)); rec_down = false; run_ms(10); }
static void settle(void) { for (int i = 0; i < 300 && recorder_state() != REC_PLAYING; i++) tick(); }

// the Samplooler's loop against the grid at ratio r: where its head is (beats into the loop) against where the grid
// says it should be, ms, modulo a beat
static double head_gap_ms(void)
{
    float fpb = (float)link_fpb();
    double pos_b = (double)recorder_position() * (double)recorder_loop_frames() / (double)fpb;   // beats into the loop
    double grid_b = beats_at(-1);
    double d = pos_b - grid_b;
    d -= floor(d + 0.5);
    return d * (double)fpb * 31.25 / 1000.0;
}
static void watch_gap(int ms)
{
    gap_worst = 0; gap_n = 0;
    for (int i = 0; i < ms / 10; i++) { tick(); double g = fabs(head_gap_ms()); if (g > (double)gap_worst) gap_worst = (float)g; gap_n++; }
}
// At a ratio r other than 1 the loop and the grid's beats pass each other: what must hold is that the loop's start
// (position 0) comes round every len / |r| frames from its anchor, a frame on the grid's beats. Watches the head for
// wraps, places each start between the ticks from the speed, and keeps the worst distance (ms) from that lattice.
static bool anchor_on_beat;
static void watch_starts(int ms, float r)
{
    gap_worst = 0; gap_n = 0;
    uint32_t go, gf, ga; int gm;
    recorder_debug_grid(&go, &gf, &gm, &ga);
    double le = recorder_loop_frames(), period = le / fabs((double)r);
    double last = (double)recorder_position() * le;
    for (int i = 0; i < ms / 10; i++) {
        tick();
        double now = (double)recorder_position() * le, sp = (double)recorder_speed_now(), wrap = -1;
        if (sp > 0 && now < last - le / 2) wrap = (double)recorder_frames() - now / sp;              // forwards: passed 0
        if (sp < 0 && now > last + le / 2) wrap = (double)recorder_frames() - (now - le) / sp;       // backwards: passed 0 going down
        last = now;
        if (wrap < 0) continue;
        double d = fmod(wrap - (double)ga, period); if (d < 0) d += period;
        if (d > period / 2) d = period - d;
        double msd = d * 31.25 / 1000.0;
        if (msd > (double)gap_worst) gap_worst = (float)msd;
        gap_n++;
    }
    anchor_on_beat = (ga - go) % gf == 0;
}

static int run, failed;
static void check(bool ok, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    printf("%s ", ok ? "  ok  " : "  FAIL"); vprintf(fmt, ap); printf("\n");
    va_end(ap);
    run++; if (!ok) failed++;
}
static void reset(void)
{
    for (int k = 0; k < npeer; k++) { free(P[k].st); memset(&P[k], 0, sizeof P[k]); }
    npeer = 0; nair = 0;
    free(board_st); board_st = link_new_state();
    use(-1);
    flock_init(0, 0);                          // the Samplooler as switched on: a fresh session
    flock_enable(true, 1, 1);
    rec_down = false; last_state = -1; loop_seen = 0xFFFFFFFFu; snap_setting = false;
    recorder_set_speed(1.0f);
    rec_down = true; run_ms(30); rec_down = false; run_ms(50);   // a tap: no loop
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    if (!recorder_init()) return 1;
    recorder_set_volume(1.0f);

    printf("== alone: a take sets the tempo, whole frames per beat, n in 1 2 4 8 16, 60..180 BPM\n");
    {
        reset();
        bool all_ok = true, pref_ok = true, whole_ok = true, origin_ok = true;
        int worst_len = 0; float worst_bpm = 0;
        for (double L = 0.5; L <= 12.01; L += 0.25) {
            take(L); settle();
            int le = recorder_loop_frames();
            const link_time_t *t = link_time();
            int n = t->beats;
            float bpm = 60.0f * (float)SR / (float)t->fpb;
            bool n_ok = n == 1 || n == 2 || n == 4 || n == 8 || n == 16;
            bool len_ok = le == (int)(t->fpb * (uint32_t)n);
            bool range_ok = bpm >= 59.9f && bpm <= 180.1f;
            // 4 or 8 when either fits
            float b4 = 60.0f * (float)SR * 4.0f / (float)le, b8 = 60.0f * (float)SR * 8.0f / (float)le;
            bool fit4 = b4 >= 60 && b4 <= 180, fit8 = b8 >= 60 && b8 <= 180;
            if ((fit4 || fit8) && !(n == 4 || n == 8)) pref_ok = false;
            if (!n_ok || !len_ok) whole_ok = false;
            if (!range_ok) { all_ok = false; worst_len = le; worst_bpm = bpm; }
            uint32_t o; float f; link_origin(&o, &f);
            if (o != recorder_loop_start() || f != 0.0f || !(link_flags_any() & LINKF_LOOP)) origin_ok = false;
            if (fabs(L - 2.0) < 1e-6 || fabs(L - 0.5) < 1e-6 || fabs(L - 12.0) < 1e-6 || fabs(L - 7.25) < 1e-6)
                printf("       %5.2f s: %2d beats of %5u frames = %6.1f BPM, loop %d frames\n", L, n, (unsigned)t->fpb, (double)bpm, le);
        }
        check(whole_ok, "every take: n in 1 2 4 8 16, the loop trimmed to n whole beats");
        check(all_ok, "every take from 0.5 to 12 s lands in 60..180 BPM%s", all_ok ? "" : " (not: see below)");
        if (!all_ok) printf("       worst: loop %d frames -> %.1f BPM\n", worst_len, (double)worst_bpm);
        check(pref_ok, "4 or 8 beats whenever one of them fits");
        check(origin_ok, "beat 0 is the loop's start, the loop flag is up");
    }

    printf("== a WRECK that only booted hears the take and takes its tempo\n");
    {
        reset();
        int w = peer_add("WRECK", 1, +20, 0);
        run_ms(2000);
        use(-1);
        check(flock_peers() == 1 && !flock_snapping(), "WRECK is heard; it set no tempo and has no loop, so the take is free");
        take(1.9); settle();
        run_ms(600);
        use(-1); const link_time_t *t = link_time(); uint32_t fpb = t->fpb; int n = t->beats;
        use(w);  const link_time_t *tw = link_time();
        check(tw->fpb == fpb && tw->beats == n && n == 4, "WRECK runs the loop's tempo: fpb %u, %d beats (%.1f BPM)", (unsigned)tw->fpb, tw->beats, (double)(60.0f * SR / (float)fpb));
        check(link_flags_any() & LINKF_LOOP, "and knows a loop is playing");
        run_ms(3000);
        double g = fabs(beats_at(-1) - beats_at(w)); g -= floor(g + 0.5);
        check(fabs(g * (double)fpb * 31.25 / 1000.0) < 0.5, "their beats agree within 0.5 ms (%.3f)", g * (double)fpb * 31.25 / 1000.0);
        use(-1);
    }

    printf("== a tempo already set: the take snaps to whole beats and plays on the beat\n");
    {
        reset();
        int w = peer_add("WRECK", 2, +30, 7e6);
        use(w); link_set_time(16000, 0, 4, 0);                        // 120 BPM, set on purpose (ver 1)
        use(-1);
        run_ms(2500);
        check(link_synced() && !link_is_clock() && flock_snapping(), "synced to WRECK (the clock), takes will snap");
        // 1.3 s at 120 BPM is 2.6 beats: the take records on to 3 beats
        rec_down = true; run_ms(1300); rec_down = false;
        int closing_ticks = 0;
        for (; closing_ticks < 200 && recorder_state() == REC_RECORDING; closing_ticks++) tick();
        int le = recorder_loop_frames();
        check(le == 3 * 16000, "1.3 s let go: the loop is 3 beats of 16000 frames (%d), closed %d ms later", le, closing_ticks * 10);
        check(link_time()->fpb == 16000 && link_time()->beats == 3, "the session keeps 120 BPM and says the loop is 3 beats (fpb %u, %d)", (unsigned)link_time()->fpb, link_time()->beats);
        watch_gap(5000);
        check(gap_worst < 1.0f, "over 5 s the head stays within 1 ms of the beat (worst %.3f ms)", (double)gap_worst);
        // a 2.1 s take: 4.2 beats, trimmed to 4
        take(2.1); settle();
        check(recorder_loop_frames() == 4 * 16000, "2.1 s: trimmed to 4 beats (%d frames)", recorder_loop_frames());
        watch_gap(3000);
        check(gap_worst < 1.0f, "and on the beat (worst %.3f ms)", (double)gap_worst);
        // the take began mid-beat: its start is put on the nearest beat
        run_ms(250);
        take(1.0); settle();
        check(recorder_loop_frames() == 2 * 16000, "a take begun between beats, 1 s: 2 beats (%d frames)", recorder_loop_frames());
        watch_gap(3000);
        check(gap_worst < 1.0f, "its start on the beat it began nearest to (worst %.3f ms)", (double)gap_worst);

        printf("== Snap on: the head at 2x, 1/2x, -1x lands on the grid; 1.37x drifts and comes back\n");
        snap_setting = true;
        recorder_set_speed(2.0f); run_ms(2000); watch_starts(6000, 2.0f);
        check(gap_n >= 2 && gap_worst < 1.0f && anchor_on_beat, "2x: the loop starts on the beat once the speed has settled (%d starts, worst %.3f ms)", gap_n, (double)gap_worst);
        recorder_set_speed(0.5f); run_ms(2000); watch_starts(12000, 0.5f);
        check(gap_n >= 1 && gap_worst < 1.0f, "1/2x: on the beat (%d starts, worst %.3f ms)", gap_n, (double)gap_worst);
        recorder_set_speed(-1.0f); run_ms(2000); watch_starts(6000, -1.0f);
        check(gap_n >= 2 && gap_worst < 1.0f, "-1x (backwards): on the beat (%d starts, worst %.3f ms)", gap_n, (double)gap_worst);
        recorder_set_speed(1.0f); run_ms(2000);
        snap_setting = false;
        recorder_set_speed(1.37f); run_ms(3000);
        recorder_set_speed(1.0f); run_ms(2000);
        double off = fabs(head_gap_ms());
        check(off > 20.0, "after 3 s at 1.37x with Snap off the loop is off the beat and left there (%.1f ms)", off);
        snap_setting = true; if (getenv("DBG")) { setenv("DBG2", "1", 1); watch_gap(2000); unsetenv("DBG2"); } else run_ms(2000);
        watch_gap(2000);
        check(gap_worst < 1.0f, "Snap on: it is back on the beat (worst %.3f ms)", (double)gap_worst);
        recorder_set_speed(1.37f); run_ms(3000); recorder_set_speed(1.0f); run_ms(2000); watch_gap(2000);
        check(gap_worst < 1.0f, "scrubbed again with Snap on: back on the beat (worst %.3f ms)", (double)gap_worst);
        snap_setting = false;

        printf("== the loop is cleared: the flag goes, the tempo stays\n");
        rec_down = true; run_ms(30); rec_down = false; run_ms(100);
        check(recorder_state() == REC_IDLE && !(link_flags_any() & LINKF_LOOP), "a tap: no loop, the flag is down");
        check(link_time()->fpb == 16000, "WRECK's 120 BPM stands (fpb %u)", (unsigned)link_time()->fpb);
        use(-1);
    }

    printf("== the harmony: key and scale travel both ways\n");
    {
        reset();
        int w = peer_add("MANTRA", 2, 0, 3e6);
        run_ms(2000);
        use(w); link_set_harm(7, 1, 0, 0, 0, -1); use(-1);
        run_ms(500);
        check(key == 7 && scale == 1, "MANTRA's G minor landed in the settings (key %d, scale %d)", key, scale);
        key = 2; scale = 3; flock_harm_set(key, scale);
        run_ms(500);
        use(w); const link_harm_t *h = link_harm(); use(-1);
        check(h->key == 2 && h->scale == 3, "D mixolydian set here reached MANTRA (key %d, scale %d)", h->key, h->scale);
    }

    printf("== a loaded loop with a tempo around: whole beats, padded or trimmed\n");
    {
        reset();
        int w = peer_add("WRECK", 2, 0, 0);
        use(w); link_set_time(20000, 0, 4, 0); use(-1);
        run_ms(2500);
        check(flock_snap_len(45000) == 40000 && flock_snap_len(52000) == 60000 && flock_snap_len(1000) == 20000,
              "45000 -> 2 beats, 52000 -> 3 beats, a tiny take -> 1 beat");
        int max = 0; int16_t *b = recorder_load_begin(&max);
        for (int i = 0; i < 52000; i++) b[i] = (int16_t)(8000.0f * sinf((float)i * 0.05f));
        recorder_load_end(52000, flock_snap_len(52000), true);
        settle();
        check(recorder_loop_frames() == 60000, "a 52000-frame slot plays as 3 beats of 20000 (%d), padded with silence", recorder_loop_frames());
        watch_gap(3000);
        check(gap_worst < 1.0f, "on the beat (worst %.3f ms)", (double)gap_worst);
    }

    printf("== a busy room: a quarter of the packets lost, the clock 35 ppm off\n");
    {
        reset();
        loss = 0.25;
        int w = peer_add("WRECK", 2, +35, 1e9);
        use(w); link_set_time(16000, 0, 4, 0); use(-1);
        run_ms(4000);
        take(1.0); settle();
        watch_gap(30000);
        check(gap_worst < 2.5f, "over 30 s the head stays within 2.5 ms of WRECK's beat (worst %.3f ms; the link alone is good for 1 ms here)", (double)gap_worst);
        loss = 0;
    }

    printf("== the volume glides (the encoder's steps)\n");
    {
        reset();
        take(2.0); settle();
        run_ms(500);
        float p0 = 0; for (int i = 0; i < 20; i++) { tick(); if (tick_peak > p0) p0 = tick_peak; }
        recorder_set_volume(0.5f);
        tick();
        float p1 = tick_peak;
        run_ms(300);
        float p2 = 0; for (int i = 0; i < 20; i++) { tick(); if (tick_peak > p2) p2 = tick_peak; }
        recorder_set_volume(1.0f);
        check(p0 > 0.1f && p1 > 0.8f * p0 && fabsf(p2 / p0 - 0.5f) < 0.05f,
              "100 %% to 50 %%: x%.2f in the first 10 ms, x%.2f after 300 ms", (double)(p1 / p0), (double)(p2 / p0));
    }

    printf("\n%d checks, %d failed\n", run, failed);
    return failed ? 1 : 0;
}
