// Joystick (ADC1 + click) and encoder (PCNT + push). BOOT is read by main.c;
// here the push is the encoder's own. S3 only (HAS_CONTROLS).
#pragma once
#include <stdbool.h>

typedef struct {
    int   enc;                 // detents turned since last poll, signed
    bool  tap, hold, longp;    // encoder push: released < 0.5 s; held 0.5 s; held 3 s (once)
    bool  pressed; int held_ms;
    bool  joy_click;           // released before 0.7 s
    bool  joy_long;            // held 0.7 s (fires once)
    bool  flick_l, flick_r, flick_u, flick_d;
    float jx, jy;              // -1..1 with dead zone, 0 when no joystick
    bool  has_joy;
} input_ev_t;

void input_init(void);
void input_poll(input_ev_t *ev);   // call every ~10 ms
