// The screen. main.c fills a ui_t once per frame and calls ui_draw(); everything visual lives here.
// 128x64: a 16-row header band (the yellow strip on two-colour panels) and a 48-row body.
// 72x40: the compact layout.
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int   state, mode;               // REC_*, REC_MODE_*
    float speed_now, speed_cmd, spd_max;
    float pitch_ratio;
    float tilt_pitch, tilt_roll;     // degrees
    bool  has_imu, tilt_on, has_joy;
    float pos, rec_fill, loop_s;     // loop position 0..1, record fill 0..1, loop length in seconds
    int   max_s;
    float meter_in, meter_out;       // 0..1+
    bool  clip;
    bool  fx_delay, fx_stutter;
    int   cpu;
    // menu and the transient setting toast
    bool  menu;
    int   param, param_n;
    const char *param_name, *param_val;
    float param_frac;                // 0..1 position of the value in its range, <0 for actions
    int   toast_ms;                  // >0: show the setting bar for that long
} ui_t;

void ui_draw(const ui_t *u);         // draws one frame and flushes; call at 20 fps
