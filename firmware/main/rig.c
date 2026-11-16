// The rig's line (rig.h, rig/PROTOCOL.md). Pure C: the board and rig/twin both build it.
#include "rig.h"
#include "rig_codec.h"
#include "tune.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KEEPALIVE_MS 3000                  // streams and key overrides stop this long after the PC's last 'G'
#define INJECT_MS    500                   // the sensor is back this long after the last 'J'

static bool m_on, q_on, o_on, e_on;
static uint32_t last_g_ms, last_j_ms;
static bool injecting, q_announced, hold;
static uint8_t seq_r, seq_q;
static uint32_t drops;

static int  list_next = -1;                // the table, a few lines a tick
static bool list_end_due;

static uint8_t scr_sent[8][128];
static bool    scr_valid[8];
static uint32_t scr_last_ms;
static uint8_t scr_drops;
static const uint8_t *scr_fb;
static bool    scr_new;

// injected motion: a small queue the main loop drains every tick
#define INJ_N 128
static int16_t inj[INJ_N][6];
static int inj_w, inj_r;

// keys from the PC
static int key_detents, key_sw = -1, key_rec = -1, key_jx = -1, key_jy = -1, key_knob, key_hand = -1;
static bool key_knob_on;
static bool sw_release, rec_release;

// events
typedef struct { const char *name; float v; int ms; char text[12]; } ev_t;
#define EV_N 32
static ev_t evq[EV_N];
static int ev_w, ev_r;

static bool send(const uint8_t *m, int n) { if (rig_port_send(m, n)) return true; drops++; return false; }

static bool send_text(uint8_t cmd, const char *s)
{
    static uint8_t b[800];
    int k = 0;
    b[k++] = 0xF0; b[k++] = 0x7D; b[k++] = cmd;
    for (; *s && k < (int)sizeof b - 1; s++) b[k++] = (uint8_t)*s & 0x7F;
    b[k++] = 0xF7;
    return send(b, k);
}

uint32_t rig_drops(void) { return drops; }
void rig_hold(bool on) { hold = on; }
bool rig_motion_on(void) { return m_on; }
bool rig_state_on(void) { return q_on; }

void rig_init(void)
{
    m_on = q_on = o_on = e_on = false;
    list_next = -1; inj_w = inj_r = 0; ev_w = ev_r = 0;
    memset(scr_valid, 0, sizeof scr_valid);
}

static void streams_off(void)
{
    m_on = q_on = o_on = e_on = false;
    key_sw = key_rec = key_jx = key_jy = key_hand = -1; key_detents = key_knob = 0; key_knob_on = false;
}

static void parse_go(const char *t)
{
    for (const char *p = t; *p; p++) {
        int v = atoi(p + 1);
        switch (*p) {
        case 'm': m_on = v != 0; break;
        case 'q': if (v && !q_on) q_announced = false; q_on = v != 0; break;
        case 'o': if (v && !o_on) memset(scr_valid, 0, sizeof scr_valid); o_on = v != 0; break;
        case 'e': e_on = v != 0; break;
        default: continue;
        }
    }
}

static void tune_reply(int i)
{
    char b[40];
    snprintf(b, sizeof b, "i=%d\nv=%g", i, (double)tune_v[i]);
    send_text('t', b);
}

bool rig_sysex(uint8_t cmd, const uint8_t *body, int n)
{
    char t[48];
    int tn = n < (int)sizeof t - 1 ? n : (int)sizeof t - 1;
    for (int i = 0; i < tn; i++) t[i] = (char)body[i];
    t[tn < 0 ? 0 : tn] = 0;
    uint32_t now = rig_port_ms();
    switch (cmd) {
    case 'G':
        last_g_ms = now;
        parse_go(t);
        return true;
    case 'T':
        if (t[0] == 'l') { list_next = 0; list_end_due = true; }
        else if (t[0] == 'g') { int i = atoi(t + 1); if (i >= 0 && i < TU_N) tune_reply(i); }
        else if (t[0] == 's') {
            int i = atoi(t + 1);
            char *eq = strchr(t, '=');
            if (i >= 0 && i < TU_N && eq) { tune_set(i, strtof(eq + 1, NULL)); tune_reply(i); }
        }
        else if (t[0] == 'w') { bool ok = tune_save(); send_text('t', ok ? "saved=1" : "saved=0"); }
        else if (t[0] == 'd') { tune_defaults(); list_next = 0; list_end_due = true; }
        else if (t[0] == 'p') { tune_preset(atoi(t + 1)); list_next = 0; list_end_due = true; }
        return true;
    case 'J': {
        if (n < 1 || body[0] == 0) { injecting = false; inj_r = inj_w; return true; }   // 'J' 0: the sensor again, now
        int k = body[0];
        if (k > (n - 1) / 18) k = (n - 1) / 18;
        for (int s = 0; s < k; s++) {
            if (inj_w - inj_r >= INJ_N) break;                 // the main loop is behind: the rest are lost
            for (int j = 0; j < 6; j++) inj[inj_w % INJ_N][j] = rig_get16(body + 1 + 18 * s + 3 * j);
            inj_w++;
        }
        last_j_ms = now; injecting = true;
        return true;
    }
    case 'K': {
        last_g_ms = now;
        if (t[0] == 'e') key_detents += atoi(t + 1);
        else if (t[0] == 'E') { if (t[1] == '1') { key_sw = 1; sw_release = false; } else { key_sw = 0; sw_release = true; } }
        else if (t[0] == 'R') { if (t[1] == '1') { key_rec = 1; rec_release = false; } else { key_rec = 0; rec_release = true; } }
        else if (t[0] == 'k') { key_knob += atoi(t + 1); key_knob_on = true; }       // the left knob, in detents
        else if (t[0] == 'h') key_hand = t[1] == '-' ? -1 : atoi(t + 1);              // a hand at this many mm ('h-': the sensor's own)
        else if (t[0] == 'x') {
            const char *y = strchr(t, 'y');
            key_jx = t[1] == '-' ? -1 : atoi(t + 1);
            key_jy = !y || y[1] == '-' ? -1 : atoi(y + 1);
        }
        return true;
    }
    default:
        return false;
    }
}

void rig_keys(rig_keys_t *k, int steps, int dir)
{
    k->counts = key_detents * steps * dir;  // the gesture code counts them back into detents
    key_detents = 0;
    k->sw = key_sw; k->rec = key_rec; k->jx = key_jx; k->jy = key_jy;
    k->knob = key_knob * TI(TU_KN_STEPS) * (TI(TU_KN_DIR) ? 1 : -1); key_knob = 0;
    k->hand = key_hand; k->knob_on = key_knob_on;
    if (sw_release) { key_sw = -1; sw_release = false; }     // the release is seen once, then the pin rules again
    if (rec_release) { key_rec = -1; rec_release = false; }
}

int rig_inject(int16_t (*raw)[6], int max)
{
    if (!injecting) return -1;
    if (rig_port_ms() - last_j_ms > INJECT_MS) { injecting = false; inj_r = inj_w; return -1; }
    int n = 0;
    while (inj_r < inj_w && n < max) { memcpy(raw[n++], inj[inj_r % INJ_N], sizeof inj[0]); inj_r++; }
    return n;
}

void rig_event(const char *name, float v, int ms, const char *text)
{
    if (!e_on || ev_w - ev_r >= EV_N) return;
    ev_t *e = &evq[ev_w % EV_N];
    e->name = name; e->v = v; e->ms = ms;
    snprintf(e->text, sizeof e->text, "%s", text ? text : "");
    ev_w++;
}

void rig_motion(const int16_t (*raw)[6], int n, uint32_t frames, int ranges, int flags)
{
    if (!m_on || n <= 0) return;
    static uint8_t b[14 + 18 * 64];
    const int16_t (*p)[6] = raw;
    while (n > 0) {
        int k = n > 64 ? 64 : n;
        uint8_t *w = b;
        *w++ = 0xF0; *w++ = 0x7D; *w++ = 'r';
        *w++ = seq_r++ & 0x7F;
        w = rig_put32(w, frames);
        *w++ = (uint8_t)TI(TU_IMU_RATE);
        *w++ = (uint8_t)(ranges & 0x7F);
        *w++ = (uint8_t)(flags & 0x7F);
        *w++ = (uint8_t)k;
        for (int s = 0; s < k; s++) for (int j = 0; j < 6; j++) w = rig_put16(w, p[s][j]);
        *w++ = 0xF7;
        send(b, (int)(w - b));
        p += k; n -= k;
    }
}

static void announce(void)
{
    static char b[720];
    int k = snprintf(b, sizeof b, "fields=");
#define RIG_NAME(nm, sc) k += snprintf(b + k, sizeof b - k, "%s:%d,", #nm, sc);
    RIG_STATE(RIG_NAME)
#undef RIG_NAME
    if (k > 0 && b[k - 1] == ',') b[--k] = 0;
    snprintf(b + k, sizeof b - k, "\nstates=idle,rec,play\nmodes=TAPE,GRAIN\nzones=dead,fwd,back");
    q_announced = send_text('Q', b);
}

void rig_state(const float *v, uint32_t frames)
{
    if (!q_on) return;
    if (!q_announced) announce();
    static const float SC[] = {
#define RIG_SC(nm, sc) (float)(sc),
        RIG_STATE(RIG_SC)
#undef RIG_SC
    };
    uint8_t b[10 + 2 * RS_N], *w = b;
    *w++ = 0xF0; *w++ = 0x7D; *w++ = 'q';
    *w++ = seq_q++ & 0x7F;
    w = rig_put32(w, frames);
    for (int i = 0; i < RS_N; i++) {
        float x = v[i] * SC[i];
        w = rig_put14(w, (int)(x + (x >= 0 ? 0.5f : -0.5f)));
    }
    *w++ = 0xF7;
    send(b, (int)(w - b));
}

bool rig_screen_on(void) { return o_on; }
void rig_screen(const uint8_t *fb) { scr_fb = fb; scr_new = true; }

static void screen_tick(uint32_t now)
{
    if (!o_on || !scr_new || !scr_fb) return;
    int fps = TI(TU_RG_FPS);
    if (fps < 1) fps = 1;
    if (now - scr_last_ms < (uint32_t)(1000 / fps)) return;
    scr_last_ms = now; scr_new = false;
    uint8_t b[6 + 147];
    for (int p = 0; p < 8; p++) {
        const uint8_t *pg = scr_fb + 128 * p;
        if (scr_valid[p] && !memcmp(pg, scr_sent[p], 128)) continue;
        b[0] = 0xF0; b[1] = 0x7D; b[2] = 'o'; b[3] = (uint8_t)p; b[4] = scr_drops & 0x7F;
        int k = 5 + rig_pack7(b + 5, pg, 128);
        b[k++] = 0xF7;
        if (send(b, k)) { memcpy(scr_sent[p], pg, 128); scr_valid[p] = true; }
        else { scr_drops++; scr_new = true; break; }   // the rest next time
    }
}

void rig_tick(int dt_ms)
{
    (void)dt_ms;
    uint32_t now = rig_port_ms();
    if (!hold && (m_on || q_on || o_on || e_on || key_sw >= 0 || key_rec >= 0 || key_jx >= 0 || key_hand >= 0 || key_knob_on) && now - last_g_ms > KEEPALIVE_MS) streams_off();
    // the tuning table, three lines a tick
    for (int s = 0; s < 3 && list_next >= 0; s++) {
        if (list_next >= TU_N) {
            char b[16]; snprintf(b, sizeof b, "end=%d", TU_N);
            if (list_end_due && send_text('t', b)) list_end_due = false;
            list_next = -1; break;
        }
        char b[400];
        tune_line(list_next, b, sizeof b);
        if (!send_text('t', b)) break;                       // the wire is full: this line again next tick
        list_next++;
    }
    // events, a few a tick
    for (int s = 0; s < 6 && ev_r < ev_w; s++) {
        ev_t *e = &evq[ev_r % EV_N];
        char b[96];
        snprintf(b, sizeof b, "n=%s\nv=%g\nms=%d\nx=%s", e->name, (double)e->v, e->ms, e->text);
        if (!send_text('e', b)) break;
        ev_r++;
    }
    screen_tick(now);
}
