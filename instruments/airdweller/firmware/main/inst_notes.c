// AIRDWELLER's notes (ROLE NOTES): four voices for the lines drawn in the air, made from nothing as the kit is.
//   BASS     a rubber bass: a sine phase-modulated by itself (the index with the envelope) through a sine folder, a sub
//            octave under it, a resonant low pass that follows the note and the envelope; each attack bounces the pitch
//            like a plucked rubber band (a damped 14 Hz wobble from a few semitones up)
//   SQUELCH  acid: a band-limited saw through a one-pole and a resonant state-variable low pass (18 dB an octave), a fast
//            envelope on the cutoff, an accent (velocity over 90: more of it, shorter, louder, more resonant), driven into
//            a soft clip; the legato is the slide
//   LEAD     latex: a band-limited pulse through two formant band passes (a vowel that opens on the attack), a scoop up
//            into the note, vibrato from 0.2 s on
//   BELL     glass: two-operator FM, the modulator at 3.5 times the carrier (inharmonic), its index falling, a glass
//            partial at 2.756 times dying twice as fast; four ring at once, the quietest gives way (a 2 ms fade) to a fifth
// BASS, SQUELCH and LEAD are one voice: a new note takes over the one sounding (the envelope attacks again from where it
// is, the phases run on: no click; another voice's note fades out in a few ms); a legato note slides the one sounding
// (the gesture's own, or the last note's: a 303's slide) to the new note over GLIDE (an S-curve, there at GLIDE) without
// a new attack. A bell's legato moves only that gesture's bells. On 0 starts the gesture's release, unless the hold
// keeps it. An event lands on its sample: the block is rendered in pieces between them, each piece's pitch ramped from
// its start to its end.
// The drag (inst_eng.h's follower, per note): the hold sustains (the release, and a bell's ring, 64 times slower at full
// hold), the amount opens the filter (BASS, SQUELCH), the vowel (LEAD) or the index (BELL), the bend bends.
// Audio task only (notes_init: UI task, before the audio starts).
#include "inst_eng.h"
#include <string.h>

#define SR    ((float)AUDIO_SR)
#define ISR   (1.0f / (float)AUDIO_SR)
#define NBELL 4
#define LG440 8.7813597f                      // log2 440
#define BELL_R 3.5f                           // the modulator over the carrier
#define GLASS_R 2.756f                        // the glass partial

float notes_pk;

// a note's pitch: where it is and where it glides to (log2 Hz), an S-curve over gl_n samples
typedef struct { float lg0, lg1; int gl_t, gl_n; } pitch_t;
static inline float pitch_lg(const pitch_t *p, int off)
{
    if (p->gl_n <= 0 || p->gl_t + off >= p->gl_n) return p->lg1;
    float u = (float)(p->gl_t + off) / (float)p->gl_n;
    return p->lg0 + (p->lg1 - p->lg0) * u * u * (3.0f - 2.0f * u);
}
static inline void pitch_glide(pitch_t *p, float lg, float glide_s)
{
    p->lg0 = pitch_lg(p, 0); p->lg1 = lg; p->gl_t = 0;
    p->gl_n = (int)(clampf(glide_s, 0.0f, 2.0f) * SR);
    if (p->gl_n < 16) { p->gl_n = 0; p->lg0 = lg; }
}
static inline float note_lg(int note) { return LG440 + (float)(note - 69) * (1.0f / 12.0f); }

// ---------------------------------------------------------------- the mono voice (BASS, SQUELCH, LEAD) and its ghost
enum { ST_IDLE, ST_ATT, ST_SUS, ST_REL };
typedef struct {
    int8_t  type;                             // NV_*, -1: idle
    uint8_t gid, gate, stage, ghost;
    int     note, t;                          // the MIDI note; samples since the attack
    float   vel, acc, bst;                    // velocity 0..1, the accent 0..1, the attack's bounce (semitones)
    float   env, fe;                          // the level, the filter (vowel) envelope
    pitch_t p;
    float   ph, ph2, lpz, lpk, vph;           // phases, SQUELCH's one-pole and its coefficient, the vibrato's phase
    svf_t   f, f2;
    dfol_t  df;
} mono_t;
static mono_t M[2];                           // M[1]: the ghost of a note another voice took over

typedef struct {
    int8_t  on;
    uint8_t gid, rel;
    int     note, t;
    float   vel, env, ie, e2;                 // the level, the index's envelope, the glass partial's own fall
    pitch_t p;
    float   pc, pm, p3;
    float   fade;                             // 1, or falling to 0 (given way)
    dfol_t  df;
} bell_t;
static bell_t B[NBELL + 1];                   // B[NBELL]: the ghost of the one that gave way

void notes_init(void) { memset(M, 0, sizeof M); memset(B, 0, sizeof B); M[0].type = M[1].type = -1; notes_pk = 0.0f; }
void notes_silence(void) { M[0].type = M[1].type = -1; M[0].stage = M[1].stage = ST_IDLE; for (int b = 0; b <= NBELL; b++) B[b].on = 0; }

// what the drag does to a voice this block
typedef struct { float A, H, hf, bend; } nctl_t;
static nctl_t ctl_of(const dfol_t *f)
{
    nctl_t c;
    c.A = clampf(f->a, -0.2f, 1.3f); c.H = clampf(f->h, 0.0f, 1.0f); c.hf = hold_rate(c.H); c.bend = clampf(f->b, -36.0f, 36.0f);
    return c;
}

// ---------------------------------------------------------------- the mono voice's pieces
// the pitch (log2 Hz) off samples into the piece: the glide, the bend, the attack's bounce (BASS), the scoop and the
// vibrato (LEAD)
static IRAM_ATTR float mono_lg(const mono_t *m, int off, const nctl_t *c)
{
    float lg = pitch_lg(&m->p, off) + c->bend * (1.0f / 12.0f);
    float ts = (float)(m->t + off) * ISR;
    if (m->type == NV_BASS) {
        if (ts < 0.3f) lg += m->bst * (1.0f / 12.0f) * fast_exp2(-57.7f * ts) * sin_turns(14.0f * ts + 0.25f);   // tau 25 ms
    } else if (m->type == NV_LEAD) {
        lg -= (1.0f / 12.0f) * fast_exp2(-48.1f * ts);                                                       // the scoop: tau 30 ms
        float d = smoothstep(0.2f, 0.55f, ts) * (0.3f / 12.0f);
        if (d > 0.0f) lg += d * sin_turns(m->vph + (float)off * (5.5f * ISR));
    }
    return lg;
}
// the envelope's target and its step a sample now (the attack overshoots to 1.5 and stops at 1); BASS, SQUELCH, LEAD
static const float ENV_A[3] = { 0.0015f, 0.001f, 0.006f }, ENV_D[3] = { 0.18f, 0.5f, 0.3f }, ENV_S[3] = { 0.65f, 0.9f, 0.85f }, ENV_R[3] = { 0.05f, 0.025f, 0.12f };
static void env_of(const mono_t *m, const nctl_t *c, float *tgt, float *k)
{
    int ty = m->type < 0 ? 0 : m->type > 2 ? 2 : m->type;
    if (m->ghost) { *tgt = 0.0f; *k = tau_k(0.002f); return; }
    switch (m->stage) {
    case ST_ATT: *tgt = 1.5f; *k = tau_k(ENV_A[ty] * (1.0f / 1.1f)); break;
    case ST_SUS: *tgt = ENV_S[ty]; *k = tau_k(ENV_D[ty]); break;
    default:     *tgt = 0.0f; *k = decay_of(c->hf / (ENV_R[ty] * SR)); break;   // the hold slows the release
    }
}
static void mono_piece(mono_t *m, float *restrict y, int a, int b, const nctl_t *c)
{
    const int len = b - a;
    float lgA = mono_lg(m, 0, c), lgB = mono_lg(m, len, c);
    float f = fast_exp2(lgA) * ISR, df = (fast_exp2(lgB) * ISR - f) / (float)len;
    float tgt, k; env_of(m, c, &tgt, &k);
    float e = m->env, fe = m->fe, ph = m->ph;
    const float vel = m->vel, hz = f * SR, Ap = fmaxf(c->A, 0.0f);
    int stage = m->stage;
    if (m->type == NV_BASS) {
        const float kfe = tau_k(0.12f), lvl = 0.42f * (0.3f + 0.7f * vel);
        const float i0 = 0.06f + 0.1f * Ap, i1 = 0.22f * vel, g0 = 1.0f + 0.5f * Ap, g1 = 1.1f * vel;
        const float base = hz * fast_exp2(2.5f * c->A), K = 12.0f * (0.35f + 0.65f * vel);
        float ps = m->ph2;
        for (int i = a; i < b; i++) {
            if (((i - a) & 7) == 0) svf_fc(&m->f, base * (1.5f + K * fe));
            ph += f; if (ph >= 1.0f) ph -= 1.0f;
            ps += 0.5f * f; if (ps >= 1.0f) ps -= 1.0f;
            f += df;
            float cr = sin_turns(ph + (i0 + i1 * fe) * sin_turns(ph));
            float u = fold_sin(cr * (g0 + g1 * fe));
            float lp = svf_lp(&m->f, 0.7f * u + 0.65f * sin_turns(ps));
            e = tgt + (e - tgt) * k;
            if (e > 1.0f) { e = 1.0f; stage = ST_SUS; env_of(&(mono_t){ .type = m->type, .stage = ST_SUS }, c, &tgt, &k); }
            fe *= kfe;
            y[i] += lp * e * lvl;
        }
        m->ph2 = ps;
    } else if (m->type == NV_SQUELCH) {
        const float acc = m->acc, kfe = tau_k(0.2f - 0.11f * acc), lvl = 0.36f * (0.55f + 0.45f * vel) * (1.0f + 0.35f * acc);
        const float fb = 150.0f * fast_exp2(3.2f * c->A + 0.3f * (lgA - 6.7813597f)), dep = (2.6f + 1.6f * acc) * (0.45f + 0.55f * vel);
        float lz = m->lpz, lk = m->lpk;
        m->f.k = 1.0f / (4.5f + 6.0f * acc);
        for (int i = a; i < b; i++) {
            if (((i - a) & 7) == 0) {
                float fc = fminf(fb * fast_exp2(dep * fe), 11000.0f);
                svf_fc(&m->f, fc);
                lk = fminf(fc * (TWO_PI * ISR), 0.95f);
            }
            ph += f; if (ph >= 1.0f) ph -= 1.0f;
            float s = saw_blep(ph, f);
            f += df;
            lz += lk * (s - lz);
            float lp = svf_lp(&m->f, lz);
            e = tgt + (e - tgt) * k;
            if (e > 1.0f) { e = 1.0f; stage = ST_SUS; env_of(&(mono_t){ .type = m->type, .stage = ST_SUS }, c, &tgt, &k); }
            fe *= kfe;
            y[i] += soft_tanh(1.7f * lp) * e * lvl;
        }
        m->lpz = flushf(lz); m->lpk = lk;
    } else {                                                              // LEAD
        const float kfe = tau_k(0.25f), lvl = 0.5f * (0.4f + 0.6f * vel);
        const float pw = 0.36f + 0.08f * sin_turns(m->vph * 0.13f);
        float p2 = m->ph2;
        for (int i = a; i < b; i++) {
            if (((i - a) & 7) == 0) {
                float open = clampf(0.15f + 0.55f * fe * (0.5f + 0.5f * vel) + 0.8f * c->A, 0.0f, 1.4f);
                svf_fc(&m->f, 300.0f * fast_exp2(1.4f * open));
                svf_fc(&m->f2, 850.0f * fast_exp2(1.25f * open));
            }
            ph += f; if (ph >= 1.0f) ph -= 1.0f;
            p2 = ph + pw; if (p2 >= 1.0f) p2 -= 1.0f;
            float o = saw_blep(ph, f) - saw_blep(p2, f);
            f += df;
            float b1 = svf_bp(&m->f, o, NULL), b2 = svf_bp(&m->f2, o, NULL);
            e = tgt + (e - tgt) * k;
            if (e > 1.0f) { e = 1.0f; stage = ST_SUS; env_of(&(mono_t){ .type = m->type, .stage = ST_SUS }, c, &tgt, &k); }
            fe *= kfe;
            y[i] += soft_tanh(1.3f * (0.9f * b1 + 0.7f * b2 + 0.12f * o)) * e * lvl;
        }
        m->ph2 = p2;
        m->f2.ic1 = flushf(m->f2.ic1); m->f2.ic2 = flushf(m->f2.ic2);
    }
    m->f.ic1 = flushf(m->f.ic1); m->f.ic2 = flushf(m->f.ic2);
    m->env = flushf(e); m->fe = flushf(fe); m->ph = ph; m->stage = (uint8_t)stage;
    m->t += len; m->p.gl_t += len; if (m->p.gl_n && m->p.gl_t >= m->p.gl_n) { m->p.gl_n = 0; m->p.lg0 = m->p.lg1; }
    m->vph += (float)len * (5.5f * ISR); if (m->vph >= 1000.0f) m->vph -= 1000.0f;
    if (m->stage == ST_REL && m->env < 1e-4f) { m->type = -1; m->stage = ST_IDLE; }
}

// ---------------------------------------------------------------- a bell's piece
static void bell_piece(bell_t *bl, float *restrict y, int a, int b, const nctl_t *c)
{
    const int len = b - a;
    float lgA = pitch_lg(&bl->p, 0) + c->bend * (1.0f / 12.0f), lgB = pitch_lg(&bl->p, len) + c->bend * (1.0f / 12.0f);
    float f = fast_exp2(lgA) * ISR, df = (fast_exp2(lgB) * ISR - f) / (float)len;
    // the ring: T60 3.2 s at middle C, shorter up high; let go: 1.2 s at most; the hold slows either
    float t60 = 3.2f * fast_exp2(-(float)(bl->note - 60) * (1.0f / 24.0f));
    if (bl->rel) t60 = fminf(t60, 1.2f);
    const float kd = decay_of(6.9078f * c->hf / (t60 * SR)), ka = tau_k(0.0015f / 1.1f), kie = tau_k(0.3f);
    const float ib = (0.3f + 2.2f * fmaxf(c->A, 0.0f)) * 0.15915494f, i1 = (1.0f + 4.0f * bl->vel) * 0.15915494f;
    const float lvl = 0.2f * (0.3f + 0.7f * bl->vel), dfade = 1.0f / (0.002f * SR);
    float e = bl->env, ie = bl->ie, e2 = bl->e2, pc = bl->pc, pm = bl->pm, p3 = bl->p3, fd = bl->fade;
    bool att = bl->t < (int)(0.006f * SR) && e < 1.0f;
    for (int i = a; i < b; i++) {
        pc += f; if (pc >= 1.0f) pc -= 1.0f;
        pm += BELL_R * f; if (pm >= 1.0f) pm -= 1.0f;
        p3 += GLASS_R * f; if (p3 >= 1.0f) p3 -= 1.0f;
        f += df;
        if (att) { e = 1.5f + (e - 1.5f) * ka; if (e >= 1.0f) { e = 1.0f; att = false; } }
        else { e *= kd; e2 *= kd; }
        float o = sin_turns(pc + (ib + i1 * ie) * sin_turns(pm)) + 0.22f * sin_turns(p3) * e2;
        ie *= kie;
        if (bl->on < 0) { o *= fd; fd = fmaxf(fd - dfade, 0.0f); }
        y[i] += o * e * lvl;
    }
    bl->env = e; bl->ie = flushf(ie); bl->e2 = flushf(e2); bl->pc = pc; bl->pm = pm; bl->p3 = p3; bl->fade = fd;
    bl->t += len; bl->p.gl_t += len; if (bl->p.gl_n && bl->p.gl_t >= bl->p.gl_n) { bl->p.gl_n = 0; bl->p.lg0 = bl->p.lg1; }
    if ((!att && e < 1e-4f) || (bl->on < 0 && fd <= 0.0f)) bl->on = 0;
}

// ---------------------------------------------------------------- events
static void mono_attack(mono_t *m, const nev_t *ev, int type)
{
    if (m->type >= 0 && m->type != type) {                            // another voice sounding: it fades out (the ghost)
        M[1] = *m; M[1].ghost = 1; M[1].stage = ST_REL;
        memset(m, 0, sizeof *m); m->type = -1;
    }
    if (m->type < 0) {                                                // from silence: everything from the start
        memset(m, 0, sizeof *m);
        m->f.k = type == NV_BASS ? 0.45f : 0.2f; m->f2.k = 1.0f / 7.0f;
        if (type == NV_LEAD) m->f.k = 1.0f / 5.0f;
    }
    m->type = (int8_t)type; m->ghost = 0;
    m->gid = ev->gid; m->gate = 1; m->stage = ST_ATT; m->t = 0; m->note = ev->note;
    m->vel = clampf((float)ev->vel * (1.0f / 127.0f), 0.0f, 1.0f);
    m->acc = smoothstep(90.0f, 127.0f, (float)ev->vel);
    m->bst = 2.0f + 4.0f * m->vel;
    m->fe = 1.0f;
    m->p.lg0 = m->p.lg1 = note_lg(ev->note); m->p.gl_n = m->p.gl_t = 0;
    dfol_init(&m->df, ev->gid);
}
static void bell_strike(const nev_t *ev)
{
    int k = -1;
    for (int b = 0; b < NBELL; b++) if (!B[b].on) { k = b; break; }
    if (k < 0) {                                                      // all ringing: the quietest gives way
        float q = 1e9f; k = 0;
        for (int b = 0; b < NBELL; b++) if (B[b].env < q) { q = B[b].env; k = b; }
        B[NBELL] = B[k]; B[NBELL].on = -1; B[NBELL].fade = 1.0f;
    }
    bell_t *bl = &B[k];
    memset(bl, 0, sizeof *bl);
    bl->on = 1; bl->gid = ev->gid; bl->note = ev->note;
    bl->vel = clampf((float)ev->vel * (1.0f / 127.0f), 0.0f, 1.0f);
    bl->ie = 1.0f; bl->e2 = 1.0f; bl->fade = 1.0f;
    bl->p.lg0 = bl->p.lg1 = note_lg(ev->note);
    dfol_init(&bl->df, ev->gid);
}
static void note_event(const nev_t *ev)
{
    mono_t *m = &M[0];
    bool mono_has = m->type >= 0 && m->gid == ev->gid && m->stage != ST_IDLE;
    if (!ev->on || !ev->vel) {                                        // the gesture's note lets go
        if (mono_has) { m->gate = 0; m->stage = ST_REL; }
        for (int b = 0; b < NBELL; b++) if (B[b].on > 0 && B[b].gid == ev->gid) B[b].rel = 1;
        return;
    }
    float gl = clampf(eng_note_glide, 0.0f, 2.0f);
    if (ev->legato) {                                                 // the note moves, gliding, without a new attack
        bool moved = false;
        for (int b = 0; b < NBELL; b++) if (B[b].on > 0 && B[b].gid == ev->gid) { pitch_glide(&B[b].p, note_lg(ev->note), gl); B[b].note = ev->note; moved = true; }
        if (m->type >= 0 && m->stage != ST_IDLE && (mono_has || eng_note_voice != NV_BELL)) {
            // the mono voice: the gesture's own note, or (a slide from the last note) whatever it is sounding
            pitch_glide(&m->p, note_lg(ev->note), gl); m->note = ev->note; moved = true;
            if (!m->gate) { m->gate = 1; m->stage = ST_SUS; }
            if (m->gid != ev->gid) { m->gid = ev->gid; m->df.gid = ev->gid; m->df.live = 1; }   // the new gesture drags it from here
        }
        if (moved || eng_note_voice == NV_BELL) return;                // (a bell that is gone is not struck again)
    }
    int v = clampi(eng_note_voice, 0, NNV - 1);
    if (v == NV_BELL) bell_strike(ev); else mono_attack(m, ev, v);
}

// ---------------------------------------------------------------- a block
int notes_run(float *acc, int n, const nblk_t *ev, int ne, const edrag_t *dg)
{
    if (!ne && M[0].type < 0 && M[1].type < 0 && !B[0].on && !B[1].on && !B[2].on && !B[3].on && !B[4].on) { notes_pk = 0.0f; return 0; }
    nctl_t mc[2], bc[NBELL + 1];
    for (int j = 0; j < 2; j++) if (M[j].type >= 0) { if (j == 0) dfol_run(&M[j].df, dg, n); mc[j] = ctl_of(&M[j].df); }
    for (int b = 0; b <= NBELL; b++) if (B[b].on) { if (b < NBELL) dfol_run(&B[b].df, dg, n); bc[b] = ctl_of(&B[b].df); }
    float y[AUDIO_BLOCK];
    for (int i = 0; i < n; i++) y[i] = 0.0f;
    int a = 0;
    for (int e = 0; e <= ne; e++) {
        int b = e < ne ? clampi(ev[e].off, a, n) : n;
        if (b > a) {
            for (int j = 0; j < 2; j++) if (M[j].type >= 0) mono_piece(&M[j], y, a, b, &mc[j]);
            for (int k = 0; k <= NBELL; k++) if (B[k].on) bell_piece(&B[k], y, a, b, &bc[k]);
        }
        if (e < ne) {
            note_event(&ev[e].e);
            if (M[0].type >= 0) mc[0] = ctl_of(&M[0].df);            // a new note: its own drag (none yet)
            if (M[1].type >= 0) mc[1] = ctl_of(&M[1].df);
            for (int k = 0; k <= NBELL; k++) if (B[k].on) bc[k] = ctl_of(&B[k].df);
        }
        a = b;
    }
    float pk = 0.0f;
    for (int i = 0; i < n; i++) { acc[i] += y[i]; pk = fmaxf(pk, fabsf(y[i])); }
    notes_pk = pk;
    int on = (M[0].type >= 0) + (M[1].type >= 0);
    for (int k = 0; k <= NBELL; k++) on += B[k].on != 0;
    return on;
}
