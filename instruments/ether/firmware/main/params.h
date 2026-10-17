// The parameter table at run time: the instrument's params (0 .. INST.nparams-1) followed by the core's
// (LOOP when INST.looper, SYSTEM). Formatting, stepping, clamping, MIDI CC scaling, presets and the
// tag/value packing that flash and the user sounds use. No device code: tools/sim builds it as is.
#pragma once
#include "core.h"

// the core's own params, at index NI + CP_*
enum {
    CP_LOOP_LEVEL, CP_LOOP_KEEP, CP_LOOP_CLEAR,
    CP_VOLUME, CP_MIC, CP_TILT_SETUP, CP_MOTION, CP_JOY, CP_JOY_TURN, CP_MIDI_CH, CP_MIDI_OUT, CP_LED, CP_HINTS, CP_SAVER,
    CP_ABOUT, CP_RESET,
    CP_N
};
#define GROUP_LOOP   0xFE          // param_t.group of the core's params
#define GROUP_SYSTEM 0xFF

extern int NI, NP;                 // instrument params, all params
#define CP(i) (NI + (i))
const param_t *param(int p);
void  params_init(void);           // builds the table from INST, sets every param to its default
void  params_defaults(bool sound_only);
float param_clamp(int p, float v);
float param_step(int p, float v, int detents);      // the value after turning; clamped
int   param_to_cc(int p);                           // 0..127
float param_from_cc(int p, int cc);
int   param_cc_number(int p);                       // the CC that carries p, or -1 (20 + p for p < 44, then 70..)
int   param_by_cc(int cc);                          // the param on that CC, or -1
int   param_by_tag(const char *tag);                // or -1
bool  param_in_sound(int p);                        // part of a preset (an instrument param, not PF_SYS, not PK_ACT)

// Presets: "tag=value tag=value". Sound params not named go to their defaults. Returns the number of
// names that were not recognised (0 for a good preset).
int   preset_apply(const char *set);

// Packing for flash: 8 bytes per param, the tag then the float. Unknown tags are skipped on unpack and
// missing ones keep their value, so a blob from an older or newer firmware still loads.
int   params_pack(uint8_t *buf, int cap, bool sound_only);   // bytes written
int   params_unpack(const uint8_t *buf, int n, bool sound_only);   // params set

// Sound snapshot: what the loaded sound set, to show '*' once something is edited.
void  params_snapshot(void);
bool  params_edited(void);
