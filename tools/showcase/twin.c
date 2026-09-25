// Digital twin of the S3 Samplooler: the real recorder_f32.c, fx.c, ui.c and oled.c, driven the way
// main.c drives them, from a scripted performance instead of a button, an IMU and a microphone.
//
//   twin <score.txt> <mic.f32> <out.f32> <oled.raw> <ui.csv>
//
// score.txt: one line per 10 ms control tick:
//     down mode pitch_deg roll_deg pitch_st toast_ms|param name|param value|param frac
// mic.f32:   mono float32 at REC_SAMPLE_RATE, what the microphone hears (at least as long as the score)
// out.f32:   the line out, after the roll effects, mono float32
// oled.raw:  the screen at 20 fps (every 5 ticks, as on the device), 128x64 bytes of 0/1 per frame
// ui.csv:    per screen frame: t state mode speed_now speed_cmd pos fx_delay fx_stutter pitch roll loop_s
#include "recorder.h"
#include "fx.h"
#include "ui.h"
#include "oled.h"
#include "audio.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// main.c's constants and defaults
#define TILT_DEAD_DEG  5.0f
#define TILT_FULL_DEG  45.0f
#define ROLL_DEAD_DEG  15.0f
#define ROLL_FULL_DEG  45.0f
static const float SPD_MIN = 0.05f, SPD_MAX = 2.0f;

static float control_to_speed(float u)            // copied from main.c
{
    float mag = fabsf(u), dead = TILT_DEAD_DEG / TILT_FULL_DEG;
    if (mag < dead) return SPD_MIN;
    float t = (mag - dead) / (1.0f - dead);
    if (t > 1.0f) t = 1.0f;
    float curve = t * t * (1.6f - 0.6f * t);
    float r = SPD_MIN + (SPD_MAX - SPD_MIN) * curve;
    return u >= 0.0f ? r : -r;
}

// the input ring for the screen's scope: "the last 128 input samples, one in 8"
static float ring[1024];
static int   ring_i;
void audio_scope(int8_t *dst)
{
    for (int i = 0; i < AUDIO_SCOPE_N; i++) {
        float v = ring[(ring_i + i * 8) & 1023] * 127.0f;
        dst[i] = (int8_t)(v > 127 ? 127 : v < -127 ? -127 : v);
    }
}

int main(int argc, char **argv)
{
    if (argc < 6) { fprintf(stderr, "usage: twin score.txt mic.f32 out.f32 oled.raw ui.csv\n"); return 2; }
    FILE *fs = fopen(argv[1], "r"), *fm = fopen(argv[2], "rb"), *fo = fopen(argv[3], "wb");
    FILE *fv = fopen(argv[4], "wb"), *fu = fopen(argv[5], "w");
    if (!fs || !fm || !fo || !fv || !fu) { perror("open"); return 1; }
    if (!recorder_init()) return 1;
    fx_init();
    oled_init(NULL);
    recorder_set_grain_ms(50);
    recorder_set_volume(1.0f);

    enum { BLOCK = 64, TICK = REC_SAMPLE_RATE / 100 };     // 10 ms = 320 samples = 5 blocks
    float in[TICK], out[TICK];
    char line[512];
    int tick = 0, held_n = 0;
    bool held = false;
    float speed_cmd = 1.0f, pitch_ratio = 1.0f, meter_in = 0, meter_out = 0;
    int mode = REC_MODE_TAPE;
    fprintf(fu, "t state mode speed_now speed_cmd pos fx_delay fx_stutter pitch roll loop_s\n");

    while (fgets(line, sizeof line, fs)) {
        int down = 0, toast_ms = 0;
        float pitch = 0, roll = 0, pitch_st = 0, frac = -1;
        char name[64] = "", val[64] = "";
        if (sscanf(line, "%d %d %f %f %f %d|%63[^|]|%63[^|]|%f", &down, &mode, &pitch, &roll, &pitch_st,
                   &toast_ms, name, val, &frac) < 6) continue;
        tick++;

        held_n = down ? (held_n < 3 ? held_n + 1 : 3) : (held_n > 0 ? held_n - 1 : 0);
        bool now = held_n == 3 ? true : held_n == 0 ? false : held;
        if (now != held) { held = now; recorder_set_record(held); }

        if (tick % 2 == 0) {                               // 50 Hz control rate
            speed_cmd = control_to_speed(pitch / TILT_FULL_DEG);
            float a = fabsf(roll) < ROLL_DEAD_DEG ? 0.0f : (fabsf(roll) - ROLL_DEAD_DEG) / (ROLL_FULL_DEG - ROLL_DEAD_DEG);
            if (a > 1.0f) a = 1.0f;
            fx_set_delay(roll < 0 ? a : 0.0f);
            fx_set_stutter(roll > 0 ? a : 0.0f);
            recorder_set_speed(speed_cmd);
            pitch_ratio = exp2f(pitch_st / 12.0f);
            recorder_set_pitch(pitch_ratio);
            recorder_set_mode(mode);
        }

        if (fread(in, sizeof(float), TICK, fm) != TICK) memset(in, 0, sizeof in);
        float pk_in = 0, pk_out = 0;
        for (int b = 0; b < TICK; b += BLOCK) {
            recorder_process(in + b, out + b, BLOCK);
            fx_process(out + b, BLOCK);
        }
        for (int i = 0; i < TICK; i++) {
            float a = fabsf(in[i]), o = fabsf(out[i]);
            if (a > pk_in) pk_in = a;
            if (o > pk_out) pk_out = o;
            ring[ring_i] = in[i]; ring_i = (ring_i + 1) & 1023;
        }
        fwrite(out, sizeof(float), TICK, fo);
        meter_in = pk_in > meter_in ? pk_in : meter_in * 0.9f;
        meter_out = pk_out > meter_out ? pk_out : meter_out * 0.9f;

        if (tick % 5 == 0) {                               // the screen at 20 fps
            int s = recorder_state();
            ui_t u = {
                .state = s, .mode = mode, .speed_now = recorder_speed_now(), .speed_cmd = speed_cmd, .spd_max = SPD_MAX,
                .pitch_ratio = pitch_ratio, .tilt_pitch = pitch, .tilt_roll = roll, .has_imu = true, .tilt_on = true,
                .has_joy = true, .pos = recorder_position(), .rec_fill = recorder_record_fill(),
                .loop_s = recorder_loop_seconds(), .max_s = recorder_max_seconds(), .meter_in = meter_in,
                .meter_out = meter_out, .clip = false, .cpu = 11, .menu = false, .param = 1, .param_n = 13,
                .param_name = name, .param_val = val, .param_frac = frac, .toast_ms = toast_ms,
                .fx_delay = fx_delay_active(), .fx_stutter = fx_stutter_active(),
            };
            ui_draw(&u);
            unsigned char px[128 * 64];
            for (int y = 0; y < 64; y++)
                for (int x = 0; x < 128; x++) px[y * 128 + x] = oled_get(x, y) ? 1 : 0;
            fwrite(px, 1, sizeof px, fv);
            fprintf(fu, "%.3f %d %d %.4f %.4f %.4f %d %d %.2f %.2f %.3f\n", tick / 100.0, s, mode, recorder_speed_now(),
                    speed_cmd, recorder_position(), fx_delay_active(), fx_stutter_active(), pitch, roll, recorder_loop_seconds());
        }
    }
    fclose(fo); fclose(fv); fclose(fu);
    fprintf(stderr, "%d ticks, %.1f s\n", tick, tick / 100.0);
    return 0;
}
