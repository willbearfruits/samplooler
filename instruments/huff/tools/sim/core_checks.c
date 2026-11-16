// The checks every instrument gets: its tables (tags, ranges, what fits on the screen), its sounds,
// flash and MIDI round trips, a walk through the whole menu, the looper, tilt setup for all 24 ways the
// IMU can sit, a factory reset, no NaN, nothing over full scale, and no click at any parameter's edge. After the
// instrument's own tests: the motion stream and takes (core_late_checks).
#include "sim.h"
#include "looper.h"
#include "calib.h"
#include "oled.h"
#include "ui.h"
#include "dsp.h"
#include "take.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

// ---------------------------------------------------------------- the motion stream and takes (front.h, take.c, motion_rec.py)
static float comp(const vec3_t *v, int j) { return j == 0 ? v->x : j == 1 ? v->y : v->z; }
// A synthetic take: a slow tilt and turn (gentle, so no instrument hears a hit), off the 14-bit grid on purpose;
// 200 samples, 100 ms without any (a gap), 100 more
static void synth_sample(int i, vec3_t *a, vec3_t *g)
{
    float t = (float)i * 0.001f;
    *a = (vec3_t){ 0.1f * sinf(8.2f * t) + 0.00031f, 0.08f * cosf(4.4f * t) - 0.00017f, 1.0f + 0.02f * sinf(31.4f * t) };
    *g = (vec3_t){ 25.0f * sinf(6.0f * t) + 0.061f, -15.0f * cosf(3.0f * t) - 0.033f, 30.0f * sinf(9.0f * t) + 0.07f };
}
static int synth_frame(int i) { return i < 200 ? 32 * i : 9600 + 32 * (i - 200); }
static bool next_sx(const uint8_t *b, int n, int *pos, const uint8_t **m, int *len)   // the next F0 .. F7 from *pos
{
    while (*pos < n && b[*pos] != 0xF0) (*pos)++;
    int s = *pos;
    while (*pos < n && b[*pos] != 0xF7) (*pos)++;
    if (*pos >= n) return false;
    (*pos)++;
    *m = b + s; *len = *pos - s;
    return true;
}

static void motion_takes(void)
{
    section("motion stream and takes");
    sim_in_t keep = SI;                        // whatever the instrument's tests left: a mic callback may write SI.burst
    bool sysex_off = sim_sysex_off;
    SI.mic = NULL; SI.imu = true; SI.rec = SI.pressed = SI.tap = false; SI.enc = 0; SI.nburst = 0;
    sim_sysex_off = false;
    // one message by hand: every field, the clamps, the flags
    static const vec3_t a3[3] = { { 7.99f, -9.0f, 0.0004f }, { -1.2345f, 0.5f, 1.0f }, { 0.0f, -8.0f, 3.3333f } };
    static const vec3_t g3[3] = { { 1999.9f, -2500.0f, 0.1f }, { -0.125f, 12.37f, -1000.0f }, { 0.0f, 0.0f, 2000.0f } };
    static uint8_t m[12 + MOTION_MAX * 12];
    static take_pkt_t pk;
    int len = front_motion_packet(m, 0x55, 0xF2345678u, 7, a3, g3, 3);
    bool ok = len == 48 && take_unpack(m, len, &pk) && pk.seq == 0x55 && pk.frames == 0xF2345678u && pk.flags == 7 && pk.n == 3;
    float worst = 0;
    for (int i = 0; ok && i < 3; i++)
        for (int j = 0; j < 3; j++) {
            float want = fmaxf(-8191.0f / 1024.0f, fminf(8191.0f / 1024.0f, comp(&a3[i], j)));
            worst = fmaxf(worst, fabsf(comp(&pk.acc[i], j) - want) * 1024.0f);
            want = fmaxf(-2000.0f, fminf(2000.0f, comp(&g3[i], j)));
            worst = fmaxf(worst, fabsf(comp(&pk.gyro[i], j) - want) * 4.0f);
        }
    check(ok && worst <= 0.501f, "a motion message (%d bytes, 7-bit) unpacks to its seq, frames, flags and values within half a step "
          "(worst %.3f), clamped at 8 g and 2000 deg/s", len, (double)worst);

    // a synthetic take on file, read back
    enum { N = 300 };
    static vec3_t sa[N], sg[N];
    FILE *f = fopen("out/motion-check.take", "w");
    if (!f) { check(false, "out/motion-check.take could not be written"); SI = keep; sim_sysex_off = sysex_off; return; }
    fprintf(f, "{\"format\": \"samplooler-take/1\", \"instrument\": \"%s\", \"take\": \"check\", \"rate\": 1000, \"axes\": \"screen\", "
               "\"label\": {\"hits\": {\"LEFT\": 2, \"FRONT\": 1}, \"others\": 0}}\n", INST.name);
    for (int i = 0; i < N; i++) {
        synth_sample(i, &sa[i], &sg[i]);
        fprintf(f, "%d %.6f %.6f %.6f %.6f %.6f %.6f %d\n", synth_frame(i), (double)sa[i].x, (double)sa[i].y, (double)sa[i].z,
                (double)sg[i].x, (double)sg[i].y, (double)sg[i].z, TAKE_GYRO | (i == 200 ? TAKE_GAP : 0));
    }
    fclose(f);
    take_t *t = take_load("out/motion-check.take");
    char keys[4][24], v[32];
    int nk = t ? take_keys(t, "label.hits", keys, 4) : 0;
    check(t && t->n == N && t->gaps == 1 && fabsf(t->secs - 12768.0f / 32000.0f) < 1e-6f && take_num(t, "label.hits.LEFT", -1) == 2.0f
          && take_num(t, "label.others", -1) == 0.0f && nk == 2 && !strcmp(keys[0], "LEFT") && !strcmp(keys[1], "FRONT")
          && take_get(t, "take", v, sizeof v) && !strcmp(v, "check"),
          "a take file loads: %d samples, one gap, %.3f s, and its label reads back", t ? t->n : 0, t ? (double)t->secs : 0.0);
    if (!t) { SI = keep; sim_sysex_off = sysex_off; return; }

    // replayed with the stream on: what the feeder gives, and what goes out of the instrument's ctl
    bool motion_was = pv[CP(CP_MOTION)] >= 1.0f;
    if (!motion_was) param_set(CP(CP_MOTION), 1);
    bool want_gyro = INST.burst_gyro && !front_status()->frozen;
    int raw0 = sim_sx_raw_n;
    front_sysex('M', (const uint8_t *)"\x01", 1);
    bool on = front_motion_on();
    take_begin(t);
    int ticks = 0, fed = 0;
    float feed_err = 0;
    static uint32_t tick_frames[64];
    while (take_feed(t)) {
        for (int k = 0; k < SI.nburst && fed < N; k++, fed++)
            for (int j = 0; j < 3; j++) {
                feed_err = fmaxf(feed_err, fabsf(comp(&SI.burst[k], j) - comp(&sa[fed], j)));
                feed_err = fmaxf(feed_err, fabsf(comp(&SI.gburst[k], j) - comp(&sg[fed], j)));
            }
        sim_tick();
        if (ticks < 64) tick_frames[ticks] = audio_frames();
        ticks++;
    }
    front_sysex('M', (const uint8_t *)"\x00", 1);
    bool off = !front_motion_on();
    int raw1 = sim_sx_raw_n;
    sim_tick();
    bool quiet = sim_sx_raw_n == raw1;
    check(fed == N && feed_err < 1e-5f && ticks == 40, "the feeder hands every sample over in its own tick, across the gap "
          "(%d of %d in %d ticks, worst %.2g off the file)", fed, N, ticks, (double)feed_err);
    if (!INST.burst) check(!on && raw1 == raw0 && quiet, "no INST.burst: 'M' 1 starts nothing, nothing goes out");
    else {
        int pos = raw0, msgs = 0, bad = 0, samples = 0, empty = 0, misplaced = 0, seq_bad = 0, frames_bad = 0, gyro_bad = 0;
        float qa = 0, qg = 0;
        const uint8_t *mm; int ml;
        while (next_sx(sim_sx_raw, raw1, &pos, &mm, &ml)) {
            if (!take_unpack(mm, ml, &pk)) { bad++; continue; }
            if (pk.seq != (msgs & 0x7F)) seq_bad++;
            if (msgs < 64 && pk.frames != tick_frames[msgs]) frames_bad++;
            if ((pk.n && ((pk.flags & TAKE_GYRO) != 0) != want_gyro) || (pk.flags & (TAKE_REC | TAKE_KNOB))) gyro_bad++;   // no samples: no gyro
            if (pk.n == 0) empty++;
            for (int k = 0; k < pk.n && samples < N; k++, samples++) {
                int64_t at = (int64_t)t->base + synth_frame(samples);     // where it belongs in the sim's frames
                if (!(at < (int64_t)pk.frames && at >= (int64_t)pk.frames - TICK_FRAMES)) misplaced++;
                for (int j = 0; j < 3; j++) {
                    qa = fmaxf(qa, fabsf(comp(&pk.acc[k], j) - comp(&sa[samples], j)) * 1024.0f);
                    if (want_gyro) qg = fmaxf(qg, fabsf(comp(&pk.gyro[k], j) - comp(&sg[samples], j)) * 4.0f);
                }
            }
            msgs++;
        }
        check(on && msgs == ticks && bad == 0 && seq_bad == 0 && frames_bad == 0 && gyro_bad == 0,
              "'M' 1 starts the stream: one whole message a tick (%d for %d ticks), seq counting up, the tick's frames, flags "
              "(gyro %s)", msgs, ticks, want_gyro ? "on" : "off");
        check(samples == N && qa <= 0.501f && qg <= 0.501f, "what the instrument was given goes out within half a 14-bit step "
              "(%d samples, worst %.3f / %.3f steps)", samples, (double)qa, (double)qg);
        check(empty == 10 && misplaced == 0, "the gap goes out as 10 ticks without samples, and every sample in its own tick "
              "(%d empty, %d out of place)", empty, misplaced);
        check(off && quiet, "'M' 0 stops it: nothing more goes out");
        // the other end: tools/motion_rec.py's decoder on these very bytes
        FILE *x = fopen("out/motion-check.syx", "wb");
        if (x) { fwrite(sim_sx_raw + raw0, 1, (size_t)(raw1 - raw0), x); fclose(x); }
        if (access("../motion_rec.py", R_OK) == 0) {
            FILE *py = popen("python3 ../motion_rec.py verify out/motion-check.syx out/motion-check.take 2>&1", "r");
            char line[256];
            while (py && fgets(line, sizeof line, py)) { line[strcspn(line, "\n")] = 0; note("%s", line); }
            int rc = py ? pclose(py) : -1;
            check(rc == 0, "tools/motion_rec.py decodes the same bytes to the same take (verify: %s)", rc == 0 ? "ok" : "failed");
        } else note("tools/motion_rec.py is not beside tools/sim: its decoder was not checked");
    }
    take_free(t);

    // REC and the knob, at the feeder only (ticked, a REC or a knob press would play the instrument)
    f = fopen("out/motion-flags.take", "w");
    take_t *u = NULL;
    if (f) {
        fprintf(f, "{\"take\": \"flags\"}\n");
        for (int i = 0; i < 1600; i++) {
            int fl = (i >= 100 && i < 300 ? TAKE_REC : 0) | ((i >= 400 && i < 550) || (i >= 700 && i < 1500) ? TAKE_KNOB : 0);
            fprintf(f, "%d 0 0 1 0 0 0 %d\n", 32 * i, fl);
        }
        fclose(f);
        u = take_load("out/motion-flags.take");
    }
    int rec_ticks = 0, knob_ticks = 0, taps = 0, tap_tick = -1, k = 1;
    bool let_go = false;
    if (u) {
        take_begin(u);
        for (;; k++) {
            SI.tap = false;
            bool more = take_feed_at(u, u->base + (uint32_t)(TICK_FRAMES * k));
            rec_ticks += SI.rec; knob_ticks += SI.pressed;
            if (SI.tap) { taps++; tap_tick = k; }
            if (!more) { let_go = !SI.rec && !SI.pressed; break; }
        }
    }
    check(u && rec_ticks == 20 && knob_ticks == 95 && taps == 1 && tap_tick == 56 && let_go && k == 161,
          "REC and the knob come back from the flags: REC 20 ticks, the knob 95, a 150 ms press also taps, a long one does not "
          "(%d, %d, %d tap at tick %d)", rec_ticks, knob_ticks, taps, tap_tick);
    take_free(u);
    SI.nburst = 0; SI.tap = false; SI.rec = false; SI.pressed = false;
    if (!motion_was) param_set(CP(CP_MOTION), 0);
    sim_run(0.1f);
    SI = keep; SI.nburst = 0; SI.tap = false; SI.enc = 0;
    sim_sysex_off = sysex_off;
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

// Rev E: the left knob and the hand reach the instrument as core.h says, and a board without them hands over nothing.
static void rev_e_controls(void)
{
    sim_in_t keep = SI;
    const ctl_t *c = front_ctl();
    sim_run(0.1f);
    check(!c->has_lknob && !c->has_hand && c->lknob == 0 && c->hand == 0.0f, "a board with a joystick: no left knob, no hand");
    sim_rev_e();
    sim_run(0.1f);
    check(c->has_lknob && c->has_hand && !c->has_joy && c->hand == 0.0f && !c->hand_seen, "rev E: the left knob and the sensor are there, no joystick, no hand yet");
    sim_lturn(3);
    int got = c->lknob; sim_tick();
    check(got == 3 && c->lknob == 0, "three detents of the left knob reach the instrument on that tick, and only then");
    SI.hand_mm = 145; sim_tick();
    check(!c->hand_seen && c->hand == 0.0f, "one reading is not a hand yet");
    float worst = 0, was = c->hand;
    for (int i = 0; i < 50; i++) { sim_tick(); float d = fabsf(c->hand - was); if (d > worst) worst = d; was = c->hand; }
    check(c->hand_seen && fabsf(c->hand - 0.5f) < 0.02f && c->hand_mm == 145 && worst < 0.14f, "a hand at 145 mm: half way (%.3f), reached by gliding (largest step %.3f)", (double)c->hand, (double)worst);
    sim_hand(40, 0.5f);
    check(c->hand > 0.98f, "at 40 mm: fully down (%.3f)", (double)c->hand);
    sim_hand(600, 0.04f);                                      // four ticks: two readings at most
    check(c->hand_seen && c->hand > 0.9f, "a reading or two out of range are a glitch: the hand stays");
    sim_hand(0, 0.25f);
    check(!c->hand_seen && c->hand > 0.2f && c->hand < 0.75f, "taken away: it lets go without a jump (%.3f after 0.25 s)", (double)c->hand);
    sim_run(2.0f);
    check(c->hand == 0.0f && c->hand_mm == 0, "and is 0 two seconds later");
    param_set(CP(CP_HAND), 0); sim_hand(60, 0.5f);
    check(!c->has_hand && c->hand == 0.0f, "SYSTEM > HAND OFF: the sensor is ignored");
    param_set(CP(CP_HAND), 1); sim_hand(0, 0.3f);
    SI = keep;
    sim_run(0.5f);
    check(!c->has_lknob && !c->has_hand && c->hand == 0.0f, "back on the board with a joystick: all of it gone again");
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

// After the instrument's own tests, so the time and the motion it takes cannot change what they hear.
void core_late_checks(void) { motion_takes(); rev_e_controls(); }

void core_health(void) { health(); }
