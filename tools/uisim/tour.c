// A tour of the S3 screen for the README and the project page: drives ui_draw() through a short
// session at 20 fps (boot gag, record, play, tilt, reverse, delay, stutter, grains, flung at 8x,
// the crawl) and dumps every frame as PBM into $OUT (default out/tour). tour.sh makes the video.
#include "ui.h"
#include "oled.h"
#include "recorder.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
static int n;
static void dump(void)
{
    const char *d = getenv("OUT"); char fn[160]; snprintf(fn, sizeof fn, "%s/%04d.pbm", d ? d : "out/tour", n++);
    FILE *f = fopen(fn, "w"); fprintf(f, "P1\n%d %d\n", OLED_W, OLED_H);
    for (int y = 0; y < OLED_H; y++) { for (int x = 0; x < OLED_W; x++) fputc(oled_get(x, y) ? '1' : '0', f); fputc('\n', f); }
    fclose(f);
}
static ui_t u;
static float target_speed, target_pitch, target_roll;
// one frame: ease the speed and tilt toward their targets like a hand would, run the playhead, draw
static void frame(void)
{
    u.speed_cmd = target_speed;
    u.speed_now += (target_speed - u.speed_now) * 0.15f;
    u.tilt_pitch += (target_pitch - u.tilt_pitch) * 0.2f;
    u.tilt_roll += (target_roll - u.tilt_roll) * 0.2f;
    if (u.state == REC_PLAYING) { u.pos += u.speed_now / (u.loop_s * 20.0f); u.pos -= floorf(u.pos); }
    float t = n * 0.05f;
    u.meter_in = 0.35f + 0.25f * fabsf(sinf(t * 2.3f)) * (0.6f + 0.4f * sinf(t * 0.7f));
    u.meter_out = u.state == REC_PLAYING ? 0.3f + 0.25f * fabsf(sinf(t * 3.1f + 1.0f)) : 0.05f;
    ui_draw(&u);
    dump();
}
static void hold(int frames) { for (int i = 0; i < frames; i++) frame(); }
static void go(float speed, float pitch, float roll, int frames) { target_speed = speed; target_pitch = pitch; target_roll = roll; hold(frames); }
int main(void)
{
    oled_init(NULL);
    u = (ui_t){ .state = REC_IDLE, .mode = REC_MODE_TAPE, .spd_max = 8, .pitch_ratio = 1, .has_imu = true, .tilt_on = true,
                .max_s = 20, .cpu = 9, .param_n = 13, .param = 4, .param_name = "Speed max", .param_val = "8.00x", .param_frac = 1.0f };
    hold(46 + 24);                                                    // the boot gag, then a moment of READY
    u.state = REC_RECORDING;                                          // hold the button and make a sound
    for (int i = 0; i < 46; i++) { u.rec_fill = i / 46.0f * 0.37f; frame(); }
    u.state = REC_PLAYING; u.loop_s = 7.3f; u.pos = 0; u.speed_now = 1.0f;   // let go: it loops
    go(1.0f, 6, 0, 40);
    go(2.3f, 22, 0, 40);                                              // nose down: faster
    go(-1.6f, -20, 0, 50);                                            // nose up: backwards
    u.fx_delay = true; go(1.2f, 8, -30, 70); u.fx_delay = false;      // roll left: tape delay
    u.fx_stutter = true; go(1.0f, 8, 35, 36); u.fx_stutter = false;   // roll right: stutter
    u.mode = REC_MODE_GRAIN; u.pitch_ratio = 1.5f; go(0.9f, 4, 0, 50); u.mode = REC_MODE_TAPE; u.pitch_ratio = 1;   // grains
    go(8.0f, 44, 0, 50);                                              // all the way down: flung
    go(0.03f, 1, 0, 50);                                              // flat: a slow crawl, never a stop
    go(1.0f, 6, 0, 30);
    fprintf(stderr, "%d frames\n", n);
    return 0;
}
