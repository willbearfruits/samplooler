// The checks every instrument gets: its tables (tags, ranges, what fits on the screen), its sounds,
// flash and MIDI round trips, a walk through the whole menu, the looper, tilt setup for all 24 ways the
// IMU can sit, a factory reset, no NaN, nothing over full scale, and no click at any parameter's edge.
#include "sim.h"
#include "looper.h"
#include "calib.h"
#include "oled.h"
#include "ui.h"
#include "dsp.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------- menu navigation by the controls only
static int group_list(int *g)                  // as front.c's build_groups: SOUND, the instrument's, LOOP, SYSTEM
{
    int n = 0;
    g[n++] = -1;
    for (int i = 0; i < INST.ngroups; i++) g[n++] = i;
    if (INST.looper) g[n++] = GROUP_LOOP;
    g[n++] = GROUP_SYSTEM;
    return n;
}
static int item_list(int group, int *it)       // as build_items, after BACK
{
    int n = 0;
    if (group == -1) { it[n++] = -10; it[n++] = -11; it[n++] = -12; return n; }
    for (int p = 0; p < NP; p++) if (!(param(p)->flags & PF_HIDE) && param(p)->group == group) it[n++] = p;
    return n;
}
static void to_top(void) { for (int i = 0; i < 70; i++) sim_turn(-1); }
static void first_sound(void) { for (int i = 0; i < 70; i++) sim_turn(-1); sim_run(0.1f); }   // browsing moves 3 a tick at most
static void down(int k) { for (int i = 0; i < k; i++) sim_turn(1); }

void sim_menu_close(void) { if (front_menu_level()) sim_hold_knob(); }
// Open the menu on group (an index into group_list) and put the cursor on item k (0 = the first after BACK).
void sim_menu_goto(int gi, int k)
{
    sim_menu_close();
    sim_tap();                                                 // open
    to_top(); down(gi + 1); sim_tap();                         // the page
    to_top(); down(k + 1);                                     // the item
}
// The param p in the menu: returns false if it is not in any page.
bool sim_menu_param(int p)
{
    int g[48], ng = group_list(g);
    for (int gi = 0; gi < ng; gi++) {
        int it[PARAMS_MAX + 4], n = item_list(g[gi], it);
        for (int k = 0; k < n; k++) if (it[k] == p) { sim_menu_goto(gi, k); return true; }
    }
    return false;
}

// ---------------------------------------------------------------- the checks
static void tables(void)
{
    section("tables");
    int bad = 0;
    for (int p = 0; p < NP; p++) {
        const param_t *d = param(p);
        if (!d->tag || strlen(d->tag) != 4) { bad++; note("param %d (%s): tag must be 4 chars", p, d->name); }
        for (int q = 0; q < p; q++) if (!strncmp(param(q)->tag, d->tag, 4)) { bad++; note("tag '%s' used twice (%s, %s)", d->tag, param(q)->name, d->name); }
        if (d->kind == PK_ACT) continue;
        if (!(d->def >= d->min && d->def <= d->max)) { bad++; note("%s: default %g outside %g..%g", d->name, (double)d->def, (double)d->min, (double)d->max); }
        if (d->kind == PK_NUM && !(d->step > 0)) { bad++; note("%s: step must be > 0", d->name); }
        if ((d->flags & PF_LOG) && !(d->min > 0 && d->step > 1)) { bad++; note("%s: PF_LOG needs min > 0 and a ratio step > 1", d->name); }
        if (d->kind == PK_ENUM && !d->labels) { bad++; note("%s: an enum without labels", d->name); }
    }
    check(bad == 0, "every param has a unique 4-char tag, a default in range and a usable step (%d problems)", bad);

    int wide = 0;
    char v[40];
    for (int p = 0; p < NP; p++) {
        const param_t *d = param(p);
        if (oled_ftext_w(F_SMALL, d->name, 1) > 70) { wide++; note("%s: name %d px (70 max)", d->name, oled_ftext_w(F_SMALL, d->name, 1)); }
        if (d->help && oled_ftext_w(F_TINY, d->help, 1) > 124) { wide++; note("%s: help %d px (124 max)", d->name, oled_ftext_w(F_TINY, d->help, 1)); }
        if (d->kind == PK_ACT) continue;
        float keep = pv[p];
        float tries[3] = { d->min, d->max, d->def };
        if (d->kind == PK_ENUM) {
            for (int i = 0; i <= (int)(d->max - d->min); i++) {
                pv[p] = d->min + (float)i;
                param_text(p, v, sizeof v);
                int w = oled_pill_w(F_SMALL, d->name) + 4 + oled_ftext_w(F_SMALL, v, 1);
                if (w > 126) { wide++; note("%s = %s: %d px in the edit strip (126 max)", d->name, v, w); }
            }
        } else for (int i = 0; i < 3; i++) {
            pv[p] = tries[i];
            param_text(p, v, sizeof v);
            int w = oled_pill_w(F_SMALL, d->name) + 4 + oled_ftext_w(F_SMALL, v, 1);
            if (w > 126) { wide++; note("%s = %s: %d px in the edit strip (126 max)", d->name, v, w); }
        }
        pv[p] = keep;
    }
    for (int g = 0; g < INST.ngroups; g++) if (oled_pill_w(F_SMALL, INST.groups[g]) > 70) { wide++; note("group %s too wide", INST.groups[g]); }
    for (int i = 0; i < INST.nhints; i++) { char l[4][48]; if (ui_wrap(F_SMALL, INST.hints[i].text, OLED_W - 14, l, 2) > 2) { wide++; note("hint \"%s\" needs more than two lines", INST.hints[i].text); } }
    for (int i = 0; i < INST.npresets; i++) if (oled_ftext_w(F_SMALL, INST.presets[i].name, 1) > 100) { wide++; note("sound %s: name too wide", INST.presets[i].name); }
    if (oled_ftext_w(F_TINY, INST.tagline, 1) > 124) { wide++; note("tagline %d px (124 max)", oled_ftext_w(F_TINY, INST.tagline, 1)); }
    check(wide == 0, "every name, value, help line, hint and sound name fits its place on the screen (%d too wide)", wide);

    int unknown = 0;
    for (int i = 0; i < INST.npresets; i++) {
        int b = preset_apply(INST.presets[i].set);
        if (b) { unknown += b; note("sound %s: %d names not known", INST.presets[i].name, b); }
    }
    check(unknown == 0, "all %d sounds name only params that exist", INST.npresets);
    check(INST.npresets >= 1 && INST.nhints >= 1, "there are sounds (%d) and hints (%d)", INST.npresets, INST.nhints);
    params_defaults(true);
}

static void flash_and_midi(void)
{
    section("flash and MIDI");
    static uint8_t buf[PARAMS_MAX * 8];
    static float before[PARAMS_MAX];
    for (int p = 0; p < NP; p++) if (param(p)->kind != PK_ACT && p != CP(CP_RESET)) pv[p] = param_step(p, pv[p], 1);
    memcpy(before, pv, sizeof before);
    int n = params_pack(buf, sizeof buf, false);
    params_defaults(false);
    int got = params_unpack(buf, n, false);
    int diff = 0;
    for (int p = 0; p < NP; p++) if (param(p)->kind != PK_ACT && p != CP(CP_RESET) && pv[p] != before[p]) diff++;
    check(diff == 0 && got > 0, "params survive a trip through flash (%d bytes, %d params, %d differ)", n, got, diff);
    // an older firmware's blob: one unknown tag in front, one param missing
    uint8_t old[16 + PARAMS_MAX * 8];
    memcpy(old, "zzzz", 4); float one = 1; memcpy(old + 4, &one, 4);
    memcpy(old + 8, buf + 8, (size_t)(n - 8));
    params_defaults(false);
    check(params_unpack(old, n, false) == got - 1, "a blob with an unknown tag and a missing one still loads the rest");
    params_defaults(false);

    int worst = 0, worst_p = -1;
    for (int p = 0; p < NI; p++) {
        int cc = param_cc_number(p);
        if (cc < 0) continue;
        if (param_by_cc(cc) != p) { worst = 999; worst_p = p; }
        for (int c = 0; c <= 127; c++) {
            pv[p] = param_from_cc(p, c);
            int back = param_to_cc(p), d = abs(back - c);
            const param_t *q = param(p);
            int steps = q->kind == PK_ENUM || (q->flags & PF_INT) ? (int)(q->max - q->min) : 127;
            int allow = steps < 127 ? 127 / (steps > 0 ? steps : 1) / 2 + 1 : 2;
            if (d > allow && d > worst) { worst = d; worst_p = p; }
        }
        pv[p] = param(p)->def;
    }
    check(worst_p < 0, "every param's CC goes out and comes back to the same value%s%s", worst_p >= 0 ? ": not " : "", worst_p >= 0 ? param(worst_p)->name : "");

    // MIDI in: a CC moves the param, a program change picks the sound
    int p0 = -1;
    for (int p = 0; p < NI; p++) if (param_cc_number(p) >= 0 && param(p)->kind == PK_NUM) { p0 = p; break; }
    if (p0 >= 0) {
        front_midi(0xB0, (uint8_t)param_cc_number(p0), 127);
        check(fabsf(pv[p0] - param(p0)->max) < 1e-4f * (fabsf(param(p0)->max) + 1), "CC %d = 127 sets %s to its maximum", param_cc_number(p0), param(p0)->name);
        front_midi(0xB0, (uint8_t)param_cc_number(p0), 0);
        front_midi(0xB1, (uint8_t)param_cc_number(p0), 127);
        check(fabsf(pv[p0] - param(p0)->min) < 1e-4f * (fabsf(param(p0)->min) + 1), "a CC on another channel is ignored");
        param_set(p0, param(p0)->def);
    }
    if (INST.npresets > 1) {
        front_midi(0xC0, 1, 0);
        check(meta.sound == 1, "program change 2 loads the second sound (%s)", INST.presets[1].name);
        front_midi(0xC0, 0, 0);
    }
    int sent = 0;
    for (int i = 0; i < midi_n; i++) if ((midi_log[i].st & 0xF0) == 0xB0) sent++;
    note("%d MIDI messages out so far (%d CC)", midi_n, sent);
}

// The app's line (front.c): who it is, the settings described, set by tag, changes sent back, quiet once the app
// has gone or stops reading, and what it missed sent when it is back.
static const char *sx_field(const char *t, const char *key, char *out, int cap)
{
    char k[24];
    snprintf(k, sizeof k, "%s=", key);
    for (const char *s = t + 1; s && *s; ) {
        if (!strncmp(s, k, strlen(k))) {
            const char *e = strchr(s, '\n');
            int n = e ? (int)(e - s - (int)strlen(k)) : (int)strlen(s + strlen(k));
            if (n > cap - 1) n = cap - 1;
            memcpy(out, s + strlen(k), (size_t)n); out[n] = 0;
            return out;
        }
        s = strchr(s, '\n'); if (s) s++;
    }
    return NULL;
}
static void sx_in(char cmd, const char *body)
{
    front_sysex((uint8_t)cmd, (const uint8_t *)body, (int)strlen(body));
}
static void app_line(void)
{
    section("app line");
    char v[96];
    int from = sim_sysex_n;
    sx_in('I', "");
    bool who = sim_sysex_n == from + 1 && sim_sysex[from % 256][0] == 'I';
    check(who && sx_field(sim_sysex[from % 256], "name", v, sizeof v) && !strcmp(v, INST.name),
          "'I' is answered with the name (%s)", who ? v : "nothing");
    int shown = who && sx_field(sim_sysex[from % 256], "params", v, sizeof v) ? atoi(v) : -1;
    check(who && sx_field(sim_sysex[from % 256], "proto", v, sizeof v) && !strcmp(v, "1"), "... and protocol 1");

    from = sim_sysex_n;
    sx_in('D', "");
    sim_run(2.0f);
    int described = 0, ended = -1, bad = 0, n_ok = 0;
    char tag[8] = "", tag_tag[8] = "";
    for (int i = from; i < sim_sysex_n && i < from + 256; i++) {
        const char *t = sim_sysex[i % 256];
        if (t[0] != 'D') continue;
        if (sx_field(t, "end", v, sizeof v)) { ended = atoi(v); continue; }
        described++;
        char k[16];
        if (!sx_field(t, "tag", tag, sizeof tag) || !sx_field(t, "kind", k, sizeof k) || !sx_field(t, "name", v, sizeof v)
            || !sx_field(t, "cc", v, sizeof v)) bad++;
        else if (!strcmp(k, "enum")) {               // every label there: nothing cut off at the end
            int p = param_by_tag(tag), bars = 0;
            char lab[720];
            if (p < 0 || !sx_field(t, "labels", lab, sizeof lab)) { bad++; continue; }
            for (const char *c = lab; *c; c++) bars += *c == '|';
            if (bars != (int)(param(p)->max - param(p)->min)) bad++;
        } else if (!strcmp(k, "num") && !tag_tag[0]) {
            int p = param_by_tag(tag);
            if (p >= 0 && p < NI && param(p)->max > param(p)->min) { memcpy(tag_tag, tag, 5); n_ok = 1; }
        }
    }
    check(described > 0 && described == ended && described == shown && bad == 0,
          "'D' describes every setting (%d, end=%d, 'I' said %d), each with a tag, a name, a kind and all its labels", described, ended, shown);

    if (n_ok) {
        int p = param_by_tag(tag_tag);
        float want = param(p)->max;
        char msg[40];
        snprintf(msg, sizeof msg, "%.4s=%.7g", param(p)->tag, (double)want);
        from = sim_sysex_n;
        sx_in('P', msg);
        bool answered = sim_sysex_n > from && sim_sysex[from % 256][0] == 'V' && sx_field(sim_sysex[from % 256], "text", v, sizeof v);
        check(fabsf(pv[p] - want) < 1e-4f * (fabsf(want) + 1) && answered, "'P %s' sets %s and is answered with 'V' (%s)", msg, param(p)->name, answered ? v : "nothing");
        from = sim_sysex_n;
        param_set(p, param(p)->def);              // as the knob would
        sim_run(0.1f);
        bool followed = false;
        for (int i = from; i < sim_sysex_n; i++) if (sim_sysex[i % 256][0] == 'V' && sx_field(sim_sysex[i % 256], "tag", v, sizeof v) && !strncmp(v, param(p)->tag, 4)) followed = true;
        check(followed, "a change on the device goes out as 'V'");
        from = sim_sysex_n;
        sx_in('P', "zzzz=1");
        sx_in('P', "rset=1");
        check(sim_sysex_n == from && pv[CP(CP_RESET)] == 0, "an unknown tag and RESET ALL are ignored");
        sim_run(6.0f);                            // the app is gone
        from = sim_sysex_n;
        param_set(p, param(p)->max);
        sim_run(0.1f);
        check(sim_sysex_n == from, "after 5 s without a word from the app, changes stay home");
        sx_in('I', "");                           // the app is back (its next 'I')
        sim_run(0.1f);
        bool caught_up = false;
        for (int i = from; i < sim_sysex_n; i++) if (sim_sysex[i % 256][0] == 'V' && sx_field(sim_sysex[i % 256], "tag", v, sizeof v) && !strncmp(v, param(p)->tag, 4)) caught_up = true;
        check(caught_up, "when it speaks again, the change it missed goes out");
        sim_sysex_off = true;                     // the computer stops reading
        param_set(p, param(p)->def);
        sim_run(0.1f);
        sim_sysex_off = false;
        from = sim_sysex_n;
        param_set(p, param(p)->max);
        sim_run(0.1f);
        check(sim_sysex_n == from, "a message that cannot go out quiets the line");
        param_set(p, param(p)->def);
    }
}

// With INST.knob the knob in play turns that param, and the sounds are walked in SOUND > SOUND instead.
static void browse_open(void) { if (INST.knob) { sim_menu_goto(0, 0); sim_tap(); } }
static void browse_close(void) { if (INST.knob) sim_menu_close(); }

static void sounds(void)
{
    section("sounds");
    sim_menu_close();
    sim_run(0.3f);
    browse_open();
    first_sound();
    int loaded = 0;
    for (int i = 0; i < INST.npresets; i++) {
        if (meta.sound == i) loaded++;
        if (i == 1) sim_shot("sound_card");
        sim_turn(1); sim_run(0.25f);
    }
    check(loaded == INST.npresets, "turning the knob %s steps through all %d sounds (%d seen)", INST.knob ? "in SOUND > SOUND" : "in play", INST.npresets, loaded);
    first_sound();
    // an edit is kept as MY EDIT when the knob moves on
    int p0 = -1;
    for (int p = 0; p < NI; p++) if (param_in_sound(p) && param(p)->kind == PK_NUM && !(param(p)->flags & PF_HIDE)) { p0 = p; break; }
    if (p0 >= 0) {
        float v = param_step(p0, pv[p0], pv[p0] < param(p0)->max ? 3 : -3);
        param_set(p0, v);
        sim_turn(1); sim_run(0.2f);
        first_sound();
        check(meta.sound == -1 && fabsf(pv[p0] - v) < 1e-6f, "an edited sound left with the knob comes back as MY EDIT");
        sim_turn(1); sim_run(0.2f);
    }
    browse_close();
    // save it as a user sound through the menu
    sim_menu_goto(0, 1);                                    // SOUND > SAVE AS
    sim_tap(); sim_turn(2); sim_shot("menu_save_as"); sim_tap();
    check(meta.user_mask & 4, "SAVE AS stores USER 3 (mask %02x)", meta.user_mask);
    sim_menu_close();
    browse_open();
    for (int i = 0; i < 70; i++) sim_turn(1);
    sim_run(0.2f);
    check(meta.sound == 102, "the user sound is in the list after the factory sounds");
    first_sound();
    browse_close();
}

static void menu_walk(void)
{
    section("menu");
    int g[48], ng = group_list(g), shots = 0, items_seen = 0;
    sim_menu_close();
    sim_tap();
    check(front_menu_level() == 1, "a tap in play opens the menu");
    sim_shot("menu_pages");
    for (int gi = 0; gi < ng; gi++) {
        int it[PARAMS_MAX + 4], n = item_list(g[gi], it);
        sim_menu_goto(gi, 0);
        char lb[48]; snprintf(lb, sizeof lb, "menu_%d", gi);
        if (shots++ < 12) sim_shot(lb);
        for (int k = 0; k < n; k++) {
            int p = it[k];
            if (p < 0 || p >= NP) continue;
            const param_t *d = param(p);
            if (d->kind == PK_ACT || p == CP(CP_RESET) || p == CP(CP_MIC) || p == CP(CP_VOLUME) || p == CP(CP_MOTION)) continue;
            to_top(); down(k + 1);
            sim_tap();                                          // edit
            if (front_menu_level() == 3) items_seen++;
            float a = pv[p];
            int dir = pv[p] >= d->max ? -1 : 1;
            sim_turn(dir); sim_run(0.05f);
            if (k == 0 && gi == 1) sim_shot("menu_edit");
            sim_turn(-dir);
            if (fabsf(pv[p] - a) > 1e-6f && d->kind == PK_NUM && !(d->flags & PF_LOG)) note("%s did not come back with one turn each way", d->name);
            sim_tap();                                          // back to the list
        }
    }
    int editable = 0;
    for (int p = 0; p < NP; p++) if (!(param(p)->flags & PF_HIDE) && param(p)->kind != PK_ACT && p != CP(CP_RESET) && p != CP(CP_MIC) && p != CP(CP_VOLUME) && p != CP(CP_MOTION)) editable++;
    if (!INST.looper) editable -= 2;                            // LOOP LEVEL, LOOP KEEP are not shown
    check(items_seen == editable, "every setting in every page opens for editing (%d of %d)", items_seen, editable);
    sim_hold_knob();
    check(front_menu_level() == 0, "holding the knob leaves the menu");
    meta.quick[0] = 0;
    // ABOUT
    sim_menu_param(CP(CP_ABOUT)); sim_tap(); sim_run(0.1f);
    check(front_screen() == 3, "SYSTEM > ABOUT shows the about card");
    sim_shot("about"); sim_tap(); sim_run(0.05f);
    sim_menu_close();
}

static void tilt_setup(void)
{
    section("tilt setup");
    int ok = 0, total = 0;
    for (int ax = -3; ax <= 3; ax++) for (int ay = -3; ay <= 3; ay++) {
        if (!ax || !ay || abs(ax) == abs(ay)) continue;
        float ex[3] = { 0 }, ey[3] = { 0 }, ez[3];
        ex[abs(ax) - 1] = ax > 0 ? 1 : -1; ey[abs(ay) - 1] = ay > 0 ? 1 : -1;
        ez[0] = ey[1] * ex[2] - ey[2] * ex[1]; ez[1] = ey[2] * ex[0] - ey[0] * ex[2]; ez[2] = ey[0] * ex[1] - ey[1] * ex[0];
        float flat[3], tip[3], sx = -0.5f, sz = 0.866f;
        for (int i = 0; i < 3; i++) { flat[i] = ez[i] + 0.02f * sim_noise(); tip[i] = sx * ex[i] + sz * ez[i] + 0.02f * sim_noise(); }
        int8_t got[3];
        const char *msg = calib_solve(flat, tip, got);
        total++;
        if (!msg) {
            float v[3] = { tip[0], tip[1], tip[2] };
            float x = v[abs(got[0]) - 1] * (got[0] < 0 ? -1.0f : 1.0f), y = v[abs(got[1]) - 1] * (got[1] < 0 ? -1.0f : 1.0f), z = v[abs(got[2]) - 1] * (got[2] < 0 ? -1.0f : 1.0f);
            if (fabsf(x + 0.5f) < 0.06f && fabsf(y) < 0.06f && fabsf(z - 0.866f) < 0.06f) ok++;
        }
    }
    check(ok == total, "tilt setup finds all %d ways the IMU can sit under the screen (%d right)", total, ok);
    // through the menu, as a player would
    sim_menu_param(CP(CP_TILT_SETUP)); sim_tap(); sim_run(0.1f);
    check(front_screen() == 2, "SYSTEM > TILT SETUP opens it");
    sim_tilt(0, 0); sim_run(0.5f); sim_shot("tilt_setup_1"); sim_tap(); sim_run(0.1f);
    sim_tilt(30, 0); sim_run(0.6f); sim_shot("tilt_setup_2"); sim_tap(); sim_run(0.1f);
    check(front_screen() == 1 && meta.axis[0] == 1 && meta.axis[1] == -2 && meta.axis[2] == 3, "two presses later the axes are set (%d %d %d: the sim's IMU has y up)", meta.axis[0], meta.axis[1], meta.axis[2]);
    sim_tilt(0, 0); sim_run(0.3f);
}

static void looper_checks(void)
{
    if (!INST.looper) return;
    section("looper");
    inst_excite();
    sim_menu_close();
    sim_run(0.5f);
    check(looper_state() == LOOP_EMPTY, "no loop at the start");
    SI.rec = true; sim_run(0.2f);
    check(looper_state() == LOOP_EMPTY, "REC down 0.2 s: not yet recording (could still be a tap)");
    sim_run(1.3f);
    check(looper_state() == LOOP_RECORDING, "REC held: recording");
    sim_shot("loop_recording");
    SI.rec = false; sim_run(0.3f);
    float len = looper_len_s();
    check(looper_state() == LOOP_PLAYING && fabsf(len - 1.5f) < 0.04f, "let go after 1.5 s: a %.3f s loop plays (the first 0.3 s were kept)", (double)len);
    sim_shot("loop_playing");
    sim_rec(0.1f); sim_run(0.1f);
    check(looper_state() == LOOP_STOPPED, "a tap stops it");
    sim_rec(0.1f); sim_run(0.1f);
    check(looper_state() == LOOP_PLAYING, "a tap plays it again");
    SI.rec = true; sim_run(0.6f);
    check(looper_state() == LOOP_DUBBING, "REC held while it plays: adding");
    SI.rec = false; sim_run(0.2f);
    check(looper_state() == LOOP_PLAYING && looper_layers() == 2, "let go: back to playing, two layers");
    sim_menu_param(CP(CP_LOOP_CLEAR)); sim_tap(); sim_run(0.1f);
    check(looper_state() == LOOP_EMPTY, "LOOP > CLEAR LOOP erases it");
    sim_menu_close();
}

static void clicks(void)
{
    section("clicks");
    inst_excite();
    sim_menu_close();
    sim_run(1.0f);
    int tested = 0, bad = 0;
    for (int p = 0; p < NI; p++) {
        const param_t *d = param(p);
        if (d->kind == PK_ACT || (d->flags & PF_HIDE)) continue;
        bool skip = false;
        for (int i = 0; inst_click_skip[i]; i++) if (!strncmp(inst_click_skip[i], d->tag, 4)) skip = true;
        if (skip) { note("%s: left out (see test_inst.c)", d->name); continue; }
        float keep = pv[p];
        float worst = 0;
        for (int dir = 0; dir < 2; dir++) {
            param_set(p, dir ? d->min : d->max);
            sim_run(0.35f);
            param_set(p, dir ? d->max : d->min);
            float tc = sim_t;
            sim_run(0.2f);
            float r = m_click(tc, NULL, NULL);
            if (r > worst) worst = r;
        }
        param_set(p, keep);
        sim_run(0.3f);
        tested++;
        if (worst > 3.0f) { bad++; note("%s: a jump %.1fx the signal's own when it goes end to end", d->name, (double)worst); }
    }
    check(bad == 0, "no click when any of %d params jumps from one end to the other (%d click)", tested, bad);
}

// Two clicks in play put the knob on VOLUME for every instrument; one click still opens the menu. The volume glides.
static void knob_volume(void)
{
    section("the knob: a double click is VOLUME");
    sim_menu_close();
    sim_run(0.5f);
    int vp = CP(CP_VOLUME);
    float keep = pv[vp];
    sim_double_tap();
    sim_run(0.05f);
    check(front_menu_level() == 0, "a double click does not open the menu");
    sim_turn(-1);
    sim_run(0.05f);
    float v1 = pv[vp];
    check(v1 < keep, "the turn after it lowers the volume (%.0f %% to %.0f %%)", (double)keep, (double)v1);
    sim_run(3.2f);
    sim_turn(1);
    check(pv[vp] == v1, "3 s at rest, and the knob does what it did before");
    sim_turn(-1);
    sim_run(0.5f);
    sim_tap();
    check(front_menu_level() == 1, "one click still opens the menu");
    sim_menu_close();
    param_set(vp, keep);
    sim_run(0.3f);

    // the master stage alone: a step from 100 % to 50 % glides over tens of ms instead of one block
    master_t m = { .vol_s = 1.0f, .g = 1.0f };
    float buf[AUDIO_BLOCK], g_first = 0, g_30 = 0, g_300 = 0;
    for (int b = 0, f = 0; b < (int)(0.3f * AUDIO_SR) / AUDIO_BLOCK; b++) {
        float pk_in = 0, pk_out = 0;
        for (int i = 0; i < AUDIO_BLOCK; i++, f++) { buf[i] = 0.25f * sinf(6.2831853f * 500.0f * (float)f / (float)AUDIO_SR); if (fabsf(buf[i]) > pk_in) pk_in = fabsf(buf[i]); }
        master_process(&m, buf, AUDIO_BLOCK, 0.5f);
        for (int i = 0; i < AUDIO_BLOCK; i++) if (fabsf(buf[i]) > pk_out) pk_out = fabsf(buf[i]);
        float g = pk_out / pk_in;
        if (b == 0) g_first = g;
        if (b == (int)(0.03f * AUDIO_SR) / AUDIO_BLOCK) g_30 = g;
        g_300 = g;
    }
    check(g_first > 0.95f && g_30 > 0.6f && g_30 < 0.9f && fabsf(g_300 - 0.5f) < 0.03f,
          "a volume step glides: x%.2f after 2 ms, x%.2f after 30 ms, x%.2f after 300 ms", (double)g_first, (double)g_30, (double)g_300);
}

static void health(void)
{
    section("audio health");
    check(m_bad() == 0, "no NaN or inf in %.0f s of output", (double)sim_t);
    check(m_peak(0, sim_t) <= 1.0f, "never over full scale (peak %.3f)", (double)m_peak(0, sim_t));
    metric("seconds_simulated", sim_t);
}

extern bool sim_speaker, sim_rec_button;
static void factory_reset(void)
{
    section("speaker");
    int sp = param_by_tag("spkr");
    if (sp >= 0) param_set(sp, 0.0f);
    bool off = !sim_speaker;
    if (sp >= 0) param_set(sp, 1.0f);
    check(sp >= 0 && off && sim_speaker, "SYSTEM > SPEAKER: OFF shuts the speaker amp, ON wakes it");
    section("rec button");
    int rb = param_by_tag("recb");
    bool start_ok = rb >= 0 && pv[rb] == (INST.rec_button_off ? 0.0f : 1.0f) && sim_rec_button == !INST.rec_button_off;
    float rb0 = rb >= 0 ? pv[rb] : 1.0f;
    if (rb >= 0) param_set(rb, 1.0f - rb0);
    bool flipped = rb >= 0 && sim_rec_button == (rb0 < 1.0f);
    if (rb >= 0) param_set(rb, rb0);
    check(start_ok && flipped, "SYSTEM > REC BUTTON starts %s (INST.rec_button_off) and switches the REC pin", INST.rec_button_off ? "OFF" : "ON");
    section("factory reset");
    sim_menu_param(CP(CP_RESET)); sim_tap(); sim_turn(1); sim_shot("factory_reset"); sim_tap(); sim_run(0.2f);
    bool defaults = true;
    for (int p = 0; p < NP; p++) if (param_in_sound(p) || (param(p)->flags & PF_SYS)) {
        if (param(p)->kind == PK_ACT) continue;
        if (pv[p] != param_def(p) && !(INST.npresets && param_in_sound(p))) defaults = false;
    }
    check(defaults && meta.user_mask == 0 && meta.sound == 0 && meta.calibrated, "FACTORY RESET: settings back to new, user sounds gone, the tilt kept");
    sim_menu_close();
}

static void screens(void)
{
    section("screens");
    sim_menu_close();
    SI.mic = NULL; SI.jx = SI.jy = 0;                            // nobody playing: instruments that call ui_awake() while played let it sleep
    pv[CP(CP_HINTS)] = 1; meta.learned = 0;
    sim_run(9.0f);
    sim_shot("hint");
    param_set(CP(CP_SAVER), 1);
    sim_run(62.0f);
    check(front_screen() == 4, "left alone a minute (SCREEN SAVER 1 MIN): the screensaver");
    sim_shot("screensaver");
    sim_turn(1); sim_run(0.1f);
    check(front_screen() == 1, "any control wakes it (and does nothing else)");
    param_set(CP(CP_SAVER), param(CP(CP_SAVER))->def);
}

void core_checks(void)
{
    tables();
    flash_and_midi();
    app_line();
    sounds();
    menu_walk();
    tilt_setup();
    looper_checks();
    clicks();
    knob_volume();
    screens();
    factory_reset();
}

void core_health(void) { health(); }
