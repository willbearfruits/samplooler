// The rev E controls on the host (see run.sh): how a take starts and ends, the left knob's jog, the filter, the
// knob's and the hand's gestures. The firmware's own files, run in 64-frame blocks as audio.c runs them.
#include "recorder.h"
#include "fx.h"
#include "gesture.h"
#include "tune.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SR 32000
#define BL 64
static int checks, fails;
static void check(bool ok, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void check(bool ok, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    printf("  %s ", ok ? "ok  " : "FAIL"); vprintf(fmt, ap); printf("\n");
    va_end(ap);
    checks++; if (!ok) fails++;
}
static uint32_t seed = 0x5eed;
static float rnd(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return (float)(seed % 1000000u) / 1e6f; }

// ---------------------------------------------------------------- the recorder, a block at a time
static long t_in;                                      // samples fed since the start
static float (*source)(long t);
static float out_blk[BL];
static float last_out, max_step;                       // the largest jump between two output samples
static bool bad;                                       // a NaN or inf came out
static void run(int blocks, bool rec)
{
    float in[BL];
    recorder_set_record(rec);
    for (int b = 0; b < blocks; b++) {
        for (int i = 0; i < BL; i++) in[i] = source ? source(t_in + i) : 0.0f;
        recorder_process(in, out_blk, BL);
        fx_process(out_blk, BL);
        for (int i = 0; i < BL; i++) {
            float o = out_blk[i];
            if (!(o == o) || o > 100.0f || o < -100.0f) bad = true;
            float d = fabsf(o - last_out); if (d > max_step) max_step = d;
            last_out = o;
        }
        t_in += BL;
    }
}
static void settle(void) { source = NULL; run(40, false); recorder_set_record(true); run(1, true); run(1, false); run(4, false); }   // a tap: idle, no loop

// what the mic hears in the take tests: silence, a click at the press, silence, a tone from tone_at, a click at the release
static long press_at, tone_at, release_at;
static float src_take(long t)
{
    if (t >= press_at - 2 * BL && t < press_at + 40 * 32) return 0.9f * (rnd() - 0.5f) * 2.0f;          // the press's click: 40 ms of noise
    if (t >= release_at - 60 * 32 && t < release_at + 20 * 32) return 0.9f * (rnd() - 0.5f) * 2.0f;     // the release's: begins 60 ms before it is seen
    if (t >= tone_at && t < release_at - 70 * 32) return 0.5f * sinf((float)(t - tone_at) * 2.0f * (float)M_PI * 220.0f / SR);
    return 0.0005f * (rnd() - 0.5f);                                                                    // the room
}
static float src_sine(long t) { return 0.5f * sinf((float)t * 2.0f * (float)M_PI * 220.0f / SR); }

static void test_take(void)
{
    printf("== a take starts at the first sound and neither click is in it\n");
    recorder_set_speed(1.0f);
    // as it was: everything held is the take, clicks and all
    recorder_set_take(false, 0.016f, 0, 0, 0);
    settle();
    source = src_take; press_at = t_in; tone_at = press_at + SR / 2; release_at = press_at + 2 * SR;
    run(2 * SR / BL, true); run(2, false);
    int n = 0; const int16_t *tk = recorder_take(&n);
    check(recorder_state() == REC_PLAYING && n == 2 * SR, "old way (all off): the take is what was held, %d samples of %d", n, 2 * SR);
    int pk0 = 0; for (int i = 0; i < 640 && i < n; i++) if (abs(tk[i]) > pk0) pk0 = abs(tk[i]);
    check(pk0 > 20000, "old way: the press's click is in it (peak %d in the first 20 ms)", pk0);

    // the new way: guard 120 ms, start at the first sound with 12 ms before it, 80 ms off the end
    recorder_set_take(true, 0.016f, 12 * 32, 120 * 32, 80 * 32);
    settle();
    source = src_take; press_at = t_in; tone_at = press_at + SR / 2; release_at = press_at + 2 * SR;
    run(4, true);
    check(recorder_waiting() && recorder_state() == REC_RECORDING, "REC down, no sound yet: recording, and waiting");
    run(2 * SR / BL - 4, true);
    check(!recorder_waiting(), "the sound came: no longer waiting");
    run(2, false);
    tk = recorder_take(&n);
    int want = (int)(release_at - tone_at) + 12 * 32 - 80 * 32;
    check(recorder_state() == REC_PLAYING, "it loops");
    check(abs(n - want) <= 2 * BL, "the take runs from 12 ms before the first sound to 80 ms before the release: %d samples, expected about %d", n, want);
    int pk = 0; for (int i = 0; i < n; i++) if (abs(tk[i]) > pk) pk = abs(tk[i]);
    check(pk < 17500, "no click in it: peak %d (the tone is 16384, a click 29000)", pk);
    int first = 0; while (first < n && abs(tk[first]) < 500) first++;
    check(first >= 10 * 32 && first <= 14 * 32, "the tone begins %d samples in (12 ms = 384: its attack is whole)", first);
    check(fabsf(recorder_loop_seconds() - (float)(want - 320) / SR) < 0.01f, "the loop is %.3f s", (double)recorder_loop_seconds());

    // REC held and let go with no sound at all: nothing, and no loop
    settle();
    source = NULL; press_at = t_in;
    run(SR / BL, true);
    check(recorder_waiting(), "a second of silence: still waiting");
    run(2, false);
    check(recorder_state() == REC_IDLE && !recorder_waiting() && recorder_take(&n) == NULL, "let go without a sound: idle, no take");

    // on the flock's grid, let go early: it records on to the beat, and the release's click is silenced inside the take
    recorder_set_take(true, 0.016f, 12 * 32, 120 * 32, 80 * 32);
    settle();
    recorder_set_grid(16000, recorder_frames(), 0.0f, GRID_SNAP_END);          // 120 BPM, beat 0 now
    source = src_take; press_at = t_in; tone_at = press_at + SR / 4; release_at = press_at + (int)(0.67f * SR);
    run((int)(0.67f * SR) / BL, true); run(1, false);
    check(recorder_closing(), "let go with 0.35 s of sound on a 0.5 s beat: it records on to the beat");
    run(SR / BL, false);
    tk = recorder_take(&n);
    check(recorder_state() == REC_PLAYING && recorder_loop_frames() == 16000, "the loop is one beat: %d frames", recorder_loop_frames());
    pk = 0; for (int i = 0; i < n; i++) if (abs(tk[i]) > pk) pk = abs(tk[i]);
    check(pk < 17500, "the release's click is not heard in it: peak %d", pk);
    recorder_set_grid(0, 0, 0.0f, 0);
    run(2, false);
}

// the head's place, unwrapped, from the loop position
static void test_jog(void)
{
    printf("== the left knob's jog\n");
    recorder_set_take(false, 0.016f, 0, 0, 0);
    recorder_set_jog_ms(60.0f);
    settle();
    source = src_sine; run(4 * SR / BL, true); run(2, false);                  // a 4 s loop of 220 Hz
    int le = recorder_loop_frames();
    check(recorder_state() == REC_PLAYING && le > 3 * SR, "a 4 s loop plays");
    recorder_set_speed(1.0f); run(20, false);
    // without a push the head moves one frame a frame
    float p0 = recorder_position() * (float)le; run(100, false);
    float d0 = recorder_position() * (float)le - p0; if (d0 < 0) d0 += (float)le;
    check(fabsf(d0 - 6400.0f) < 2.0f, "no push: the head moves %.1f frames in 6400", (double)d0);
    // a push of 9600 frames (0.3 s of tape) is travelled, no more, no less
    p0 = recorder_position() * (float)le;
    max_step = 0; last_out = out_blk[BL - 1];
    recorder_jog(9600.0f);
    run(1, false);
    float jv = recorder_jog_now();
    run(399, false);                                                           // 0.8 s: thirteen glides
    float d1 = recorder_position() * (float)le - p0; while (d1 < 0) d1 += (float)le;
    check(fabsf(d1 - (25600.0f + 9600.0f)) < 12.0f, "a push of 9600 frames: the head went %.1f frames further than the tape alone", (double)(d1 - 25600.0f));
    check(jv > 3.0f && jv < 5.1f, "at its start the push adds %.2fx (9600 frames over a 60 ms glide: 5x, less one block's decay)", (double)jv);
    check(fabsf(recorder_jog_now()) < 0.001f, "and it has died away: %.5fx", (double)recorder_jog_now());
    // 220 Hz at 0.5 moves 0.0216 a sample at 1x; at 6x it is 0.13: nothing near a click (a jump would be up to 1.0)
    check(max_step < 0.2f && !bad, "no click on the way: the largest step between samples %.3f", (double)max_step);
    // backwards, against the tape, and many small pushes add up like one
    p0 = recorder_position() * (float)le;
    for (int k = 0; k < 40; k++) { recorder_jog(-480.0f); run(5, false); }
    run(300, false);
    float d2 = recorder_position() * (float)le - p0; while (d2 < 0) d2 += (float)le;
    float tape = 500.0f * BL; while (tape >= (float)le) tape -= (float)le;
    float exp2 = tape - 19200.0f; while (exp2 < 0) exp2 += (float)le;
    float e2 = d2 - exp2; if (e2 > (float)le / 2) e2 -= (float)le; if (e2 < -(float)le / 2) e2 += (float)le;
    check(fabsf(e2) < 24.0f, "forty pushes of -480 frames: 19200 back in all (off by %.1f)", (double)e2);
    // a wild spin: the extra speed is held to 8x and nothing breaks
    bad = false;
    for (int k = 0; k < 200; k++) { recorder_jog((rnd() - 0.5f) * 200000.0f); run(1, false); if (fabsf(recorder_jog_now()) > 8.01f) bad = true; }
    run(400, false);
    check(!bad && recorder_position() >= 0.0f && recorder_position() < 1.0f, "a wild spin: the push stays within 8x, the head inside the loop, no bad sample");
    // GRAIN too
    recorder_set_mode(REC_MODE_GRAIN); max_step = 0; bad = false; run(4, false); last_out = out_blk[BL - 1]; max_step = 0;
    recorder_jog(6400.0f); run(300, false);
    check(max_step < 0.2f && !bad, "GRAIN: a push without a click (largest step %.3f)", (double)max_step);
    recorder_set_mode(REC_MODE_TAPE);
    // a push while nothing plays is forgotten
    settle(); recorder_jog(50000.0f); run(10, false);
    check(recorder_jog_now() == 0.0f, "a push with no loop does nothing, now or later");
}

// ---------------------------------------------------------------- the filter alone
static float tone_gain(float hz, float pos, float res)
{
    float buf[BL]; double e_in = 0, e_out = 0; long t = 0;
    fx_set_filter(pos, res);
    for (int b = 0; b < 600; b++) {
        for (int i = 0; i < BL; i++, t++) buf[i] = 0.25f * sinf((float)t * 2.0f * (float)M_PI * hz / SR);
        float in[BL]; memcpy(in, buf, sizeof in);
        fx_process(buf, BL);
        if (b >= 300) for (int i = 0; i < BL; i++) { e_in += (double)in[i] * (double)in[i]; e_out += (double)buf[i] * (double)buf[i]; }
    }
    return 10.0f * log10f((float)(e_out / e_in) + 1e-12f);
}
static void test_filter(void)
{
    printf("== the filter\n");
    fx_set_delay(0); fx_set_stutter(0);
    float buf[BL], in[BL];
    fx_set_filter(0.0f, 0.5f);
    for (int b = 0; b < 400; b++) { memset(buf, 0, sizeof buf); fx_process(buf, BL); }      // the delay's tail and the wet share die
    bool same = true;
    for (int b = 0; b < 50; b++) {
        for (int i = 0; i < BL; i++) in[i] = buf[i] = rnd() - 0.5f;
        fx_process(buf, BL);
        if (memcmp(in, buf, sizeof in)) same = false;
    }
    check(same, "open (0): what goes in comes out, bit for bit");
    float lo = tone_gain(100.0f, -1.0f, 0.3f), hi = tone_gain(4000.0f, -1.0f, 0.3f);
    check(lo > -3.0f && hi < -40.0f, "fully closed low-pass: 100 Hz %.1f dB, 4 kHz %.1f dB", (double)lo, (double)hi);
    lo = tone_gain(100.0f, 1.0f, 0.3f); hi = tone_gain(12000.0f, 1.0f, 0.3f);
    check(lo < -40.0f && hi > -3.0f, "fully risen high-pass: 100 Hz %.1f dB, 12 kHz %.1f dB", (double)lo, (double)hi);
    float mid = tone_gain(1000.0f, -0.15f, 0.3f);
    check(mid > -1.5f, "a little closed (-0.15): 1 kHz still passes (%.1f dB)", (double)mid);
    float pk = tone_gain(1342.0f, -0.5f, 0.9f);
    check(pk > 6.0f && pk < 16.0f, "resonance 0.9 at the cutoff: +%.1f dB (a peak, not a howl)", (double)pk);
    // a sweep through everything, on a sine: no click, nothing bad, through open from one kind to the other
    fx_set_filter(0.0f, 0.5f);
    for (int b = 0; b < 100; b++) { memset(buf, 0, sizeof buf); fx_process(buf, BL); }
    long t = 0; float last = 0, worst = 0; bool nan = false;
    for (int b = 0; b < 4000; b++) {
        float pos = sinf((float)b * 0.004f);                                               // -1..1 and back, 3 s a cycle
        if (b > 3000) pos = rnd() * 2.0f - 1.0f;                                           // then a jump every block
        fx_set_filter(pos, 0.5f);
        for (int i = 0; i < BL; i++, t++) buf[i] = 0.5f * sinf((float)t * 2.0f * (float)M_PI * 330.0f / SR);
        fx_process(buf, BL);
        for (int i = 0; i < BL; i++) {
            if (!(buf[i] == buf[i]) || fabsf(buf[i]) > 4.0f) nan = true;
            float d = fabsf(buf[i] - last); if (b <= 3000 && d > worst) worst = d; last = buf[i];
        }
    }
    check(!nan, "a slow sweep, then a random position every block: nothing bad comes out");
    check(worst < 0.12f, "the slow sweep has no click: the largest step %.3f (the sine alone steps 0.032)", (double)worst);
    // a bad number in does not stick
    fx_set_filter(-0.5f, 0.5f);
    for (int i = 0; i < BL; i++) buf[i] = i == 3 ? NAN : 0.1f;
    fx_process(buf, BL);
    bool ok = true;
    for (int b = 0; b < 20; b++) { for (int i = 0; i < BL; i++) buf[i] = 0.1f; fx_process(buf, BL); }
    for (int i = 0; i < BL; i++) if (!(buf[i] == buf[i])) ok = false;
    check(ok, "a NaN on the way in: the filter is clean again within 20 blocks");
    fx_set_filter(0.0f, 0.5f);
}

// ---------------------------------------------------------------- the gestures
static int ev_on, ev_off;
static void on_event(const char *name, float v, int ms, const char *text) { (void)v; (void)ms; (void)text; if (!strcmp(name, "hand.on")) ev_on++; if (!strcmp(name, "hand.off")) ev_off++; }
static void test_gestures(void)
{
    printf("== the knob and the hand\n");
    tune_defaults();
    gest_on_event(on_event);
    knob_out_t k;
    int det = 0, cnt = 0;
    for (int i = 0; i < 10; i++) { gest_knob(3, &k); det += k.det; cnt += k.counts; }
    check(det == -7 && cnt == -30, "30 counts at knob.dir -1: %d counts, %d detents (4 a detent, the rest kept)", cnt, det);
    gest_knob(2, &k); det += k.det;
    check(det == -8, "two more make the eighth detent");
    tune_set(TU_KN_DIR, 1);
    gest_knob(8, &k);
    check(k.det == 2 && k.counts == 8, "knob.dir +1 turns it round");

    hand_out_t h;
    // no hand: readings out of range or bad
    for (int i = 0; i < 100; i++) gest_hand(i % 3 == 0, 900, 0, 10, &h);
    check(h.v == 0 && !h.seen, "a ceiling at 900 mm is no hand");
    for (int i = 0; i < 100; i++) gest_hand(i % 3 == 0, 120, 2, 10, &h);
    check(h.v == 0 && !h.seen, "readings the sensor calls bad are no hand");
    // one good reading among bad ones does nothing
    gest_hand(true, 100, 0, 10, &h); for (int i = 0; i < 30; i++) gest_hand(i % 3 == 0, 900, 0, 10, &h);
    check(h.v == 0 && !h.seen && ev_on == 0, "one stray reading does not bring the hand in");
    // a hand at 145 mm (half way between 40 and 250): seen after two readings, glides to 0.5
    for (int i = 0; i < 60; i++) gest_hand(i % 3 == 0, 145, 0, 10, &h);
    check(h.seen && ev_on == 1 && fabsf(h.v - 0.5f) < 0.01f && h.mm == 145, "a hand at 145 mm: seen, value %.3f", (double)h.v);
    // a wild reading in between changes nothing
    gest_hand(true, 41, 0, 10, &h); float v1 = h.v; for (int i = 1; i < 12; i++) gest_hand(i % 3 == 0, 145, 0, 10, &h);
    check(fabsf(v1 - 0.5f) < 0.01f && fabsf(h.v - 0.5f) < 0.01f, "one wild reading (41 mm) is not followed: %.3f", (double)v1);
    // it never steps: down to 40 mm, the largest change per 10 ms
    float worst = 0, was = h.v;
    for (int i = 0; i < 60; i++) { gest_hand(i % 3 == 0, 40, 0, 10, &h); float d = fabsf(h.v - was); if (d > worst) worst = d; was = h.v; }
    check(h.v > 0.99f && worst < 0.13f, "down to 40 mm: full (%.3f), and it glides (largest step in 10 ms %.3f)", (double)h.v, (double)worst);
    // two misses are a glitch, three are the hand leaving; then it glides back over hand.release
    gest_hand(true, 900, 0, 10, &h); gest_hand(true, 900, 0, 10, &h);
    check(h.seen, "two misses: still there");
    gest_hand(true, 900, 0, 10, &h);
    check(!h.seen && ev_off == 1 && h.v > 0.9f, "three: gone, and the value has not jumped (%.3f)", (double)h.v);
    for (int i = 0; i < 25; i++) gest_hand(false, 0, 0, 10, &h);
    check(h.v > 0.3f && h.v < 0.45f, "250 ms later it is at %.3f (one time constant: 0.37)", (double)h.v);
    for (int i = 0; i < 300; i++) gest_hand(false, 0, 0, 10, &h);
    check(h.v == 0.0f, "and then exactly 0");
    // the curve
    tune_set(TU_HA_CURVE, 2.0f);
    for (int i = 0; i < 90; i++) gest_hand(i % 3 == 0, 145, 0, 10, &h);
    check(fabsf(h.v - 0.25f) < 0.01f, "hand.curve 2 at half way: %.3f", (double)h.v);
    // nonsense in: nothing bad out
    tune_defaults();
    bool ok = true;
    for (int i = 0; i < 20000; i++) {
        gest_hand(rnd() < 0.5f, (int)(rnd() * 70000.0f) - 2000, (int)(rnd() * 12.0f) - 2, 1 + (int)(rnd() * 200.0f), &h);
        if (!(h.v >= 0.0f && h.v <= 1.0f)) ok = false;
    }
    check(ok, "20000 random readings: the value stays in 0..1");
}

int main(void)
{
    tune_defaults();
    fx_init();
    if (!recorder_init()) return 2;
    test_take();
    test_jog();
    test_filter();
    test_gestures();
    printf("\n%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
