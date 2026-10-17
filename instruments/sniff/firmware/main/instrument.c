// SNIFF: the Wi-Fi around you, as sound. Every frame a device sends strikes a note (its own note, from a
// salted hash of its address), routers' beacons ring as bells, phones looking for networks chirp, the
// tiny ACKs tick. The room itself sings too: the channel state information (CSI) of the busiest
// sender is the room's radio shape, and people moving through it bend that shape, so a chord swells
// when someone walks past. Interference (broken frames, a raised noise floor: a microwave oven, a
// crowd) is a storm. HUNT follows one device like a Geiger counter: walk towards it and it beeps faster.
//
// This file: the param table, the sounds, the hints, and the 100 Hz tick that turns what the radio
// heard into events for the audio engine (inst_sound.c) and the screen (inst_scene.c).
#include "inst.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

sniff_t S;

// ---------------------------------------------------------------- the table
static const char *const groups[] = { "EARS", "TONE", "RHYTHM", "SPACE", "RADIO", "MOTION" };
static const char *const mode_l[] = { "LISTEN", "HUNT" };
static const char *const voice_l[] = { "AUTO", "PLUCK", "BELL", "KALIMBA", "DROP", "CHIRP", "BEEP", "STATIC" };
static const char *const scale_l[] = { "MAJ PENTA", "MIN PENTA", "MAJOR", "MINOR", "DORIAN", "PELOG", "HIRAJOSHI", "WHOLE TONE", "CHROMATIC", "FREE" };
static const char *const key_l[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const char *const reg_l[] = { "LOW", "MID", "HIGH", "AIR" };
static const char *const grid_l[] = { "FREE", "1/4", "1/8", "1/16", "1/32", "1/8 TRIP", "1/16 TRIP" };
static const char *const etime_l[] = { "1/16", "1/8", "3/16", "1/4", "3/8", "1/2" };
static const char *const chan_l[] = { "AUTO", "HOP", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13" };
static const char *const onoff_l[] = { "OFF", "ON" };
static const char *const tilt_l[] = { "OFF", "RING", "PITCH", "REACH" };
static const char *const lean_l[] = { "OFF", "SCALE WALK", "BRIGHT", "ECHO" };
static const char *const stx_l[] = { "OFF", "BRIGHT", "SCALE WALK", "REVERB" };
static const char *const sty_l[] = { "OFF", "REACH", "DENSITY", "ROOM" };

static const param_t params[P_N] = {
    [P_MODE]    = { "MODE", "mode", 0, PK_ENUM, 0, 0, 1, 1, 0, "", mode_l, "HUNT: FIND A DEVICE BY EAR" },
    [P_PKT]     = { "PACKETS", "pkts", 0, PK_NUM, PF_INT, 0, 100, 5, 80, "%", NULL, "EVERY FRAME SOMEONE SENDS" },
    [P_ROOM]    = { "ROOM", "room", 0, PK_NUM, PF_INT, 0, 100, 5, 45, "%", NULL, "PEOPLE MOVING THE SIGNAL" },
    [P_SENS]    = { "ROOM SENS", "sens", 0, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "HOW SMALL A MOVE IT HEARS" },
    [P_STORM]   = { "STORM", "strm", 0, PK_NUM, PF_INT, 0, 100, 5, 35, "%", NULL, "INTERFERENCE, RADIO NOISE" },
    [P_TICKS]   = { "ACK TICKS", "tick", 0, PK_NUM, PF_INT, 0, 100, 5, 30, "%", NULL, "THE TINY REPLIES, AS CLICKS" },
    [P_REACH]   = { "REACH", "rech", 0, PK_NUM, PF_INT, -100, -40, 2, -92, "dBm", NULL, "FAINTER ONES ARE LEFT OUT" },
    [P_DENS]    = { "DENSITY", "dens", 0, PK_NUM, PF_LOG, 4, 400, 1.12f, 50, "/s", NULL, "NOTES A SECOND, THEN RAIN" },
    [P_NEXT]    = { "NEXT TARGET", "next", 0, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "HUNT THE NEXT DEVICE" },
    [P_VOICE]   = { "VOICE", "voic", 1, PK_ENUM, 0, 0, V_N - 1, 1, V_AUTO, "", voice_l, "AUTO: A SOUND PER KIND" },
    [P_SCALE]   = { "SCALE", "scal", 1, PK_ENUM, 0, 0, 9, 1, 0, "", scale_l, "FREE: NO SCALE AT ALL" },
    [P_KEY]     = { "KEY", "key ", 1, PK_ENUM, 0, 0, 11, 1, 9, "", key_l, "MIDI NOTES IN SET IT TOO" },
    [P_REG]     = { "REGISTER", "reg ", 1, PK_ENUM, 0, 0, 3, 1, 1, "", reg_l, "HOW HIGH THE DEVICES SING" },
    [P_SPREAD]  = { "SPREAD", "sprd", 1, PK_NUM, PF_INT, 1, 4, 1, 2, "oct", NULL, "OCTAVES THE DEVICES SPAN" },
    [P_TRANS]   = { "TRANSPOSE", "trns", 1, PK_NUM, PF_INT | PF_SIGNED, -12, 12, 1, 0, "st", NULL, "EVERYTHING UP OR DOWN" },
    [P_RING]    = { "RING", "ring", 1, PK_NUM, PF_LOG, 0.03f, 8, 1.15f, 0.7f, "s", NULL, "HOW LONG EACH NOTE RINGS" },
    [P_LENR]    = { "BY LENGTH", "lenr", 1, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "LONG FRAMES RING LONGER" },
    [P_BRIGHT]  = { "BRIGHT", "brit", 1, PK_NUM, PF_INT, 0, 100, 5, 60, "%", NULL, "DARK TO SHARP" },
    [P_GRID]    = { "GRID", "grid", 2, PK_ENUM, 0, 0, 6, 1, 0, "", grid_l, "FREE, OR SNAPPED TO A BEAT" },
    [P_TEMPO]   = { "TEMPO", "tmpo", 2, PK_NUM, PF_INT, 40, 240, 1, 100, "BPM", NULL, "FOR THE GRID AND THE ECHO" },
    [P_SWING]   = { "SWING", "swng", 2, PK_NUM, PF_INT, 0, 75, 5, 0, "%", NULL, "EVERY OTHER STEP A BIT LATE" },
    [P_PERSTEP] = { "PER STEP", "pstp", 2, PK_NUM, PF_INT, 1, 8, 1, 2, "", NULL, "MOST NOTES ON ONE STEP" },
    [P_VERB]    = { "REVERB", "verb", 3, PK_NUM, PF_INT, 0, 100, 5, 35, "%", NULL, "HOW MUCH ROOM AROUND IT" },
    [P_SIZE]    = { "SIZE", "size", 3, PK_NUM, PF_INT, 0, 100, 5, 55, "%", NULL, "SMALL ROOM TO HUGE HALL" },
    [P_DIST]    = { "DISTANCE", "dist", 3, PK_NUM, PF_INT, 0, 100, 5, 60, "%", NULL, "FAR DEVICES SOUND FARTHER" },
    [P_ECHO]    = { "ECHO", "echo", 3, PK_NUM, PF_INT, 0, 100, 5, 15, "%", NULL, "REPEATS IN TIME WITH IT" },
    [P_ETIME]   = { "ECHO TIME", "etim", 3, PK_ENUM, 0, 0, 5, 1, 2, "", etime_l, "IN BEATS OF THE TEMPO" },
    [P_EFB]     = { "ECHO REPEAT", "efb ", 3, PK_NUM, PF_INT, 0, 90, 5, 40, "%", NULL, "HOW MANY TIMES IT RETURNS" },
    [P_CHAN]    = { "CHANNEL", "chan", 4, PK_ENUM, PF_SYS, 0, 14, 1, CH_AUTO, "", chan_l, "AUTO: THE BUSIEST CHANNEL" },
    [P_DWELL]   = { "HOP TIME", "dwel", 4, PK_NUM, PF_LOG | PF_SYS, 0.1f, 3, 1.15f, 0.4f, "s", NULL, "HOW LONG ON EACH CHANNEL" },
    [P_BEACON]  = { "BEACONS", "bcon", 4, PK_ENUM, 0, 0, 1, 1, 1, "", onoff_l, "ROUTERS SAYING: I AM HERE" },
    [P_BROKEN]  = { "BROKEN", "brkn", 4, PK_ENUM, 0, 0, 1, 1, 1, "", onoff_l, "BROKEN FRAMES MAKE STORM" },
    [P_TILT]    = { "TILT", "tilt", 5, PK_ENUM, 0, 0, 3, 1, 1, "", tilt_l, "BOTTOM EDGE DOWN DOES" },
    [P_LEAN]    = { "LEAN", "lean", 5, PK_ENUM, 0, 0, 3, 1, 1, "", lean_l, "LEFT AND RIGHT DO THIS" },
    [P_STX]     = { "STICK X", "stkx", 5, PK_ENUM, 0, 0, 3, 1, 1, "", stx_l, "JOYSTICK LEFT AND RIGHT" },
    [P_STY]     = { "STICK Y", "stky", 5, PK_ENUM, 0, 0, 3, 1, 1, "", sty_l, "JOYSTICK UP AND DOWN" },
};

static const preset_t presets[] = {
    { "THE ROOM",   "" },
    { "MUSIC BOX",  "voic=3 scal=0 key=0 reg=2 grid=3 tmpo=96 ring=0.9 verb=40 echo=20 etim=1 pstp=2 room=30 dens=40" },
    { "RAIN",       "voic=4 scal=9 reg=3 sprd=2 ring=0.12 dens=220 verb=45 strm=55 room=25 tick=50 brit=70" },
    { "BELL TOWER", "voic=2 scal=5 key=2 reg=0 ring=5 dens=12 verb=65 size=90 echo=10 room=40" },
    { "BIRDS",      "voic=5 scal=2 key=7 reg=3 ring=0.3 dens=40 verb=40 dist=80 room=20 tick=10" },
    { "GHOSTS",     "pkts=0 room=100 sens=70 strm=10 verb=60 size=80 scal=1 key=9 reg=0 tick=0" },
    { "STORM",      "pkts=20 room=10 strm=100 voic=7 verb=30 tick=60" },
    { "HUNT",       "mode=1 verb=15 echo=0" },
    { "MORSE",      "voic=6 scal=0 sprd=1 reg=2 lenr=100 ring=0.12 verb=15 dens=25 echo=0 room=0 strm=10 tick=0" },
    { "DRONE",      "voic=2 reg=0 ring=8 dens=6 room=80 verb=80 size=95 echo=25 etim=5 efb=60 scal=1 tick=0" },
    { "GLITCH",     "voic=7 scal=8 sprd=4 reg=2 ring=0.04 grid=4 tmpo=140 pstp=3 verb=0 echo=40 etim=0 efb=60 brit=90 tick=70" },
    { "KALIMBA",    "voic=3 scal=1 key=9 reg=1 ring=1.4 verb=30 room=35" },
    { "SCANNER",    "voic=7 pkts=70 strm=60 tick=80 verb=10 ring=0.2 brit=40 room=0" },
    { "CLOCKWORK",  "voic=1 grid=5 tmpo=120 pstp=1 ring=0.25 scal=3 key=4 echo=25 etim=3 verb=20" },
};
#define NPRESETS ((int)(sizeof presets / sizeof presets[0]))

enum { LEARN_HUNT = LEARN_INST, LEARN_GHOST };
static const hint_t hints[] = {
    { "TURN THE KNOB: NEW SOUNDS", LEARN_TURN },
    { "WALK PAST THE ROUTER AND LISTEN", LEARN_GHOST },
    { "TILT ME: THE NOTES RING LONGER", LEARN_TILT },
    { "STICK UP: ONLY HEAR WHAT IS NEAR", LEARN_JOY },
    { "FIND THE ROUTER: TRY THE SOUND HUNT", LEARN_HUNT },
    { "HOLD REC TO LOOP THE AIR", LEARN_REC },
    { "PRESS THE KNOB FOR THE MENU", LEARN_MENU },
};

// ---------------------------------------------------------------- scales and notes
typedef struct { int n; int8_t st[12]; } scale_t;
static const scale_t scales[] = {
    { 5, { 0, 2, 4, 7, 9 } }, { 5, { 0, 3, 5, 7, 10 } }, { 7, { 0, 2, 4, 5, 7, 9, 11 } }, { 7, { 0, 2, 3, 5, 7, 8, 10 } },
    { 7, { 0, 2, 3, 5, 7, 9, 10 } }, { 5, { 0, 1, 3, 7, 8 } }, { 5, { 0, 2, 3, 7, 8 } }, { 6, { 0, 2, 4, 6, 8, 10 } },
    { 12, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } }, { 0, { 0 } },
};
static const int reg_base[4] = { 36, 48, 60, 72 };

// the note of scale degree deg (any integer) in the current scale, key and register
static float degree_note(int deg)
{
    const scale_t *s = &scales[(int)pv[P_SCALE]];
    int n = s->n ? s->n : 12;
    int oct = deg >= 0 ? deg / n : -((-deg + n - 1) / n), step = deg - oct * n;
    int st = s->n ? s->st[step] : step;
    return (float)(reg_base[(int)pv[P_REG]] + (int)pv[P_KEY] + (int)pv[P_TRANS] + 12 * oct + st);
}

// ---------------------------------------------------------------- state
uint32_t sniff_dbg_at[64], sniff_dbg_n;                // the last strikes' start frames (tools/sim checks the grid)
static uint32_t ticks;
static float walk_deg, ring_mul = 1.0f, pitch_bend, reach_add, bright_add, echo_add, verb_add, dens_mul = 1.0f, room_add;
static float tokens, overflow_rate, overflow_n;
static uint32_t step_idx[8]; static int step_cnt[8];
static int  midi_off_note[32]; static uint32_t midi_off_at[32]; static int midi_offs;
static float led_r, led_g, led_b;
// channels
static int  cur_chan = 1, dwell_ticks, scan_i;
static int  quiet_ticks, scan_age;
static uint32_t chan_hits;
// room
static uint32_t room_src; static int room_src_dev = -1, csi_ticks_since;
static float base_shape[CSI_BANDS], dev_shape[CSI_BANDS], m_floor = 0.02f, m_raw;
static bool base_ok;
// storm
static float bad_fast, bad_slow, nf_s, nf_base; static int bad_tick, thunder_cool; static bool nf_ok;
// hunt
static uint32_t target_who; static int target_seen_tick; static float t_fast = -90, t_slow = -90;
static bool stick_armed = true;

// ---------------------------------------------------------------- devices
static int dev_find(uint32_t who, bool add)
{
    int free_i = -1, old_i = 0;
    for (int i = 0; i < NDEV; i++) {
        if (S.dev[i].who == who) return i;
        if (!S.dev[i].who) { if (free_i < 0) free_i = i; }
        else if (S.dev[i].last < S.dev[old_i].last || !S.dev[old_i].who) old_i = i;
    }
    if (!add) return -1;
    int i = free_i >= 0 ? free_i : old_i;
    S.dev[i] = (wdev_t){ .who = who, .rssi = -90, .ang = (float)(who & 0xffffu) / 65536.0f * TWO_PI, .last = ticks };
    if (i == S.target && who != target_who) S.target = -1;
    return i;
}

static void devices_second(void)
{
    int n = 0;
    for (int i = 0; i < NDEV; i++) {
        wdev_t *d = &S.dev[i];
        if (!d->who) continue;
        d->rate += 0.5f * ((float)d->hits - d->rate);
        d->hits = 0;
        uint32_t age = ticks - d->last;
        if (age > 6000) { if (i == S.target) S.target = -1; d->who = 0; continue; }
        if (age < 3000) n++;
    }
    S.ndev = n;
}

// ---------------------------------------------------------------- channels
static void set_chan(int c) { if (c < 1) c = 1; if (c > 13) c = 13; if (c != cur_chan) { cur_chan = c; radio_set_channel(c); } S.chan = c; }

static void channels_tick(void)
{
    int mode = (int)pv[P_CHAN];
    // HUNT stays with its target
    if (S.mode == MODE_HUNT && S.target >= 0 && S.dev[S.target].chan) { S.scanning = false; set_chan(S.dev[S.target].chan); return; }
    if (mode >= 2) { S.scanning = false; set_chan(mode - 1); S.hist[cur_chan] += 0.01f * ((float)chan_hits * 100.0f - S.hist[cur_chan]); chan_hits = 0; return; }
    int dwell = mode == CH_HOP ? (int)(pv[P_DWELL] * 100.0f) : 25;
    if (mode == CH_AUTO && !S.scanning) {
        // locked on the busiest channel: look again if it goes quiet for 15 s, or every 5 minutes
        quiet_ticks = S.hist[cur_chan] < 2.0f ? quiet_ticks + 1 : 0;
        if (++scan_age > 30000 || quiet_ticks > 1500) { S.scanning = true; scan_i = 0; scan_age = 0; quiet_ticks = 0; dwell_ticks = 0; set_chan(1); chan_hits = 0; }
        else { S.hist[cur_chan] += 0.01f * ((float)chan_hits * 100.0f - S.hist[cur_chan]); chan_hits = 0; return; }
    }
    if (mode == CH_HOP) S.scanning = false;
    if (++dwell_ticks < dwell) return;
    // the end of a dwell: what this channel carried, then the next one
    float r = (float)chan_hits * 100.0f / (float)dwell_ticks;
    S.hist[cur_chan] = S.hist[cur_chan] > 0 ? 0.5f * (S.hist[cur_chan] + r) : r;
    chan_hits = 0; dwell_ticks = 0;
    if (mode == CH_AUTO) {
        if (++scan_i >= 13) {
            int best = 1;
            for (int c = 2; c <= 13; c++) if (S.hist[c] > S.hist[best]) best = c;
            S.scanning = false; scan_age = 0;
            set_chan(best);
            app_log("busiest channel: %d (%.0f frames/s)", best, (double)S.hist[best]);
            return;
        }
        set_chan(scan_i + 1);
    } else set_chan(cur_chan % 13 + 1);
}

// ---------------------------------------------------------------- sounding a frame
static uint32_t grid_at(uint32_t at)
{
    static const float div[7] = { 0, 1.0f, 0.5f, 0.25f, 0.125f, 1.0f / 3.0f, 1.0f / 6.0f };
    int g = (int)pv[P_GRID];
    if (!g) return at;
    float step = 60.0f / pv[P_TEMPO] * div[g] * (float)AUDIO_SR;
    uint32_t k = (uint32_t)((float)at / step) + 1u;
    uint32_t t = (uint32_t)((float)k * step);
    if (k & 1u) t += (uint32_t)(pv[P_SWING] * 0.01f * 0.5f * step);
    // at most PER STEP notes on one step: the rest are rain
    int slot = (int)(k & 7u);
    if (step_idx[slot] != k) { step_idx[slot] = k; step_cnt[slot] = 0; }
    if (step_cnt[slot] >= (int)pv[P_PERSTEP]) return 0;
    step_cnt[slot]++;
    return t ? t : 1;
}

static void midi_out_note(int note, int vel)
{
    if (note < 0 || note > 127 || midi_offs >= 32) return;
    midi_note(note, vel);
    midi_off_note[midi_offs] = note; midi_off_at[midi_offs] = ticks + 8; midi_offs++;
}

static void sound_frame(const radio_pkt_t *p, int di, float prox)
{
    wdev_t *d = &S.dev[di];
    int voice = (int)pv[P_VOICE];
    bool beacon = p->kind == PKT_MGMT && (p->sub == 8 || p->sub == 5);
    bool probe = p->kind == PKT_MGMT && p->sub == 4;
    strike_t s = { .bright = clampf(pv[P_BRIGHT] * 0.01f + bright_add, 0, 1) };
    // the device's own degree in the scale, from its hash; lean and the stick walk it up and down
    int n = scales[(int)pv[P_SCALE]].n;
    float note;
    if (!n) note = (float)reg_base[(int)pv[P_REG]] + (float)(d->who % (1200u * (uint32_t)pv[P_SPREAD])) * 0.01f + pv[P_TRANS];
    else note = degree_note((int)(d->who % (uint32_t)(n * (int)pv[P_SPREAD])) + (int)lroundf(walk_deg));
    note += pitch_bend;
    float len_k = 1.0f + pv[P_LENR] * 0.01f * ((float)p->len / 1500.0f * 2.0f - 0.5f);
    s.ring = pv[P_RING] * ring_mul * clampf(len_k, 0.3f, 2.5f);
    s.amp = 0.03f + 0.13f * prox * prox + 0.04f * prox;
    s.wet = clampf(0.06f + pv[P_DIST] * 0.01f * (1.0f - prox) * 0.9f, 0, 0.95f);
    if (S.mode == MODE_HUNT) {                                    // only the target, as soft drops
        s.timbre = T_DROP; s.ring = 0.08f; s.amp *= 0.6f; note += 12.0f;
    } else if (voice == V_AUTO) {
        if (beacon) { s.timbre = T_BELL; note += 12.0f; s.amp *= 0.45f; s.ring *= 1.4f; }
        else if (probe) { s.timbre = T_CHIRP; note += 12.0f; s.ring = fminf(s.ring, 0.35f); }
        else if (p->kind == PKT_MGMT) s.timbre = T_KALIMBA;
        else s.timbre = T_PLUCK;
    } else {
        static const uint8_t tm[V_N] = { T_PLUCK, T_PLUCK, T_BELL, T_KALIMBA, T_DROP, T_CHIRP, T_BEEP, T_STATIC };
        s.timbre = tm[voice];
        if (beacon) { note += 12.0f; s.amp *= 0.5f; }
        if (s.timbre == T_BEEP) s.ring = 0.03f + pv[P_RING] * ring_mul * clampf(len_k, 0.2f, 3.0f) * 0.25f;
    }
    s.f = midi_hz(clampf(note, 12.0f, 115.0f));
    // the budget: past DENSITY notes a second the frame is a raindrop instead
    if (tokens < 1.0f) { overflow_n += 1.0f; return; }
    uint32_t at = p->frame + 640u;                                  // 20 ms behind, keeping the frames' own spacing
    at = grid_at(at);
    if (!at) { overflow_n += 1.0f; return; }
    s.at = at;
    if (!snd_strike(&s)) return;
    sniff_dbg_at[sniff_dbg_n++ & 63] = at;
    tokens -= 1.0f;
    S.sniff = 1.0f;
    scene_heard(di, beacon ? 0 : probe ? 1 : 2);
    d->pulse = 1.0f;
    midi_out_note((int)lroundf(note), 30 + (int)(prox * 97.0f));
    if (beacon) { led_b += 60; led_g += 25; } else if (probe) { led_r += 70; led_g += 55; } else { led_g += 50; led_r += 10; }
}

// ---------------------------------------------------------------- the room (CSI)
static void room_frame(const radio_csi_t *c)
{
    float tot = 0;
    for (int b = 0; b < CSI_BANDS; b++) tot += (float)c->amp[b];
    if (tot < 13.0f) return;
    csi_ticks_since = 0;
    float sh[CSI_BANDS], dsum = 0;
    for (int b = 0; b < CSI_BANDS; b++) sh[b] = (float)c->amp[b] * (float)CSI_BANDS / tot;
    if (!base_ok) { memcpy(base_shape, sh, sizeof sh); base_ok = true; }
    for (int b = 0; b < CSI_BANDS; b++) {
        float dv = fabsf(sh[b] - base_shape[b]);
        dev_shape[b] += 0.3f * (dv - dev_shape[b]);
        dsum += dv;
        base_shape[b] += 0.01f * (sh[b] - base_shape[b]);
        S.shape[b] += 0.25f * (sh[b] - S.shape[b]);
    }
    float m = dsum / (float)CSI_BANDS;
    m_raw += (m > m_raw ? 0.3f : 0.03f) * (m - m_raw);
    if (m_raw < m_floor) m_floor += 0.05f * (m_raw - m_floor); else m_floor += 0.0005f * (m_raw - m_floor);
}

static void room_tick(void)
{
    if (csi_ticks_since < 100000) csi_ticks_since++;
    S.room_live = csi_ticks_since < 200;
    float k = 4.0f + 36.0f * pv[P_SENS] * 0.01f;
    float target = S.room_live ? clampf((m_raw - m_floor * 1.3f) * k, 0, 1) : 0.0f;
    S.motion += (target > S.motion ? 0.2f : 0.02f) * (target - S.motion);
    if (!S.room_live) for (int b = 0; b < CSI_BANDS; b++) { S.shape[b] += 0.01f * (1.0f - S.shape[b]); dev_shape[b] *= 0.98f; }
    if (S.motion > 0.5f) coach_done(LEARN_GHOST);
    // once a second: follow the sender whose frames carry the most CSI on this channel
    if (ticks % 100 == 0) {
        int best = -1; float bs = 0;
        for (int i = 0; i < NDEV; i++) {
            wdev_t *d = &S.dev[i];
            if (!d->who || d->chan != cur_chan) { d->csi = 0; continue; }
            float sc_ = (float)d->csi * (1.0f + (d->rssi + 100.0f) / 40.0f);
            if (d->csi >= 3 && sc_ > bs) { bs = sc_; best = i; }
            d->csi = 0;
        }
        uint32_t w = best >= 0 ? S.dev[best].who : 0;
        if (w != room_src && (best >= 0 || csi_ticks_since > 300)) { room_src = w; room_src_dev = best; radio_follow(w); base_ok = false; }
    }
    // the pad: three chord tones over three octaves, the louder the more the room moves in that band
    int n = scales[(int)pv[P_SCALE]].n ? scales[(int)pv[P_SCALE]].n : 12;
    static const int8_t deg5[PAD_N] = { 0, 2, 4, 5, 7, 9, 10, 12 };      // pentatonic-ish steps over three octaves
    for (int k2 = 0; k2 < PAD_N; k2++) {
        int deg = n >= 7 ? (k2 / 3) * n + (k2 % 3) * 2 : deg5[k2];
        if (n == 12) deg = (k2 / 3) * 12 + (int)(k2 % 3) * 4;
        sc.pad_f[k2] = midi_hz(degree_note(deg) - 12.0f);
        int b = k2 * (CSI_BANDS - 1) / (PAD_N - 1);
        sc.pad_g[k2] = clampf(0.3f + 0.5f * S.shape[b] + 10.0f * dev_shape[b], 0, 2.0f) * (k2 < 3 ? 1.0f : k2 < 6 ? 0.7f : 0.45f);
    }
    sc.motion = S.motion;
}

// ---------------------------------------------------------------- storm
static void storm_tick(float level)
{
    float br = (float)bad_tick * 100.0f;
    bad_tick = 0;
    bad_fast += 0.05f * (br - bad_fast);
    bad_slow += 0.001f * (br - bad_slow);
    float elev = nf_s - nf_base; if (elev < 0) elev = 0;
    S.storm = clampf(log10f(1.0f + bad_fast / 5.0f) * 0.5f + elev / 12.0f, 0, 1);
    sc.crackle = fminf(bad_fast * 1.5f + elev * 4.0f, 1500.0f);
    sc.rumble = clampf(elev / 10.0f + S.storm * 0.3f, 0, 1);
    if (thunder_cool > 0) thunder_cool--;
    else if (bad_fast > 3.0f * bad_slow + 25.0f && level > 0.01f) { sc.thunder++; S.thunder = 1.0f; thunder_cool = 250; led_r += 255; led_g += 200; led_b += 255; }
    if (S.storm > 0.05f) led_r += 25.0f * S.storm * (float)(ticks % 7 < 2);
}

// ---------------------------------------------------------------- HUNT
static void hunt_pick(int dir)
{
    // devices heard in the last 10 s, loudest first
    int idx[NDEV], n = 0;
    for (int i = 0; i < NDEV; i++) if (S.dev[i].who && ticks - S.dev[i].last < 1000) idx[n++] = i;
    for (int a = 1; a < n; a++) for (int b = a; b > 0 && S.dev[idx[b]].rssi > S.dev[idx[b - 1]].rssi; b--) { int t = idx[b]; idx[b] = idx[b - 1]; idx[b - 1] = t; }
    if (!n) { S.target = -1; target_who = 0; return; }
    int cur = -1;
    for (int i = 0; i < n; i++) if (idx[i] == S.target) cur = i;
    int k = dir == 0 || cur < 0 ? 0 : ((cur + dir) % n + n) % n;
    S.target = idx[k]; target_who = S.dev[idx[k]].who; target_seen_tick = (int)ticks;
    S.target_rank = k + 1; S.target_of = n;
    t_fast = t_slow = S.target_rssi = S.dev[idx[k]].rssi;
    S.lost = false;
}

static void hunt_tick(void)
{
    sc.hunt = S.mode == MODE_HUNT ? 1.0f : 0.0f;
    if (S.mode != MODE_HUNT) return;
    coach_done(LEARN_HUNT);
    if (S.target < 0 || S.dev[S.target].who != target_who) hunt_pick(0);
    if (S.target < 0) { S.lost = true; sc.hunt_rate = 0.4f; sc.hunt_pitch = 220.0f; S.prox = 0; return; }
    S.lost = (int)ticks - target_seen_tick > 500;
    t_fast += 0.08f * (S.target_rssi - t_fast);
    t_slow += 0.006f * (S.target_rssi - t_slow);
    S.trend = t_fast - t_slow > 1.5f ? 1 : t_fast - t_slow < -1.5f ? -1 : 0;
    float p = clampf((t_fast + 92.0f) / 57.0f, 0, 1);
    S.prox = S.lost ? S.prox * 0.99f : p;
    sc.hunt_rate = S.lost ? 0.5f : 1.0f + 15.0f * S.prox * S.prox;
    sc.hunt_pitch = S.lost ? 220.0f : 330.0f * exp2f(2.6f * S.prox);
}

// ---------------------------------------------------------------- the instrument
static void apply(void)
{
    static const float et[6] = { 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f };
    float beat = 60.0f / pv[P_TEMPO];
    float hunt_duck = S.mode == MODE_HUNT ? 0.3f : 1.0f;
    sc.pkt = pv[P_PKT] * 0.01f * (S.mode == MODE_HUNT ? 1.0f : 1.0f);
    sc.room = clampf(pv[P_ROOM] * 0.01f + room_add, 0, 1) * hunt_duck;
    sc.storm = pv[P_STORM] * 0.01f * hunt_duck;
    sc.ticks = pv[P_TICKS] * 0.01f * hunt_duck;
    sc.bright = clampf(pv[P_BRIGHT] * 0.01f + bright_add, 0, 1);
    sc.verb = clampf(pv[P_VERB] * 0.01f + verb_add, 0, 1);
    sc.size = pv[P_SIZE] * 0.01f;
    sc.echo = clampf(pv[P_ECHO] * 0.01f + echo_add, 0, 1);
    sc.efb = pv[P_EFB] * 0.01f;
    float es = beat * et[(int)pv[P_ETIME]];
    while (es > 1.0f) es *= 0.5f;
    sc.echo_s = es;
}

static void changed(int p)
{
    if (p == -1 || p == P_MODE) {
        int m = (int)pv[P_MODE];
        if (m != S.mode) { S.mode = m; if (m == MODE_HUNT) hunt_pick(0); }
    }
    if (p == -1 || p == P_BROKEN) radio_set_broken(pv[P_BROKEN] >= 1.0f);
    static int chan_mode = -1;                                      // a sound that leaves CHANNEL as it was must not rescan
    if ((p == -1 || p == P_CHAN) && (int)pv[P_CHAN] != chan_mode) {
        chan_mode = (int)pv[P_CHAN]; dwell_ticks = 0; chan_hits = 0;
        if (chan_mode == CH_AUTO) { S.scanning = true; scan_i = 0; set_chan(1); }
    }
    apply();
}

static void action(int p)
{
    if (p != P_NEXT) return;
    if (S.mode != MODE_HUNT) { toast("SET MODE TO HUNT FIRST", 1500); return; }
    hunt_pick(1);
    char b[32]; snprintf(b, sizeof b, "TARGET %d OF %d", S.target_rank, S.target_of); toast(b, 1200);
}

static void init(void)
{
    snd_init();
    memset(&S, 0, sizeof S);
    S.target = -1; S.mode = (int)pv[P_MODE];
    for (int b = 0; b < CSI_BANDS; b++) S.shape[b] = 1.0f;
    S.radio_ok = radio_start();
    S.scanning = (int)pv[P_CHAN] == CH_AUTO;
    cur_chan = 0; set_chan((int)pv[P_CHAN] >= 2 ? (int)pv[P_CHAN] - 1 : 1);
    radio_set_broken(pv[P_BROKEN] >= 1.0f);
    if (!S.radio_ok) app_log("Wi-Fi would not start: SNIFF is deaf");
    apply();
}

static void motion(const ctl_t *c)
{
    // tilt (bottom edge down), lean (left/right), the stick: each does what MOTION says
    float ty = c->has_imu ? clampf(c->lean_y / 40.0f, -1, 1) : 0;
    float lx = c->has_imu ? clampf(c->lean_x / 40.0f, -1, 1) : 0;
    float jx = c->has_joy ? c->jx : 0, jy = c->has_joy ? c->jy : 0;
    ring_mul = 1; pitch_bend = 0; reach_add = 0; bright_add = 0; echo_add = 0; verb_add = 0; dens_mul = 1; room_add = 0;
    float walk = 0;
    switch ((int)pv[P_TILT]) { case 1: ring_mul = exp2f(ty * 2.5f); break; case 2: pitch_bend = ty * 12.0f; break; case 3: reach_add = ty * 30.0f; break; }
    switch ((int)pv[P_LEAN]) { case 1: walk += lx * 6.0f; break; case 2: bright_add += lx * 0.5f; break; case 3: echo_add += fmaxf(lx, 0) * 0.7f; break; }
    if (S.mode != MODE_HUNT)
        switch ((int)pv[P_STX]) { case 1: bright_add += jx * 0.5f; break; case 2: walk += jx * 6.0f; break; case 3: verb_add += jx * 0.5f; break; }
    switch ((int)pv[P_STY]) { case 1: reach_add += jy * 45.0f; break; case 2: dens_mul = exp2f(jy * 2.5f); break; case 3: room_add = jy * 0.6f; break; }
    walk_deg += 0.25f * (walk - walk_deg);
    // HUNT: a flick of the stick picks the next or the previous device
    if (S.mode == MODE_HUNT && c->has_joy) {
        if (stick_armed && fabsf(jx) > 0.7f) { hunt_pick(jx > 0 ? 1 : -1); stick_armed = false; char b[32]; snprintf(b, sizeof b, "TARGET %d OF %d", S.target_rank, S.target_of); toast(b, 900); }
        else if (fabsf(jx) < 0.3f) stick_armed = true;
    }
}

static void tick(const ctl_t *c)
{
    ticks++;
    motion(c);
    apply();
    hunt_tick();
    float reach = clampf(pv[P_REACH] + reach_add, -100.0f, -30.0f);
    float dens = clampf(pv[P_DENS] * dens_mul, 1.0f, 1000.0f);
    tokens = fminf(tokens + dens * c->dt, fmaxf(2.0f, dens * 0.25f));
    // what the radio heard since the last tick
    radio_pkt_t p;
    int k = 0, acks = 0;
    while (k < 400 && radio_pkt(&p)) {
        k++;
        chan_hits++;
        if (p.noise && !nf_ok) { nf_s = nf_base = (float)p.noise; nf_ok = true; }
        if (p.noise) { nf_s += 0.02f * ((float)p.noise - nf_s); if ((float)p.noise < nf_base) nf_base += 0.2f * ((float)p.noise - nf_base); else nf_base += 0.0003f * ((float)p.noise - nf_base); }
        if (p.kind == PKT_BAD) { bad_tick++; continue; }
        if (p.kind == PKT_CTRL || !p.who) {
            if (p.rssi < reach || S.mode == MODE_HUNT || ++acks > 6) continue;
            float pr = clampf(((float)p.rssi - reach) / (-35.0f - reach), 0, 1);
            strike_t t = { .at = p.frame + 640u, .amp = 0.25f + 0.75f * pr, .timbre = T_TICK };
            snd_strike(&t);
            continue;
        }
        int di = dev_find(p.who, true);
        wdev_t *d = &S.dev[di];
        d->rssi += 0.3f * ((float)p.rssi - d->rssi);
        d->last = ticks; d->hits++; d->chan = p.chan ? p.chan : (uint8_t)cur_chan;
        if (p.kind == PKT_MGMT && p.sub == 8) d->ap = 1;
        if (S.mode == MODE_HUNT) {
            if (p.who != target_who) continue;
            S.target_rssi += 0.35f * ((float)p.rssi - S.target_rssi); target_seen_tick = (int)ticks;
        } else {
            if (p.rssi < reach) continue;
            if (p.kind == PKT_MGMT && p.sub == 8 && pv[P_BEACON] < 1.0f) continue;
        }
        float prox = clampf(((float)p.rssi - reach) / (-35.0f - reach), 0, 1);
        sound_frame(&p, di, prox);
    }
    radio_csi_t cs;
    int kc = 0;
    while (kc < 64 && radio_csi(&cs)) {
        kc++;
        int di = dev_find(cs.who, false);
        if (di >= 0) S.dev[di].csi++;
        if (cs.who == room_src || !room_src) room_frame(&cs);
    }
    overflow_rate += 0.1f * (overflow_n * 100.0f - overflow_rate); overflow_n = 0;
    sc.rain = fminf(overflow_rate, 2000.0f);
    S.rate += 0.05f * ((float)k * 100.0f - S.rate);
    room_tick();
    storm_tick(sc.storm);
    channels_tick();
    if (ticks % 100 == 0) devices_second();
    // the screen's decays
    S.sniff *= 0.9f; S.thunder *= 0.9f;
    for (int i = 0; i < NDEV; i++) S.dev[i].pulse *= 0.93f;
    // MIDI: note-offs, and the layers as CCs (3 rate, 4 room motion, 5 storm, 6 HUNT nearness)
    for (int i = 0; i < midi_offs; ) {
        if ((int32_t)(ticks - midi_off_at[i]) >= 0) { midi_note(midi_off_note[i], 0); midi_off_note[i] = midi_off_note[--midi_offs]; midi_off_at[i] = midi_off_at[midi_offs]; }
        else i++;
    }
    if (ticks % 5 == 0) {
        static int last[4] = { -1, -1, -1, -1 };
        int v[4] = { (int)(log10f(1.0f + S.rate) / log10f(2001.0f) * 127.0f), (int)(S.motion * 127.0f), (int)(S.storm * 127.0f), (int)(S.prox * 127.0f) };
        for (int i = 0; i < 4; i++) {
            int x = v[i] < 0 ? 0 : v[i] > 127 ? 127 : v[i], d = x - last[i];
            if (d >= 2 || d <= -2 || (x != last[i] && (x == 0 || x == 127))) { last[i] = x; midi_cc(3 + i, x); }
        }
    }
    // the LED: blue beacons, green data, yellow searches, red storm, a room glow; HUNT blinks with its beeps
    static uint32_t beeps_seen;
    if (S.mode == MODE_HUNT) {
        if (snd_beeps != beeps_seen) { beeps_seen = snd_beeps; led_g += 120.0f + 135.0f * S.prox; led_r += 120.0f * (1.0f - S.prox); }
    }
    led_b += 30.0f * S.motion * (0.6f + 0.4f * sinf((float)ticks * 0.05f)) * sc.room;
    led_r = fminf(led_r, 255.0f); led_g = fminf(led_g, 255.0f); led_b = fminf(led_b, 255.0f);
    led_rgb((uint8_t)led_r, (uint8_t)led_g, (uint8_t)led_b);
    led_r *= 0.82f; led_g *= 0.82f; led_b *= 0.85f;
}

static void midi_in(uint8_t st, uint8_t d1, uint8_t d2)
{
    if ((st & 0xF0) == 0x90 && d2 > 0) param_set(P_KEY, (float)(d1 % 12));     // a note sets the key
}

const instrument_t INST = {
    .name = "SNIFF", .tagline = "THE WI-FI AROUND YOU",
    .params = params, .nparams = P_N, .groups = groups, .ngroups = 6,
    .presets = presets, .npresets = NPRESETS, .hints = hints, .nhints = (int)(sizeof hints / sizeof hints[0]),
    .looper = true,
    .init = init, .audio = snd_audio, .tick = tick, .draw = scene_draw, .splash = scene_splash,
    .changed = changed, .action = action, .midi_in = midi_in, .saver = scene_saver,
};
