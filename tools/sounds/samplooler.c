// The Samplooler's clip for tools/sounds.py: a fixed 12 s performance through the firmware's own audio files
// (recorder_f32.c, fx.c, gesture.c, tune.c), in 64-frame blocks as audio.c runs them, with main.c's part every 10 ms:
// REC held over a sung phrase, let go, the loop tipped faster and backwards, rolled left and right, pushed with the left
// knob, a hand brought down over the sensor and lifted. Writes a 32-bit float WAV of what goes to the DAC (the output
// stage's soft clip and 0.9, as audio.c has it). A NaN or inf is written as it came, for the check to count.
//   samplooler out.wav
#include "recorder.h"
#include "fx.h"
#include "gesture.h"
#include "tune.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SR 32000
#define BL 64
static uint32_t seed = 0x5eed;
static float rnd(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return (float)(seed % 1000000u) / 1e6f - 0.5f; }

// what the mic hears: the room, the press's click, four sung notes (a saw with vibrato, softened), the release's click
static long t_in, press_at, release_at;
static float ph, lp;
static float mic(long t)
{
    float x = 0.0005f * rnd();
    if ((t >= press_at && t < press_at + 30 * 32) || (t >= release_at - 50 * 32 && t < release_at)) x += 0.6f * rnd();
    static const float note[4] = { 220.0f, 261.63f, 329.63f, 293.66f };
    long s = t - (press_at + SR / 3);                        // the phrase starts a third of a second after the press
    if (s >= 0 && s < 4 * SR / 2) {
        int k = (int)(s / (SR / 2)); float in = (float)(s % (SR / 2)) / SR;
        float env = fminf(in / 0.03f, 1.0f) * fminf((0.5f - in) / 0.06f, 1.0f);
        float f = note[k] * (1.0f + 0.006f * sinf(2.0f * (float)M_PI * 5.5f * (float)s / SR));
        ph += f / SR; if (ph >= 1.0f) ph -= 1.0f;
        x += 0.35f * env * (1.0f - 2.0f * ph);
    }
    lp += 0.25f * (x - lp);                                  // the mic and the room take the edge off
    return lp;
}

static float softclip(float x) { if (x > 1.5f) return 1.0f; if (x < -1.5f) return -1.0f; return x - x * x * x * (4.0f / 27.0f); }

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: samplooler out.wav\n"); return 2; }
    tune_defaults();
    fx_init();
    if (!recorder_init()) return 2;
    recorder_set_volume(1.0f);
    recorder_set_take(true, exp2f(TV(TU_RE_START_DB) / 6.0206f), TI(TU_RE_PREROLL) * 32, TI(TU_RE_GUARD) * 32, TI(TU_RE_TAIL) * 32);   // Loop start SOUND, by the button

    const int ticks = 1200;                                  // 12 s
    float *out = malloc((size_t)ticks * 5 * BL * sizeof(float));
    int n = 0;
    hand_out_t hand = { 0 };
    tilt_out_t to;
    press_at = 30 * 320; release_at = 260 * 320;              // REC from 0.3 s to 2.6 s
    float pitch = 0, roll = 0, flt_was = 2.0f;
    for (int tick = 0; tick < ticks; tick++) {
        float t = (float)tick / 100.0f;
        // the hands: where the box points, the left knob, the hand over the sensor
        // (flat is Speed min, 0.05x: the box is held nose down about 29 degrees to play at 1x, Speed max 2x at 45)
        float pg = t < 2.4f ? 0 : 29, rg = 0; int knob = 0, mm = 0;
        if (t >= 4.0f && t < 5.2f) pg = 42;                  // further down: faster
        else if (t >= 5.2f && t < 6.6f) pg = -29;            // nose up: backwards
        if (t >= 7.0f && t < 8.2f) rg = -40;                 // rolled left: the tape delay
        else if (t >= 8.2f && t < 9.0f) rg = 40;             // rolled right: the stutter
        pitch += 0.12f * (pg - pitch); roll += 0.12f * (rg - roll);
        if (t >= 9.2f && t < 10.0f && tick % 10 == 0) knob = (tick / 10) % 2 ? 8 : -4;   // the left knob pushed, two detents on, one back
        if (t >= 10.0f && t < 11.0f) mm = 240 - (int)((t - 10.0f) * 200.0f);             // a hand coming down to 40 mm
        else if (t >= 11.0f && t < 11.4f) mm = 40 + (int)((t - 11.0f) * 500.0f);         // and lifting
        // main.c's part
        recorder_set_record(tick >= press_at / 320 && tick < release_at / 320);
        gest_hand(tick % 3 == 0, mm > 0 ? mm : 0, mm > 0 ? 0 : 2, 10, &hand);
        if (knob) recorder_jog((float)knob * TV(TU_KN_TURN) * (float)SR / (float)TI(TU_KN_REV));
        gest_tilt(pitch, roll, true, true, false, 0.0f, 0.05f, 2.0f, &to);
        recorder_set_speed(to.speed);
        fx_set_delay(to.delay);
        fx_set_stutter(to.stutter);
        float flt = -hand.v;                                 // Hand: FILTER
        if (flt != flt_was) { flt_was = flt; fx_set_filter(flt, TV(TU_FL_RES)); }
        // audio.c's part: five blocks
        for (int b = 0; b < 5; b++) {
            float in[BL], o[BL];
            for (int i = 0; i < BL; i++) in[i] = mic(t_in + i);
            t_in += BL;
            recorder_process(in, o, BL);
            fx_process(o, BL);
            for (int i = 0; i < BL; i++) { uint32_t bits; memcpy(&bits, &o[i], 4); out[n++] = (bits & 0x7f800000u) == 0x7f800000u ? o[i] : softclip(o[i]) * 0.9f; }
        }
        if (tick % 100 == 0) fprintf(stderr, "  %4.1f s  state %d  speed %+.2f  delay %.2f  stutter %.2f  hand %.2f  loop %.2f s\n",
                                    (double)t, recorder_state(), (double)recorder_speed_now(), (double)to.delay, (double)to.stutter, (double)hand.v, (double)recorder_loop_seconds());
    }
    FILE *f = fopen(argv[1], "wb");
    if (!f) return 2;
    uint16_t fmt = 3, one = 1, ba = 4, bits = 32;
    uint32_t sr = SR, br = SR * 4, ds = (uint32_t)n * 4, rs = 36 + ds, fl = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&rs, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fl, 4, 1, f); fwrite(&fmt, 2, 1, f); fwrite(&one, 2, 1, f); fwrite(&sr, 4, 1, f);
    fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&ds, 4, 1, f);
    fwrite(out, 4, (size_t)n, f);
    fclose(f);
    free(out);
    return 0;
}
