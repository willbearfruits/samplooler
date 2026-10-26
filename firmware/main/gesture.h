// Gestures: every decision the controls make, from angles, encoder counts and the joystick's ADC to what the
// Samplooler does, by the tuning table (tune.h). Pure C, also built on the PC (rig/twin, the v1 check), so the rig
// replays a recorded hand through this very code. Each gesture that fires is an event (gest_on_event): the rig shows
// them as they happen.
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef void (*gest_event_fn)(const char *name, float v, int ms, const char *text);
void gest_on_event(gest_event_fn fn);
void gest_emit(const char *name, float v, int ms, const char *text);

// Tilt and roll: the speed (before Snap), the delay and the stutter.
enum { ZONE_DEAD, ZONE_FWD, ZONE_BACK };
typedef struct {
    float speed;             // asked of the recorder (before Snap)
    float u;                 // the control, -1..1 past full: angle * sign / full (or the joystick's Y)
    float delay, stutter;    // 0..1
    float detent;            // the speed a detent holds, 0 if none
    int   zone;              // ZONE_*
} tilt_out_t;
void  gest_tilt(float pitch, float roll, bool tilt_on, bool has_imu, bool joy_speed, float joy_y,
                float spd_min, float spd_max, tilt_out_t *o);
float gest_speed(float u, float spd_min, float spd_max, float *detent);   // the curve alone: dead zone, curve, detents
float gest_snap(float s);                                                  // Snap: octaves and fifths, 1/4..8
void  gest_reset(void);

// The encoder: raw counts and the switch, every ~10 ms.
typedef struct { int enc; bool tap, hold, longp, pressed; int held_ms; } enc_out_t;
void gest_enc(int counts, bool pressed, int64_t now_us, enc_out_t *o);

// The joystick: raw 12-bit ADC and the centres measured at rest.
typedef struct { float jx, jy; bool flick_l, flick_r, flick_u, flick_d; } joy_out_t;
void gest_joy(int raw_x, int raw_y, float cx, float cy, joy_out_t *o);
