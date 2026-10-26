// The rig's line (rig/PROTOCOL.md): streams of the raw motion, the state, the screen and the gestures to the PC, the
// tuning table both ways, injected motion and keys from the PC. Pure C: rig/twin builds this same file, so the board
// and the twin speak one protocol. Everything runs in the UI task.
#pragma once
#include <stdbool.h>
#include <stdint.h>

// The state, one 14-bit value each (raw = value * scale), in this order. 'Q' announces the list.
#define RIG_STATE(X) \
    X(state, 1) X(mode, 1) X(flags, 1) X(qw, 8191) X(qx, 8191) X(qy, 8191) X(qz, 8191) \
    X(pitch, 45) X(roll, 45) X(yaw, 45) X(tilt_u, 2000) X(zone, 1) X(detent, 1000) X(speed_cmd, 1000) X(speed_now, 1000) \
    X(pos, 8191) X(loop_s, 400) X(delay, 8191) X(stutter, 8191) X(joy_x, 8191) X(joy_y, 8191) X(joy_rx, 2) X(joy_ry, 2) \
    X(enc, 1) X(in_peak, 8191) X(out_peak, 8191) X(in_clips, 1) X(cpu, 1) X(heap_kb, 1) X(i2c_fail, 1) \
    X(bias_x, 400) X(bias_y, 400) X(bias_z, 400) X(rate_hz, 1) X(menu_param, 1)
#define RIG_SID(n, s) RS_##n,
enum { RIG_STATE(RIG_SID) RS_N };
#undef RIG_SID
// flags
enum { RF_FROZEN = 1, RF_MENU = 2, RF_INJECTED = 4, RF_REC_DOWN = 8, RF_ENC_DOWN = 16, RF_JOY = 32, RF_IMU = 64,
       RF_RECORDING = 128, RF_PLAYING = 256, RF_JOY_OVERRIDE = 512, RF_REST = 1024, RF_MENU_EDIT = 2048 };

// Asked of the platform: the board's USB MIDI (usb.c) or the twin's stdout.
bool     rig_port_send(const uint8_t *m, int n);       // a whole SysEx message; false: not sent (it is dropped)
uint32_t rig_port_ms(void);

void rig_init(void);
bool rig_sysex(uint8_t cmd, const uint8_t *body, int n);   // one of F0 7D <cmd> ... F7; true if it was the rig's
void rig_tick(int dt_ms);                                  // every loop tick: keepalive, the table, events, the screen

bool rig_motion_on(void);
bool rig_state_on(void);
void rig_motion(const int16_t (*raw)[6], int n, uint32_t frames, int ranges, int flags);   // r: 1 injected, 2 overflow, 4 gyro
void rig_state(const float *v, uint32_t frames);           // RS_N values
bool rig_screen_on(void);
void rig_screen(const uint8_t *fb);                        // the OLED's 8 pages of 128 bytes, after a flush
int  rig_inject(int16_t (*raw)[6], int max);               // injected samples since the last call; -1 when not injecting

typedef struct { int counts; int sw, rec; int jx, jy; } rig_keys_t;   // sw, rec, jx, jy: -1 = not overridden
void rig_keys(rig_keys_t *k, int steps, int dir);          // what the PC presses and turns (counts are consumed)
void rig_event(const char *name, float v, int ms, const char *text);  // a gesture fired (gest_on_event)
uint32_t rig_drops(void);                                  // messages that did not fit on the wire
