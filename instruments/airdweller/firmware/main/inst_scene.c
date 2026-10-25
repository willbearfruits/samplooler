// AIRDWELLER's screens (inst_ad.h is the contract; D is what instrument.c fills each tick). Drawing only, in the UI
// task, nothing per sample. SOPHIE's look on a 1-bit panel: bold outlines, filled blobs, elastic easing, hard cuts.
//
//   the header   the role in the pill (filled while CUT); REC IN n / REC n (filled) while a loop is recorded
//   the panel    right, x 84..127, the same in every role: this box and the other (L / R / ONE; the other's role, its
//                pill popping with the voice of each hit it sends), the beat (four bubbles, the one now popping), the
//                loops (four little orbits: a dot when empty, a ring when off, a disc with a hand going round when on,
//                an arc growing while one is recorded)
//   DRUM         the four pads as a compass of capsules with their voices (forward up, back down, left, right); a
//                liquid stick in the middle follows the gyro, stretches with a stroke and snaps to the pad it hits; a
//                hit pops its capsule and splashes; the voice in big letters bottom right; the other hand's hits ripple
//                in from its side
//   PAINT        the material as a shape that rings with its eight modes (eng_mode_amp: which ones; D.mat_lvl: how
//                much): METAL a hard hexagon plate that goes spiky, GLASS a thin bright ring that glints, RUBBER a fat
//                blob that squashes, LATEX a band between two pegs that swings like a string. Tilt morphs between them
//                (a bubble in the vial at the left edge, its name beside it), twisting leans it, every strike dents it
//                from its side with a mallet, a ring and a splash; a hard one flips its fill for a frame
//   PUMP         the same material as a vessel: the energy is the liquid in it and the shape swells with it, the bow
//                saws across it as the box moves, a shake rattles it, the other hand's hits squash it
//   CUT          black: a flat line, the beat a tick running along it, CUT under it; in and out on a one-frame flash
//   the splash   two sticks strike a blob from both sides, it rings through the four materials, AIRDWELLER drops in
//                letter by letter, the tagline
//   the saver    two bubbles circling each other, drifting slowly over the whole panel
//
// When the core's strip is up the scene squeezes above it (elastic): the compass and the shape shrink, the big name
// and the tempo or pitch go. A new role and the end of a CUT come in on one inverted frame (a hard cut).
#include "inst_ad.h"
#include "ui.h"
#include "dsp.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define PANEL_X  84                    // the right panel
#define STRIP_Y  46                    // the rows the core may cover
#define NPT      36                    // points on a material's contour (6 a side for the hexagon)
#define COLX(k)  (89 + 11 * (k))       // the panel's four columns: beats, loops
#define BEAT_Y   30
#define LOOP_Y   40

typedef struct { float x, y; } pt_t;

// ---------------------------------------------------------------- state
static uint32_t fr;                    // frames drawn
static uint32_t seed = 0x2545f491u;
static UI_RAM parts_t parts;
static float room = 1.0f, room_v;      // 1: the whole scene; 0: squeezed above the core's strip
static int   role_was = -1, flip_f, cut_was, cut_f;
// hits since the last frame (scene_hit runs in the tick)
typedef struct { int8_t pad; uint8_t vel, remote; } ev_t;
static ev_t evq[8];
static int  nev;
// DRUM
static float pop[NPAD], popv[NPAD];
static float tipx, tipy, tipvx, tipvy;
static pt_t  trail[6];
static int   trail_i;
static const char *big_txt;            // the last sound, in big letters for a moment
static int   big_age = 99;
// the other hand
static float peer_pop, peer_popv;
static int   peer_name_f;
static struct { float r; int life; } rip[3];
// the beat and the loops
static int   bib_was = -1, rec_was, rec_bars;
static float beat_pop;
// the material
static float ph[8], spike_ph, morph_s = -1.0f, bow_ph, squash, squash_v;
typedef struct { float ang, amp; int age; } imp_t;   // a strike: where on the contour (turns), how hard, frames since
static imp_t imp[4];
static pt_t  shp[NPT];
static struct { float x, y, r; int life; } wave[3];   // a ring out of the shape at a strike, behind it
static float sh_cx = 54.0f, sh_cy = 34.0f, sh_r = 13.0f;   // where the shape was drawn last
static int8_t surf[128];               // PUMP: the liquid's surface in each column

// ---------------------------------------------------------------- primitives
static void xpix(int x, int y) { oled_pixel(x, y, !oled_get(x, y)); }
static void xor_disc(int cx, int cy, int r)
{
    for (int j = -r; j <= r; j++) for (int i = -r; i <= r; i++) if (i * i + j * j <= r * r + r) xpix(cx + i, cy + j);
}
static int inset(int r, int k)         // the rounded corner's cut on row k (as oled.c's)
{
    static const uint8_t ins[5][4] = { { 0 }, { 1 }, { 2, 1 }, { 3, 1, 1 }, { 4, 2, 1, 1 } };
    if (r > 4) r = 4;
    return k < r ? ins[r][k] : 0;
}
static void xor_rrect(int x, int y, int w, int h, int r)
{
    for (int j = 0; j < h; j++) {
        int k = j < h - 1 - j ? j : h - 1 - j, in = inset(r, k);
        for (int i = x + in; i < x + w - in; i++) xpix(i, y + j);
    }
}
// a capsule with a word: 0 outlined, 1 filled (black letters), 2 a ghost (dotted)
static void capsule(int x, int y, int w, int h, const char *s, int style)
{
    int r = h >= 9 ? 3 : 2, tw = oled_ftext_w(F_TINY, s, 0);
    oled_rrect(x, y, w, h, r, false);
    if (style != 1) oled_rframe(x, y, w, h, r);
    oled_ftext_ex(F_TINY, x + (w - tw + 1) / 2, y + (h - 5) / 2, s, 0);
    if (style == 1) xor_rrect(x, y, w, h, r);
    if (style == 2) oled_dither(x, y, w, h, 2, false);
}
static int capsule_w(const char *s) { return oled_ftext_w(F_TINY, s, 0) + 8; }
// a dotted circle, every other pixel, kept to rows y0..y1
static void dot_circle(int cx, int cy, int r, int y0, int y1)
{
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        const int px[8] = { cx + x, cx + y, cx - y, cx - x, cx - x, cx - y, cx + y, cx + x };
        const int py[8] = { cy + y, cy + x, cy + x, cy + y, cy - y, cy - x, cy - x, cy - y };
        for (int k = 0; k < 8; k++) if (py[k] >= y0 && py[k] <= y1 && ((px[k] + py[k]) & 1) == 0) oled_pixel(px[k], py[k], true);
        y++;
        if (err < 0) err += 2 * y + 1; else { x--; err += 2 * (y - x) + 1; }
    }
}
static void arc_thick(int cx, int cy, int r, float frac)   // from 12 o'clock, clockwise, two pixels thick
{
    if (frac <= 0.0f) return;
    ui_arc(cx, cy, r, -90.0f, -90.0f + 360.0f * clampf(frac, 0.0f, 1.0f));
    ui_arc(cx, cy, r - 1, -90.0f, -90.0f + 360.0f * clampf(frac, 0.0f, 1.0f));
}
// a spring for a pop: kicked to 1, it falls back past 0 and settles (elastic)
static void spring(float *x, float *v) { *v += -0.45f * *x; *v *= 0.62f; *x += *v; if (fabsf(*x) < 0.01f && fabsf(*v) < 0.01f) *x = *v = 0.0f; }
static int rnd(int n) { return (int)(rng_next(&seed) % (uint32_t)n); }

// ---------------------------------------------------------------- hits
void scene_hit(int pad, int vel, int remote)
{
    if (nev < 8) evq[nev++] = (ev_t){ (int8_t)pad, (uint8_t)clampi(vel, 0, 127), (uint8_t)(remote != 0) };
}

static bool other_on_right(void)
{
    if (D.hand == HAND_LEFT) return true;
    if (D.hand == HAND_RIGHT) return false;
    return D.peer_hand != HAND_LEFT;
}

static void strike(float ang, float amp)
{
    int k = 0;
    for (int i = 1; i < 4; i++) if (imp[i].age > imp[k].age) k = i;
    imp[k] = (imp_t){ ang, amp, 0 };
    // where it lands (the contour of the last frame): a ring behind, drops thrown off
    int i = clampi((int)lroundf(ang * (float)NPT), 0, NPT) % NPT;
    float x = shp[i].x, y = shp[i].y;
    if (x == 0.0f && y == 0.0f) return;
    for (int j = 0; j < 3; j++) if (wave[j].life <= 0) { wave[j].x = sh_cx; wave[j].y = sh_cy; wave[j].r = sh_r * 1.3f + 2.0f; wave[j].life = 5; break; }
    float dx = x - sh_cx, dy = y - sh_cy, l = sqrtf(dx * dx + dy * dy) + 1e-3f;
    int n = 2 + (int)(amp * 6.0f);
    for (int j = 0; j < n; j++) {
        float sp = 50.0f + 90.0f * amp * rng_uni(&seed);
        parts_emit(&parts, x, y, dx / l * sp + 50.0f * rng_bi(&seed), dy / l * sp - 40.0f - 40.0f * rng_uni(&seed), 0.3f + 0.2f * rng_uni(&seed), PART_SQUARE, rnd(2));
    }
}

// ---------------------------------------------------------------- the panel: the hands, the beat, the loops
static void hands(void)
{
    const int y = 17, h = 9;
    if (!D.paired && D.hand == HAND_AUTO) {           // alone
        int w = capsule_w("ONE");
        capsule(127 - w, y, w, h, "ONE", 1);
        return;
    }
    bool me_left = D.hand != HAND_RIGHT;
    const char *me = me_left ? "L" : "R";
    static const char *const role_l[NROLE] = { "DRUM", "PAINT", "PUMP" };
    char ob[16];
    const char *who = peer_name_f > 0 && D.peer_voice >= 0 && D.peer_voice < NDV ? dv_name[D.peer_voice] : role_l[clampi(D.peer_role, 0, NROLE - 1)];
    if (D.paired) snprintf(ob, sizeof ob, "%s %s", me_left ? "R" : "L", who);
    else snprintf(ob, sizeof ob, "%s", me_left ? "R" : "L");
    int wm = capsule_w(me), wo = capsule_w(ob), gap = 5;
    int g = (int)lroundf(peer_pop * 2.0f);
    // left to right as the hands are held
    int xl = 127 - wm - gap - wo, xm, xo;
    if (me_left) { xm = xl; xo = xl + wm + gap; } else { xo = xl; xm = xl + wo + gap; }
    capsule(xm, y, wm, h, me, 1);
    if (D.paired) {
        int x0 = xo - (g > 0 ? g : 0), w0 = wo + 2 * (g > 0 ? g : 0);
        if (x0 + w0 > 127) x0 = 127 - w0;
        capsule(x0, y - (g > 0 ? 1 : 0), w0, h + (g > 0 ? 2 : 0), ob, peer_pop > 0.3f || D.peer_flash > 0.5f ? 1 : 0);
        // the link between them: a dot running from the other to this one
        int lx = me_left ? xm + wm : xo + wo, cy = y + h / 2;
        oled_hline_pat(lx + 1, cy, gap - 2, 0x55, true);
        if (peer_name_f > 0) oled_rect(me_left ? lx + gap - 2 - (peer_name_f & 1) : lx + 1 + (peer_name_f & 1), cy - 1, 2, 3, true);
    } else capsule(xo, y, wo, h, ob, 2);               // the other hand not heard: a ghost
}

static void beat_row(void)
{
    for (int k = 0; k < 4; k++) {
        int x = COLX(k);
        if (k == D.beat_in_bar) {
            int r = 2 + (int)lroundf(2.0f * clampf(beat_pop, 0.0f, 1.0f)) + (k == 0);
            oled_disc(x, BEAT_Y, r, true);
            if (r >= 3) oled_pixel(x - 1, BEAT_Y - 1, false);   // its shine
        } else if (k == 0) oled_circle(x, BEAT_Y, 2, true);
        else { oled_pixel(x, BEAT_Y, true); oled_pixel(x - 1, BEAT_Y, true); oled_pixel(x + 1, BEAT_Y, true); oled_pixel(x, BEAT_Y - 1, true); oled_pixel(x, BEAT_Y + 1, true); }
    }
}

static void loop_row(void)
{
    for (int k = 0; k < NLOOP; k++) {
        int x = COLX(k), y = LOOP_Y;
        bool recording = D.rec && D.rec_slot == k;
        if (recording && D.rec == 1) {                  // waiting for the bar: blinks
            if ((fr >> 2) & 1) oled_disc(x, y, 4, true); else oled_circle(x, y, 4, true);
            continue;
        }
        if (recording && D.rec == 2) {                  // the take: an arc growing through each bar
            oled_circle(x, y, 4, true);
            oled_rect(x - 1, y - 1, 3, 3, true);
            float f = ((float)D.beat_in_bar + D.beat_phase) / 4.0f;
            oled_disc(x, y, 4, false);
            arc_thick(x, y, 4, f);
            oled_rect(x - 1, y - 1, 3, 3, true);
            continue;
        }
        if (!D.loop_bars[k]) { oled_pixel(x, y, true); oled_pixel(x + 1, y, true); oled_pixel(x, y + 1, true); oled_pixel(x + 1, y + 1, true); continue; }
        if (D.loop_on[k] && eng_run) {                  // playing: a disc with its hand going round
            oled_disc(x, y, 4, true);
            float a = D.loop_pos[k];
            oled_line(x, y, x + (int)lroundf(3.4f * sin_turns(a)), y - (int)lroundf(3.4f * sin_turns(a + 0.25f)), false);
            oled_pixel(x - 2, y - 2, false);
        } else oled_circle(x, y, 4, true);
    }
}

// the voice that just sounded, big; else the tempo (DRUM) or the pitch (PAINT, PUMP)
static void readout(void)
{
    char b[24];
    if (big_txt && big_age < 14 && !(big_age >= 10 && (big_age & 1))) {
        int w = oled_ftext_w(F_MED, big_txt, 1);
        if (w <= 127 - PANEL_X + 2) oled_ftext_ex(F_MED, 127 - w, 47, big_txt, 1);
    }
    if (D.role == ROLE_DRUM) snprintf(b, sizeof b, "%d BPM", (int)lroundf(D.bpm));
    else if (fabsf(D.bend) >= 0.5f) snprintf(b, sizeof b, "%dHZ %+dST", (int)lroundf(D.mat_hz), (int)lroundf(D.bend));
    else snprintf(b, sizeof b, "%dHZ", (int)lroundf(D.mat_hz));
    oled_ftext_r(F_TINY, 127, 58, b, 0);
}

// ---------------------------------------------------------------- the count-in: a big bubble with the beats left
static void count_in(int cx, int cy)
{
    if (D.rec != 1) return;
    char b[4]; snprintf(b, sizeof b, "%d", clampi(4 - D.beat_in_bar, 1, 4));
    int r = 6 + (int)lroundf(5.0f * ease_back(clampf(D.beat_phase * 5.0f, 0.0f, 1.0f)));
    oled_disc(cx, cy, r + 1, false);
    oled_ftext_c(F_BIG, cx + 1, cy - 9, b, 0);
    xor_disc(cx, cy, r);
    oled_pixel(cx - r / 2 - 1, cy - r / 2 - 1, false);
}

// ---------------------------------------------------------------- DRUM: the compass and the liquid stick
static const int pdx[NPAD] = { 0, -1, 1, 0 }, pdy[NPAD] = { -1, 0, 0, 1 };   // FRONT LEFT RIGHT BACK
static pt_t padc[NPAD];

static void stick_target(float ax, float ay, float *tx, float *ty)
{
    float fx = D.gyro.x, lr = fabsf(D.gyro.y) > fabsf(D.gyro.z) ? D.gyro.y : D.gyro.z;
    if (pv[P_FLIP] >= 1.0f) fx = -fx;
    if (pv[P_SIDES] >= 1.0f) lr = -lr;
    float x = lr / 420.0f, y = -fx / 420.0f, l = sqrtf(x * x + y * y);
    if (l > 1.0f) { x /= l; y /= l; }
    *tx = x * ax; *ty = y * ay;
}

static void blob_line(float x0, float y0, float r0, float x1, float y1, float r1)
{
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    float pinch = clampf(1.0f - len / 22.0f, 0.35f, 1.0f);      // stretched, the neck thins
    int n = (int)(len / 2.0f) + 1;
    for (int i = 0; i <= n; i++) {
        float t = (float)i / (float)n;
        float r = lerpf(r0, r1, t) * (1.0f - (1.0f - pinch) * sin_turns(0.5f * t));
        oled_disc((int)lroundf(x0 + dx * t), (int)lroundf(y0 + dy * t), (int)lroundf(r), true);
    }
}

static void drum_scene(void)
{
    int cx = 40, cy = (int)lroundf(lerpf(31.0f, 38.0f, room)), arm = (int)lroundf(lerpf(11.0f, 17.0f, room));
    int h = room > 0.5f ? 9 : 7;
    float ax = 9.0f, ay = (float)arm - (float)h / 2.0f - 2.5f;
    // the pads
    for (int p = 0; p < NPAD; p++) {
        int v = D.pad_voice[p];
        const char *nm = dv_name[clampi(v, 0, NDV)];
        int w = capsule_w(nm), x, y;
        if (pdx[p] < 0) x = cx - 11 - w; else if (pdx[p] > 0) x = cx + 11; else x = cx - w / 2;
        y = cy + pdy[p] * arm - h / 2;
        padc[p] = (pt_t){ (float)x + (float)w * 0.5f, (float)y + (float)h * 0.5f };
        int gx = (int)lroundf(pop[p] * 3.0f), gy = (int)lroundf(pop[p] * 1.5f);
        if (gx < -1) gx = -1;
        if (gy < 0) gy = 0;
        bool lit = pop[p] > 0.3f || D.flash[p] > 0.45f;
        capsule(x - gx, y - gy, w + 2 * gx, h + 2 * gy, nm, v == DV_OFF ? 2 : lit ? 1 : 0);
        // the axis to it, dotted: the way to swing for it
        int ix = (int)padc[p].x - pdx[p] * (w / 2 + 2), iy = (int)padc[p].y - pdy[p] * (h / 2 + 1);
        for (int k = 5; k < 40; k += 2) {
            int x1 = cx + pdx[p] * k, y1 = cy + pdy[p] * k;
            if ((pdx[p] && (x1 - ix) * pdx[p] > 0) || (pdy[p] && (y1 - iy) * pdy[p] > 0)) break;
            oled_pixel(x1, y1, true);
        }
    }
    // the stick: a liquid bead on a spring, pulled by the gyro, stretching out of the middle
    float tx, ty; stick_target(ax, ay, &tx, &ty);
    tipvx += (tx - tipx) * 0.5f; tipvy += (ty - tipy) * 0.5f;
    tipvx *= 0.55f; tipvy *= 0.55f;
    tipx += tipvx; tipy += tipvy;
    trail[trail_i] = (pt_t){ tipx, tipy }; trail_i = (trail_i + 1) % 6;
    bool stroke = D.gest == 1;
    for (int i = 0; i < 6; i++) {
        const pt_t *q = &trail[(trail_i + i) % 6];
        if (fabsf(q->x) + fabsf(q->y) > 2.0f && (stroke || i >= 3)) oled_pixel(cx + (int)lroundf(q->x), cy + (int)lroundf(q->y), true);
    }
    float len = sqrtf(tipx * tipx + tipy * tipy);
    float rt = stroke ? 3.0f : 2.2f;
    if (len > 1.5f) blob_line((float)cx, (float)cy, 3.6f, (float)cx + tipx, (float)cy + tipy, rt);
    else { oled_disc(cx, cy, 4, true); oled_pixel(cx - 4, cy, false); oled_pixel(cx + 4, cy, false); oled_pixel(cx, cy - 4, false); oled_pixel(cx, cy + 4, false); }
    oled_pixel(cx - 2, cy - 2, false); oled_pixel(cx - 1, cy - 2, false); oled_pixel(cx - 2, cy - 1, false);   // the shine
    count_in(cx, cy);
}

// ---------------------------------------------------------------- the material
static void mode_weights(float *w)
{
    float s = 0.0f;
    for (int k = 0; k < 8; k++) { w[k] = fabsf(eng_mode_amp[k]); s += w[k]; }
    if (!(s > 1e-6f)) { s = 0.0f; for (int k = 0; k < 8; k++) { w[k] = 1.0f / (float)(k + 1); s += w[k]; } }   // no report: falling partials
    for (int k = 0; k < 8; k++) w[k] /= s;
}
static float vib_now(void) { float l = D.mat_lvl; if (!(l > 0.0f)) l = 0.0f; return clampf(1.0f - expf(-l * 5.0f), 0.0f, 1.0f); }

// material m around (0, 0), R px: NPT points from the right, clockwise on screen
static void contour(int m, float R, float vib, const float *w, float swell, pt_t *o)
{
    float hi = w[4] + w[5] + w[6] + w[7];
    for (int i = 0; i < NPT; i++) {
        float th = (float)i / (float)NPT, c = sin_turns(th + 0.25f), s = sin_turns(th), x, y;
        if (m == MAT_LATEX) {                           // a band between pegs: the bottom edge right to left, the top back
            const int hn = NPT / 2;
            float u = i <= hn ? 1.0f - (float)i / (float)hn : (float)(i - hn) / (float)hn;
            float d = 0.0f;
            for (int k = 0; k < 8; k++) d += w[k] * sin_turns(0.5f * (float)(k + 1) * u) * sin_turns(ph[k]);
            float amp = R * 0.7f;
            d *= vib * amp;
            float th_ = R * (0.3f + 0.75f * swell) * clampf(sin_turns(0.5f * u) * 2.5f, 0.0f, 1.0f) * (1.0f - 0.4f * fabsf(d) / amp);
            x = (u - 0.5f) * 2.0f * R * 1.8f;
            y = d + (i <= hn ? th_ : -th_);
        } else {
            float d = 0.0f;
            int kmax = m == MAT_RUBBER ? 3 : 8;
            for (int k = 0; k < kmax; k++) d += w[k] * sin_turns(th * (float)(k + 2) + 0.25f) * sin_turns(ph[k]);
            if (m == MAT_METAL) {                       // a hexagon plate; its upper modes make it spiky
                float seg = th * 6.0f; seg -= (float)(int)seg;
                float r = R * 0.866f / sin_turns((seg - 0.5f) / 6.0f + 0.25f);
                r += d * vib * R * 0.28f + vib * vib * R * (0.08f + 0.5f * hi) * ((i & 1) ? 1.0f : -1.0f) * sin_turns(spike_ph);
                x = c * r * 1.12f; y = s * r;
            } else if (m == MAT_GLASS) {                // thin and stiff: a small shiver
                float r = R + d * vib * R * 0.15f;
                x = c * r * 1.05f; y = s * r * 0.95f;
            } else {                                    // rubber: fat and squat, its low modes wobble and squash it
                float sq = vib * 0.25f * sin_turns(ph[0]);
                float r = R + d * vib * R * 0.45f;
                x = c * r * 1.3f * (1.0f + sq); y = s * r * 0.8f * (1.0f - sq);
            }
        }
        // the strikes: a dent that springs back
        for (int k = 0; k < 4; k++) {
            if (imp[k].age > 14 || imp[k].amp <= 0.0f) continue;
            float dt = th - imp[k].ang; dt -= (float)(int)dt; if (dt < 0.0f) dt += 1.0f; if (dt > 0.5f) dt -= 1.0f;
            float f = 1.0f - fabsf(dt) * 5.0f;
            if (f <= 0.0f) continue;
            float sp = sin_turns((float)imp[k].age * 0.13f + 0.25f) * powf(0.8f, (float)imp[k].age);
            float dent = imp[k].amp * R * 0.4f * f * f * sp;
            float l = sqrtf(x * x + y * y) + 1e-3f;
            x -= dent * x / l; y -= dent * y / l;
        }
        o[i] = (pt_t){ x, y };
    }
}

static int ipt(float v) { return (int)lroundf(v); }

static void poly_line(const pt_t *p, int n, int thick)
{
    for (int i = 0; i < n; i++) {
        const pt_t *a = &p[i], *b = &p[(i + 1) % n];
        oled_line(ipt(a->x), ipt(a->y), ipt(b->x), ipt(b->y), true);
        if (thick > 1) oled_line(ipt(a->x) + 1, ipt(a->y), ipt(b->x) + 1, ipt(b->y), true);
        if (thick > 2) oled_line(ipt(a->x), ipt(a->y) + 1, ipt(b->x), ipt(b->y) + 1, true);
    }
}
// scanline fill: level 0..4 (a dither's grey), on or off; with liquid, only below the surface
static void poly_fill(const pt_t *p, int n, int level, bool on, bool liquid)
{
    static const uint8_t order[4] = { 0, 3, 1, 2 };
    float ymin = 1e9f, ymax = -1e9f;
    for (int i = 0; i < n; i++) { if (p[i].y < ymin) ymin = p[i].y; if (p[i].y > ymax) ymax = p[i].y; }
    int y0 = (int)ceilf(ymin - 0.5f), y1 = (int)floorf(ymax - 0.5f);
    if (y0 < 16) y0 = 16;
    if (y1 > 63) y1 = 63;
    for (int y = y0; y <= y1; y++) {
        float yc = (float)y + 0.5f, xs[12]; int nx = 0;
        for (int i = 0, j = n - 1; i < n; j = i++) {
            float ya = p[i].y, yb = p[j].y;
            if ((ya <= yc) != (yb <= yc) && nx < 12) xs[nx++] = p[i].x + (yc - ya) / (yb - ya) * (p[j].x - p[i].x);
        }
        for (int a = 1; a < nx; a++) { float v = xs[a]; int b = a - 1; while (b >= 0 && xs[b] > v) { xs[b + 1] = xs[b]; b--; } xs[b + 1] = v; }
        for (int k = 0; k + 1 < nx; k += 2) {
            int xa = (int)ceilf(xs[k] - 0.5f), xb = (int)floorf(xs[k + 1] - 0.5f);
            if (xa < 0) xa = 0;
            if (xb > 127) xb = 127;
            for (int x = xa; x <= xb; x++) {
                if (liquid && y < surf[x]) continue;
                if (level >= 4 || order[((x & 1) << 1) | (y & 1)] < level) oled_pixel(x, y, on);
            }
        }
    }
}

// the hand's tilt through the materials: a bubble in a vial at the left edge, the material's name beside it
static void vial(float morph, int y0, int y1)
{
    oled_rframe(0, y0, 7, y1 - y0 + 1, 3);
    for (int m = 0; m < NMAT; m++) { int y = y0 + 4 + m * (y1 - y0 - 8) / 3; oled_pixel(7, y, true); oled_pixel(8, y, true); }
    int by = y0 + 4 + (int)lroundf(morph / 3.0f * (float)(y1 - y0 - 8));
    oled_disc(3, by, 2, true);
    oled_pixel(2, by - 1, false);
    const char *nm = mat_name[clampi((int)(morph + 0.5f), 0, NMAT - 1)];
    int ty = clampi(by - 2, y0, y1 - 4), w = oled_ftext_w(F_TINY, nm, 0);
    oled_rect(10, ty - 1, w + 2, 7, false);
    oled_ftext_ex(F_TINY, 11, ty, nm, 0);
}

// the bow: its stick and its hair across the shape, sawing as the box moves
// the bow: resting on the top of the shape, sawing as the box moves (its stick, its hair, the frog)
static void bow(float cx, float ytop, float R)
{
    int len = (int)(R * 2.6f), o = (int)lroundf(7.0f * sin_turns(bow_ph));
    int x0 = (int)cx - len / 2 + o, x1 = x0 + len, y = (int)ytop - 1;
    oled_rect(x0 - 3, y - 5, len + 5, 5, false);
    oled_line(x0, y - 1, x1, y - 1, true);               // the hair
    oled_line(x0, y - 4, x1, y - 4, true);               // the stick
    oled_line(x1, y - 4, x1, y - 1, true);               // its tip
    oled_rect(x0 - 3, y - 5, 4, 5, true);                // the frog
    oled_pixel(x0 - 2, y - 4, false);
}

static void material_scene(bool pump)
{
    float w[8]; mode_weights(w);
    float vib = vib_now();
    if (pump) vib = fmaxf(vib, 0.15f * D.energy);
    float m = morph_s;
    int m0 = clampi((int)m, 0, NMAT - 1), m1 = m0 < NMAT - 1 ? m0 + 1 : m0;
    float f = smoothstep(0.0f, 1.0f, m - (float)m0);
    float R = lerpf(11.0f, 15.0f, room) * (pump ? 0.6f + 0.5f * D.energy : 1.0f);
    float cx = 54.0f, cy = lerpf(31.0f, 37.0f, room);
    sh_cx = cx; sh_cy = cy; sh_r = R;
    float swell = pump ? D.energy : 0.0f;
    static pt_t a[NPT], b[NPT];
    contour(m0, R, vib, w, swell, a);
    if (m1 != m0 && f > 0.001f) contour(m1, R, vib, w, swell, b); else f = 0.0f;
    // the shake rattles it; the other hand's hits squash it (PUMP); the twist leans it
    int sh = (int)(D.shake * 3.0f + 0.5f);
    float jx = sh ? (float)(rnd(2 * sh + 1) - sh) : 0.0f, jy = sh ? (float)(rnd(3) - 1) : 0.0f;
    float lean = clampf(D.bend / 24.0f, -1.0f, 1.0f) * 0.55f;
    float sqx = 1.0f + 0.18f * squash, sqy = 1.0f - 0.22f * squash;
    for (int i = 0; i < NPT; i++) {
        float x = lerpf(a[i].x, b[i].x, f) * sqx, y = lerpf(a[i].y, b[i].y, f) * sqy;
        x -= y * lean;
        shp[i] = (pt_t){ cx + jx + x, cy + jy + y };
    }
    for (int j = 0; j < 3; j++) if (wave[j].life > 0) dot_circle(ipt(wave[j].x), ipt(wave[j].y), (int)wave[j].r, 16, 63);
    bool hard = false;                                  // a hard strike: the fill flips for a frame
    for (int k = 0; k < 4; k++) if (imp[k].age == 0 && imp[k].amp > 0.75f) hard = true;
    int lvl = clampi((int)((m - 1.0f) * 5.0f + 0.5f), 0, 4);
    if (pump) {
        // the vessel: the liquid at the energy's height, its surface sloshing with the movement
        float ymin = 1e9f, ymax = -1e9f;
        for (int i = 0; i < NPT; i++) { if (shp[i].y < ymin) ymin = shp[i].y; if (shp[i].y > ymax) ymax = shp[i].y; }
        float level = ymax - clampf(D.energy, 0.0f, 1.0f) * (ymax - ymin);
        float wa = 0.6f + 2.5f * D.motion + 3.0f * D.shake + 2.0f * squash;
        for (int x = 0; x < 128; x++) surf[x] = (int8_t)clampi((int)lroundf(level + wa * sin_turns((float)x / 23.0f + (float)fr * 0.045f)), -1, 100);
        if (D.energy > 0.01f) poly_fill(shp, NPT, 4, true, true);
        poly_line(shp, NPT, 2);
    } else {
        if (hard) lvl = lvl >= 2 ? 0 : 4;
        if (lvl) poly_fill(shp, NPT, lvl, true, false);
        poly_line(shp, NPT, m < 0.5f ? 2 : 1);
    }
    // the look of each: metal's rivets, glass's shine and glints, rubber's and latex's gloss, latex's pegs
    float wm = 1.0f - clampf(m, 0.0f, 1.0f), wg = 1.0f - clampf(fabsf(m - 1.0f), 0.0f, 1.0f);
    bool filled = !pump && lvl >= 3;
    if (wm > 0.5f && !pump) for (int i = 0; i < NPT; i += 6) {
        int x = ipt(lerpf(cx, shp[i].x, 0.72f)), y = ipt(lerpf(cy, shp[i].y, 0.72f));
        oled_rect(x, y, 2, 2, true);
    }
    if (filled || wg > 0.5f) {                          // the shine: an arc inside the upper left, and a dot
        bool on = !filled;
        for (int i = NPT * 5 / 9; i < NPT * 25 / 36; i++) {
            float k = m > 2.5f ? 0.45f : 0.68f;
            int x0 = ipt(lerpf(cx, shp[i].x, k)), y0 = ipt(lerpf(cy, shp[i].y, k));
            int x1 = ipt(lerpf(cx, shp[i + 1].x, k)), y1 = ipt(lerpf(cy, shp[i + 1].y, k));
            oled_line(x0, y0, x1, y1, on);
        }
        int i = NPT * 3 / 4 - 2;
        oled_pixel(ipt(lerpf(cx, shp[i].x, 0.6f)) + 2, ipt(lerpf(cy, shp[i].y, 0.6f)), on);
    }
    if (wg > 0.5f && vib > 0.15f) for (int g = 0; g < 1 + (vib > 0.5f); g++) {   // glints
        const pt_t *q = &shp[rnd(NPT)];
        int x = ipt(q->x), y = ipt(q->y);
        oled_line(x - 2, y, x + 2, y, true); oled_line(x, y - 2, x, y + 2, true);
    }
    if (m > 2.5f) {                                     // the pegs the band is stretched between
        int hn = NPT / 2;
        oled_rrect(ipt(shp[0].x) - 1, ipt(shp[0].y) - 3, 3, 7, 1, true);
        oled_rrect(ipt(shp[hn].x) - 1, ipt(shp[hn].y) - 3, 3, 7, 1, true);
    }
    // the mallets: a stick with a bead coming in from where it struck
    for (int k = 0; k < 4; k++) {
        if (imp[k].age > 3 || imp[k].amp <= 0.0f) continue;
        int i = clampi((int)lroundf(imp[k].ang * (float)NPT), 0, NPT * 2) % NPT;
        float dx = shp[i].x - cx, dy = shp[i].y - cy, l = sqrtf(dx * dx + dy * dy) + 1e-3f;
        dx /= l; dy /= l;
        float o = 2.0f + 2.5f * (float)imp[k].age;
        int x0 = ipt(shp[i].x + dx * o), y0 = ipt(shp[i].y + dy * o), x1 = ipt((float)x0 + dx * 11.0f), y1 = ipt((float)y0 + dy * 11.0f);
        oled_line(x0, y0, x1, y1, true); oled_line(x0 + 1, y0, x1 + 1, y1, true);
        oled_disc(x0, y0, 2, true);
    }
    if (pump || D.motion > 0.06f) {
        float yt = 1e9f;
        for (int i = 0; i < NPT; i++) if (fabsf(shp[i].x - cx) < R * 0.6f && shp[i].y < yt) yt = shp[i].y;
        bow(cx, yt, R);
    }
    count_in((int)cx, (int)cy);
}

// ---------------------------------------------------------------- CUT: the silence
static void cut_scene(void)
{
    if (cut_f == 0) { oled_rect(0, 16, 128, 48, true); return; }   // the cut itself: one white frame
    if (cut_f == 1) return;                                         // then nothing at all
    int y = 31;
    oled_rect(0, y, 128, 1, true);
    int x = (int)(((float)D.beat_in_bar + D.beat_phase) / 4.0f * 127.0f);
    oled_rect(x, y - 2, 1, 5, true);
    oled_ftext_ex(F_SMALL, 2, y + 4, "CUT", 3);
}

// ---------------------------------------------------------------- one frame's movement
static void events(void)
{
    for (int e = 0; e < nev; e++) {
        const ev_t *ev = &evq[e];
        float k = 0.35f + 0.65f * (float)ev->vel / 127.0f;
        if (ev->remote) {
            peer_pop = 1.0f; peer_popv = 0.0f; peer_name_f = 12;
            int v = clampi(D.peer_voice, 0, NDV - 1);
            if (D.role == ROLE_DRUM) {
                for (int i = 0; i < 3; i++) if (rip[i].life <= 0) { rip[i].r = 4.0f; rip[i].life = 9; break; }
            } else {
                bool right = other_on_right();
                float heavy = v == DV_KICK || v == DV_SUB ? 1.0f : v == DV_HAT || v == DV_GLASS ? 0.45f : 0.7f;
                float ang = right ? (heavy > 0.9f ? 0.95f : 0.875f) : (heavy > 0.9f ? 0.55f : 0.625f);
                strike(ang, heavy * k);
                if (D.role == ROLE_PUMP) { squash = 1.0f * k; squash_v = 0.0f; }
                big_txt = dv_name[v]; big_age = 0;
            }
            continue;
        }
        int p = ev->pad;
        if (p < 0 || p >= NPAD) continue;
        if (D.role == ROLE_DRUM) {
            pop[p] = 1.0f; popv[p] = 0.0f;
            big_txt = dv_name[clampi(D.pad_voice[p], 0, NDV)]; big_age = 0;
            int n = 4 + ev->vel / 25;
            parts_emit(&parts, padc[p].x, padc[p].y, 0.0f, 0.0f, 0.2f, PART_RING, 2);
            for (int i = 0; i < n; i++) {                // a splash out of the pad, thrown up
                float sp = 50.0f + 80.0f * k * rng_uni(&seed);
                float vx = (float)pdx[p] * sp * 0.6f + 70.0f * rng_bi(&seed), vy = (float)pdy[p] * sp * 0.6f - 50.0f - 60.0f * rng_uni(&seed);
                parts_emit(&parts, padc[p].x + 12.0f * rng_bi(&seed), padc[p].y - 2.0f, vx, vy, 0.3f + 0.25f * rng_uni(&seed), PART_SQUARE, rnd(2));
            }
            float gm = sqrtf(D.gyro.x * D.gyro.x + D.gyro.y * D.gyro.y + D.gyro.z * D.gyro.z);
            if (gm > 150.0f) { tipx = (float)pdx[p] * 9.0f; tipy = (float)pdy[p] * 9.0f; tipvx = tipvy = 0.0f; }   // a live hit: the stick snaps out
        } else {
            static const float ang[NPAD] = { 0.75f, 0.6f, 0.9f, 0.25f };      // FRONT light from above, LEFT, RIGHT, BACK a scrape
            static const float amp[NPAD] = { 0.45f, 1.0f, 0.7f, 0.3f };
            static const char *const kind[NPAD] = { "LIGHT", "HEAVY", "MIDDLE", "SCRAPE" };
            strike(ang[p], amp[p] * k);
            big_txt = kind[p]; big_age = 0;
        }
    }
    nev = 0;
}

static void step(const view_t *v)
{
    fr++;
    float want = v->cover > 0 ? 0.0f : 1.0f;            // the room the scene has: elastic
    room_v += (want - room) * 0.35f; room_v *= 0.6f; room += room_v;
    room = clampf(room, -0.15f, 1.15f);
    for (int p = 0; p < NPAD; p++) spring(&pop[p], &popv[p]);
    spring(&peer_pop, &peer_popv);
    spring(&squash, &squash_v);
    if (peer_name_f > 0) peer_name_f--;
    if (big_age < 99) big_age++;
    for (int i = 0; i < 3; i++) if (rip[i].life > 0) { rip[i].life--; rip[i].r += 9.0f; }
    for (int k = 0; k < 4; k++) if (imp[k].age < 99) imp[k].age++;
    for (int j = 0; j < 3; j++) if (wave[j].life > 0) { wave[j].life--; wave[j].r += 3.0f; }
    // the beat
    if (D.beat_in_bar != bib_was) { beat_pop = 1.0f; bib_was = D.beat_in_bar; } else beat_pop *= 0.7f;
    // the take's bars
    if (D.rec == 2 && rec_was != 2) rec_bars = 0;
    else if (D.rec == 2 && D.beat_in_bar == 0 && bib_was == 0 && beat_pop == 1.0f) rec_bars++;
    rec_was = D.rec;
    // the material's modes: each at a speed of its own (what the eye can follow, not the pitch)
    static const float rate[NMAT][2] = { { 0.21f, 0.13f }, { 0.29f, 0.17f }, { 0.07f, 0.045f }, { 0.12f, 0.08f } };
    int mi = clampi((int)(morph_s + 0.5f), 0, NMAT - 1);
    for (int k = 0; k < 8; k++) ph[k] = wrap1(ph[k] + rate[mi][0] + rate[mi][1] * (float)k * 1.37f);
    spike_ph = wrap1(spike_ph + 0.43f);
    if (morph_s < 0.0f) morph_s = D.morph;
    morph_s += (D.morph - morph_s) * 0.35f;
    bow_ph = wrap1(bow_ph + 0.06f * clampf(D.motion * 1.5f + D.energy * 0.2f * (D.role == ROLE_PUMP), 0.0f, 1.0f));
    // a shake throws rattling bits off the bottom (PAINT, PUMP)
    if (D.role != ROLE_DRUM && D.shake > 0.2f && rnd(3) == 0)
        parts_emit(&parts, 30.0f + (float)rnd(36), lerpf(40.0f, 50.0f, room), 60.0f * rng_bi(&seed), -60.0f - 40.0f * rng_uni(&seed), 0.35f, PART_SQUARE, 0);
    events();
    parts_step(&parts, 0.04f, 260.0f, 1.5f);
}

// ---------------------------------------------------------------- the whole frame
void scene_draw(const view_t *v)
{
    step(v);
    if (D.role != role_was) { if (role_was >= 0) flip_f = 1; role_was = D.role; }   // a new role: a hard cut
    if (D.cut != cut_was) { if (D.cut) cut_f = 0; else flip_f = 1; cut_was = D.cut; }
    if (D.cut) {
        cut_scene();
        cut_f++;
    } else {
        // the other hand's hits ripple in from its side (DRUM)
        bool right = other_on_right();
        for (int i = 0; i < 3; i++) if (rip[i].life > 0) dot_circle(right ? 134 : -6, 31, (int)rip[i].r, 16, 63);
        if (D.role == ROLE_DRUM) drum_scene();
        else {
            int y1 = (int)lroundf(lerpf(44.0f, 58.0f, clampf(room, 0.0f, 1.0f)));
            material_scene(D.role == ROLE_PUMP);
            vial(morph_s, 17, y1);
        }
        parts_draw(&parts);
        oled_rect(PANEL_X - 2, 16, 128 - PANEL_X + 2, 30, false);
        hands();
        beat_row();
        loop_row();
        if (v->cover <= 0) readout();
    }
    if (flip_f > 0) { oled_invert(0, 16, 128, 48); flip_f--; }
    if (v->cover > 0) oled_rect(0, STRIP_Y, 128, 64 - STRIP_Y, false);
    oled_rect(0, 0, 128, 16, false);
    static const char *const role_l[NROLE] = { "DRUM", "PAINT", "PUMP" };
    char hb[16];
    if (D.rec == 1) { snprintf(hb, sizeof hb, "REC IN %d", clampi(4 - D.beat_in_bar, 1, 4)); ui_head(hb, true); }
    else if (D.rec >= 2) { snprintf(hb, sizeof hb, "REC %d", D.rec == 3 && D.loop_bars[D.rec_slot] ? D.loop_bars[D.rec_slot] : rec_bars + 1); ui_head(hb, true); }
    else ui_head(role_l[clampi(D.role, 0, NROLE - 1)], D.cut != 0);
}

// ---------------------------------------------------------------- the splash: two sticks strike a blob from both sides
static void stick_at(int px, int py, int tx, int ty)    // a stick from its pivot to a bead
{
    oled_line(px, py, tx, ty, true); oled_line(px + 1, py, tx + 1, ty, true);
    oled_disc(tx, ty, 2, true);
}
bool scene_splash(int f)
{
    // when each stick lands (frames): together first, then a roll, then together
    static const int8_t hits_l[] = { 12, 40, 56, 66, 78 }, hits_r[] = { 12, 48, 61, 71, 78 };
    static const float morph_at[] = { 0.0f, 1.0f, 2.0f, 3.0f, 2.0f };
    if (f == 0) {
        parts.n = 0; memset(imp, 0, sizeof imp); memset(wave, 0, sizeof wave); for (int k = 0; k < 4; k++) imp[k].age = 99;
        morph_s = 0.0f; squash = squash_v = 0.0f;
    }
    fr++;
    for (int k = 0; k < 8; k++) ph[k] = wrap1(ph[k] + 0.11f + 0.09f * (float)k);
    spike_ph = wrap1(spike_ph + 0.43f);
    for (int k = 0; k < 4; k++) if (imp[k].age < 99) imp[k].age++;
    spring(&squash, &squash_v);
    // the hits
    float target = 0.0f; static float ring;
    for (int i = 0; i < 5; i++) {
        if (f == hits_l[i] || f == hits_r[i]) {
            target = morph_at[i];
            if (f == hits_l[i]) strike(0.5f, 0.9f);
            if (f == hits_r[i]) strike(0.0f, 0.9f);
            squash = 0.9f; squash_v = 0.0f; ring = 1.0f;
            for (int j = 0; j < 3; j++) if (wave[j].life <= 0) { wave[j].x = 64.0f; wave[j].y = 38.0f; wave[j].r = 10.0f; wave[j].life = 8; break; }
            for (int j = 0; j < 6; j++) parts_emit(&parts, 64.0f + 14.0f * rng_bi(&seed), 32.0f, 90.0f * rng_bi(&seed), -60.0f - 60.0f * rng_uni(&seed), 0.5f, PART_SQUARE, rnd(2));
        }
        if (f >= hits_l[i]) target = morph_at[i];
    }
    morph_s += (target - morph_s) * 0.45f;
    ring *= 0.9f;
    for (int j = 0; j < 3; j++) if (wave[j].life > 0) { wave[j].life--; wave[j].r += 6.0f; dot_circle(64, 38, (int)wave[j].r, 16, 63); }
    parts_step(&parts, 0.04f, 260.0f, 1.0f);
    // the blob (the material scene's contour, ringing)
    float w[8]; for (int k = 0; k < 8; k++) w[k] = 1.0f / (float)(k + 1);
    float s = 0; for (int k = 0; k < 8; k++) s += w[k];
    for (int k = 0; k < 8; k++) w[k] /= s;
    if (f >= 12) {
        float R = 12.0f;
        static pt_t a[NPT], b[NPT];
        int m0 = clampi((int)morph_s, 0, NMAT - 1), m1 = m0 < NMAT - 1 ? m0 + 1 : m0;
        float t = smoothstep(0.0f, 1.0f, morph_s - (float)m0);
        contour(m0, R, 0.9f * ring + 0.05f, w, 0.0f, a);
        contour(m1, R, 0.9f * ring + 0.05f, w, 0.0f, b);
        for (int i = 0; i < NPT; i++) shp[i] = (pt_t){ 64.0f + lerpf(a[i].x, b[i].x, t) * (1.0f + 0.2f * squash), 38.0f + lerpf(a[i].y, b[i].y, t) * (1.0f - 0.25f * squash) };
        int lvl = clampi((int)((morph_s - 1.0f) * 5.0f + 0.5f), 0, 4);
        if (lvl) poly_fill(shp, NPT, lvl, true, false);
        poly_line(shp, NPT, morph_s < 0.5f ? 2 : 1);
        if (lvl >= 3) {
            for (int i = NPT * 5 / 9; i < NPT * 25 / 36; i++) {
                float k = morph_s > 2.5f ? 0.45f : 0.68f;
                oled_line(ipt(lerpf(64.0f, shp[i].x, k)), ipt(lerpf(38.0f, shp[i].y, k)), ipt(lerpf(64.0f, shp[i + 1].x, k)), ipt(lerpf(38.0f, shp[i + 1].y, k)), false);
            }
        }
    }
    parts_draw(&parts);
    // the sticks: each swings in from its lower corner, lands, lifts away
    for (int side = 0; side < 2; side++) {
        const int8_t *hs = side ? hits_r : hits_l;
        int last = -100, next = 200;
        for (int i = 0; i < 5; i++) { if (hs[i] <= f && hs[i] > last) last = hs[i]; if (hs[i] > f && hs[i] < next) next = hs[i]; }
        float out;                                       // 0 on the blob, 1 lifted
        if (f - last <= 1) out = 0.0f;
        else if (next - f <= 4) out = (float)(next - f) / 4.0f;
        else out = clampf((float)(f - last) / 6.0f, 0.0f, 1.0f);
        out = f < 12 && last < 0 ? clampf((float)(12 - f) / 12.0f, 0.0f, 1.0f) * 1.6f : ease_out(out);
        float tx = side ? 79.0f : 49.0f, ty = 38.0f;
        float lx = side ? 112.0f : 16.0f, ly = 18.0f - 10.0f * fmaxf(0.0f, out - 1.0f);
        int x = ipt(lerpf(tx, lx, fminf(out, 1.6f))), y = ipt(lerpf(ty, ly, fminf(out, 1.0f)));
        stick_at(side ? 127 : 0, 63, x, y);
    }
    if (f == 12) oled_invert(0, 16, 128, 48);           // the first strike: a hard flash
    // the name drops in letter by letter, bouncing; then the tagline
    oled_rect(0, 0, 128, 16, false);
    if (f >= 20) {
        const char *nm = "AIRDWELLER";
        int x = 64 - oled_ftext_w(F_MED, nm, 3) / 2;
        for (int i = 0; nm[i]; i++) {
            char c[2] = { nm[i], 0 };
            float t = clampf((float)(f - 20 - 2 * i) / 8.0f, 0.0f, 1.0f);
            if (t > 0.0f) {
                int y = ipt(-10.0f + 13.0f * ease_back(t));
                oled_ftext_ex(F_MED, x, y, c, 0); oled_ftext_ex(F_MED, x + 1, y, c, 0);
            }
            x += oled_ftext_w(F_MED, c, 0) + 3;
        }
    }
    if (f >= 56) {
        const char *tag = "TWO HANDS, ONE INSTRUMENT";
        char b[32]; snprintf(b, sizeof b, "%.*s", clampi((f - 56) * 2, 0, 31), tag);
        oled_rect(0, 55, 128, 9, false);
        oled_ftext_ex(F_SMALL, 64 - oled_ftext_w(F_SMALL, tag, 0) / 2, 57, b, 0);   // typed on, from the left
    }
    return f < 98;
}

// ---------------------------------------------------------------- the saver: two bubbles circling, drifting
void scene_saver(uint32_t f)
{
    float t = (float)f;
    float cx = 64.0f + 44.0f * sin_turns(t * 0.0009f), cy = 32.0f + 18.0f * sin_turns(t * 0.0013f + 0.3f);
    float a = t * 0.004f;
    for (int i = 0; i < 2; i++) {
        float d = i ? 11.0f : 7.0f, s = i ? -1.0f : 1.0f;
        int x = ipt(cx + s * d * sin_turns(a + 0.25f)), y = ipt(cy + s * d * 0.7f * sin_turns(a));
        int r = (i ? 4 : 7) + (int)lroundf(sin_turns(t * 0.007f + (float)i * 0.5f));
        oled_circle(x, y, r, true);
        ui_arc(x, y, r - 2, 195.0f, 245.0f);
        oled_pixel(x - r / 2 + 1, y - r / 2 - 1, true);
    }
}
