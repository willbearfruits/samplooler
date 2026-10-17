// See front.h. The screens, top to bottom of front_draw(): screensaver, splash, tilt setup, ABOUT, the menu
// list; otherwise the instrument's scene with the core's strips over its bottom 18 rows (an edit, the sound
// card, a hint, a boot note) and a toast on top.
#include "front.h"
#include "params.h"
#include "looper.h"
#include "calib.h"
#include "dsp.h"
#include "ui.h"
#include "oled.h"
#include <math.h>
#include <stdarg.h>
#ifdef ESP_PLATFORM
#include "esp_attr.h"                // front_audio() runs from IRAM (flash through the cache is far slower)
#else
#define IRAM_ATTR
#endif
#include <stdio.h>
#include <string.h>

#define HOLD_MS     600         // the knob held this long: back to play, or freeze
#define STRIP_Y     46          // the bottom strip: rows 46..63
#define SOUND_ID_USER 100
#define N_USER      8
#define NAME_LEN    12          // a user sound's blob starts with its name
#define BLOB_MAX    (NAME_LEN + PARAMS_MAX * 8)

front_meta_t meta = { .axis = { 1, -2, 3 } };      // a right-handed IMU, x right and z out of the screen: its y points up
static front_status_t status;
static bool   dirty, has_imu_hw, frozen;
static uint32_t ticks;
static float  t_now;

// ---------------------------------------------------------------- motion
static vec3_t acc_s = { 0, 0, 1 }, gyro_s, burst_s[64];
static vec3_t raw_avg = { 0, 0, 1 };
static float  lean_x, lean_y;
static ctl_t  ctl;

static float pick(const vec3_t *v, int code)
{
    float c = code == 1 || code == -1 ? v->x : code == 2 || code == -2 ? v->y : v->z;
    return code < 0 ? -c : c;
}
static vec3_t to_screen(const vec3_t *v) { return (vec3_t){ pick(v, meta.axis[0]), pick(v, meta.axis[1]), pick(v, meta.axis[2]) }; }

// ---------------------------------------------------------------- overlays
static char   toast_s[48];  static int toast_ms;
static char   note_s[40];   static int note_ms;
static int    card_ms;                         // the sound card after a turn in play
static int    quick_ms, quick_p = -1;          // a quick edit in play
static int    hint_ms, hint_gap_ms = 1500, hint_i = -1;  // -1: the first hint shown is hints[0]
static int    idle_ms, saver_idle_ms;
static bool   saver;
static int    strip_kind, strip_anim;          // which strip is up and how far it has slid in (frames)
static uint32_t frame;
static float  sel_y_anim = -1;

void toast(const char *s, int ms) { snprintf(toast_s, sizeof toast_s, "%s", s); toast_ms = ms; }
void ui_awake(void) { saver_idle_ms = 0; }
void front_note(const char *s) { snprintf(note_s, sizeof note_s, "%s", s); note_ms = 4500; }

void coach_done(int learn)
{
    if (learn < 0 || learn > 31) return;
    uint32_t b = 1u << learn;
    if (meta.learned & b) return;
    meta.learned |= b; dirty = true;
    if (hint_ms > 0 && hint_i < INST.nhints && INST.hints[hint_i].learn == learn) { hint_ms = 0; hint_gap_ms = 2500; }
}

// ---------------------------------------------------------------- LED and MIDI for the instruments
static uint8_t led_r, led_g, led_b, led_sent[3] = { 1, 1, 1 };
static void led_apply(void)
{
    static const float k[3] = { 0.0f, 0.12f, 0.5f };
    float s = k[(int)pv[CP(CP_LED)]];
    uint8_t o[3] = { (uint8_t)(led_r * s), (uint8_t)(led_g * s), (uint8_t)(led_b * s) };
    if (memcmp(o, led_sent, 3)) { memcpy(led_sent, o, 3); plat_led(o[0], o[1], o[2]); }
}
void led_rgb(uint8_t r, uint8_t g, uint8_t b) { led_r = r; led_g = g; led_b = b; led_apply(); }

static int ch(void) { return ((int)pv[CP(CP_MIDI_CH)] - 1) & 15; }
void midi_note_ch(int c, int note, int vel)
{
    if (note < 0 || note > 127) return;
    vel = vel < 0 ? 0 : vel > 127 ? 127 : vel;
    plat_midi3((uint8_t)((vel ? 0x90 : 0x80) | ((c - 1) & 15)), (uint8_t)note, (uint8_t)vel);
}
void midi_note(int note, int vel) { midi_note_ch(ch() + 1, note, vel); }
void midi_bend(int v)
{
    v = v < 0 ? 0 : v > 16383 ? 16383 : v;
    plat_midi3((uint8_t)(0xE0 | ch()), (uint8_t)(v & 0x7F), (uint8_t)(v >> 7));
}
void midi_cc(int cc, int val)
{
    if (cc < 0 || cc > 119) return;
    val = val < 0 ? 0 : val > 127 ? 127 : val;
    plat_midi3((uint8_t)(0xB0 | ch()), (uint8_t)cc, (uint8_t)val);
}

// ---------------------------------------------------------------- params
static void on_param(int p, float old)
{
    if (p == CP(CP_MIC)) plat_mic_gain((int)pv[p]);
    if (p == CP(CP_HINTS) && pv[p] >= 1.0f && old < 1.0f) { meta.learned = 0; hint_gap_ms = 800; }
    if (p == CP(CP_LED)) led_apply();
}

static void set_param(int p, float v, bool echo)
{
    if (p < 0 || p >= NP) return;
    float o = pv[p];
    v = param_clamp(p, v);
    if (v == o) return;
    pv[p] = v; dirty = true;
    on_param(p, o);
    if (p < NI && INST.changed) INST.changed(p);
    int cc = param_cc_number(p);
    if (echo && cc >= 0) plat_midi3((uint8_t)(0xB0 | ch()), (uint8_t)cc, (uint8_t)param_to_cc(p));
}
void param_set(int p, float v) { set_param(p, v, true); }

// ---------------------------------------------------------------- sounds
static UI_RAM float stash[PARAMS_MAX];
static bool  stash_ok;
static UI_RAM char  user_name[N_USER][NAME_LEN + 1];

static int sound_list(int16_t *ids)
{
    int n = 0;
    if (stash_ok) ids[n++] = -1;
    for (int i = 0; i < INST.npresets; i++) ids[n++] = (int16_t)i;
    for (int k = 0; k < N_USER; k++) if (meta.user_mask & (1u << k)) ids[n++] = (int16_t)(SOUND_ID_USER + k);
    return n;
}
static int sound_pos(const int16_t *ids, int n, int16_t id) { for (int i = 0; i < n; i++) if (ids[i] == id) return i; return 0; }

static const char *sound_name(int16_t id)
{
    if (id == -1) return "MY EDIT";
    if (id >= SOUND_ID_USER && id < SOUND_ID_USER + N_USER) return user_name[id - SOUND_ID_USER];
    if (id >= 0 && id < INST.npresets) return INST.presets[id].name;
    return "?";
}

static void program_out(int16_t id)
{
    int pc = id >= SOUND_ID_USER ? INST.npresets + (id - SOUND_ID_USER) : id;
    if (pc >= 0 && pc < 128) plat_midi3((uint8_t)(0xC0 | ch()), (uint8_t)pc, 0);
}

static bool load_sound(int16_t id, bool announce)
{
    if (id == -1) {
        if (!stash_ok) return false;
        for (int p = 0; p < NI; p++) if (param_in_sound(p)) pv[p] = stash[p];
    } else if (id >= SOUND_ID_USER) {
        static UI_RAM uint8_t blob[BLOB_MAX];
        int k = id - SOUND_ID_USER, n = plat_load_user(k, blob, sizeof blob);
        if (n <= NAME_LEN) { meta.user_mask &= (uint8_t)~(1u << k); dirty = true; return false; }
        params_defaults(true);
        params_unpack(blob + NAME_LEN, n - NAME_LEN, true);
    } else if (id >= 0 && id < INST.npresets) {
        int bad = preset_apply(INST.presets[id].set);
        if (bad) app_log("sound %s: %d names not known", INST.presets[id].name, bad);
    } else return false;
    meta.sound = id; dirty = true;
    params_snapshot();
    if (INST.changed) INST.changed(-1);
    if (announce) program_out(id);
    return true;
}

static void browse(int d)
{
    if (params_edited()) {                              // keep the edit reachable: it becomes MY EDIT
        for (int p = 0; p < NI; p++) stash[p] = pv[p];
        stash_ok = true;
        if (meta.sound == -1) params_snapshot();
    }
    int16_t ids[2 + 64 + N_USER];
    int n = sound_list(ids), i = sound_pos(ids, n, meta.sound), j = i + d;
    if (j < 0) j = 0;
    if (j > n - 1) j = n - 1;
    if (j != i) load_sound(ids[j], true);
}

static void save_user(int k)
{
    static UI_RAM uint8_t blob[BLOB_MAX];
    char nm[NAME_LEN + 1];
    const char *base = sound_name(meta.sound);
    if (meta.sound >= SOUND_ID_USER || meta.sound == -1) base = meta.sound == -1 ? "EDIT" : base + 3;
    snprintf(nm, sizeof nm, "U%d %s", k + 1, base);
    memset(blob, 0, NAME_LEN);
    memcpy(blob, nm, strlen(nm));
    int n = NAME_LEN + params_pack(blob + NAME_LEN, BLOB_MAX - NAME_LEN, true);
    char t[32];
    if (!plat_save_user(k, blob, n)) { toast("SAVE FAILED", 1500); return; }
    memcpy(user_name[k], nm, sizeof nm);
    meta.user_mask |= (uint8_t)(1u << k);
    meta.sound = (int16_t)(SOUND_ID_USER + k); dirty = true;
    params_snapshot();
    snprintf(t, sizeof t, "SAVED AS USER %d", k + 1);
    toast(t, 1500);
}

static void load_user_names(void)
{
    static UI_RAM uint8_t blob[BLOB_MAX];
    for (int k = 0; k < N_USER; k++) {
        if (!(meta.user_mask & (1u << k))) continue;
        int n = plat_load_user(k, blob, sizeof blob);
        if (n <= NAME_LEN) { meta.user_mask &= (uint8_t)~(1u << k); continue; }
        memcpy(user_name[k], blob, NAME_LEN); user_name[k][NAME_LEN] = 0;
    }
}

// ---------------------------------------------------------------- the menu
enum { SND_SOUND = -10, SND_SAVE = -11, SND_REVERT = -12, ITEM_BACK = -20 };
#define G_SOUND (-1)
static int groups[40], ngroups;                 // level 1: G_SOUND, instrument groups, GROUP_LOOP, GROUP_SYSTEM
static int items[PARAMS_MAX + 4], nitems;      // level 2: ITEM_BACK, then params or SND_*
static int mlev, sel1, sel2, top1, top2;       // level: 0 closed, 1 pages, 2 items, 3 editing
static int pick_k;                              // SAVE AS: the user slot being chosen
static int edit_ms;

static const char *group_name(int g)
{
    if (g == G_SOUND) return "SOUND";
    if (g == GROUP_LOOP) return "LOOP";
    if (g == GROUP_SYSTEM) return "SYSTEM";
    return INST.groups[g];
}

static void build_groups(void)
{
    ngroups = 0;
    groups[ngroups++] = G_SOUND;
    for (int g = 0; g < INST.ngroups && ngroups < 36; g++) groups[ngroups++] = g;
    if (INST.looper) groups[ngroups++] = GROUP_LOOP;
    groups[ngroups++] = GROUP_SYSTEM;
}

static void build_items(int g)
{
    nitems = 0;
    items[nitems++] = ITEM_BACK;
    if (g == G_SOUND) { items[nitems++] = SND_SOUND; items[nitems++] = SND_SAVE; items[nitems++] = SND_REVERT; return; }
    for (int p = 0; p < NP; p++) {
        const param_t *d = param(p);
        if (d->flags & PF_HIDE) continue;
        if (d->group != g) continue;
        if (p == CP(CP_TILT_SETUP) && !has_imu_hw) continue;
        items[nitems++] = p;
    }
}

static const char *item_name(int it)
{
    switch (it) {
    case ITEM_BACK: return S_LEFT " BACK";
    case SND_SOUND: return "SOUND";
    case SND_SAVE: return "SAVE AS";
    case SND_REVERT: return "REVERT";
    default: return param(it)->name;
    }
}

static const char *item_value(int it, char *b, int n)
{
    switch (it) {
    case ITEM_BACK: return "";
    case SND_SOUND: return sound_name(meta.sound);
    case SND_SAVE: snprintf(b, (size_t)n, "USER %d", pick_k + 1); return mlev == 3 ? b : S_RIGHT;
    case SND_REVERT: return S_RIGHT;
    default: return param_text(it, b, n);
    }
}

static void menu_open(void) { mlev = 1; sel_y_anim = -1; build_groups(); if (sel1 >= ngroups + 1) sel1 = 0; coach_done(LEARN_MENU); }
static void menu_close(void) { mlev = 0; edit_ms = 0; }
void menu_leave(void) { menu_close(); }
static int coach_pause_ms;
void coach_pause(void) { coach_pause_ms = 100; hint_ms = 0; }
static bool grab, grab_turned;                  // knob_grab(): the instrument has the knob in play
void knob_grab(bool on) { grab = on; grab_turned = false; if (on) { hint_ms = 0; card_ms = 0; quick_ms = 0; } }

enum { SCR_SPLASH, SCR_PLAY, SCR_CALIB, SCR_ABOUT };
static int scr = SCR_SPLASH, splash_f, calib_step, calib_msg_ms;
static const char *calib_msg;
static vec3_t calib_flat;

static void start_calib(bool first) { scr = SCR_CALIB; calib_step = first ? 0 : 1; calib_msg = NULL; }
static void splash_done(void)
{
    scr = SCR_PLAY;
    if (has_imu_hw && !meta.calibrated && pv[CP(CP_MOTION)] >= 1.0f) start_calib(true);   // first run: the tilt first
}

static void run_action(int p)
{
    if (p == CP(CP_TILT_SETUP)) { start_calib(false); return; }
    if (p == CP(CP_ABOUT)) { scr = SCR_ABOUT; return; }
    if (p == CP(CP_LOOP_CLEAR)) { looper_clear(); toast("LOOP CLEARED", 1200); return; }
    if (p < NI && INST.action) INST.action(p);
}

static void factory_reset(void)
{
    int8_t ax[3]; memcpy(ax, meta.axis, 3);
    uint8_t cal = meta.calibrated;
    plat_factory_reset();
    memset(&meta, 0, sizeof meta);
    memcpy(meta.axis, ax, 3); meta.calibrated = cal;          // the tilt is the board's, not a preference
    stash_ok = false;
    params_defaults(false);
    load_sound(0, false);
    plat_mic_gain((int)pv[CP(CP_MIC)]);
    led_apply();
    dirty = true;
    toast("FACTORY RESET DONE", 1800);
}

static void menu_tap(void)
{
    if (mlev == 1) {
        if (sel1 == 0) { menu_close(); return; }
        build_items(groups[sel1 - 1]); sel2 = nitems > 1 ? 1 : 0; top2 = 0; mlev = 2; sel_y_anim = -1;
        return;
    }
    if (mlev == 2) {
        int it = items[sel2];
        if (it == ITEM_BACK) { mlev = 1; sel_y_anim = -1; return; }
        if (it == SND_REVERT) { if (load_sound(meta.sound, true)) toast("BACK TO THE SAVED SOUND", 1300); return; }
        if (it == SND_SAVE) { pick_k = meta.sound >= SOUND_ID_USER ? meta.sound - SOUND_ID_USER : 0; mlev = 3; edit_ms = 0; return; }
        if (it == SND_SOUND) { mlev = 3; edit_ms = 0; return; }
        if (param(it)->kind == PK_ACT) { run_action(it); return; }
        mlev = 3; edit_ms = 0;
        return;
    }
    if (mlev == 3) {
        int it = items[sel2];
        if (it == SND_SAVE) save_user(pick_k);
        else if (it == CP(CP_RESET)) {
            if (pv[it] >= 1.0f) { pv[it] = 0; factory_reset(); menu_close(); return; }
        }
        mlev = 2;
    }
}

static void menu_turn(int d, int det)
{
    if (mlev == 1) { sel1 += d; if (sel1 < 0) sel1 = 0; if (sel1 > ngroups) sel1 = ngroups; return; }
    if (mlev == 2) { sel2 += d; if (sel2 < 0) sel2 = 0; if (sel2 > nitems - 1) sel2 = nitems - 1; return; }
    if (mlev == 3) {
        int it = items[sel2];
        if (it == SND_SOUND) { browse(d); memset(meta.quick, 0, 4); return; }
        if (it == SND_SAVE) { pick_k += d; if (pick_k < 0) pick_k = 0; if (pick_k > N_USER - 1) pick_k = N_USER - 1; return; }
        set_param(it, param_step(it, pv[it], det), true);
        if (it != CP(CP_RESET) && param(it)->kind != PK_ACT) { memcpy(meta.quick, param(it)->tag, 4); dirty = true; }
    }
}

// ---------------------------------------------------------------- init
static bool hint_ok(int i)
{
    const hint_t *h = &INST.hints[i];
    if (meta.learned & (1u << h->learn)) return false;
    if (h->learn == LEARN_JOY && !ctl.has_joy) return false;
    if (h->learn == LEARN_TILT && !ctl.has_imu) return false;
    if (h->learn == LEARN_REC && INST.looper && !looper_ready()) return false;
    return true;
}

void front_init(bool has_imu, bool safe_mode, bool from_flash)
{
    has_imu_hw = has_imu;
    build_groups();
    load_user_names();
    if (INST.looper && !looper_init()) app_log("looper: not enough memory");
    if (INST.init) INST.init();                         // before anything can call changed()
    // What the flash held is the current state; the snapshot is what the sound itself sets, for the '*'.
    static UI_RAM float cur[PARAMS_MAX];
    memcpy(cur, pv, sizeof cur);
    int16_t id = safe_mode ? 0 : meta.sound;            // safe mode: the first factory sound
    if (id == -1) {                                     // the last edit: it is MY EDIT again
        for (int p = 0; p < NI; p++) stash[p] = cur[p];
        stash_ok = true;
        params_snapshot();
    } else {
        bool ok = id >= SOUND_ID_USER ? (meta.user_mask & (1u << (id - SOUND_ID_USER))) != 0 : id >= 0 && id < INST.npresets;
        if (!ok) id = 0;
        load_sound(id, false);
        if (from_flash && !safe_mode) memcpy(pv, cur, sizeof cur);   // a fresh device starts on the sound itself
    }
    plat_mic_gain((int)pv[CP(CP_MIC)]);
    if (INST.changed) INST.changed(-1);
    pv[CP(CP_RESET)] = 0;
    led_apply();
}

// ---------------------------------------------------------------- tick
static bool rec_was, rec_confirmed, hold_used, midi_rec;
static int  rec_ms;

bool front_dirty(void) { return dirty; }
void front_saved(void) { dirty = false; }
bool front_busy(void) { return scr == SCR_SPLASH || scr == SCR_CALIB; }
int  front_menu_level(void) { return mlev; }
int  front_screen(void) { return saver ? 4 : scr; }
static int quiet_ms;
bool front_quiet(void) { return quiet_ms >= 300; }

static master_t master;
static volatile float in_pk;
static float out_lvl, in_lvl;
static int clip_ms;

const front_status_t *front_status(void)
{
    status.sound = sound_name(meta.sound);
    status.edited = params_edited();
    status.frozen = frozen;
    status.out_level = out_lvl;
    status.in_level = in_lvl;
    status.clip = clip_ms > 0;
    return &status;
}

static void telemetry(void)
{
    static int last[4] = { -1, -1, -1, -1 };
    if (pv[CP(CP_MIDI_OUT)] < 1.0f) return;
    int v[4] = { (int)(64.0f + ctl.lean_y * 64.0f / 90.0f), (int)(64.0f + ctl.lean_x * 64.0f / 90.0f),
                 (int)(64.0f + ctl.jx * 63.0f), (int)(64.0f + ctl.jy * 63.0f) };
    for (int i = 0; i < 4; i++) {
        if (i < 2 && !ctl.has_imu) continue;
        if (i >= 2 && !ctl.has_joy) continue;
        int c = v[i] < 0 ? 0 : v[i] > 127 ? 127 : v[i];
        int d = c - last[i]; if (d < 0) d = -d;
        if (d >= 2 || (c != last[i] && (c == 0 || c == 127))) { last[i] = c; plat_midi3((uint8_t)(0xB0 | ch()), (uint8_t)(12 + i), (uint8_t)c); }
    }
}

static void calib_tick(const front_in_t *in, bool hold)
{
    if (hold) { scr = SCR_PLAY; if (!meta.calibrated) toast("TILT NOT SET: SYSTEM MENU", 2000); return; }
    if (!in->tap) return;
    if (calib_step == 0) { calib_step = 1; return; }
    if (calib_step == 1) { calib_flat = raw_avg; calib_step = 2; return; }
    int8_t ax[3];
    float f[3] = { calib_flat.x, calib_flat.y, calib_flat.z }, t[3] = { raw_avg.x, raw_avg.y, raw_avg.z };
    const char *msg = calib_solve(f, t, ax);
    if (msg) { calib_msg = msg; calib_msg_ms = 1500; if (msg[0] == 'T' && msg[1] == 'R') calib_step = 1; return; }
    memcpy(meta.axis, ax, 3); meta.calibrated = 1; dirty = true;
    app_log("tilt: screen x = %d, y = %d, z = %d", ax[0], ax[1], ax[2]);
    scr = SCR_PLAY; toast("TILT SET", 1500);
}

void front_tick(const front_in_t *in)
{
    ticks++;
    t_now += in->dt;
    int dtm = (int)(in->dt * 1000.0f + 0.5f);

    // the meters (the audio task keeps maxima; read and restart them)
    float op = master.peak; master.peak = 0;
    float ip = in_pk; in_pk = 0;
    out_lvl = op > out_lvl ? op : out_lvl * 0.92f;
    in_lvl = ip > in_lvl ? ip : in_lvl * 0.92f;
    if (master.gr < 0.6f) clip_ms = 400; else if (clip_ms > 0) clip_ms -= dtm;
    quiet_ms = out_lvl < 0.01f ? quiet_ms + dtm : 0;

    // motion: the IMU's axes to the screen's, then the lean angles
    bool motion = in->imu_ok && pv[CP(CP_MOTION)] >= 1.0f;
    raw_avg.x += 0.1f * (in->acc_raw.x - raw_avg.x); raw_avg.y += 0.1f * (in->acc_raw.y - raw_avg.y); raw_avg.z += 0.1f * (in->acc_raw.z - raw_avg.z);
    if (!frozen) {
        acc_s = motion ? to_screen(&in->acc_raw) : (vec3_t){ 0, 0, 1 };
        gyro_s = motion ? to_screen(&in->gyro_raw) : (vec3_t){ 0, 0, 0 };
        float lx = asinf(fmaxf(-1.0f, fminf(1.0f, -acc_s.x))) * 57.29578f, ly = asinf(fmaxf(-1.0f, fminf(1.0f, -acc_s.y))) * 57.29578f;
        lean_x += 0.2f * (lx - lean_x); lean_y += 0.2f * (ly - lean_y);
    }
    int nb = 0;
    if (INST.burst && motion) { nb = in->nburst > 64 ? 64 : in->nburst; for (int i = 0; i < nb; i++) burst_s[i] = to_screen(&in->burst_raw[i]); }

    // the knob: a hold fires once at HOLD_MS; the release after it is not a tap
    if (!in->pressed) hold_used = false;
    bool hold = in->pressed && in->held_ms >= HOLD_MS && !hold_used;
    if (hold) hold_used = true;
    bool tap = in->tap && !hold_used;
    int enc = in->enc;
    int det = enc * (enc > 1 || enc < -1 ? 3 : 1);              // a fast turn moves numbers further
    bool rec = in->rec || midi_rec;
    bool any = enc || tap || hold || (rec && !rec_was) || in->pressed;
    if (any) { idle_ms = 0; saver_idle_ms = 0; } else { idle_ms += dtm; saver_idle_ms += dtm; }

    // the screensaver swallows the input that wakes it
    static const int saver_min[4] = { 0, 1, 3, 10 };
    int smin = saver_min[(int)pv[CP(CP_SAVER)] & 3];
    if (saver) { if (any) { saver = false; enc = 0; tap = hold = false; } }
    else if (smin && saver_idle_ms > smin * 60000 && scr == SCR_PLAY && mlev == 0) saver = true;

    bool screen_had_rec = scr != SCR_PLAY;
    switch (scr) {
    case SCR_SPLASH:
        if (enc || tap || hold || (rec && !rec_was)) { splash_done(); enc = 0; tap = hold = false; }
        break;
    case SCR_CALIB: calib_tick(in, hold); enc = 0; tap = hold = false; break;
    case SCR_ABOUT: if (tap || hold) scr = SCR_PLAY; enc = 0; tap = hold = false; break;
    default: break;
    }
    if (screen_had_rec) rec = false;                            // REC on a full screen only closes it

    // the knob for an instrument that holds it: turns, taps and holds are its own; a press with a turn is neither
    int g_enc = 0; bool g_tap = false, g_hold = false;
    if (grab && scr == SCR_PLAY && mlev == 0) {
        if (in->pressed && enc) grab_turned = true;
        g_enc = enc; g_tap = tap && !grab_turned; g_hold = hold && !grab_turned;
        if (!in->pressed) grab_turned = false;
        enc = 0; tap = hold = false;
        coach_pause();
    }
    if (scr == SCR_PLAY) {
        if (mlev == 0) {
            if (hold) {
                if (INST.hold && INST.hold()) coach_done(LEARN_HOLD);
                else { frozen = !frozen; toast(frozen ? "MOTION HELD" : "MOTION FREE", 1100); coach_done(LEARN_HOLD); }
            }
            if (tap) menu_open();
            if (enc) {
                int qp = meta.quick[0] ? param_by_tag(meta.quick) : INST.knob ? param_by_tag(INST.knob) : -1;
                if (qp >= 0 && param(qp)->kind != PK_ACT) { set_param(qp, param_step(qp, pv[qp], det), true); quick_p = qp; quick_ms = 1500; card_ms = 0; }
                else { browse(enc > 3 ? 3 : enc < -3 ? -3 : enc); card_ms = 1500; quick_ms = 0; }
                coach_done(LEARN_TURN);
            }
        } else {
            if (hold) menu_close();
            else {
                if (tap) menu_tap();
                if (enc) menu_turn(enc > 0 ? (enc > 1 ? 2 : 1) : (enc < -1 ? -2 : -1), det);
            }
        }
    }

    // REC: the looper's, or the instrument's
    bool rec_now = rec;
    bool rec_press = rec_now && !rec_was, rec_release = !rec_now && rec_was;
    int  rec_held = rec_ms;
    rec_ms = rec_now ? rec_ms + dtm : 0;
    if (INST.looper) {
        if (rec_press) {
            if (looper_ready()) { looper_press(); rec_confirmed = false; }
            else toast("NO MEMORY FOR LOOPS", 1500);
        }
        if (rec_now && !rec_confirmed && rec_ms >= LOOP_HOLD_MS && looper_ready()) { looper_confirm(); rec_confirmed = true; coach_done(LEARN_REC); }
        if (rec_release && looper_ready()) looper_release(rec_confirmed);
    } else if (rec_press) coach_done(LEARN_REC);
    rec_was = rec_now;

    // the instrument
    ctl = (ctl_t){
        .dt = in->dt, .frames = in->frames, .ticks = ticks,
        .rec = INST.looper ? false : rec_now, .rec_press = INST.looper ? false : rec_press,
        .rec_release = INST.looper ? false : rec_release, .rec_ms = rec_release ? rec_held : rec_ms,
        .has_imu = motion, .frozen = frozen, .acc = acc_s, .gyro = frozen ? (vec3_t){ 0, 0, 0 } : gyro_s,
        .lean_x = lean_x, .lean_y = lean_y, .burst = burst_s, .nburst = nb,
        .has_joy = in->joy_ok && pv[CP(CP_JOY)] >= 1.0f, .menu_open = mlev > 0,
        .knob = g_enc, .knob_tap = g_tap, .knob_hold = g_hold, .knob_down = grab && in->pressed, .knob_turned = grab && grab_turned,
    };
    ctl.gyro_mag = sqrtf(ctl.gyro.x * ctl.gyro.x + ctl.gyro.y * ctl.gyro.y + ctl.gyro.z * ctl.gyro.z);
    if (ctl.has_joy) {
        float x = in->jx, y = in->jy;
        switch ((int)pv[CP(CP_JOY_TURN)]) {                     // the module may sit turned on some builds
        case 1: ctl.jx = y; ctl.jy = -x; break;
        case 2: ctl.jx = -x; ctl.jy = -y; break;
        case 3: ctl.jx = -y; ctl.jy = x; break;
        default: ctl.jx = x; ctl.jy = y; break;
        }
        if (fabsf(x) > 0.5f || fabsf(y) > 0.5f) coach_done(LEARN_JOY);
    }
    if (motion && (fabsf(lean_x) > 20.0f || fabsf(lean_y) > 20.0f)) coach_done(LEARN_TILT);
    INST.tick(&ctl);
    if (ticks % 2 == 0) telemetry();

    // timers
    if (toast_ms > 0) toast_ms -= dtm;
    if (note_ms > 0) note_ms -= dtm;
    if (card_ms > 0) card_ms -= dtm;
    if (quick_ms > 0) quick_ms -= dtm;
    if (calib_msg_ms > 0) calib_msg_ms -= dtm;
    if (mlev == 3) edit_ms += dtm;
    // the coach: in play, idle, one unlearned hint at a time
    if (hint_ms > 0) { hint_ms -= dtm; if (hint_ms <= 0) hint_gap_ms = 7000; }
    else if (coach_pause_ms > 0) coach_pause_ms -= dtm;
    else if (scr == SCR_PLAY && mlev == 0 && pv[CP(CP_HINTS)] >= 1.0f && INST.nhints) {
        if (hint_gap_ms > 0) hint_gap_ms -= dtm;
        if (hint_gap_ms <= 0 && idle_ms > 2500 && !card_ms && !quick_ms && !note_ms) {
            for (int k = 1; k <= INST.nhints; k++) {
                int i = (hint_i + k) % INST.nhints;
                if (hint_ok(i)) { hint_i = i; hint_ms = 4500; break; }
            }
            if (hint_ms <= 0) hint_gap_ms = 4000;
        }
    }
}

// ---------------------------------------------------------------- audio
IRAM_ATTR void front_audio(const float *in, float *out, int n)
{
    float pk = 0;
    for (int i = 0; i < n; i++) { float a = fabsf(in[i]); if (a > pk) pk = a; }
    if (pk > in_pk) in_pk = pk;
    INST.audio(in, out, n);
    if (INST.looper) looper_process(out, n, pv[CP(CP_LOOP_LEVEL)] * 0.01f, pv[CP(CP_LOOP_KEEP)] * 0.01f);
    master_process(&master, out, n, pv[CP(CP_VOLUME)] * 0.01f);
}

// ---------------------------------------------------------------- MIDI in
void front_midi(uint8_t st, uint8_t d1, uint8_t d2)
{
    if (st < 0x80 || st >= 0xF0) return;
    if ((st & 0x0F) != ch()) return;
    uint8_t k = st & 0xF0;
    if (k == 0xB0) {
        int p = param_by_cc(d1);
        if (p >= 0) { set_param(p, param_from_cc(p, d2), false); return; }
        if (d1 == 64) { midi_rec = d2 >= 64; return; }                 // the sustain pedal is REC
    } else if (k == 0xC0) {
        int16_t ids[2 + 64 + N_USER];
        int n = sound_list(ids), want = d1 < INST.npresets ? d1 : SOUND_ID_USER + (d1 - INST.npresets);
        for (int i = 0; i < n; i++) if (ids[i] == want) { load_sound(ids[i], false); card_ms = 1500; }
        return;
    } else if ((k == 0x90 || k == 0x80) && (d1 == 60 || d1 == 64) && !INST.midi_notes) {
        bool on = k == 0x90 && d2 > 0;
        if (d1 == 60) midi_rec = on;
        else if (on) { frozen = !frozen; toast(frozen ? "MOTION HELD" : "MOTION FREE", 1100); }
        return;
    }
    if (INST.midi_in) INST.midi_in(st, d1, d2);
}

// ---------------------------------------------------------------- drawing
static void strip_bg(int y)
{
    oled_rect(0, y, OLED_W, OLED_H - y, false);
    oled_hline_pat(0, y, OLED_W, 0x55, true);
}

static void draw_value_strip(int y, const char *name, const char *val, int kind, float frac, int nopt, int opt)
{
    strip_bg(y);
    int x = oled_pill(F_SMALL, 1, y + 2, name, true);
    const gfx_font_t *f = oled_ftext_w(F_MED, val, 1) <= OLED_W - x - 6 ? F_MED : F_SMALL;
    oled_ftext_r(f, OLED_W - 2, y + (f == F_MED ? 2 : 4), val, 1);
    if (kind == PK_NUM) ui_slider(2, y + 11, OLED_W - 4, frac);
    else if (kind == PK_ENUM && nopt > 1 && nopt <= 24) {
        int gap = (OLED_W - 8) / nopt;
        for (int i = 0; i < nopt; i++) {
            int cx = 4 + gap / 2 + i * gap;
            if (i == opt) oled_rrect(cx - 2, y + 12, 5, 5, 1, true); else oled_pixel(cx, y + 14, true);
        }
    }
}

static float param_frac(int p)
{
    const param_t *d = param(p);
    if (d->max <= d->min) return 0;
    if (d->flags & PF_LOG) return logf(pv[p] / d->min) / logf(d->max / d->min);
    return (pv[p] - d->min) / (d->max - d->min);
}

static void draw_param_strip(int y, int p)
{
    char v[28];
    const param_t *d = param(p);
    draw_value_strip(y, d->name, param_text(p, v, sizeof v), d->kind, param_frac(p), (int)(d->max - d->min) + 1, (int)(pv[p] - d->min));
}

static void draw_sound_card(int y)
{
    strip_bg(y);
    int16_t ids[2 + 64 + N_USER];
    int n = sound_list(ids), i = sound_pos(ids, n, meta.sound);
    char b[24]; snprintf(b, sizeof b, "SOUND %d/%d", i + 1, n);
    oled_ftext_ex(F_TINY, 3, y + 3, b, 1);
    if (meta.sound == -1) oled_ftext_r(F_TINY, OLED_W - 3, y + 3, "YOUR EDIT", 1);
    else if (meta.sound >= SOUND_ID_USER) oled_ftext_r(F_TINY, OLED_W - 3, y + 3, "USER", 1);
    const char *nm = sound_name(meta.sound);
    if (i > 0) oled_ftext_ex(F_SMALL, 2, y + 11, S_LEFT, 0);
    if (i < n - 1) oled_ftext_r(F_SMALL, OLED_W - 2, y + 11, S_RIGHT, 0);
    oled_ftext_c(oled_ftext_w(F_MED, nm, 1) < 112 ? F_MED : F_SMALL, 64, y + 9, nm, 1);
}

static void draw_hint(int y)
{
    strip_bg(y);
    const char *t = INST.hints[hint_i].text;
    oled_disc(5, y + 9, 4, true);                                  // a small "i" in a dot
    oled_rect(5, y + 6, 1, 1, false); oled_rect(5, y + 8, 1, 4, false);
    char l[3][48];
    int n = ui_wrap(F_SMALL, t, OLED_W - 14, l, 2);
    if (n <= 1) oled_ftext_ex(F_SMALL, 12, y + 7, l[0], 1);
    else { oled_ftext_ex(F_SMALL, 12, y + 3, l[0], 1); ui_text_fit(F_SMALL, 12, y + 11, OLED_W - 14, l[1]); }
}

static void draw_list(void)
{
    char v[32];
    int sel, n, *top;
    if (mlev == 1) {
        n = ngroups + 1; sel = sel1; top = &top1;
        oled_pill(F_SMALL, 0, 2, "MENU", true);
        oled_ftext_r(F_SMALL, OLED_W - 1, 5, INST.name, 1);
    } else {
        n = nitems; sel = sel2; top = &top2;
        int gx = oled_pill(F_SMALL, 0, 2, group_name(groups[sel1 - 1]), true);
        (void)gx;
        snprintf(v, sizeof v, "%d/%d", sel, n - 1);
        if (sel > 0) oled_ftext_r(F_SMALL, OLED_W - 1, 5, v, 1);
    }
    const int rows = 4, rh = 12, y0 = 16;
    if (sel < *top) *top = sel;
    if (sel > *top + rows - 1) *top = sel - rows + 1;
    float want = (float)(y0 + (sel - *top) * rh);
    sel_y_anim = sel_y_anim < 0 ? want : sel_y_anim + 0.65f * (want - sel_y_anim);
    for (int r = 0; r < rows && *top + r < n; r++) {
        int i = *top + r, y = y0 + r * rh;
        const char *name, *val = "";
        if (mlev == 1) {
            if (i == 0) name = S_LEFT " PLAY";
            else {
                name = group_name(groups[i - 1]);
                val = groups[i - 1] == G_SOUND ? sound_name(meta.sound) : S_RIGHT;
            }
        } else {
            name = item_name(items[i]);
            val = item_value(items[i], v, sizeof v);
        }
        int nx = oled_ftext_ex(F_SMALL, 4, y + 3, name, 1);
        if (val && *val) {
            int room = OLED_W - 8 - nx - 6;
            if (oled_ftext_w(F_SMALL, val, 1) <= room) oled_ftext_r(F_SMALL, OLED_W - 7, y + 3, val, 1);
            else ui_text_fit(F_SMALL, nx + 6, y + 3, room, val);
        }
    }
    int sy = (int)lroundf(sel_y_anim);
    oled_invert(1, sy, OLED_W - 6, rh - 1);
    // scroll bar
    if (n > rows) {
        int th = 47 * rows / n; if (th < 4) th = 4;
        int ty = 16 + (47 - th) * *top / (n - rows);
        oled_hline_pat(OLED_W - 2, 16, 1, 1, true);
        for (int y = 16; y < 63; y += 2) oled_pixel(OLED_W - 2, y, true);
        oled_rect(OLED_W - 3, ty, 3, th, true);
    }
}

static void box_art(int cx, int cy, float deg)
{
    // the box seen from the front edge: a slab with the screen on top, turned by deg (right edge down > 0)
    float a = deg * 0.0174533f, c = cosf(a), s = sinf(a);
    const float hw = 22, hh = 5;
    float px[4] = { -hw, hw, hw, -hw }, py[4] = { -hh, -hh, hh, hh };
    int X[4], Y[4];
    for (int i = 0; i < 4; i++) { X[i] = cx + (int)lroundf(px[i] * c - py[i] * s); Y[i] = cy + (int)lroundf(px[i] * s + py[i] * c); }
    for (int i = 0; i < 4; i++) oled_line(X[i], Y[i], X[(i + 1) % 4], Y[(i + 1) % 4], true);
    for (int k = -14; k <= 14; k += 2) {                          // the screen, a dotted strip on the top face
        float x0 = (float)k, y0 = -hh - 2;
        oled_pixel(cx + (int)lroundf(x0 * c - y0 * s), cy + (int)lroundf(x0 * s + y0 * c), true);
    }
}

static void draw_calib(void)
{
    oled_pill(F_SMALL, 0, 2, "TILT SETUP", true);
    if (calib_step) { char b[8]; snprintf(b, sizeof b, "%d/2", calib_step); oled_ftext_r(F_SMALL, OLED_W - 1, 5, b, 1); }
    if (calib_step == 0) {
        oled_ftext_c(F_SMALL, 64, 20, "FIRST, TEACH ME WHICH", 1);
        oled_ftext_c(F_SMALL, 64, 29, "WAY IS DOWN. TWO STEPS.", 1);
        box_art(64, 44, 8.0f * sinf(t_now * 3.0f));
        oled_ftext_c(F_TINY, 64, 57, "PRESS: START   HOLD: LATER", 1);
    } else if (calib_step == 1) {
        oled_ftext_c(F_SMALL, 64, 19, "LAY ME FLAT, SCREEN UP", 1);
        box_art(64, 37, 0);
        oled_hline_pat(20, 44, 88, 0x33, true);
        oled_ftext_c(F_SMALL, 64, 54, "THEN PRESS THE KNOB", 1);
    } else {
        oled_ftext_c(F_SMALL, 64, 19, "TIP MY RIGHT EDGE DOWN", 1);
        float d = sqrtf((raw_avg.x - calib_flat.x) * (raw_avg.x - calib_flat.x) + (raw_avg.y - calib_flat.y) * (raw_avg.y - calib_flat.y)
                        + (raw_avg.z - calib_flat.z) * (raw_avg.z - calib_flat.z));
        box_art(52, 40, 20.0f + 3.0f * sinf(t_now * 4.0f));
        oled_line(88, 31, 88, 43, true); oled_line(85, 40, 88, 43, true); oled_line(91, 40, 88, 43, true);
        ui_vbar(104, 24, 8, 22, d / 0.5f);
        oled_ftext_ex(F_TINY, 98, 48, d >= 0.25f ? "GOOD" : "MORE", 1);
        oled_ftext_c(F_SMALL, 64, 55, "30 DEGREES, THEN PRESS", 1);
    }
    if (calib_msg_ms > 0 && calib_msg) ui_box(36, calib_msg);
}

static void draw_about(void)
{
    oled_pill(F_SMALL, 0, 2, "ABOUT", true);
    oled_ftext_r(F_MED, OLED_W - 1, 3, INST.name, 1);
    oled_ftext_ex(F_TINY, 2, 18, INST.tagline, 1);
    char lines[6][32];
    int n = plat_about(lines, 6);
    for (int i = 0; i < n && i < 6; i++) oled_ftext_ex(F_TINY, 2, 26 + i * 7, lines[i], 1);
}

static void default_saver(uint32_t f)
{
    int w = oled_ftext_w(F_SMALL, INST.name, 1);
    int x = (int)((sinf((float)f * 0.011f) * 0.5f + 0.5f) * (float)(OLED_W - w));
    int y = 16 + (int)((sinf((float)f * 0.0073f + 1.0f) * 0.5f + 0.5f) * 40.0f);
    oled_ftext_ex(F_SMALL, x, y, INST.name, 1);
}

static void default_splash(int f)
{
    float e = ease_out((float)f / 15.0f);
    int y = (int)(64.0f - 40.0f * e);
    oled_ftext_c(F_MED, 64, y, INST.name, 2);
    if (f > 12) oled_ftext_c(F_TINY, 64, y + 14, INST.tagline, 1);
}

void front_draw(void)
{
    frame++;
    oled_clear();
    if (saver) { if (INST.saver) INST.saver(frame); else default_saver(frame); return; }
    if (scr == SCR_SPLASH) {
        bool more = splash_f < 100 && (INST.splash ? INST.splash(splash_f) : (default_splash(splash_f), splash_f < 40));
        splash_f++;
        if (!more) splash_done();
        return;
    }
    if (scr == SCR_CALIB) { draw_calib(); return; }
    if (scr == SCR_ABOUT) { draw_about(); return; }
    if (mlev == 1 || mlev == 2) { draw_list(); if (toast_ms > 0) ui_box(40, toast_s); return; }

    // the bottom strip: one at a time, sliding in
    int kind = 0;
    if (mlev == 3) kind = 1;
    else if (card_ms > 0) kind = 2;
    else if (quick_ms > 0 && quick_p >= 0) kind = 3;
    else if (note_ms > 0) kind = 4;
    else if (hint_ms > 0) kind = 5;
    if (kind != strip_kind) { if (!strip_kind || !kind) strip_anim = 0; strip_kind = kind; }
    if (strip_anim < 6) strip_anim++;
    view_t v = { .frame = frame, .t = t_now, .cover = kind ? OLED_H - STRIP_Y : 0, .out_level = out_lvl, .in_level = in_lvl };
    INST.draw(&v);
    int y = STRIP_Y + (int)((1.0f - ease_out((float)strip_anim / 6.0f)) * (float)(OLED_H - STRIP_Y));
    char b[32];
    switch (kind) {
    case 1: {
        int it = items[sel2];
        if (it == SND_SOUND) draw_sound_card(y);
        else if (it == SND_SAVE) {
            snprintf(b, sizeof b, "USER %d%s", pick_k + 1, meta.user_mask & (1u << pick_k) ? " (REPLACE)" : "");
            draw_value_strip(y, "SAVE AS", b, PK_ENUM, 0, N_USER, pick_k);
        } else draw_param_strip(y, it);
        if (it >= 0 && edit_ms < 3500 && param(it)->help && y == STRIP_Y) {   // the help line, for a moment
            const char *h = param(it)->help;
            int w = oled_ftext_w(F_TINY, h, 1) + 6;
            oled_rect(64 - w / 2, STRIP_Y - 9, w, 9, false);
            oled_ftext_c(F_TINY, 64, STRIP_Y - 7, h, 1);
        }
        break;
    }
    case 2: draw_sound_card(y); break;
    case 3: draw_param_strip(y, quick_p); break;
    case 4: strip_bg(y); oled_pill(F_SMALL, 2, y + 5, note_s, true); break;
    case 5: draw_hint(y); break;
    }
    if (toast_ms > 0) ui_box(34, toast_s);
}
