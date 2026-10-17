#include "params.h"
#include "font.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

float pv[PARAMS_MAX];
int NI, NP;
static UI_RAM float snap_vals[PARAMS_MAX];

static const char *const on_off[] = { "OFF", "ON" };
static const char *const joy_l[] = { "OFF", "AUTO" };
static const char *const turn_l[] = { "AS BUILT", "90 RIGHT", "UPSIDE DN", "90 LEFT" };
static const char *const led_l[] = { "OFF", "LOW", "HIGH" };
static const char *const saver_l[] = { "OFF", "1 MIN", "3 MIN", "10 MIN" };
static const char *const reset_l[] = { "NO", "YES" };

#define SYS (PF_SYS | PF_NOCC)
static const param_t core_params[CP_N] = {
    [CP_LOOP_LEVEL] = { "LOOP LEVEL", "lplv", GROUP_LOOP, PK_NUM, PF_INT | PF_SYS, 0, 100, 5, 80, "%", NULL, "HOW LOUD THE LOOP PLAYS" },
    [CP_LOOP_KEEP]  = { "LOOP KEEP", "lpkp", GROUP_LOOP, PK_NUM, PF_INT | PF_SYS, 0, 100, 5, 100, "%", NULL, "LESS: IT FADES EACH PASS" },
    [CP_LOOP_CLEAR] = { "CLEAR LOOP", "lpcl", GROUP_LOOP, PK_ACT, SYS, 0, 0, 0, 0, "", NULL, "PRESS TO ERASE IT" },
    [CP_VOLUME]     = { "VOLUME", "vol ", GROUP_SYSTEM, PK_NUM, PF_INT | PF_SYS, 0, 150, 5, 100, "%", NULL, "OVER 100%: LIMITER WORKS" },
    [CP_MIC]        = { "MIC GAIN", "mic ", GROUP_SYSTEM, PK_NUM, PF_INT | SYS, 0, 36, 3, 12, "dB", NULL, "HOW HARD THE MIC LISTENS" },
    [CP_TILT_SETUP] = { "TILT SETUP", "tlst", GROUP_SYSTEM, PK_ACT, SYS, 0, 0, 0, 0, "", NULL, "WHICH WAY IS DOWN" },
    [CP_MOTION]     = { "MOTION", "mot ", GROUP_SYSTEM, PK_ENUM, SYS, 0, 1, 1, 1, "", on_off, "OFF: TILT IS IGNORED" },
    [CP_JOY]        = { "JOYSTICK", "joy ", GROUP_SYSTEM, PK_ENUM, SYS, 0, 1, 1, 1, "", joy_l, "AUTO: USED IF FITTED" },
    [CP_JOY_TURN]   = { "JOY TURN", "joyt", GROUP_SYSTEM, PK_ENUM, SYS, 0, 3, 1, 0, "", turn_l, "IF THE STICK IS TURNED" },
    [CP_MIDI_CH]    = { "MIDI CH", "mch ", GROUP_SYSTEM, PK_NUM, PF_INT | SYS, 1, 16, 1, 1, "", NULL, "CHANNEL FOR NOTES AND CC" },
    [CP_MIDI_OUT]   = { "MIDI CC OUT", "mcc ", GROUP_SYSTEM, PK_ENUM, SYS, 0, 1, 1, 1, "", on_off, "SEND TILT AND STICK AS CC" },
    [CP_LED]        = { "LED", "led ", GROUP_SYSTEM, PK_ENUM, SYS, 0, 2, 1, 1, "", led_l, "THE BOARD'S COLOUR LIGHT" },
    [CP_HINTS]      = { "HINTS", "hint", GROUP_SYSTEM, PK_ENUM, SYS, 0, 1, 1, 1, "", on_off, "ON: THE TIPS COME BACK" },
    [CP_SAVER]      = { "SAVER", "svr ", GROUP_SYSTEM, PK_ENUM, SYS, 0, 3, 1, 2, "", saver_l, "DARK WHEN LEFT ALONE" },
    [CP_ABOUT]      = { "ABOUT", "abut", GROUP_SYSTEM, PK_ACT, SYS, 0, 0, 0, 0, "", NULL, "VERSION, CPU, MEMORY" },
    [CP_RESET]      = { "RESET ALL", "rset", GROUP_SYSTEM, PK_ENUM, SYS, 0, 1, 1, 0, "", reset_l, "TURN TO YES, THEN PRESS" },
};

const param_t *param(int p)
{
    if (p < NI) return &INST.params[p];
    return &core_params[p - NI];
}

void params_init(void)
{
    NI = INST.nparams;
    NP = NI + CP_N;
    params_defaults(false);
    params_snapshot();
}

void params_defaults(bool sound_only)
{
    for (int p = 0; p < NP; p++) if (!sound_only || param_in_sound(p)) pv[p] = param(p)->def;
}

bool param_in_sound(int p) { return p < NI && param(p)->kind != PK_ACT && !(param(p)->flags & PF_SYS); }

float param_clamp(int p, float v)
{
    const param_t *d = param(p);
    if (d->kind == PK_ACT) return 0;
    if (!(v >= d->min)) v = d->min;              // NaN lands on min
    if (v > d->max) v = d->max;
    if (d->kind == PK_ENUM || (d->flags & PF_INT)) v = roundf(v);
    return v;
}

float param_step(int p, float v, int det)
{
    const param_t *d = param(p);
    if (d->kind == PK_ACT) return 0;
    if (d->kind == PK_ENUM) return param_clamp(p, v + (float)det);
    if (d->flags & PF_LOG) {
        float r = powf(d->step, (float)det);
        v *= r;
        float m = powf(10.0f, floorf(log10f(fabsf(v) + 1e-9f)) - 2.0f);   // three significant digits
        v = roundf(v / m) * m;
        return param_clamp(p, v);
    }
    v += d->step * (float)det;
    v = d->min + roundf((v - d->min) / d->step) * d->step;
    return param_clamp(p, v);
}

int param_to_cc(int p)
{
    const param_t *d = param(p);
    float t;
    if (d->max <= d->min) return 0;
    if (d->flags & PF_LOG) t = logf(pv[p] / d->min) / logf(d->max / d->min);
    else t = (pv[p] - d->min) / (d->max - d->min);
    int c = (int)lroundf(t * 127.0f);
    return c < 0 ? 0 : c > 127 ? 127 : c;
}

float param_from_cc(int p, int cc)
{
    const param_t *d = param(p);
    float t = (float)cc / 127.0f, v;
    if (d->flags & PF_LOG) v = d->min * powf(d->max / d->min, t);
    else v = d->min + t * (d->max - d->min);
    if (d->kind == PK_NUM && !(d->flags & (PF_INT | PF_LOG)) && d->step > 0) v = d->min + roundf((v - d->min) / d->step) * d->step;
    return param_clamp(p, v);
}

int param_cc_number(int p)
{
    if (p >= NI || (param(p)->flags & PF_NOCC) || param(p)->kind == PK_ACT) return -1;
    if (p < 44) return 20 + p;                   // CC 20..63
    if (p < 44 + 50) return 70 + (p - 44);       // CC 70..119 (64..69 are pedals)
    return -1;
}

int param_by_cc(int cc)
{
    int p = cc >= 20 && cc < 64 ? cc - 20 : cc >= 70 && cc < 120 ? cc - 70 + 44 : -1;
    return p >= 0 && p < NI && param_cc_number(p) == cc ? p : -1;
}

int param_by_tag(const char *tag)
{
    for (int p = 0; p < NP; p++) if (!strncmp(param(p)->tag, tag, 4)) return p;
    return -1;
}

static void trim(char *b)
{
    char *e = strchr(b, '.');
    if (!e) return;
    char *z = b + strlen(b) - 1;
    while (z > e && *z == '0') *z-- = 0;
    if (z == e) *z = 0;
}

const char *param_text(int p, char *b, int n)
{
    const param_t *d = param(p);
    float v = pv[p];
    if (d->kind == PK_ACT) { snprintf(b, (size_t)n, S_RIGHT); return b; }
    if (d->kind == PK_ENUM) { int i = (int)(v - d->min); snprintf(b, (size_t)n, "%s", d->labels ? d->labels[i] : "?"); return b; }
    const char *u = d->unit ? d->unit : "";
    char num[24];
    const char *sign = (d->flags & PF_SIGNED) && v > 0 ? "+" : "";
    if (!strcmp(u, "s") && fabsf(v) < 1.0f) { snprintf(b, (size_t)n, "%s%d ms", sign, (int)lroundf(v * 1000.0f)); return b; }
    if (!strcmp(u, "Hz") && fabsf(v) >= 1000.0f) { snprintf(num, sizeof num, "%.2f", (double)(v / 1000.0f)); trim(num); snprintf(b, (size_t)n, "%s%s kHz", sign, num); return b; }
    if (d->flags & PF_INT) snprintf(num, sizeof num, "%d", (int)lroundf(v));
    else {
        float a = fabsf(v);
        snprintf(num, sizeof num, a >= 100.0f ? "%.0f" : a >= 10.0f ? "%.1f" : "%.2f", (double)v);
        trim(num);
    }
    bool tight = !strcmp(u, "%") || !strcmp(u, "x") || !*u;
    snprintf(b, (size_t)n, "%s%s%s%s", sign, num, tight ? "" : " ", u);
    return b;
}

int preset_apply(const char *set)
{
    params_defaults(true);
    int bad = 0;
    const char *s = set;
    while (s && *s) {
        while (*s == ' ') s++;
        if (!*s) break;
        const char *eq = strchr(s, '=');
        if (!eq) { bad++; break; }
        char tag[5] = "    ";
        int tl = (int)(eq - s);
        for (int i = 0; i < 4 && i < tl; i++) tag[i] = s[i];     // tags shorter than 4 are padded with spaces
        char *end;
        float v = strtof(eq + 1, &end);
        int p = param_by_tag(tag);
        if (p < 0 || !param_in_sound(p) || end == eq + 1) bad++;
        else pv[p] = param_clamp(p, v);
        s = end;
        while (*s && *s != ' ') s++;
    }
    return bad;
}

int params_pack(uint8_t *buf, int cap, bool sound_only)
{
    int n = 0;
    for (int p = 0; p < NP; p++) {
        if (param(p)->kind == PK_ACT) continue;
        if (sound_only && !param_in_sound(p)) continue;
        if (n + 8 > cap) break;
        memcpy(buf + n, param(p)->tag, 4);
        memcpy(buf + n + 4, &pv[p], 4);
        n += 8;
    }
    return n;
}

int params_unpack(const uint8_t *buf, int n, bool sound_only)
{
    int set = 0;
    for (int i = 0; i + 8 <= n; i += 8) {
        char tag[5]; memcpy(tag, buf + i, 4); tag[4] = 0;
        int p = param_by_tag(tag);
        if (p < 0 || param(p)->kind == PK_ACT || (sound_only && !param_in_sound(p))) continue;
        if (p == CP(CP_RESET)) continue;                   // never load a pending reset
        float v; memcpy(&v, buf + i + 4, 4);
        pv[p] = param_clamp(p, v);
        set++;
    }
    return set;
}

void params_snapshot(void) { memcpy(snap_vals, pv, sizeof snap_vals); }
bool params_edited(void)
{
    for (int p = 0; p < NI; p++) if (param_in_sound(p) && pv[p] != snap_vals[p]) return true;
    return false;
}
