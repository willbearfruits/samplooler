// The tuning table: every number behind the controls (IMU, motion, tilt, roll, joystick, encoder, REC, mic, USB),
// changeable while it plays (the rig's SysEx 'T', rig/PROTOCOL.md) and kept in NVS ("tune", apart from the settings).
// The defaults are the firmware as it was before the table (preset v1): with them nothing plays differently.
// Pure C: tools and rig/twin build it on the PC (the NVS part is ESP only).
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { TK_FLOAT, TK_INT, TK_ENUM, TK_BOOL, TK_ACTION };

// id, name, kind, min, max, step, default, unit, labels (enums: "a|b|c" from min), help
#define TUNE_TABLE(X) \
X(IMU_ARANGE,  "imu.acc_range",   TK_ENUM,   0,     3,    1,     0,    "",    "2 g|4 g|8 g|16 g", "accelerometer full scale: smaller reads tilt finer, larger survives knocks") \
X(IMU_GRANGE,  "imu.gyro_range",  TK_ENUM,   0,     3,    1,     2,    "deg/s", "250|500|1000|2000", "gyro full scale: a fast flick reaches 1000 deg/s") \
X(IMU_DLPF,    "imu.dlpf",        TK_ENUM,   0,     6,    1,     3,    "Hz",  "260|184|94|44|21|10|5", "the sensor's own low-pass: lower is calmer and slower") \
X(IMU_RATE,    "imu.rate",        TK_ENUM,   0,     3,    1,     2,    "Hz",  "100|250|500|1000", "samples per second read from the sensor's FIFO") \
X(IMU_AOFF_X,  "imu.acc_off_x",   TK_FLOAT, -0.25f, 0.25f, 0.001f, 0,  "g",   "", "accelerometer offset, the chip's x (subtracted)") \
X(IMU_AOFF_Y,  "imu.acc_off_y",   TK_FLOAT, -0.25f, 0.25f, 0.001f, 0,  "g",   "", "accelerometer offset, the chip's y") \
X(IMU_AOFF_Z,  "imu.acc_off_z",   TK_FLOAT, -0.25f, 0.25f, 0.001f, 0,  "g",   "", "accelerometer offset, the chip's z") \
X(IMU_GBIAS_X, "imu.gyro_bias_x", TK_FLOAT, -30,   30,    0.01f, 0,    "deg/s", "", "gyro bias, the chip's x (subtracted, on top of the learned bias)") \
X(IMU_GBIAS_Y, "imu.gyro_bias_y", TK_FLOAT, -30,   30,    0.01f, 0,    "deg/s", "", "gyro bias, the chip's y") \
X(IMU_GBIAS_Z, "imu.gyro_bias_z", TK_FLOAT, -30,   30,    0.01f, 0,    "deg/s", "", "gyro bias, the chip's z") \
X(IMU_AUTOBIAS,"imu.auto_bias",   TK_BOOL,   0,     1,    1,     1,    "",    "off|on", "learn the gyro's bias whenever the board lies still") \
X(IMU_REST_DPS,"imu.rest_gyro",   TK_FLOAT,  0.2f, 10,    0.1f,  3,    "deg/s", "", "still: the gyro stays under this") \
X(IMU_REST_G,  "imu.rest_acc",    TK_FLOAT,  0.005f, 0.2f, 0.005f, 0.04f, "g", "", "still: the acceleration stays this close to 1 g") \
X(IMU_REST_MS, "imu.rest_time",   TK_INT,    100,  5000,  100,   800,  "ms",  "", "still for this long before the bias is learned") \
X(IMU_BIAS_TAU,"imu.bias_tau",    TK_FLOAT,  0.5f, 60,    0.5f,  4,    "s",   "", "how slowly the learned bias follows") \
X(IMU_BIAS_KEEP,"imu.bias_keep",  TK_ACTION, 0,     1,    1,     0,    "",    "", "copy the learned bias into imu.gyro_bias_* (then save)") \
X(MO_FUSION,   "motion.fusion",   TK_ENUM,   0,     2,    1,     0,    "",    "ACC|COMP|MAHONY", "ACC: tilt from gravity alone (v1); COMP: gyro steered by gravity; MAHONY: COMP with a learned drift term") \
X(MO_RATE,     "motion.rate",     TK_ENUM,   0,     1,    1,     0,    "Hz",  "50|100", "how often the controls follow the motion") \
X(MO_ACC_SMOOTH,"motion.acc_smooth", TK_FLOAT, 0.01f, 1,  0.01f, 0.25f, "",   "", "ACC: one-pole step per control tick (1 = raw)") \
X(MO_COMP_TAU, "motion.comp_tau", TK_FLOAT,  0.05f, 10,   0.05f, 0.6f, "s",   "", "COMP: how long gravity takes to pull the gyro's angle back") \
X(MO_KP,       "motion.kp",       TK_FLOAT,  0,     20,   0.1f,  2,    "",    "", "MAHONY: proportional gain towards gravity") \
X(MO_KI,       "motion.ki",       TK_FLOAT,  0,     2,    0.01f, 0.05f, "",   "", "MAHONY: integral gain (learns the gyro's drift while moving)") \
X(MO_ACC_GATE, "motion.acc_gate", TK_FLOAT,  0,     1,    0.01f, 0.2f, "g",   "", "COMP/MAHONY: trust gravity less as |a| leaves 1 g, not at all past this (0: always)") \
X(MO_AX_X,     "motion.ax_x",     TK_ENUM,   0,     5,    1,     3,    "",    "+x|-x|+y|-y|+z|-z", "the chip axis behind the screen's x (to the right edge)") \
X(MO_AX_Y,     "motion.ax_y",     TK_ENUM,   0,     5,    1,     0,    "",    "+x|-x|+y|-y|+z|-z", "the chip axis behind the screen's y (to the bottom edge)") \
X(MO_AX_Z,     "motion.ax_z",     TK_ENUM,   0,     5,    1,     4,    "",    "+x|-x|+y|-y|+z|-z", "the chip axis behind the screen's z (out of the screen)") \
X(MO_YAW_RESET,"motion.yaw_reset",TK_ACTION, 0,     1,    1,     0,    "",    "", "turn the yaw back to 0 (it has no compass and drifts)") \
X(TI_AXIS,     "tilt.axis",       TK_ENUM,   0,     1,    1,     0,    "",    "pitch|roll", "which lean sets the speed") \
X(TI_SIGN,     "tilt.sign",       TK_ENUM,   0,     1,    1,     1,    "",    "-1|+1", "+1: nose down plays forward") \
X(TI_DEAD,     "tilt.dead",       TK_FLOAT,  0,     30,   0.5f,  5,    "deg",   "", "no speed change inside this angle") \
X(TI_FULL,     "tilt.full",       TK_FLOAT,  10,    90,   1,     45,   "deg",   "", "Speed max at this angle") \
X(TI_CURVE,    "tilt.curve",      TK_FLOAT,  0,     1,    0.05f, 1,    "",    "", "0: straight; 1: the soft start (v1)") \
X(TI_HYST,     "tilt.hyst",       TK_FLOAT,  0,     5,    0.1f,  0,    "deg",   "", "leaving the dead zone needs this much more") \
X(TI_DET,      "tilt.detents",    TK_ENUM,   0,     3,    1,     0,    "",    "off|1x|1x 2x|octaves", "speeds the tilt sticks to") \
X(TI_DET_W,    "tilt.detent_w",   TK_FLOAT,  0.01f, 0.4f, 0.01f, 0.12f, "oct", "", "how wide a detent holds, in octaves of speed") \
X(TI_SLEW,     "tilt.slew",       TK_FLOAT,  1,     500,  1,     50,   "ms",  "", "the tape's glide to a new speed") \
X(RO_ON,       "roll.on",         TK_BOOL,   0,     1,    1,     1,    "",    "off|on", "roll left for the delay, right for the stutter") \
X(RO_SIGN,     "roll.sign",       TK_ENUM,   0,     1,    1,     1,    "",    "-1|+1", "-1 swaps delay and stutter") \
X(RO_DEAD,     "roll.dead",       TK_FLOAT,  0,     45,   0.5f,  15,   "deg",   "", "no effect inside this angle") \
X(RO_FULL,     "roll.full",       TK_FLOAT,  10,    90,   1,     45,   "deg",   "", "full effect at this angle") \
X(JO_ON,       "joy.on",          TK_BOOL,   0,     1,    1,     0,    "",    "off|on", "read the joystick (rev C has one; the firmware ships with it off)") \
X(JO_DEAD,     "joy.dead",        TK_FLOAT,  0,     0.5f, 0.01f, 0.12f, "",   "", "no movement inside this share of the travel") \
X(JO_SMOOTH,   "joy.smooth",      TK_FLOAT,  0.01f, 1,    0.01f, 0.3f, "",    "", "one-pole step per tick (1 = raw)") \
X(JO_ROT,      "joy.rotate",      TK_ENUM,   0,     3,    1,     1,    "deg",   "0|90|180|270", "how the module sits on the board") \
X(JO_INV_X,    "joy.invert_x",    TK_BOOL,   0,     1,    1,     0,    "",    "off|on", "") \
X(JO_INV_Y,    "joy.invert_y",    TK_BOOL,   0,     1,    1,     0,    "",    "off|on", "") \
X(JO_FLICK,    "joy.flick",       TK_FLOAT,  0.3f,  1,    0.05f, 0.7f, "",    "", "a flick fires past this") \
X(JO_REARM,    "joy.rearm",       TK_FLOAT,  0,     0.6f, 0.05f, 0.3f, "",    "", "and is ready again back inside this") \
X(JO_PITCH,    "joy.pitch_st",    TK_FLOAT,  0,     24,   1,     12,   "st",  "", "X bends the pitch this far in GRAIN") \
X(JO_SPEED,    "joy.speed",       TK_ENUM,   0,     1,    1,     0,    "",    "tilt off|never", "Y sets the speed when Tilt is off") \
X(JO_CENTRE,   "joy.centre",      TK_ACTION, 0,     1,    1,     0,    "",    "", "take the joystick's rest position as its centre now") \
X(EN_DIR,      "enc.dir",         TK_ENUM,   0,     1,    1,     0,    "",    "-1|+1", "which way a turn counts") \
X(EN_STEPS,    "enc.steps",       TK_INT,    1,     8,    1,     4,    "",    "", "counts per detent") \
X(EN_GLITCH,   "enc.glitch",      TK_INT,    0,     12000, 100,  1000, "ns",  "", "pulses shorter than this are contact bounce") \
X(EN_TAP_MIN,  "enc.tap_min",     TK_INT,    0,     200,  5,     40,   "ms",  "", "a press shorter than this is bounce") \
X(EN_HOLD,     "enc.hold",        TK_INT,    150,   2000, 10,    500,  "ms",  "", "shorter is a tap, longer a hold") \
X(EN_LONG,     "enc.long",        TK_INT,    1000,  10000, 100,  3000, "ms",  "", "a long press fires while still down") \
X(EN_DOUBLE,   "enc.double",      TK_INT,    0,     800,  10,    300,  "ms",  "", "a second tap within this puts the knob on the Volume") \
X(EN_ACCEL,    "enc.accel",       TK_FLOAT,  0,     4,    0.1f,  0,    "",    "", "fast turns count more (0: off)") \
X(EN_VOL_MS,   "enc.vol_ms",      TK_INT,    500,   10000, 100,  3000, "ms",  "", "the knob leaves the Volume after resting this long") \
X(RE_DEB,      "rec.debounce",    TK_INT,    1,     10,   1,     3,    "ticks", "", "REC must agree this many 10 ms ticks in a row") \
X(RE_RESET,    "rec.reset_ms",    TK_INT,    1000,  10000, 100,  3000, "ms",  "", "REC held from power-on this long, then let go: factory reset") \
X(RE_STUCK,    "rec.stuck_ms",    TK_INT,    3000,  20000, 500,  6000, "ms",  "", "REC still down this long from power-on is taken as stuck") \
X(MI_HPF,      "mic.hpf",         TK_FLOAT,  20,    400,  5,     80,   "Hz",  "", "the mic's high-pass (rumble, handling noise)") \
X(MI_SLOT,     "mic.slot",        TK_ENUM,   0,     2,    1,     0,    "",    "auto|L|R", "which I2S slot carries the mic") \
X(MI_TRIM,     "mic.trim",        TK_FLOAT, -12,    12,   0.5f,  0,    "dB",  "", "added to Mic gain") \
X(US_OUT,      "usb.out",         TK_ENUM,   0,     1,    1,     0,    "",    "mix|mix + raw mic", "what the board sends to the computer (right channel)") \
X(RG_FPS,      "rig.screen_fps",  TK_INT,    1,     20,   1,     10,   "fps", "", "screen frames a second to the rig")

#define TUNE_ID(id, ...) TU_##id,
enum { TUNE_TABLE(TUNE_ID) TU_N };
#undef TUNE_ID

typedef struct {
    const char *name, *unit, *labels, *help;
    uint8_t kind;
    float min, max, step, def;
} tune_def_t;

extern const tune_def_t tune_def[TU_N];
extern float tune_v[TU_N];

static inline float TV(int i) { return tune_v[i]; }
static inline int   TI(int i) { return (int)tune_v[i]; }

void     tune_defaults(void);
float    tune_set(int i, float v);              // clamped and snapped to the step; returns what is kept
uint32_t tune_gen(void);                        // counts changes: anything that caches a value re-reads when it moves
int      tune_find(const char *name);           // -1 if none
int      tune_line(int i, char *b, int n);      // the 't' description line: "i=… n=… k=… lo=… hi=… st=… df=… v=… u=… l=… h=…"
int      tune_presets(void);
const char *tune_preset_name(int p);
void     tune_preset(int p);                    // 0 = v1 (the defaults)
void     tune_on_action(void (*fn)(int id));    // who runs the actions (main.c)
bool     tune_dirty(void);                      // changed since the last save
// NVS (device only): pairs of (name hash, value), so an older or newer table still loads what it knows
void     tune_load(void);
bool     tune_save(void);
void     tune_erase(void);                      // the factory reset: back to v1 at the next boot
