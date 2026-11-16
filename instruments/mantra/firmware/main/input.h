// The knob (encoder on the PCNT, its push), the record button and the joystick (ADC1, found at boot). When no
// joystick answers, its two pins are read as rev E's left knob (a second PCNT unit).
#pragma once
#include <stdbool.h>

typedef struct {
    int   enc;                 // detents turned since the last poll, signed
    bool  tap;                 // the push released within 0.5 s (after at least 40 ms)
    bool  pressed; int held_ms;
    bool  rec;                 // the record button is down
    bool  has_joy;             // a joystick answered at boot (and has not been pinned off centre for 8 s)
    float jx, jy;              // -1..1, dead zone applied; x right, y up (the kit's mounting); 0 without one
    bool  lknob_turned;        // rev E's left knob has turned since power-on (it sits on the joystick's pins)
    int   lknob;               // its detents since the last poll, signed; 0 on a board with a joystick
} input_ev_t;

void input_init(void);
void input_poll(input_ev_t *ev);   // every 10 ms
