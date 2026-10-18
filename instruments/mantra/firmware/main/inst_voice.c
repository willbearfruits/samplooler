// VOICE (inst_voice.h): MANTRA's formant voices, after Klatt's cascade synthesizer (1980, 1990) and the overtone
// singers of Tuva.
//
// A voice, per sample:
//   the source   a glottal pulse: the flow's derivative of KLGLOTT88 (u = t^2 - t^3 over the open phase), its
//                closure made band limited (a polyBLEP step), an open quotient (pressed .. lax), a spectral tilt (a
//                one-pole), jitter and shimmer per period, a slow drift and flutter, vibrato that grows in on a held
//                note; aspiration noise, louder while the glottis is open; creak (ROUGH): now and then a long, weak
//                period; the undertone (sub): every other pulse weaker and later, plus the false folds' own pulse at
//                half the rate (kargyraa); the 8-bit robot: a narrow pulse instead.
//   the tract    a nasal pole and zero (equal for an oral sound: they cancel; M, N, NG move the zero to about 1.1,
//                1.7, 2.6 kHz, the mouth's formants damped for the murmur), then F1..F5 in cascade (Klatt's
//                resonators, unity at DC: the formants' levels come out right by themselves); TRACT scales them all.
//   the noise    frication in parallel: two bands and a flat part per fricative (S high, SH lower, F and TH flat and
//                weak), the voiced ones pulsed by the glottis. A stop: a closure (silence, or a murmured voice bar for
//                B D G), a burst (a few ms of noise shaped by its place: P flat, T high, K on the next vowel's F2),
//                and for P T K aspiration through the formants as they move to the next vowel. H: aspiration through
//                the next vowel's formants.
// The phonemes: Peterson and Barney (1952, men) for the vowels, Hillenbrand (1995) for the diphthongs, Klatt (1980)
//   for the consonants. A mouth moves to each phoneme's targets through two one-poles in a row (an S curve: vowels
//   50-70 ms, consonants 15-40, MOUTH SPEED scales them); diphthongs glide through their length.
// Control rate: every block (64 frames, 2 ms) and wherever an event or a stop's phase begins (exact to the sample);
//   amplitudes ramp per sample. Coefficients are only remade when what they come from moved.
// The overtone whistle (the lead only): F2 and F3 meet on harmonic `overtone` of the voice's own pitch now (remade
//   every block, so the peak stays on its harmonic through vibrato and glides), narrowed (khoomei about 40 Hz wide ..
//   sygyt 10), F1 pulled down to an "er/ng" throat, F4 and F5 damped and the glottis pressed: one loud, narrow peak.
// The choir: each voice its own singer (tract, vibrato, drift); CHOIR spreads them in cents and onsets.
// The 8-bit robot: a narrow pulse, the pitch on semitones without vibrato, the formants stepping coarsely every
//   8 ms on a grid, the voices' mix held at a lower rate (RATE) and bit depth (BITS), then band limited.
// The effect (mv_effect): the mic, whitened by a 12-pole LPC (its own mouth taken out), through the jaw's vowel;
//   the whistle a narrow resonance on the tracked pitch's harmonic, the undertone a glottal voice an octave under
//   the tracked pitch, both through the same mouth; the level matched to the mic's. Pitch: SING's YIN at 8 kHz.
//
// The tract's top: F6 and F7 (the same for every voice) and the higher poles' lift (two shelves) run once on the
//   voices' mix; frication goes around them. The bus rides its gain down when six loud voices pass 0.55 together.
// Cost (tools/sim, a Ryzen 7 3700X): 6.6 us per 64-frame block with six voices saying words (the worst case), 6.4 with
//   six singing vowels (budget 8); the effect about 2 more. Memory: about 22 KB static (internal RAM), nothing
//   allocated. tools/voicelab is a harness of its own: renders, LPC formants, spectra, the cost.
#include "mantra.h"
#include <string.h>

#define SR     ((float)AUDIO_SR)
#define ISR    (1.0f / (float)AUDIO_SR)
#define PI_F   3.14159265f
#define QN     32u
// the whistle's throat and level (tools/voicelab: whistle)
#define WH_BNP   900.0f
#define WH_BNZ   250.0f
#define WH_GAIN0 1.0f
#define WH_GAIN1 0.56f
// the tract's top: F4..F7 about a kilohertz apart, as in a man's 17 cm tract; then the higher poles' lift (a cascade
// of seven poles alone goes dark above 4 kHz: the poles above them would lift 3..6 kHz by 10..30 dB), two shelves on
// the voices' mix (tools/voicelab: env)
#define FRIC_GAIN 1.4f
#define F4_HZ    3400.0f
#define F4_BW    200.0f
#define F5_HZ    4400.0f
#define F5_BW    250.0f
#define F6_HZ    5400.0f
#define F6_BW    350.0f
#define F7_HZ    6400.0f
#define F7_BW    450.0f
#define HPC1_HZ  2500.0f
#define HPC1_DB  12.0f
#define HPC2_HZ  5000.0f
#define HPC2_DB  18.0f

// ---------------------------------------------------------------- params
enum { VP_LEVEL, VP_TRACT, VP_BREATH, VP_ROUGH, VP_VIB, VP_VRATE, VP_SHARP, VP_SUB, VP_CHOIR, VP_8BIT,
       VP_BACK, VP_PRESS, VP_NASAL, VP_RING, VP_SPEED, VP_GLIDE, VP_DRIFT, VP_BITS, VP_RATE, VP_N };
static const char *const pages[] = { "VOICE", "VOICE 2" };
static const param_t params[VP_N] = {
    [VP_LEVEL] = { "LEVEL", "vlvl", 0, PK_NUM, PF_INT, 0, 100, 5, 80, "%", NULL, "HOW LOUD THE SINGERS ARE" },
    [VP_TRACT] = { "TRACT", "vtrc", 0, PK_NUM, PF_INT | PF_SIGNED, -100, 100, 5, 0, "%", NULL, "A CHILD .. A GIANT" },
    [VP_BREATH]= { "BREATH", "vbre", 0, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "AIR IN THE VOICE" },
    [VP_ROUGH] = { "ROUGH", "vrou", 0, PK_NUM, PF_INT, 0, 100, 5, 10, "%", NULL, "A WORN, CREAKY VOICE" },
    [VP_VIB]   = { "VIBRATO", "vvib", 0, PK_NUM, PF_INT, 0, 100, 5, 25, "%", NULL, "HOW MUCH A NOTE WAVERS" },
    [VP_VRATE] = { "VIB RATE", "vvrt", 0, PK_NUM, 0, 3, 8, 0.1f, 5.5f, "Hz", NULL, "HOW FAST IT WAVERS" },
    [VP_SHARP] = { "WHISTLE", "vshp", 0, PK_NUM, PF_INT, 0, 100, 5, 30, "%", NULL, "KHOOMEI .. SYGYT" },
    [VP_SUB]   = { "UNDERTONE", "vund", 0, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "AN OCTAVE BELOW: KARGYRAA" },
    [VP_CHOIR] = { "CHOIR", "vchr", 0, PK_NUM, PF_INT, 0, 100, 5, 30, "%", NULL, "THE SINGERS SPREAD" },
    [VP_8BIT]  = { "8-BIT", "v8bt", 0, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "AN OLD COMPUTER SINGS" },
    [VP_BACK]  = { "BACKING", "vbak", 1, PK_NUM, PF_INT, 0, 100, 5, 100, "%", NULL, "THE OTHERS UNDER THE LEAD" },
    [VP_PRESS] = { "PRESSED", "vprs", 1, PK_NUM, PF_INT, 0, 100, 5, 40, "%", NULL, "LAX .. PRESSED AND BRIGHT" },
    [VP_NASAL] = { "NASAL", "vnas", 1, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "SINGING THROUGH THE NOSE" },
    [VP_RING]  = { "RING", "vrng", 1, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "THE OPERA SINGER'S RING" },
    [VP_SPEED] = { "MOUTH SPEED", "vart", 1, PK_NUM, PF_INT, 50, 200, 10, 100, "%", NULL, "HOW FAST THE MOUTH MOVES" },
    [VP_GLIDE] = { "GLIDE", "vgld", 1, PK_NUM, PF_INT, 0, 500, 10, 60, "ms", NULL, "NOTES SLIDE INTO THE NEXT" },
    [VP_DRIFT] = { "DRIFT", "vdri", 1, PK_NUM, PF_INT, 0, 100, 5, 30, "%", NULL, "THE PITCH WANDERS A LITTLE" },
    [VP_BITS]  = { "BITS", "vbit", 1, PK_NUM, PF_INT, 3, 8, 1, 5, "", NULL, "8-BIT: HOW COARSE" },
    [VP_RATE]  = { "RATE", "vrat", 1, PK_NUM, PF_LOG | PF_INT, 4000, 16000, 1.06f, 8000, "Hz", NULL, "8-BIT: ITS SAMPLE RATE" },
};
// each sound's voice (the score sings it; these are the singers)
static const char *const sounds[MN_NSOUNDS] = {
    "vtrc=30 vbre=60 vrou=20 vvib=5 vund=45 vchr=25 vprs=55 vnas=15 vdri=25 vgld=120",             // OM
    "vtrc=25 vbre=60 vrou=25 vvib=10 vund=20 vchr=70 vprs=50 vnas=15 vdri=35 vgld=120",            // MONKS
    "vtrc=15 vbre=50 vrou=15 vvib=10 vund=15 vchr=40 vprs=50 vnas=10 vdri=25 vart=90",             // OM MANI
    "vtrc=0 vbre=35 vrou=5 vvib=0 vshp=25 vprs=80 vdri=10 vgld=80",                                // KHOOMEI
    "vtrc=-10 vbre=30 vrou=5 vvib=0 vshp=90 vprs=90 vdri=10 vgld=60",                              // SYGYT
    "vtrc=35 vbre=40 vrou=35 vvib=0 vund=95 vprs=85 vdri=15 vgld=150",                             // KARGYRAA
    "vtrc=5 vbre=35 vrou=0 vvib=15 vchr=10 vprs=45 vdri=8 vgld=40",                                // DAISY
    "v8bt=100 vbre=0 vrou=0 vvib=0 vdri=0 vchr=0 vbit=5 vrat=8000 vgld=0",                         // DAISY 8-BIT
    "vbre=55 vrou=10 vvib=35 vgld=80",                                                             // VOWELS
    "vbre=45 vvib=20 vgld=40",                                                                     // HUM ALONG
    "vtrc=30 vbre=50 vrou=35 vvib=5 vund=35 vchr=60 vprs=60 vnas=10 vdri=30 vgld=150",            // SUNN: a little undertone, rough
    "vtrc=10 vbre=35 vrou=5 vvib=20 vchr=10 vprs=45 vdri=10 vgld=120",                             // DOOM DAISY: plain and slow
    "vtrc=15 vbre=45 vrou=0 vvib=15 vund=0 vchr=45 vprs=45 vnas=5 vdri=15 vgld=120",              // ORGAN: clean
};

// ---------------------------------------------------------------- the phonemes
static const char *const names[PH_N_] = { "SIL", "AA", "AE", "AH", "AO", "EH", "ER", "IH", "IY", "OW", "UH", "UW", "AY", "EY", "AW", "OY",
    "M", "N", "NG", "L", "R", "W", "Y", "F", "V", "TH", "DH", "S", "Z", "SH", "ZH", "HH", "P", "B", "T", "D", "K", "G", "CH", "JH" };
enum { K_SIL, K_VOW, K_DIPH, K_NAS, K_LIQ, K_GLI, K_FRI, K_ASP, K_STOP, K_AFF };
enum { FR_NONE, FR_S, FR_SH, FR_F, FR_TH, FR_BP, FR_BT, FR_BK, FR_N };
typedef struct {
    uint8_t  kind, voiced, fric;
    uint16_t f1, f2, f3, b1, b2, b3;
    uint16_t e1, e2, e3;       // a diphthong's end (0: none)
    uint16_t fz;               // the nasal zero (0: oral)
    uint8_t  av, af, ah;       // voicing, frication, aspiration (%)
    uint8_t  open, round;      // the mouth (%), and at a diphthong's end
    uint8_t  eopen, eround;
    uint8_t  ms;               // the move into it, ms
    uint8_t  len;              // how long spoken, 10 ms
    int8_t   db;               // level trim
} phon_t;
#define VOW(a, b, c, ba, bb, bc, op, ro, len, db) { K_VOW, 1, FR_NONE, a, b, c, ba, bb, bc, 0, 0, 0, 0, 100, 0, 0, op, ro, op, ro, 60, len, db }
#define DIP(a, b, c, ba, bb, bc, x, y, z, op, ro, eo, er, len, db) { K_DIPH, 1, FR_NONE, a, b, c, ba, bb, bc, x, y, z, 0, 100, 0, 0, op, ro, eo, er, 60, len, db }
#define CON(k, vc, fr, a, b, c, ba, bb, bc, fz, av, af, ah, op, ro, ms, len, db) { k, vc, fr, a, b, c, ba, bb, bc, 0, 0, 0, fz, av, af, ah, op, ro, op, ro, ms, len, db }
static const phon_t PT[PH_N_] = {
    [PH_SIL] = CON(K_SIL, 0, FR_NONE, 500, 1500, 2500, 90, 110, 170, 0, 0, 0, 0, 25, 0, 40, 10, 0),
    // vowels: Peterson and Barney's men
    [PH_AA] = VOW(730, 1090, 2440, 90, 90, 160, 95, 0, 16, 0),
    [PH_AE] = VOW(660, 1720, 2410, 80, 110, 170, 80, 0, 16, 0),
    [PH_AH] = VOW(640, 1190, 2390, 80, 90, 160, 70, 0, 12, 0),
    [PH_AO] = VOW(570, 840, 2410, 80, 80, 160, 70, 60, 16, 0),
    [PH_EH] = VOW(530, 1840, 2480, 70, 100, 170, 60, 0, 13, 0),
    [PH_ER] = VOW(490, 1350, 1690, 80, 80, 120, 40, 30, 15, 0),
    [PH_IH] = VOW(390, 1990, 2550, 60, 100, 170, 35, 0, 11, 0),
    [PH_IY] = VOW(270, 2290, 3010, 60, 120, 200, 20, 0, 15, 0),
    [PH_OW] = DIP(500, 910, 2460, 80, 80, 160, 420, 830, 2350, 55, 80, 35, 95, 18, 0),
    [PH_UH] = VOW(440, 1020, 2240, 70, 90, 160, 35, 55, 11, 0),
    [PH_UW] = VOW(300, 870, 2240, 65, 80, 160, 15, 100, 15, 0),
    // diphthongs: Hillenbrand's onsets, gliding to their ends
    [PH_AY] = DIP(750, 1300, 2500, 90, 90, 160, 400, 1950, 2600, 90, 0, 30, 0, 22, 0),
    [PH_EY] = DIP(480, 2050, 2650, 70, 100, 170, 320, 2250, 2950, 50, 0, 25, 0, 20, 0),
    [PH_AW] = DIP(720, 1250, 2500, 90, 90, 160, 420, 900, 2300, 85, 0, 35, 70, 24, 0),
    [PH_OY] = DIP(560, 880, 2400, 80, 80, 160, 400, 1950, 2600, 70, 60, 30, 0, 24, 0),
    // nasals: the mouth's formants for the transitions, the murmur from the nasal pole and zero (lips shut: M)
    // (a nasal at a word's end is what tells WON'T from WOMB: they last a little longer than spoken, N's zero sits
    // lower and its mouth's F2 and F3 are damped less, so the move into it is heard; measured with whisper.cpp)
    [PH_M]  = CON(K_NAS, 1, FR_NONE, 270, 1000, 2200, 120, 200, 250, 1100, 85, 0, 0, 0, 30, 25, 9, 0),
    [PH_N]  = CON(K_NAS, 1, FR_NONE, 270, 1700, 2600, 120, 140, 200, 1500, 85, 0, 0, 10, 0, 25, 9, 0),
    [PH_NG] = CON(K_NAS, 1, FR_NONE, 270, 2050, 2350, 120, 160, 220, 2600, 85, 0, 0, 20, 0, 25, 10, 0),
    // liquids and glides (Klatt)
    [PH_L]  = CON(K_LIQ, 1, FR_NONE, 360, 1050, 2880, 50, 100, 280, 0, 90, 0, 0, 30, 0, 35, 7, 0),
    [PH_R]  = CON(K_LIQ, 1, FR_NONE, 310, 1060, 1380, 70, 100, 120, 0, 90, 0, 0, 25, 40, 40, 7, 0),
    [PH_W]  = CON(K_GLI, 1, FR_NONE, 290, 610, 2150, 50, 80, 60, 0, 90, 0, 0, 10, 100, 40, 6, 0),
    [PH_Y]  = CON(K_GLI, 1, FR_NONE, 260, 2070, 3020, 40, 250, 500, 0, 90, 0, 0, 15, 0, 35, 6, 0),
    // fricatives (Klatt's formants; the noise's bands below)
    [PH_F]  = CON(K_FRI, 0, FR_F,  340, 1100, 2080, 200, 120, 150, 0, 0, 30, 0, 10, 20, 20, 10, 0),
    [PH_V]  = CON(K_FRI, 1, FR_F,  220, 1100, 2080, 60, 90, 120, 0, 55, 25, 0, 10, 20, 20, 7, 0),
    [PH_TH] = CON(K_FRI, 0, FR_TH, 320, 1290, 2540, 200, 90, 200, 0, 0, 25, 0, 15, 0, 20, 9, 0),
    [PH_DH] = CON(K_FRI, 1, FR_TH, 270, 1290, 2540, 60, 80, 170, 0, 60, 15, 0, 15, 0, 20, 5, 0),
    [PH_S]  = CON(K_FRI, 0, FR_S,  320, 1390, 2530, 200, 80, 200, 0, 0, 80, 0, 15, 0, 20, 12, 0),
    [PH_Z]  = CON(K_FRI, 1, FR_S,  240, 1390, 2530, 70, 60, 180, 0, 50, 50, 0, 15, 0, 20, 8, 0),
    [PH_SH] = CON(K_FRI, 0, FR_SH, 300, 1840, 2750, 200, 100, 300, 0, 0, 100, 0, 20, 50, 20, 12, 0),
    [PH_ZH] = CON(K_FRI, 1, FR_SH, 300, 1840, 2750, 70, 60, 280, 0, 50, 65, 0, 20, 50, 20, 8, 0),
    [PH_HH] = CON(K_ASP, 0, FR_NONE, 500, 1500, 2500, 300, 150, 250, 0, 0, 0, 100, 40, 0, 15, 7, 0),
    // stops: the loci the formants move from and to; the closure, burst and aspiration from the event's length
    [PH_P]  = CON(K_STOP, 0, FR_BP, 400, 900, 2100, 300, 150, 220, 0, 0, 0, 0, 0, 20, 40, 12, 0),
    [PH_B]  = CON(K_STOP, 1, FR_BP, 200, 900, 2100, 60, 110, 130, 0, 0, 0, 0, 0, 20, 40, 9, 0),
    [PH_T]  = CON(K_STOP, 0, FR_BT, 400, 1700, 2700, 300, 120, 250, 0, 0, 0, 0, 10, 0, 40, 11, 0),
    [PH_D]  = CON(K_STOP, 1, FR_BT, 200, 1700, 2700, 60, 100, 170, 0, 0, 0, 0, 10, 0, 40, 8, 0),
    [PH_K]  = CON(K_STOP, 0, FR_BK, 300, 1900, 2300, 250, 160, 330, 0, 0, 0, 0, 20, 0, 40, 12, 0),
    [PH_G]  = CON(K_STOP, 1, FR_BK, 200, 1900, 2300, 60, 150, 280, 0, 0, 0, 0, 20, 0, 40, 9, 0),
    [PH_CH] = CON(K_AFF, 0, FR_SH, 350, 1800, 2820, 200, 90, 300, 0, 0, 100, 0, 15, 40, 30, 15, 0),
    [PH_JH] = CON(K_AFF, 1, FR_SH, 260, 1800, 2820, 60, 80, 270, 0, 50, 65, 0, 15, 40, 30, 12, 0),
};
// the noise's shape per fricative and burst: band 1 (Hz, width, gain %), band 2, the flat part
// (the noise first loses its lows by `hp`: e - hp e1, for the sharp ones)
typedef struct { uint16_t f1, b1; uint8_t g1; uint16_t f2, b2; uint8_t g2, flat, hp; } fric_t;
static const fric_t FT[FR_N] = {
    [FR_NONE] = { 2500, 1000, 0, 5000, 2000, 0, 0, 0 },
    [FR_S]  = { 5500, 1600, 100, 8000, 3000, 35, 2, 90 },
    [FR_SH] = { 2800, 800, 180, 4300, 1600, 100, 3, 60 },
    [FR_F]  = { 6500, 5000, 30, 2500, 2500, 10, 30, 0 },
    [FR_TH] = { 6500, 4000, 35, 3800, 2500, 15, 25, 20 },
    [FR_BP] = { 600, 900, 100, 1500, 1500, 30, 2, 0 },
    [FR_BT] = { 4500, 2000, 100, 6800, 3000, 50, 6, 85 },
    [FR_BK] = { 2000, 400, 100, 2300, 700, 45, 2, 30 },
};

const char *mv_ph_name(int ph) { return ph >= 0 && ph < PH_N_ ? names[ph] : "?"; }
int mv_ph_parse(const char *s) { for (int i = 0; i < PH_N_; i++) if (!strcmp(s, names[i])) return i; return -1; }
bool mv_ph_vowel(int ph) { return ph > PH_SIL && ph < PH_N_ && (PT[ph].kind == K_VOW || PT[ph].kind == K_DIPH); }
float mv_ph_len(int ph) { return ph >= 0 && ph < PH_N_ ? (float)PT[ph].len * 0.01f : 0.1f; }
int mv_parse(const char *s, uint8_t *ph, int max)
{
    int n = 0;
    while (s && *s && n < max) {
        while (*s == ' ' || *s == ',' || *s == '-') s++;
        char w[4]; int k = 0;
        while (*s && *s != ' ' && *s != ',' && *s != '-') { if (k < 3) w[k++] = *s; s++; }
        w[k] = 0;
        if (!k) break;
        int p = mv_ph_parse(w);
        if (p >= 0) ph[n++] = (uint8_t)p;
    }
    return n;
}

// ---------------------------------------------------------------- small maths (block rate; no libm)
static inline float cos_acc(float w)               // cos(w), 0 <= w <= pi, within 5e-7
{
    float s = 1.0f;
    if (w > 1.5707963f) { w = PI_F - w; s = -1.0f; }
    float x = w * w;
    return s * (1.0f + x * (-0.5f + x * (1.0f / 24.0f + x * (-1.0f / 720.0f + x * (1.0f / 40320.0f + x * (-1.0f / 3628800.0f))))));
}
static inline float expn(float x)                  // exp(-x), 0 <= x <= 0.6
{
    return 1.0f - x * (1.0f - x * (0.5f - x * (1.0f / 6.0f - x * (1.0f / 24.0f - x * (1.0f / 120.0f - x * (1.0f / 720.0f))))));
}
// Klatt's resonator: y = A x + B y1 + C y2, unity at DC
static inline void reso(float f, float bw, float *A, float *B, float *C)
{
    f = clampf(f, 30.0f, 0.44f * SR); bw = clampf(bw, 4.0f, 5500.0f);
    float r = expn(PI_F * bw * ISR), c = cos_acc(TWO_PI * f * ISR);
    *B = 2.0f * r * c; *C = -r * r; *A = 1.0f - *B - *C;
}
// a constant-peak band pass (Smith and Angell): y = g (x - x2) + B y1 + C y2, 1 at its centre
static inline void bandp(float f, float bw, float *g, float *B, float *C)
{
    f = clampf(f, 30.0f, 0.44f * SR); bw = clampf(bw, 4.0f, 5500.0f);
    float r = expn(PI_F * bw * ISR), c = cos_acc(TWO_PI * f * ISR);
    *B = 2.0f * r * c; *C = -r * r; *g = (1.0f - r * r) * 0.5f;
}
static inline float qnote(float n) { return (float)((int)(n + 512.5f) - 512); }   // the nearest whole number
static inline float kmove(float samples, float tau_s)                            // a one-pole's step over a stretch
{
    float x = samples / (tau_s * SR + 1e-3f);
    return x / (1.0f + x);
}
static inline void sm(float *s, float to, float k)                               // a smoother that settles (no creep
{                                                                                // towards a denormal, no tail)
    *s += k * (to - *s);
    if (fabsf(to - *s) < 1e-6f) *s = to;
}

// ---------------------------------------------------------------- the tract: nasal pole, nasal zero, F1..F5
#define NF 5                                       // formants per voice: F1..F5 (F6, F7 stand still: on the mix)
#define TS (NF + 2)
typedef struct {
    float A[TS], B[TS], C[TS], y1[TS], y2[TS];     // 0: nasal pole; 1: nasal zero (y1, y2 its inputs); 2..: F1..F7
    float key[2 * NF + 4];                         // what they were made from
    bool  nas;                                     // the nasal pair is not a straight wire
} tract_t;
// f: F1..F5, b: B1..B5; the nasal pole and zero (equal: no nasal at all, and the pair is left out). A pair coming in
// starts from rest: pole then zero from rest is exactly a wire, so it comes in without a click.
static void tract_design(tract_t *t, const float *f, const float *b, float fnp, float bnp, float fnz, float bnz)
{
    bool same = true;
    for (int k = 0; k < NF && same; k++) if (fabsf(f[k] - t->key[k]) > 0.5f || fabsf(b[k] - t->key[NF + k]) > 0.5f) same = false;
    float *nk = t->key + 2 * NF;
    if (same && fabsf(fnp - nk[0]) < 0.5f && fabsf(bnp - nk[1]) < 0.5f && fabsf(fnz - nk[2]) < 0.5f && fabsf(bnz - nk[3]) < 0.5f) return;
    for (int k = 0; k < NF; k++) { t->key[k] = f[k]; t->key[NF + k] = b[k]; reso(f[k], b[k], &t->A[2 + k], &t->B[2 + k], &t->C[2 + k]); }
    nk[0] = fnp; nk[1] = bnp; nk[2] = fnz; nk[3] = bnz;
    bool nas = fabsf(fnp - fnz) > 0.5f || fabsf(bnp - bnz) > 0.5f;
    if (nas && !t->nas) for (int k = 0; k < 2; k++) t->y1[k] = t->y2[k] = 0.0f;
    t->nas = nas;
    if (!nas) return;
    reso(fnp, bnp, &t->A[0], &t->B[0], &t->C[0]);
    float a, bb, c;
    reso(fnz, bnz, &a, &bb, &c);
    float ia = 1.0f / a;
    t->A[1] = ia; t->B[1] = -bb * ia; t->C[1] = -c * ia;
}
static inline float tract_run(tract_t *t, float x)
{
    float z = x;
    if (t->nas) {
        float y = t->A[0] * x + t->B[0] * t->y1[0] + t->C[0] * t->y2[0];
        t->y2[0] = t->y1[0]; t->y1[0] = y;
        z = t->A[1] * y + t->B[1] * t->y1[1] + t->C[1] * t->y2[1];
        t->y2[1] = t->y1[1]; t->y1[1] = y;
    }
    for (int k = 2; k < TS; k++) {
        float o = t->A[k] * z + t->B[k] * t->y1[k] + t->C[k] * t->y2[k];
        t->y2[k] = t->y1[k]; t->y1[k] = o; z = o;
    }
    return z;
}
static void tract_flush(tract_t *t) { for (int k = 0; k < TS; k++) { t->y1[k] = flush_denormal(t->y1[k]); t->y2[k] = flush_denormal(t->y2[k]); } }

// ---------------------------------------------------------------- the controls from the UI task (a seqlock)
typedef struct {
    mv_free_t fr[MV_N];
    float jaw, hum, tract, breath, vib, vrate, rough, sub, overtone, sharp, choir, c64, bend, level, send;
    float back, press, nasal, ring, speed, glide, drift, bits, rate;
} vctl_t;
static vctl_t pin, P;
static volatile uint32_t pin_seq;
static bool have_p;

void mv_set(const mv_ctl_t *c)
{
    vctl_t x;
    for (int v = 0; v < MV_N; v++) x.fr[v] = c->v[v];
    x.jaw = clampf(c->jaw, 0.0f, 1.0f);
    x.hum = clampf(c->hum, 0.0f, 1.0f);
    x.tract = clampf(c->tract + MP(M_VOICE, VP_TRACT) * 0.01f, -1.0f, 1.0f);
    x.breath = clampf(c->breath * MP(M_VOICE, VP_BREATH) * 0.02f, 0.0f, 1.0f);
    x.vib = clampf(c->vib_depth + MP(M_VOICE, VP_VIB) * 0.006f, 0.0f, 2.0f);
    x.vrate = clampf((c->vib_rate > 0.0f ? c->vib_rate / 5.5f : 1.0f) * MP(M_VOICE, VP_VRATE), 1.0f, 10.0f);
    x.rough = clampf(c->rough + MP(M_VOICE, VP_ROUGH) * 0.01f, 0.0f, 1.0f);
    x.sub = clampf(c->sub + MP(M_VOICE, VP_SUB) * 0.01f, 0.0f, 1.0f);
    x.overtone = c->overtone > 0.0f ? clampf(c->overtone, 2.0f, 24.0f) : 0.0f;
    x.sharp = clampf(c->sharp + MP(M_VOICE, VP_SHARP) * 0.01f, 0.0f, 1.0f);
    x.choir = clampf(c->choir + MP(M_VOICE, VP_CHOIR) * 0.01f, 0.0f, 1.0f);
    x.c64 = clampf(fmaxf(c->c64, MP(M_VOICE, VP_8BIT) * 0.01f), 0.0f, 1.0f);
    x.bend = clampf(c->bend, -48.0f, 48.0f);
    x.level = clampf(c->level * MP(M_VOICE, VP_LEVEL) * 0.0125f, 0.0f, 1.25f);
    x.send = clampf(c->send, 0.0f, 1.0f);
    x.back = MP(M_VOICE, VP_BACK) * 0.01f;
    x.press = MP(M_VOICE, VP_PRESS) * 0.01f;
    x.nasal = MP(M_VOICE, VP_NASAL) * 0.01f;
    x.ring = MP(M_VOICE, VP_RING) * 0.01f;
    x.speed = clampf(MP(M_VOICE, VP_SPEED) * 0.01f, 0.5f, 2.0f);
    x.glide = MP(M_VOICE, VP_GLIDE) * 0.001f;
    x.drift = MP(M_VOICE, VP_DRIFT) * 0.01f;
    x.bits = clampf(MP(M_VOICE, VP_BITS), 3.0f, 8.0f);
    x.rate = clampf(MP(M_VOICE, VP_RATE), 2000.0f, 16000.0f);
    __atomic_add_fetch(&pin_seq, 1, __ATOMIC_ACQ_REL);
    pin = x;
    __atomic_add_fetch(&pin_seq, 1, __ATOMIC_ACQ_REL);
}
static void params_pickup(void)
{
    for (int tries = 0; tries < 3; tries++) {
        uint32_t s = __atomic_load_n(&pin_seq, __ATOMIC_ACQUIRE);
        if (s == 0 || (s & 1u)) continue;
        vctl_t c = pin;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (s == __atomic_load_n(&pin_seq, __ATOMIC_ACQUIRE)) { P = c; have_p = true; return; }
    }
}

// ---------------------------------------------------------------- the queues (one producer: the UI task)
typedef struct { mv_ev_t buf[QN]; uint32_t w, r; } evq_t;
static evq_t Q[MV_N];
static uint32_t fl_to[MV_N], fl_seq[MV_N], fl_seen[MV_N];
bool mv_push(int v, const mv_ev_t *e)
{
    if (v < 0 || v >= MV_N || !e || e->ph >= PH_N_) return false;
    evq_t *q = &Q[v];
    uint32_t w = __atomic_load_n(&q->w, __ATOMIC_RELAXED);
    if (w - __atomic_load_n(&q->r, __ATOMIC_ACQUIRE) >= QN) return false;
    q->buf[w % QN] = *e;
    __atomic_store_n(&q->w, w + 1, __ATOMIC_RELEASE);
    return true;
}
void mv_flush(int v)
{
    if (v < 0 || v >= MV_N) return;
    __atomic_store_n(&fl_to[v], __atomic_load_n(&Q[v].w, __ATOMIC_RELAXED), __ATOMIC_RELAXED);
    __atomic_add_fetch(&fl_seq[v], 1, __ATOMIC_RELEASE);
}
int mv_queued(int v)
{
    if (v < 0 || v >= MV_N) return 0;
    return (int)(__atomic_load_n(&Q[v].w, __ATOMIC_ACQUIRE) - __atomic_load_n(&Q[v].r, __ATOMIC_ACQUIRE));
}
static inline const mv_ev_t *q_peek(int v)
{
    uint32_t r = Q[v].r;
    return r != __atomic_load_n(&Q[v].w, __ATOMIC_ACQUIRE) ? &Q[v].buf[r % QN] : NULL;
}
static inline void q_pop(int v) { __atomic_store_n(&Q[v].r, Q[v].r + 1, __ATOMIC_RELEASE); }

// ---------------------------------------------------------------- a voice
// what the mouth moves through (two one-poles in a row each)
enum { C_F1, C_F2, C_F3, C_F4, C_F5, C_B1, C_B2, C_B3, C_B4, C_B5, C_FNZ, C_BNZ, C_OPEN, C_ROUND, C_TRIM, C_N };
typedef struct {
    float c[C_N];
    float av, ah, af;          // voicing, aspiration, frication (0..1)
    int   fric;
    float ms_f, ms_a;          // the moves (ms)
} tg_t;

typedef struct {
    // the event now
    bool     has, hold;
    mv_ev_t  ev;
    uint32_t t0, t_end, t_rel, t_vot;              // start, end; a stop's burst and its voicing (or frication) onset
    int      stage;                                // a stop: 0 closure, 1 burst, 2 aspiration / frication
    bool     pend; uint32_t pend_r, pend_st;       // the next event's start, worked out once
    int      ph;
    // the mouth
    float    s1[C_N], s2[C_N];
    float    av, ah, af, fx[8];                    // amplitudes now; the noise's bands (f, bw, g, f, bw, g, flat, hp)
    int      fric_was;
    float    step_t;                               // the 8-bit: time to the next step
    // the pitch
    float    note, note_tg, glide_tau, hz;
    float    lvl, lvl_tg, gain;                    // event / free level; the gain now (per sample ramps)
    float    acc;                                  // the accent's envelope
    float    vib_ph, vib_on, vib_wob, drift, drift_v, fl_ph;
    float    detune;                               // the choir's, smoothed
    // the source
    float    gp, gdp, oq, ioq, pamp, per;          // phase in the period, its increment; open quotient; amplitude
    int      parity;
    float    pend_s;                               // the sample before (the BLEP's one sample of lookahead)
    float    tilt, tilt_k, cdp, an1;
    float    burst, burst_k;
    uint32_t rng;
    // the tract and the noise's bands
    tract_t  tr;
    float    nB[2], nC[2], ng[2], u1[2], u2[2], ux1, ux2, ue1;
    float    nkey[4];
    // what the screen sees
    float    env, f_now[5], wh;
    bool     active, snap;
} voice_t;
static voice_t vo[MV_N];

// each voice its own singer: tract, cents and onset (spread by CHOIR), vibrato rate
static const float pers_tract[MV_N] = { 0.0f, 0.055f, -0.045f, 0.09f, -0.075f, 0.03f };
static const float pers_cents[MV_N] = { 0.0f, 11.0f, -9.0f, 17.0f, -14.0f, 6.0f };
static const float pers_onset[MV_N] = { 0.0f, 0.021f, 0.009f, 0.035f, 0.015f, 0.028f };
static const float pers_vib[MV_N] = { 1.0f, 0.92f, 1.08f, 0.96f, 1.05f, 0.89f };

// the block's shared state (audio task)
static uint32_t T;                                 // the frame at the block's start
static float s_choir, s_c64, s_sub, s_sharp, s_ot, s_wh, s_level, s_send, s_back, s_bits, s_vib, s_tract;
static float s_jaw, s_hum, s_breath, s_rough, s_press, s_nasal, s_ring, s_bend;
static float trim_lin[PH_N_];
static uint32_t (*clock_fn)(void) = audio_frames;
uint32_t (*mv_clock)(void) = NULL;                 // tools: a clock of their own (NULL: audio_frames)

// the targets for a phoneme, x 0..1 through it (diphthongs)
static void ph_target(int ph, float x, tg_t *t)
{
    const phon_t *p = &PT[ph];
    float f1 = p->f1, f2 = p->f2, f3 = p->f3, op = p->open, ro = p->round;
    if (p->e1) {
        float s = smoothstep(0.2f, 0.95f, x);
        f1 += ((float)p->e1 - f1) * s; f2 += ((float)p->e2 - f2) * s; f3 += ((float)p->e3 - f3) * s;
        op += ((float)p->eopen - op) * s; ro += ((float)p->eround - ro) * s;
    }
    t->c[C_F1] = f1; t->c[C_F2] = f2; t->c[C_F3] = f3; t->c[C_F4] = F4_HZ; t->c[C_F5] = F5_HZ;
    t->c[C_B1] = p->b1; t->c[C_B2] = p->b2; t->c[C_B3] = p->b3; t->c[C_B4] = F4_BW; t->c[C_B5] = F5_BW;
    if (p->kind == K_NAS) { t->c[C_B4] = 400.0f; t->c[C_B5] = 500.0f; }   // (B2, B3: the table's, per nasal)
    t->c[C_FNZ] = p->fz ? (float)p->fz : 450.0f;
    t->c[C_BNZ] = p->fz ? 110.0f : 100.0f;
    t->c[C_OPEN] = op * 0.01f; t->c[C_ROUND] = ro * 0.01f;
    t->c[C_TRIM] = trim_lin[ph];
    t->av = p->av * 0.01f; t->af = p->af * 0.01f; t->ah = p->ah * 0.01f;
    t->fric = p->fric;
    t->ms_f = p->ms;
    t->ms_a = p->kind == K_VOW || p->kind == K_DIPH ? 14.0f : p->kind == K_FRI ? 12.0f : p->kind == K_SIL ? 45.0f : 8.0f;
}
static void lerp_tg(tg_t *a, const tg_t *b, float s)
{
    for (int k = 0; k < C_N; k++) a->c[k] += (b->c[k] - a->c[k]) * s;
    a->av += (b->av - a->av) * s; a->ah += (b->ah - a->ah) * s; a->af += (b->af - a->af) * s;
}
// the free voices' vowel from the jaw: OO OH AH EH EE
static void jaw_target(float jaw, tg_t *t)
{
    static const uint8_t pts[5] = { PH_UW, PH_OW, PH_AA, PH_EH, PH_IY };
    float j = clampf(jaw, 0.0f, 1.0f) * 4.0f;
    int k = (int)j; if (k > 3) k = 3;
    tg_t b;
    ph_target(pts[k], 0.0f, t);
    ph_target(pts[k + 1], 0.0f, &b);
    lerp_tg(t, &b, j - (float)k);
    t->ms_f = 70.0f;
}
static int jaw_ph(float jaw) { static const uint8_t pts[5] = { PH_UW, PH_OW, PH_AA, PH_EH, PH_IY }; return pts[clampi((int)(jaw * 4.0f + 0.5f), 0, 4)]; }

// stops: where the burst and the voicing fall in the event
static void stop_phases(voice_t *V, const phon_t *p, uint32_t len)
{
    float d = (float)len * ISR;
    float burst = p->fric == FR_BP ? 0.006f : p->fric == FR_BT ? 0.009f : 0.014f;
    float after = p->kind == K_AFF ? 0.55f * d : p->voiced ? 0.014f : (p->fric == FR_BP ? 0.045f : p->fric == FR_BT ? 0.055f : 0.065f);
    float most = 0.7f * d;
    if (burst + after > most) { float s = most / (burst + after); burst *= s; after *= s; }
    V->t_rel = V->t0 + (uint32_t)((d - burst - after) * SR);
    V->t_vot = V->t_rel + (uint32_t)(burst * SR);
    V->stage = 0;
}

static inline bool quiet(const voice_t *V) { return (!V->has || V->ev.ph == PH_SIL) && V->av < 0.02f; }
static void begin(voice_t *V, const mv_ev_t *e, uint32_t st)
{
    bool was_quiet = quiet(V);
    float dur = clampf(e->dur_s, 0.004f, 600.0f);
    uint32_t len = (uint32_t)(dur * SR);
    V->has = true; V->ev = *e; V->t0 = st; V->t_end = st + len; V->hold = (e->flags & MVF_HOLD) != 0;
    V->ph = e->ph;
    if (e->note > 0.0f) {
        float nn = clampf(e->note, 12.0f, 108.0f);
        if (fabsf(nn - V->note_tg) > 0.4f) V->vib_on = 0.0f;
        V->note_tg = nn;
        if (was_quiet && !(e->flags & MVF_GLIDE)) V->note = nn;
        V->glide_tau = (e->flags & MVF_GLIDE) ? (P.glide > 0.005f ? P.glide : dur * 0.33f) * 0.35f : 0.006f;
    }
    V->lvl_tg = clampf(e->level, 0.0f, 1.0f);
    if (e->flags & MVF_ACCENT) V->acc = 1.0f;
    const phon_t *p = &PT[e->ph];
    if (p->kind == K_STOP || p->kind == K_AFF) stop_phases(V, p, len);
    V->snap = was_quiet && e->ph != PH_SIL;       // a mouth that was silent is shaped before it sounds
}

// when the head of voice v's queue starts (worked out once for it)
static uint32_t head_start(voice_t *V, int v, const mv_ev_t *e, uint32_t now)
{
    uint32_t r = Q[v].r;
    if (V->pend && V->pend_r == r) return V->pend_st;
    float onset = s_choir * pers_onset[v] * SR;
    uint32_t st;
    if (e->at) st = e->at + (uint32_t)onset;
    else if (V->has && !V->hold) return V->t_end;              // right after the one now
    else st = now + (quiet(V) ? (uint32_t)onset : 0u);
    V->pend = true; V->pend_r = r; V->pend_st = st;
    return st;
}

static float glide_time(void) { return P.glide > 0.005f ? P.glide : 0.02f; }

// the targets now, and what the noise does
static void targets(voice_t *V, int v, uint32_t now, tg_t *t, bool *burst_now)
{
    *burst_now = false;
    const mv_free_t *fr = &P.fr[v];
    if (V->has) {
        const mv_ev_t *e = &V->ev;
        const phon_t *p = &PT[e->ph];
        float x = V->t_end != V->t0 ? (float)(int32_t)(now - V->t0) / (float)(int32_t)(V->t_end - V->t0) : 1.0f;
        ph_target(e->ph, clampf(x, 0.0f, 1.0f), t);
        const mv_ev_t *nx = q_peek(v);
        if (p->kind == K_ASP) {                                   // H: the next vowel's mouth, breathing
            if (nx && nx->ph != PH_SIL) { tg_t n; ph_target(nx->ph, 0.0f, &n); for (int k = 0; k < C_N; k++) t->c[k] = n.c[k]; }
            else for (int k = 0; k < C_TRIM; k++) t->c[k] = V->s2[k];
            t->c[C_B1] = fmaxf(t->c[C_B1], 400.0f);
            t->c[C_TRIM] = 1.0f;
            t->ms_f = 20.0f;
        } else if (p->kind == K_STOP || p->kind == K_AFF) {
            if (p->fric == FR_BK && nx && nx->ph != PH_SIL) {   // the velar pinch: K and G on the next vowel's F2
                tg_t n; ph_target(nx->ph, 0.0f, &n);
                float f2 = clampf(n.c[C_F2] * 1.5f, 1250.0f, 3200.0f);
                t->c[C_F2] = f2; t->c[C_F3] = f2 + 300.0f;
            }
            int st = (int32_t)(now - V->t_rel) < 0 ? 0 : (int32_t)(now - V->t_vot) < 0 ? 1 : 2;
            if (st != V->stage && st == 1) *burst_now = true;
            V->stage = st;
            if (st == 0) {                                        // the closure: lips (or tongue) shut
                t->av = p->voiced ? 0.22f : 0.0f; t->af = 0.0f; t->ah = 0.0f;
                if (p->kind == K_AFF) t->fric = FR_BT;
                for (int k = C_B2; k <= C_B5; k++) t->c[k] *= 4.0f;
                t->c[C_OPEN] = 0.0f;
                t->ms_a = 4.0f;
            } else {                                              // released: on to the next vowel
                if (p->kind == K_STOP && nx && nx->ph != PH_SIL && PT[nx->ph].kind != K_STOP) {
                    tg_t n; ph_target(nx->ph, 0.0f, &n);
                    for (int k = 0; k < C_TRIM; k++) t->c[k] = n.c[k];
                    t->ms_f = p->voiced ? 55.0f : 70.0f;
                }
                t->ms_a = 1.5f;
                if (p->kind == K_AFF && st == 1) { t->av = p->voiced ? 0.25f : 0.0f; t->af = 0.0f; t->ah = 0.0f; t->fric = FR_BT; }
                else if (p->kind == K_AFF) { t->af = p->voiced ? 0.65f : 1.0f; t->av = p->voiced ? 0.5f : 0.0f; t->fric = p->fric; t->ms_a = 3.0f; }
                else if (st == 1) { t->av = p->voiced ? 0.25f : 0.0f; t->af = 0.0f; t->ah = 0.0f; }
                else { t->av = p->voiced ? 0.6f : 0.0f; t->ah = p->voiced ? 0.04f : 0.5f; t->af = 0.0f; if (!p->voiced) t->c[C_B1] = 400.0f; }
            }
        } else if (p->kind == K_SIL) {
            for (int k = 0; k < C_TRIM; k++) t->c[k] = V->s2[k];
            t->c[C_OPEN] = 0.0f;
        } else if ((p->kind == K_VOW || p->kind == K_DIPH) && !V->hold && nx && !nx->at) {
            // the mouth is already on its way to the consonant after it: its place in the vowel's last 60 ms
            const phon_t *q = &PT[nx->ph];
            int32_t left = (int32_t)(V->t_end - now);
            if (q->kind >= K_NAS && q->kind != K_ASP && left < (int32_t)(0.06f * SR)) {
                tg_t n; ph_target(nx->ph, 0.0f, &n);
                float w = 0.55f * smoothstep(0.06f * SR, 0.0f, (float)left);
                for (int k = C_F1; k <= C_F3; k++) t->c[k] += (n.c[k] - t->c[k]) * w;
                t->ms_f = 40.0f;
            }
        }
        V->lvl_tg = e->level;
    } else if (fr->on && fr->level > 0.0f) {
        jaw_target(s_jaw, t);
        t->ms_a = 25.0f;
        V->ph = jaw_ph(s_jaw);
        float nn = clampf(fr->note, 12.0f, 108.0f);
        if (fabsf(nn - V->note_tg) > 0.4f) V->vib_on *= 0.5f;
        if (quiet(V)) V->note = nn;
        V->note_tg = nn;
        V->glide_tau = glide_time() * 0.35f;
        V->lvl_tg = fr->level;
    } else {                                                      // a rest: the mouth stays, the voice stops
        ph_target(PH_SIL, 0.0f, t);
        for (int k = 0; k < C_TRIM; k++) t->c[k] = V->s2[k];
        t->c[C_OPEN] = 0.0f;
        V->ph = PH_SIL;
    }
    // the hum: the mouth shut to M
    if (s_hum > 0.001f) {
        tg_t m; ph_target(PH_M, 0.0f, &m);
        m.av = t->av > 0.0f ? fmaxf(t->av, m.av) * 0.9f : 0.0f; m.af = t->af; m.ah = t->ah * 0.3f;
        lerp_tg(t, &m, s_hum);
        if (V->ph != PH_SIL && s_hum > 0.5f && !V->has) V->ph = PH_M;
    }
    // a nasal twang on the vowels
    if (s_nasal > 0.001f && t->c[C_FNZ] < 600.0f) { t->c[C_FNZ] = 450.0f + 350.0f * s_nasal; t->c[C_BNZ] = 100.0f + 80.0f * s_nasal; }
}

// one glottal period begins: its length (jitter, creak, the undertone), its amplitude (shimmer), its shape
typedef struct { float gdp, oq, ioq, pamp, per; int parity; uint32_t rng; } glot_t;
static inline void new_period(glot_t *g, float jit, float shim, float creak, float sub, float oq)
{
    g->parity ^= 1;
    float per = g->per * (1.0f + jit * rng_bi(&g->rng));
    float amp = 1.0f + shim * rng_bi(&g->rng);
    if (creak > 0.0f && rng_uni(&g->rng) < creak) { per *= 1.35f + 0.7f * rng_uni(&g->rng); amp *= 0.45f; }
    if (sub > 0.0f) {
        if (g->parity) { per *= 1.0f + 0.10f * sub; amp *= 1.0f - 0.55f * sub; oq = fminf(oq * (1.0f + 0.25f * sub), 0.85f); }
        else per *= 1.0f - 0.10f * sub;
    }
    per = clampf(per, 8.0f, 4000.0f);
    g->gdp = 1.0f / per;
    g->oq = clampf(oq, 0.2f, 0.85f); g->ioq = 1.0f / g->oq;
    g->pamp = amp;
}

// the segment's constants for the sample loop
typedef struct {
    float av0, dav, ah0, dah, af0, daf, g0, dg;
    float jit, shim, creak, sub, oq;
    float wg, wc, wf;                              // glottal, 8-bit pulse, false folds
    bool  noise, fric, fric_voiced;
} seg_t;

static void render(voice_t *V, float *restrict mix, float *restrict mixf, int n, const seg_t *S)
{
    float av = S->av0, ah = S->ah0, af = S->af0, g = S->g0;
    const float dav = S->dav, dah = S->dah, daf = S->daf, dg = S->dg, wg = S->wg, wc = S->wc, wf = S->wf;
    const float duty = 0.16f, tk = V->tilt_k;
    glot_t G = { V->gdp, V->oq, V->ioq, V->pamp, V->per, V->parity, V->rng };
    float gp = V->gp, pend = V->pend_s, tilt = V->tilt, an1 = V->an1;
    // the tract and the bands in registers
    float tA[TS], tB[TS], tC[TS], y1[TS], y2[TS];
    for (int k = 0; k < TS; k++) { tA[k] = V->tr.A[k]; tB[k] = V->tr.B[k]; tC[k] = V->tr.C[k]; y1[k] = V->tr.y1[k]; y2[k] = V->tr.y2[k]; }
    float ng0 = V->ng[0], ng1 = V->ng[1], nB0 = V->nB[0], nB1 = V->nB[1], nC0 = V->nC[0], nC1 = V->nC[1];
    float u10 = V->u1[0], u11 = V->u1[1], u20 = V->u2[0], u21 = V->u2[1], ux1 = V->ux1, ux2 = V->ux2, ue1 = V->ue1;
    float bg0 = V->fx[2] * FRIC_GAIN, bg1 = V->fx[5] * FRIC_GAIN, bfl = V->fx[6] * FRIC_GAIN, hp = V->fx[7], burst = V->burst;
    const float burst_k = V->burst_k;
    const bool noise = S->noise, fric = S->fric, fv = S->fric_voiced, nas = V->tr.nas;
    for (int i = 0; i < n; i++) {
        av += dav; ah += dah; af += daf; g += dg;
        const float gdp = G.gdp;
        float p = gp + gdp, cp = 0.0f, cc = 0.0f;   // BLEP corrections: the sample before, this one
        bool wrapped = false;
        if (p >= 1.0f) {
            p -= 1.0f; wrapped = true;
            float d = p / gdp;                       // the 8-bit pulse rises here
            if (wc > 0.0f) { cp += wc * d * d * 0.5f; cc -= wc * (1.0f - d) * (1.0f - d) * 0.5f; }
            new_period(&G, S->jit, S->shim, S->creak, S->sub, S->oq);
            p = p * G.gdp / gdp;                     // the rest of this sample, in the new period's units
        }
        const float oq = G.oq;
        float src = 0.0f;
        if (p < oq) { float t = p * G.ioq; src = t * (2.0f - 3.0f * t) * G.pamp * wg; }
        else if (gp < oq || wrapped) {               // the glottis closed since the last sample: a band-limited step
            float d = (p - oq) / G.gdp, h = G.pamp * wg;
            if (d < 1.0f) { cp += h * d * d * 0.5f; cc -= h * (1.0f - d) * (1.0f - d) * 0.5f; }
        }
        if (wc > 0.0f) {                             // the 8-bit robot's narrow pulse
            src += wc * (p < duty ? 1.0f - duty : -duty);
            if (p >= duty && (gp < duty || wrapped)) {
                float d = (p - duty) / G.gdp;
                if (d < 1.0f) { cp -= wc * d * d * 0.5f; cc += wc * (1.0f - d) * (1.0f - d) * 0.5f; }
            }
        }
        if (wf > 0.0f) {                             // the false folds: one wide pulse every two periods
            float p2 = (p + (float)G.parity) * 0.5f;
            const float oq2 = 0.7f;
            if (p2 < oq2) { float t = p2 * (1.0f / oq2); src += t * (2.0f - 3.0f * t) * wf; }
            else if (G.parity && (gp + 1.0f) * 0.5f < oq2 && !wrapped) {
                float d = (p2 - oq2) * 2.0f / G.gdp;
                if (d < 1.0f) { cp += wf * d * d * 0.5f; cc -= wf * (1.0f - d) * (1.0f - d) * 0.5f; }
            }
        }
        gp = p;
        float out = pend + cp; pend = src + cc;       // one sample late: the step's both sides corrected
        tilt += tk * (out - tilt);
        float x = tilt * av, nz = 0.0f;
        if (noise) {
            nz = rng_bi(&G.rng);
            x += (nz - 0.7f * an1) * ah * (p < oq ? 1.0f : 0.3f);
            an1 = nz;
        }
        // the tract: nasal pole, nasal zero, F1..F5
        float z = x;
        if (nas) {
            float y = tA[0] * x + tB[0] * y1[0] + tC[0] * y2[0];
            y2[0] = y1[0]; y1[0] = y;
            z = tA[1] * y + tB[1] * y1[1] + tC[1] * y2[1];
            y2[1] = y1[1]; y1[1] = y;
        }
        for (int k = 2; k < TS; k++) {
            float o = tA[k] * z + tB[k] * y1[k] + tC[k] * y2[k];
            y2[k] = y1[k]; y1[k] = o; z = o;
        }
        mix[i] += z * g;
        if (fric) {                                  // the noise's bands, in parallel (around the higher poles' lift)
            float e = nz * (af * (fv ? (p < oq ? 1.0f : 0.45f) : 1.0f) + burst);
            burst *= burst_k;
            float eh = e - hp * ue1; ue1 = e;
            float b0 = ng0 * (eh - ux2) + nB0 * u10 + nC0 * u20;
            u20 = u10; u10 = b0;
            float b1 = ng1 * (eh - ux2) + nB1 * u11 + nC1 * u21;
            u21 = u11; u11 = b1;
            ux2 = ux1; ux1 = eh;
            mixf[i] += (b0 * bg0 + b1 * bg1 + e * bfl) * g;
        }
    }
    V->gdp = G.gdp; V->oq = G.oq; V->ioq = G.ioq; V->pamp = G.pamp; V->parity = G.parity; V->rng = G.rng;
    V->gp = gp; V->pend_s = pend; V->tilt = tilt; V->an1 = an1;
    for (int k = 0; k < TS; k++) { V->tr.y1[k] = y1[k]; V->tr.y2[k] = y2[k]; }
    V->u1[0] = u10; V->u1[1] = u11; V->u2[0] = u20; V->u2[1] = u21; V->ux1 = ux1; V->ux2 = ux2; V->ue1 = ue1; V->burst = burst;
    V->av = av; V->ah = ah; V->af = af; V->gain = g;
}

// work out the voice's controls for the next n samples, then play them
static void segment(voice_t *V, int v, uint32_t now, float *mix, float *mixf, int n)
{
    tg_t t;
    bool burst_now;
    targets(V, v, now, &t, &burst_now);
    const float dt = (float)n;
    const bool robot = s_c64 > 0.5f;
    t.ms_a *= 1.0f - 0.6f * V->acc;
    // the mouth moves
    float speed = P.speed > 0.0f ? P.speed : 1.0f;
    float kf = kmove(dt, t.ms_f * 0.001f * 0.25f / speed);
    if (robot) {                                     // the 8-bit: halfway there every 8 ms, on a coarse grid
        V->step_t -= dt;
        if (V->step_t <= 0.0f) {
            V->step_t += 256.0f;
            for (int k = 0; k < C_N; k++) {
                float s = V->s2[k] + (t.c[k] - V->s2[k]) * 0.6f;
                float grid = k == C_F1 ? 30.0f : k <= C_F5 ? 60.0f : k <= C_B5 ? 20.0f : 0.0f;
                if (grid > 0.0f) s = qnote(s / grid) * grid;
                V->s1[k] = V->s2[k] = s;
            }
        }
    } else if (V->snap) {
        for (int k = 0; k < C_N; k++) V->s1[k] = V->s2[k] = t.c[k];
    } else {
        // the velum and the lips move fast (a nasal's release is abrupt), the tongue and the jaw at the phoneme's pace
        float kn = kmove(dt, 0.012f * 0.25f / speed);
        for (int k = 0; k < C_N; k++) {
            float kk = (k == C_FNZ || k == C_BNZ) ? fmaxf(kf, kn) : kf;
            V->s1[k] += kk * (t.c[k] - V->s1[k]); V->s2[k] += kk * (V->s1[k] - V->s2[k]);
        }
    }
    V->snap = false;
    // amplitudes: ramps over the segment
    float ka = kmove(dt, t.ms_a * 0.001f * 0.5f / speed);
    float breath = s_breath * 0.32f * (1.0f - s_c64);
    float av1 = V->av + ka * (t.av - V->av);
    float ah1 = V->ah + ka * (t.ah + breath * t.av - V->ah);
    float af1 = V->af + kmove(dt, 0.004f) * (t.af - V->af);
    if (av1 < 1e-6f) av1 = 0.0f;                     // what decays to nothing is nothing (no denormals)
    if (ah1 < 1e-6f) ah1 = 0.0f;
    if (af1 < 1e-6f) af1 = 0.0f;
    if (V->burst < 1e-6f) V->burst = 0.0f;
    if (V->acc < 1e-5f) V->acc = 0.0f;
    // the noise's bands: at once on a new fricative or burst, else they follow
    if (t.fric != FR_NONE) {
        const fric_t *f = &FT[t.fric];
        float want[8] = { f->f1, f->b1, f->g1 * 0.01f, f->f2, f->b2, f->g2 * 0.01f, f->flat * 0.01f, f->hp * 0.01f };
        if (t.fric == FR_BK) {                       // K and G: on the next vowel's F2 (front vowels high, back low)
            float f2 = clampf(V->s2[C_F2] * 1.5f, 1250.0f, 3400.0f);
            const mv_ev_t *nx = q_peek(v);
            if (nx && V->has) { tg_t nn; ph_target(nx->ph, 0.0f, &nn); f2 = clampf(nn.c[C_F2] * 1.5f, 1250.0f, 3400.0f); }
            want[0] = f2; want[3] = f2 + 300.0f;
        }
        float kk = t.fric != V->fric_was ? 1.0f : kmove(dt, 0.004f);
        for (int k = 0; k < 8; k++) V->fx[k] += kk * (want[k] - V->fx[k]);
        V->fric_was = t.fric;
    }
    if (burst_now) {
        const phon_t *p = &PT[V->ev.ph];
        V->burst = (p->voiced ? 0.6f : 1.0f) * (p->fric == FR_BP ? 0.8f : p->fric == FR_BT ? 1.5f : 2.6f);
        V->burst_k = expn(1.0f / ((p->fric == FR_BK ? 0.009f : p->fric == FR_BT ? 0.004f : 0.003f) * SR));
    }
    // the pitch: glide, vibrato that grows in, drift, flutter, the choir's cents, the bend
    V->note += kmove(dt, robot ? 0.0005f : fmaxf(V->glide_tau, 0.002f)) * (V->note_tg - V->note);
    float sec = dt * ISR;
    V->vib_on = fminf(1.0f, V->vib_on + sec * 3.0f);
    V->vib_wob += kmove(dt, 0.4f) * (rng_bi(&V->rng) - V->vib_wob);
    V->vib_ph = wrap1(V->vib_ph + sec * P.vrate * pers_vib[v] * (1.0f + 0.06f * V->vib_wob));
    float vib = s_vib * V->vib_on * V->vib_on * (1.0f + 0.15f * V->vib_wob) * sin_turns(V->vib_ph);
    V->drift_v += kmove(dt, 0.25f) * (rng_bi(&V->rng) * 2.0f - V->drift_v);
    V->drift += kmove(dt, 0.35f) * (V->drift_v - V->drift);
    V->fl_ph = wrap1(V->fl_ph + sec * 4.7f);
    float fl = 0.5f * sin_turns(V->fl_ph) + 0.3f * sin_turns(V->fl_ph * 2.7f) + 0.2f * sin_turns(V->fl_ph * 1.51f);
    float wander = (1.0f - s_c64) * (P.drift * (0.12f * V->drift + 0.03f * fl) + (v ? s_choir * 0.05f * V->drift : 0.0f));
    V->detune += kmove(dt, 0.05f) * (s_choir * pers_cents[v] * 0.01f - V->detune);
    float nn = V->note + s_bend + V->detune + (1.0f - s_c64) * vib + wander;
    if (robot) nn = qnote(nn);
    float hz = 440.0f * fast_exp2((nn - 69.0f) * (1.0f / 12.0f));
    V->hz = hz;
    V->per = SR / hz;
    // the tract: its length, the singer's own, the ring, the whistle (the bandwidths scale by the square root:
    // exp2 of half the exponent and a series for the singer's few per cent: no sqrtf in the segment)
    float own = pers_tract[v] * (0.4f + 0.6f * s_choir);
    float scale = fast_exp2(-0.5f * s_tract) * (1.0f + own);
    float bsc = fast_exp2(-0.25f * s_tract) * (1.0f + own * (0.5f - 0.125f * own));
    float f[NF], b[NF];
    for (int k = 0; k < 5; k++) { f[k] = V->s2[C_F1 + k] * scale; b[k] = V->s2[C_B1 + k] * bsc; }
    if (s_ring > 0.001f) {                           // the singer's formant: F3..F5 gathered near 2.8 kHz
        f[2] += (2650.0f * scale - f[2]) * 0.6f * s_ring; f[3] += (2950.0f * scale - f[3]) * 0.8f * s_ring;
        f[4] += (3300.0f * scale - f[4]) * 0.8f * s_ring;
        b[2] += (90.0f - b[2]) * 0.5f * s_ring; b[3] += (110.0f - b[3]) * 0.5f * s_ring;
    }
    float press = s_press, oq_open = 0.0f, wgain = 1.0f;
    // the nasal pole follows the zero a third of the way: a nasal formant under the antiresonance
    float fnp = (450.0f + 0.33f * (V->s2[C_FNZ] - 450.0f)) * scale, bnp = 100.0f * bsc, fnz = V->s2[C_FNZ] * scale, bnz = V->s2[C_BNZ] * bsc;
    if (fabsf(V->s2[C_FNZ] - 450.0f) < 0.5f && fabsf(V->s2[C_BNZ] - 100.0f) < 0.5f) { fnz = fnp; bnz = bnp; }
    float wh = v == 0 ? s_wh : 0.0f;
    V->wh = wh;
    if (wh > 0.0005f) {                              // the whistle: F2 and F3 met on the harmonic, narrow
        float fw = clampf(s_ot * hz, 200.0f, 6000.0f);
        float bw = 42.0f - 18.0f * s_sharp;          // khoomei 42 Hz wide .. sygyt 24
        float fth = clampf(0.45f * fw, 280.0f, 430.0f);
        f[0] += (fth - f[0]) * wh; b[0] += (120.0f - b[0]) * wh;
        f[1] += (fw - f[1]) * wh; b[1] += (bw - b[1]) * wh;
        f[2] += (fw * (1.012f - 0.008f * s_sharp) - f[2]) * wh; b[2] += (bw * 1.4f - b[2]) * wh;
        f[3] += (fmaxf(3500.0f, fw * 1.35f) - f[3]) * wh; b[3] += (700.0f - b[3]) * wh;
        f[4] += (fmaxf(4200.0f, fw * 1.6f) - f[4]) * wh; b[4] += (900.0f - b[4]) * wh;
        press += (1.0f - press) * wh * 0.8f;
        oq_open = -0.1f * wh;
        // a broad dip under the narrow peak (the nasal pole and zero both on it, the zero narrower): the whistle
        // comes down to the drone's level and keeps its height over its neighbours
        fnp += (fw - fnp) * wh; fnz += (fw - fnz) * wh;
        bnp += (WH_BNP - bnp) * wh; bnz += (WH_BNZ - bnz) * wh;
        wgain = 1.0f + (WH_GAIN0 + (WH_GAIN1 - WH_GAIN0) * s_sharp - 1.0f) * wh;
    }
    for (int k = 0; k < 5; k++) V->f_now[k] = f[k];
    tract_design(&V->tr, f, b, fnp, bnp, fnz, bnz);
    // the noise's bands
    bool noise = s_breath > 0.0f || V->ah > 1e-4f || ah1 > 1e-4f || V->af > 1e-5f || af1 > 1e-5f || V->burst > 1e-5f;
    if (noise && (V->af > 1e-5f || af1 > 1e-5f || V->burst > 1e-5f)) {
        float key[4] = { V->fx[0], V->fx[1], V->fx[3], V->fx[4] };
        if (fabsf(key[0] - V->nkey[0]) > 1.0f || fabsf(key[1] - V->nkey[1]) > 1.0f || fabsf(key[2] - V->nkey[2]) > 1.0f || fabsf(key[3] - V->nkey[3]) > 1.0f) {
            bandp(key[0] * fminf(scale, 1.3f), key[1], &V->ng[0], &V->nB[0], &V->nC[0]);
            bandp(key[2] * fminf(scale, 1.3f), key[3], &V->ng[1], &V->nB[1], &V->nC[1]);
            for (int k = 0; k < 4; k++) V->nkey[k] = key[k];
        }
    }
    // the source: open quotient (pressed .. lax, breathy), tilt
    float acc = V->acc;
    V->acc *= expn(fminf(dt / (0.07f * SR), 0.6f));
    float oq = 0.62f - 0.22f * press + 0.12f * s_breath - 0.08f * acc + oq_open;
    if (robot) oq = 0.5f;
    float tilt_hz = 1400.0f + 5200.0f * press + 2000.0f * acc - 900.0f * s_breath;
    tilt_hz += (9000.0f - tilt_hz) * s_c64;
    V->tilt_k = 1.0f - fast_exp2(-1.442695f * TWO_PI * clampf(tilt_hz, 600.0f, 9000.0f) * ISR);
    // the level
    float lv = V->lvl_tg * s_level * (v ? s_back : 1.0f) * V->s2[C_TRIM] * (1.0f + 0.45f * acc) * wgain * 0.11f;
    V->lvl += (lv - V->lvl) * kmove(dt, 0.01f);
    if (V->lvl < 1e-7f) V->lvl = 0.0f;
    seg_t S = {
        .av0 = V->av, .dav = (av1 - V->av) / dt, .ah0 = V->ah, .dah = (ah1 - V->ah) / dt, .af0 = V->af, .daf = (af1 - V->af) / dt,
        .g0 = V->gain, .dg = (V->lvl - V->gain) / dt,
        .jit = (1.0f - s_c64) * (0.003f + 0.03f * s_rough), .shim = (1.0f - s_c64) * (0.03f + 0.22f * s_rough),
        .creak = s_rough > 0.4f ? (s_rough - 0.4f) * 0.25f * (1.0f - s_c64) : 0.0f,
        .sub = s_sub, .oq = oq,
        .wg = 1.0f - s_c64, .wc = s_c64 * 0.85f, .wf = s_sub * s_sub * 0.75f,
        .noise = noise, .fric = noise && (V->af > 1e-5f || af1 > 1e-5f || V->burst > 1e-5f),
        .fric_voiced = PT[V->ph].voiced && (PT[V->ph].kind == K_FRI || PT[V->ph].kind == K_AFF),
    };
    if (V->gdp <= 0.0f) { V->gdp = 1.0f / V->per; V->oq = oq; V->ioq = 1.0f / oq; V->pamp = 1.0f; }
    render(V, mix, mixf, n, &S);
    V->env = V->gain * 9.0f * (V->av + 0.5f * V->ah + 0.5f * V->af);
}

static bool idle(voice_t *V, int v)
{
    return !V->has && !(P.fr[v].on && P.fr[v].level > 0.0f) && V->av < 1e-5f && V->ah < 1e-5f && V->af < 1e-5f && V->burst < 1e-5f;
}

static void voice_block(int v, float *mix, float *mixf, int n)
{
    voice_t *V = &vo[v];
    uint32_t s = __atomic_load_n(&fl_seq[v], __ATOMIC_ACQUIRE);
    if (s != fl_seen[v]) {                           // a flush: drop what was queued, the voice lets go
        fl_seen[v] = s;
        uint32_t to = __atomic_load_n(&fl_to[v], __ATOMIC_RELAXED);
        while ((int32_t)(to - Q[v].r) > 0) q_pop(v);
        V->has = false; V->pend = false;
    }
    int i = 0;
    while (i < n) {
        uint32_t now = T + (uint32_t)i;
        // what starts now
        for (int guard = 0; guard < 8; guard++) {
            const mv_ev_t *e = q_peek(v);
            if (!e) break;
            uint32_t st = head_start(V, v, e, now);
            if ((int32_t)(st - now) > 0) break;
            mv_ev_t ev = *e;
            q_pop(v); V->pend = false;
            begin(V, &ev, now);
        }
        if (V->has && !V->hold && (int32_t)(now - V->t_end) >= 0) V->has = false;
        // the next change
        int seg = n - i;
        const mv_ev_t *e = q_peek(v);
        if (e) { int32_t d = (int32_t)(head_start(V, v, e, now) - now); if (d > 0 && d < seg) seg = d; }
        if (V->has) {
            if (!V->hold) { int32_t d = (int32_t)(V->t_end - now); if (d > 0 && d < seg) seg = d; }
            const phon_t *p = &PT[V->ev.ph];
            if (p->kind == K_STOP || p->kind == K_AFF) {
                int32_t d1 = (int32_t)(V->t_rel - now), d2 = (int32_t)(V->t_vot - now);
                if (d1 > 0 && d1 < seg) seg = d1;
                if (d2 > 0 && d2 < seg) seg = d2;
            }
        }
        if (!V->active && idle(V, v)) { i += seg; continue; }   // nothing to sing: costs nothing
        V->active = true;
        segment(V, v, now, mix + i, mixf + i, seg);
        i += seg;
    }
    tract_flush(&V->tr);
    for (int k = 0; k < 2; k++) { V->u1[k] = flush_denormal(V->u1[k]); V->u2[k] = flush_denormal(V->u2[k]); }
    V->tilt = flush_denormal(V->tilt);
    if (idle(V, v)) {                                // let it ring out, then rest
        float e = 0;
        for (int k = 2; k < TS; k++) e += fabsf(V->tr.y1[k]);
        if (e < 1e-6f) { V->active = false; V->env = 0.0f; }
    }
}

// ---------------------------------------------------------------- the tract's top, on a mix
// F6 and F7 (the same for every voice but for TRACT: the cascade is linear, so once on the mix is the same as in
// each voice) and the higher poles' lift (two shelves)
typedef struct { biquad_t a, b; float A[2], B[2], C[2], y1[2], y2[2]; float scale; } hpc_t;
static void hpc_set(hpc_t *h, float scale)
{
    float z[4] = { h->a.z1, h->a.z2, h->b.z1, h->b.z2 };
    biquad_set(&h->a, BQ_HSHELF, HPC1_HZ * scale, 0.7071f, HPC1_DB);
    biquad_set(&h->b, BQ_HSHELF, HPC2_HZ * scale, 0.7071f, HPC2_DB);
    h->a.z1 = z[0]; h->a.z2 = z[1]; h->b.z1 = z[2]; h->b.z2 = z[3];
    float bsc = sqrtf(scale);
    reso(F6_HZ * scale, F6_BW * bsc, &h->A[0], &h->B[0], &h->C[0]);
    reso(F7_HZ * scale, F7_BW * bsc, &h->A[1], &h->B[1], &h->C[1]);
    h->scale = scale;
}
static void hpc_run(hpc_t *h, float *x, int n)
{
    for (int i = 0; i < n; i++) {
        float z = x[i];
        for (int k = 0; k < 2; k++) { float o = h->A[k] * z + h->B[k] * h->y1[k] + h->C[k] * h->y2[k]; h->y2[k] = h->y1[k]; h->y1[k] = o; z = o; }
        x[i] = biquad_run(&h->b, biquad_run(&h->a, z));
    }
    h->a.z1 = flush_denormal(h->a.z1); h->a.z2 = flush_denormal(h->a.z2);
    h->b.z1 = flush_denormal(h->b.z1); h->b.z2 = flush_denormal(h->b.z2);
    for (int k = 0; k < 2; k++) { h->y1[k] = flush_denormal(h->y1[k]); h->y2[k] = flush_denormal(h->y2[k]); }
}
static hpc_t hpc_mix, hpc_fx;
static inline bool hpc_quiet(const hpc_t *h)       // every state exactly zero (flushed): nothing left to ring
{
    return h->a.z1 == 0.0f && h->a.z2 == 0.0f && h->b.z1 == 0.0f && h->b.z2 == 0.0f
        && h->y1[0] == 0.0f && h->y2[0] == 0.0f && h->y1[1] == 0.0f && h->y2[1] == 0.0f;
}

// ---------------------------------------------------------------- the 8-bit: the mix held at a lower rate, fewer bits
static float lf_ph, lf_hold, lf_rate = -1;
static biquad_t lf_a, lf_b, lf_c;                  // a sixth-order Butterworth under the held rate's Nyquist
static void lofi(float *x, int n, float c0, float c1)
{
    if (fabsf(P.rate - lf_rate) > 1.0f) {
        lf_rate = P.rate;
        float fc = clampf(0.4f * lf_rate, 1000.0f, 7000.0f);
        float z[6] = { lf_a.z1, lf_a.z2, lf_b.z1, lf_b.z2, lf_c.z1, lf_c.z2 };
        biquad_set(&lf_a, BQ_LP, fc, 0.5176f, 0); biquad_set(&lf_b, BQ_LP, fc, 0.7071f, 0); biquad_set(&lf_c, BQ_LP, fc, 1.9319f, 0);
        lf_a.z1 = z[0]; lf_a.z2 = z[1]; lf_b.z1 = z[2]; lf_b.z2 = z[3]; lf_c.z1 = z[4]; lf_c.z2 = z[5];
    }
    float inc = lf_rate * ISR;
    float step = 0.5f * fast_exp2(1.0f - s_bits) * 0.6f, istep = 1.0f / step;
    float c = c0, dc = (c1 - c0) / (float)n;
    for (int i = 0; i < n; i++) {
        c += dc;
        lf_ph += inc;
        if (lf_ph >= 1.0f) { lf_ph -= 1.0f; lf_hold = (float)((int)(x[i] * istep + 4096.5f) - 4096) * step; }
        float y = biquad_run(&lf_c, biquad_run(&lf_b, biquad_run(&lf_a, lf_hold)));
        x[i] += (y - x[i]) * c;
    }
    lf_a.z1 = flush_denormal(lf_a.z1); lf_a.z2 = flush_denormal(lf_a.z2);
    lf_b.z1 = flush_denormal(lf_b.z1); lf_b.z2 = flush_denormal(lf_b.z2);
    lf_c.z1 = flush_denormal(lf_c.z1); lf_c.z2 = flush_denormal(lf_c.z2);
}

// ---------------------------------------------------------------- the view
static volatile float vw_level[MV_N], vw_note[MV_N], vw_mouth, vw_round, vw_f[3], vw_wh, vw_harm;
static volatile int vw_ph[MV_N];
void mv_view(mv_view_t *v)
{
    for (int i = 0; i < MV_N; i++) { v->level[i] = vw_level[i]; v->note[i] = vw_note[i]; v->ph[i] = vw_ph[i]; }
    v->mouth = vw_mouth; v->round = vw_round;
    v->f1 = vw_f[0]; v->f2 = vw_f[1]; v->f3 = vw_f[2];
    v->whistle = vw_wh; v->harmonic = (int)vw_harm;
}

// ---------------------------------------------------------------- the block
static float mixb[AUDIO_BLOCK], mixf[AUDIO_BLOCK], s_ot_t, s_ot1, bus_g = 1.0f;
#define BUS_TOP 0.55f
void mv_process(float *out, float *send, int n)
{
    if (n <= 0) return;
    if (n > AUDIO_BLOCK) n = AUDIO_BLOCK;
    params_pickup();
    if (!have_p) return;
    T = (mv_clock ? mv_clock : clock_fn)();
    // the global controls, smoothed per block
    const float k30 = kmove((float)n, 0.03f), k60 = kmove((float)n, 0.06f), k10 = kmove((float)n, 0.012f);
    sm(&s_choir, P.choir, k60);
    float c64_was = s_c64;
    s_c64 += k30 * (P.c64 - s_c64); if (fabsf(P.c64 - s_c64) < 1e-4f) s_c64 = P.c64;
    sm(&s_sub, P.sub, k60);
    sm(&s_sharp, P.sharp, k60);
    sm(&s_tract, P.tract, k60);
    sm(&s_jaw, P.jaw, k10);
    sm(&s_hum, P.hum, k30);
    sm(&s_breath, P.breath, k30);
    sm(&s_rough, P.rough, k30);
    sm(&s_press, P.press, k30);
    sm(&s_nasal, P.nasal, k30);
    sm(&s_ring, P.ring, k30);
    sm(&s_vib, P.vib, k60);
    sm(&s_back, P.back, k30);
    sm(&s_bits, P.bits, k30);
    sm(&s_bend, P.bend, k10);                       // the stick and the roll arrive in 10 ms steps: no zipper
    float send_was = s_send;
    sm(&s_level, P.level, k10);
    sm(&s_send, P.send, k10);
    // the whistle: on while the overtone is set; its harmonic moves as a tongue does (an S curve, about 60 ms)
    if (P.overtone > 0.0f) { if (s_ot <= 0.0f) s_ot = s_ot1 = P.overtone; s_ot_t = P.overtone; }
    s_wh += kmove((float)n, 0.05f) * ((P.overtone > 0.0f ? 1.0f : 0.0f) - s_wh);
    if (s_wh < 1e-4f && P.overtone <= 0.0f) { s_wh = 0.0f; s_ot = 0.0f; }
    if (s_ot > 0.0f) { float k = kmove((float)n, 0.014f); s_ot1 += k * (s_ot_t - s_ot1); s_ot += k * (s_ot1 - s_ot); }
    memset(mixb, 0, (size_t)n * sizeof(float)); memset(mixf, 0, (size_t)n * sizeof(float));
    bool live = false;
    for (int v = 0; v < MV_N; v++) { voice_block(v, mixb, mixf, n); live |= vo[v].active; }
    { float sc = fast_exp2(-0.5f * s_tract); if (fabsf(sc - hpc_mix.scale) > 0.03f * sc) hpc_set(&hpc_mix, sc); }
    // every voice silent, the top's filters settled (flushed to exactly zero within a few ms of silence), the 8-bit
    // off: an empty block adds nothing, so nothing per sample is run
    if (!live && hpc_quiet(&hpc_mix) && s_c64 <= 1e-4f && c64_was <= 1e-4f) {
        bus_g += (1.0f - bus_g) * kmove((float)n, 0.25f);
    } else {
        hpc_run(&hpc_mix, mixb, n);
        for (int i = 0; i < n; i++) mixb[i] += mixf[i];
        if (s_c64 > 1e-4f || c64_was > 1e-4f) lofi(mixb, n, c64_was, s_c64);
        // six loud voices together: the bus rides its gain down on the block's peak (at once) and back up (slowly),
        // and a soft knee over 0.6 takes what a block's start still has; a voice or two never get near it
        float pk = 0.0f;
        for (int i = 0; i < n; i++) { float a = fabsf(mixb[i]); if (a > pk) pk = a; }
        float g0 = bus_g, gt = pk * bus_g > BUS_TOP ? BUS_TOP / pk : 1.0f;
        bus_g = gt < bus_g ? gt : bus_g + (gt - bus_g) * kmove((float)n, 0.25f);
        float sd = send_was, dsd = (s_send - send_was) / (float)n, g = g0, dg = (bus_g - g0) / (float)n;
        for (int i = 0; i < n; i++) {
            sd += dsd; g += dg;
            float y = mixb[i] * g, a = fabsf(y);
            if (a > 0.6f) { float u = (a - 0.6f) * (1.0f / 0.38f); a = 0.6f + 0.38f * u / (1.0f + u); y = y < 0.0f ? -a : a; }
            out[i] += y; send[i] += y * sd;
        }
    }
    // the view
    for (int v = 0; v < MV_N; v++) {
        voice_t *V = &vo[v];
        vw_level[v] = V->active ? clampf(V->env, 0.0f, 1.0f) : 0.0f;
        vw_note[v] = V->note + s_bend;
        vw_ph[v] = V->active && V->env > 0.01f ? V->ph : PH_SIL;
    }
    vw_mouth = vo[0].s2[C_OPEN]; vw_round = vo[0].s2[C_ROUND];
    vw_f[0] = vo[0].f_now[0]; vw_f[1] = vo[0].f_now[1]; vw_f[2] = vo[0].f_now[2];
    vw_wh = clampf(s_wh * vo[0].env * 2.0f, 0.0f, 1.0f);
    vw_harm = s_wh > 0.1f ? (float)(int)(s_ot + 0.5f) : 0.0f;
}

// ---------------------------------------------------------------- the mic: its pitch (SING's YIN at 8 kHz)
#define XN   1024u
#define XM   (XN - 1u)
#define DN   512u
#define DM   (DN - 1u)
#define YW   80
#define TMIN 6
#define TMAX 116
#define RW   256
#define DEC  4
static float X[XN], D[DN], tmp[RW + 4 * TMAX + 16];
static uint32_t xt, td;
static int dec_ph, hop;
static biquad_t aa1, aa2;
static float env, env_pk, f0, dbuf[TMAX + 2], e_lag[TMAX + 2], f0_hist[3];
static int f0_n, hang;
static bool voiced;
static volatile float track_note;
float mv_track(void) { return track_note; }

static void yin_part(int lo, int hi)
{
    const float *b = tmp + TMAX;
    for (int tau = lo; tau <= hi; tau++) {
        const float *a = tmp + TMAX - tau;
        float r = 0;
        for (int j = 0; j < YW; j++) r += a[j] * b[j];
        float d = e_lag[0] + e_lag[tau] - 2.0f * r;
        dbuf[tau] = d > 0.0f ? d : 0.0f;
    }
}
static void yin_start(void)
{
    uint32_t w0 = td - (uint32_t)(YW + TMAX);
    for (int j = 0; j < YW + TMAX; j++) tmp[j] = D[(w0 + (uint32_t)j) & DM];
    float e = 0;
    for (int j = TMAX; j < TMAX + YW; j++) e += tmp[j] * tmp[j];
    e_lag[0] = e;
    for (int tau = 1; tau <= TMAX; tau++) {
        float in_ = tmp[TMAX - tau], out_ = tmp[TMAX - tau + YW];
        e += in_ * in_ - out_ * out_; e_lag[tau] = e;
    }
}
static void tracker_frame(void)
{
    float cum = 0, dn[TMAX + 2], cm[TMAX + 2];
    dn[0] = 1.0f; cm[0] = 0;
    for (int tau = 1; tau <= TMAX; tau++) { cum += dbuf[tau]; cm[tau] = cum; dn[tau] = cum > 1e-12f ? dbuf[tau] * (float)tau / cum : 1.0f; }
    int best = -1;
    float found = 1.0f;
    for (int tau = TMIN; tau <= TMAX; tau++) {
        float v = dn[tau], thr = 0.1f;
        if (tau <= 9) {
            thr = 0.15f;
            float a = dbuf[tau - 1], b = dbuf[tau], c = dbuf[tau + 1], den = a - 2.0f * b + c;
            if (b <= a && b <= c && den > 1e-12f && cm[tau] > 1e-12f) {
                float bi = b - (a - c) * (a - c) / (8.0f * den);
                v = (bi > 0.0f ? bi : 0.0f) * (float)tau / cm[tau];
            }
        }
        if (v < thr) { while (tau + 1 <= TMAX && dn[tau + 1] < dn[tau]) tau++; best = tau; found = v; break; }
    }
    if (best >= 0 && best <= 9 && found >= 0.1f && 2 * best + 1 <= TMAX) {
        int b2 = 2 * best;
        if (dn[b2 - 1] < dn[b2]) b2--; else if (dn[b2 + 1] < dn[b2]) b2++;
        if (dn[b2] < 0.3f * found) best = b2; else dn[best] = found;
    }
    if (best < 0) { best = TMIN; for (int tau = TMIN + 1; tau <= TMAX; tau++) if (dn[tau] < dn[best]) best = tau; }
    float dmin = dn[best];
    if (voiced && f0 > 0) {                          // an octave under the last pitch from a dip hardly clearer: YIN's error
        float t_last = (float)AUDIO_SR / (float)DEC / f0;
        int b2 = best / 2;
        if (fabsf((float)best - 2.0f * t_last) < 0.12f * (float)best && b2 > TMIN && b2 < TMAX) {
            if (dn[b2 - 1] < dn[b2]) b2--; else if (dn[b2 + 1] < dn[b2]) b2++;
            if (dn[b2] < dmin + 0.12f) { best = b2; dmin = dn[best]; }
        }
    }
    bool loud = env > (voiced ? 0.005f : 0.008f) && env > (voiced ? 0.03f : 0.05f) * env_pk;
    bool v = loud && dmin < (voiced ? 0.35f : 0.2f) && best > TMIN && best < TMAX;
    if (!v && voiced && hang < 6 && env > 0.25f * env_pk && dmin < 0.6f) { hang++; return; }
    if (!v) { voiced = false; f0_n = 0; hang = 0; return; }
    hang = 0;
    float tf = (float)best;
    {
        float a = dn[best - 1], b = dn[best], c = dn[best + 1], den = a - 2.0f * b + c;
        if (den > 1e-9f) tf += 0.5f * (a - c) / den;
    }
    // refine at 32 kHz: the plain difference over 8 ms around four times the lag
    int c = (int)(tf * (float)DEC + 0.5f), lo = c - 4, hi = c + 4;
    if (lo < 20) lo = 20;
    uint32_t base = xt - 1u - (uint32_t)(RW + hi);
    for (int j = 0; j < RW + hi + 1; j++) tmp[j] = X[(base + (uint32_t)j) & XM];
    const float *nb = tmp + hi + 1;
    float e0 = 0;
    for (int j = 0; j < RW; j++) e0 += nb[j] * nb[j];
    float el = 0;
    for (int j = hi + 1 - lo; j < hi + 1 - lo + RW; j++) el += tmp[j] * tmp[j];
    float dd[16]; int nd = hi - lo + 1, bi = 0;
    for (int k = 0; k < nd; k++) {
        int tau = lo + k;
        const float *a = tmp + hi + 1 - tau;
        if (k) el += a[0] * a[0] - a[RW] * a[RW];
        float r = 0;
        for (int j = 0; j < RW; j++) r += a[j] * nb[j];
        dd[k] = e0 + el - 2.0f * r;
        if (dd[k] < dd[bi]) bi = k;
    }
    float t32 = (float)(lo + bi);
    if (bi > 0 && bi < nd - 1) {
        float a = dd[bi - 1], b = dd[bi], cc = dd[bi + 1], den = a - 2.0f * b + cc;
        if (den > 1e-12f) t32 += 0.5f * (a - cc) / den;
    }
    float fn = (float)AUDIO_SR / t32;
    if (voiced && f0 > 0 && dmin < 0.1f && fabsf(fn - f0) > 0.04f * f0 && env > 0.2f * env_pk) { f0_hist[0] = f0_hist[1] = f0_hist[2] = fn; f0_n = 3; }
    f0_hist[f0_n % 3] = fn; f0_n++;
    float m = fn;
    if (f0_n >= 3) {
        float a = f0_hist[0], b = f0_hist[1], cc = f0_hist[2];
        m = a > b ? (b > cc ? b : (a > cc ? cc : a)) : (a > cc ? a : (b > cc ? cc : b));
    } else if (f0_n == 2) m = 0.5f * (f0_hist[0] + f0_hist[1]);
    f0 = m; voiced = true;
}
static void listen(const float *in, int n)
{
    for (int i = 0; i < n; i++) {
        float x = in[i];
        X[xt & XM] = x; xt++;
        float a = fabsf(x);
        env += (a > env ? 0.25f : 0.0031f) * (a - env);
        env_pk = env > env_pk ? env : env_pk * 0.99998f;
        float d = biquad_run(&aa2, biquad_run(&aa1, x));
        if (++dec_ph >= DEC) { dec_ph = 0; D[td & DM] = d; td++; }
    }
    aa1.z1 = flush_denormal(aa1.z1); aa1.z2 = flush_denormal(aa1.z2);
    aa2.z1 = flush_denormal(aa2.z1); aa2.z2 = flush_denormal(aa2.z2);
    env = flush_denormal(env); env_pk = flush_denormal(env_pk);
    if (!voiced && env <= 0.005f) {                  // too quiet to be voiced (tracker_frame would say so): YIN rests
        f0_n = 0; hang = 0; hop = 0; track_note = 0.0f;
        return;
    }
    if (hop == 0) { yin_start(); yin_part(1, TMAX / 2); hop = 1; }
    else { yin_part(TMAX / 2 + 1, TMAX); tracker_frame(); hop = 0; }
    track_note = voiced && f0 > 0.0f ? 69.0f + 12.0f * log2f(f0 / 440.0f) : 0.0f;
}

// ---------------------------------------------------------------- the effect: the mic through the mouth
#define LPN 12
static float lp_r[LPN + 1], lp_a[LPN + 1], lp_aw[LPN + 1], lp_hist[LPN + 1], lp_pre, lp_de;
static tract_t fx_tr;
static float fx_s[C_N], fx_amt, fx_g = 1.0f, fx_ein, fx_eout, fx_wh, fx_sub, fx_hz, fx_note;
static float fw_B[2], fw_C[2], fw_g[2], fw_u1[2], fw_u2[2], fw_x1, fw_x2, fw_w1, fw_w2, fw_key, fx_eres;
static voice_t fx_uv;                                // the undertone's source (its glottis only)
static void levinson(const float *r, float *a)
{
    float e = r[0], tmpa[LPN + 1];
    a[0] = 1.0f;
    for (int k = 1; k <= LPN; k++) a[k] = 0.0f;
    if (e <= 1e-9f) return;
    for (int i = 1; i <= LPN; i++) {
        float acc = r[i];
        for (int j = 1; j < i; j++) acc += a[j] * r[i - j];
        float k = -acc / e;
        if (!(k == k)) break;
        k = clampf(k, -0.995f, 0.995f);              // a very clean input: keep going, a little less exact
        for (int j = 0; j <= i; j++) tmpa[j] = a[j];
        for (int j = 1; j < i; j++) a[j] = tmpa[j] + k * tmpa[i - j];
        a[i] = k;
        e *= 1.0f - k * k;
        if (e <= r[0] * 1e-7f) break;
    }
}
void mv_effect(const float *mic, float *out, float *send, int n, const mv_fx_t *p)
{
    if (n <= 0 || !p) return;
    if (n > AUDIO_BLOCK) n = AUDIO_BLOCK;
    listen(mic, n);
    float amt = clampf(p->amount, 0.0f, 1.0f);
    float k = kmove((float)n, 0.03f);
    float a0 = fx_amt;
    fx_amt += k * (amt - fx_amt);
    if (amt <= 0.0f && fx_amt < 1e-4f) { fx_amt = 0.0f; return; }
    // whiten: the mic's own mouth out (pre-emphasis, a 12-pole LPC from ~16 ms of autocorrelation)
    static float pe[AUDIO_BLOCK + LPN], res[AUDIO_BLOCK];
    for (int j = 0; j < LPN; j++) pe[j] = lp_hist[j];
    for (int i = 0; i < n; i++) { float x = mic[i]; pe[LPN + i] = x - 0.92f * lp_pre; lp_pre = x; }
    lp_pre = flush_denormal(lp_pre);
    for (int lag = 0; lag <= LPN; lag++) {
        float r = 0;
        for (int i = 0; i < n; i++) r += pe[LPN + i] * pe[LPN + i - lag];
        lp_r[lag] = flush_denormal(lp_r[lag] * 0.88f + r);
    }
    float rr[LPN + 1];
    for (int lag = 0; lag <= LPN; lag++) { float w = (float)lag * 0.0185f; rr[lag] = lp_r[lag] * (1.0f - w * w * 0.5f); }   // ~60 Hz lag window
    rr[0] *= 1.0005f; rr[0] += 1e-9f;              // a floor 33 dB down: stays well conditioned
    levinson(rr, lp_a);
    for (int i = 0; i < n; i++) {
        float t = (float)(i + 1) / (float)n, e = pe[LPN + i];
        for (int j = 1; j <= LPN; j++) e += (lp_aw[j] + (lp_a[j] - lp_aw[j]) * t) * pe[LPN + i - j];
        lp_de = e + 0.975f * lp_de;                  // the residual de-emphasised: a glottal source's slope again
        res[i] = lp_de;
    }
    lp_de = flush_denormal(lp_de);
    for (int j = 1; j <= LPN; j++) lp_aw[j] = lp_a[j];
    for (int j = 0; j < LPN; j++) lp_hist[j] = pe[n + j];
    // the mouth: the jaw's vowel, the hum, the tract's length
    tg_t t; jaw_target(p->jaw, &t);
    if (p->hum > 0.001f) { tg_t m; ph_target(PH_M, 0.0f, &m); lerp_tg(&t, &m, clampf(p->hum, 0.0f, 1.0f)); }
    float km = kmove((float)n, 0.012f);
    for (int c = 0; c < C_N; c++) fx_s[c] += km * (t.c[c] - fx_s[c]);
    float scale = fast_exp2(-0.5f * clampf(p->tract, -1.0f, 1.0f)), bsc = sqrtf(scale);
    float f[NF], b[NF];
    for (int c = 0; c < 5; c++) { f[c] = fx_s[C_F1 + c] * scale; b[c] = fx_s[C_B1 + c] * bsc; }
    float fnz = fx_s[C_FNZ] * scale, bnz = fx_s[C_BNZ] * bsc;
    if (fabsf(fx_s[C_FNZ] - 450.0f) < 0.5f && fabsf(fx_s[C_BNZ] - 100.0f) < 0.5f) { fnz = 450.0f * scale; bnz = 100.0f * bsc; }
    tract_design(&fx_tr, f, b, 450.0f * scale, 100.0f * bsc, fnz, bnz);
    // the pitch heard: the undertone an octave under it, the whistle on its harmonic
    float on = voiced && f0 > 0.0f ? 1.0f : 0.0f;
    if (on > 0.0f) { fx_note = 69.0f + 12.0f * log2f(f0 / 440.0f); fx_hz += (fx_hz > 0.0f ? 0.3f : 1.0f) * (f0 - fx_hz); }
    float sub_t = on * clampf(p->sub, 0.0f, 1.0f), wh_t = on * (p->overtone > 0.0f ? 1.0f : 0.0f);
    float s0 = fx_sub, w0 = fx_wh;
    sm(&fx_sub, sub_t, kmove((float)n, 0.03f));
    sm(&fx_wh, wh_t, kmove((float)n, 0.03f));
    float wf = fx_hz * clampf(p->overtone, 1.0f, 24.0f);
    if (fx_wh > 1e-4f && fabsf(wf - fw_key) > 0.2f) {
        float bw = 30.0f - 20.0f * clampf(p->sharp, 0.0f, 1.0f);
        bandp(wf, bw, &fw_g[0], &fw_B[0], &fw_C[0]);
        bandp(wf, bw, &fw_g[1], &fw_B[1], &fw_C[1]);
        fw_key = wf;
    }
    voice_t *U = &fx_uv;
    if (fx_sub > 1e-4f || s0 > 1e-4f) {
        U->per = SR / clampf(fx_hz * 0.5f, 30.0f, 600.0f);
        if (U->gdp <= 0.0f) { U->gdp = 1.0f / U->per; U->oq = 0.6f; U->ioq = 1.0f / 0.6f; U->pamp = 1.0f; U->rng = 0x1234567u; }
        U->tilt_k = 0.3f;
    }
    float inv = 0, rv = 0;
    for (int i = 0; i < n; i++) { inv += mic[i] * mic[i]; rv += res[i] * res[i]; }
    fx_ein = flush_denormal(fx_ein + 0.08f * (inv / (float)n - fx_ein));
    fx_eres = flush_denormal(fx_eres + 0.08f * (sqrtf(rv / (float)n) - fx_eres));
    float a = a0, da = (fx_amt - a0) / (float)n, su = s0, dsu = (fx_sub - s0) / (float)n, wh = w0, dwh = (fx_wh - w0) / (float)n;
    float g0 = fx_g, ov = 0;
    static float fxo[AUDIO_BLOCK];
    if (fabsf(scale - hpc_fx.scale) > 0.03f * scale) hpc_set(&hpc_fx, scale);
    for (int i = 0; i < n; i++) {
        a += da; su += dsu; wh += dwh;
        float x = res[i];
        if (su > 1e-4f) {                            // the undertone: a glottal pulse at half the pitch heard
            float gp = U->gp + U->gdp, s = 0.0f;
            if (gp >= 1.0f) { gp -= 1.0f; U->gdp = 1.0f / U->per; }
            if (gp < U->oq) { float tt = gp * U->ioq; s = tt * (2.0f - 3.0f * tt); }
            U->gp = gp;
            U->tilt += U->tilt_k * (s - U->tilt);
            x += U->tilt * su * fx_eres * 8.0f;
        }
        float y = tract_run(&fx_tr, x);
        if (wh > 1e-4f) {                            // the whistle: two narrow band passes on the harmonic
            float w1 = fw_g[0] * (x - fw_x2) + fw_B[0] * fw_u1[0] + fw_C[0] * fw_u2[0];
            fw_u2[0] = fw_u1[0]; fw_u1[0] = w1;
            float w2 = fw_g[1] * (w1 - fw_w2) + fw_B[1] * fw_u1[1] + fw_C[1] * fw_u2[1];
            fw_u2[1] = fw_u1[1]; fw_u1[1] = w2;
            fw_x2 = fw_x1; fw_x1 = x; fw_w2 = fw_w1; fw_w1 = w1;
            y += w2 * wh * 40.0f;
        }
        fxo[i] = y * g0;
    }
    hpc_run(&hpc_fx, fxo, n);
    a = a0;
    for (int i = 0; i < n; i++) {
        a += da;
        float y = fxo[i];
        ov += y * y;
        out[i] += y * a; send[i] += y * a * 0.6f;
    }
    // the level: as loud as the mic, slowly
    fx_eout = flush_denormal(fx_eout + 0.08f * (ov / (float)n - fx_eout));
    if (fx_eout > 1e-12f && fx_ein > 1e-10f) fx_g = clampf(fx_g * (1.0f + 0.03f * clampf(sqrtf(fx_ein / fx_eout) - 1.0f, -0.5f, 1.0f)), 0.02f, 50.0f);
    tract_flush(&fx_tr);
    for (int c = 0; c < 2; c++) { fw_u1[c] = flush_denormal(fw_u1[c]); fw_u2[c] = flush_denormal(fw_u2[c]); }
    (void)fx_note;
}

// ---------------------------------------------------------------- the module
void mv_init(void)
{
    for (int i = 0; i < PH_N_; i++) trim_lin[i] = fast_exp2((float)PT[i].db * 0.16609640f);
    for (int v = 0; v < MV_N; v++) {
        voice_t *V = &vo[v];
        memset(V, 0, sizeof *V);
        V->rng = 0x9E3779B9u * (uint32_t)(v + 1) + 0x7F4A7C15u;
        tg_t t; jaw_target(0.5f, &t);
        for (int k = 0; k < C_N; k++) V->s1[k] = V->s2[k] = t.c[k];
        V->note = V->note_tg = 48.0f;
        V->per = SR / 130.0f;
        V->vib_ph = 0.17f * (float)v;
        V->fl_ph = 0.31f * (float)v;
        for (int k = 0; k < 2 * NF + 4; k++) V->tr.key[k] = -1.0f;
        V->nkey[0] = -1.0f;
    }
    for (int k = 0; k < 2 * NF + 4; k++) fx_tr.key[k] = -1.0f;
    { tg_t t; jaw_target(0.5f, &t); for (int k = 0; k < C_N; k++) fx_s[k] = t.c[k]; }
    biquad_set(&aa1, BQ_LP, 3000.0f, 0.5412f, 0);
    biquad_set(&aa2, BQ_LP, 3000.0f, 1.3066f, 0);
    s_back = 1.0f; s_bits = 5.0f; s_level = 0.0f;
    hpc_set(&hpc_mix, 1.0f); hpc_set(&hpc_fx, 1.0f);
}
static void init(void) { }
static void changed(int p) { (void)p; }
const module_t mod_voice = { .name = "VOICE", .letter = 'v', .pages = pages, .npages = 2, .params = params, .nparams = VP_N,
                             .sounds = sounds, .init = init, .changed = changed };

// ---------------------------------------------------------------- for tools/sim and tools/voicelab (not on the board)
#ifndef ESP_PLATFORM
float mv_test_hz(int v) { return v >= 0 && v < MV_N ? vo[v].hz : 0.0f; }
float mv_test_env(int v) { return v >= 0 && v < MV_N ? vo[v].gain * (vo[v].av + vo[v].ah + vo[v].af) : 0.0f; }
uint32_t mv_test_t0(int v) { return v >= 0 && v < MV_N && vo[v].has ? vo[v].t0 : 0u; }
void  mv_test_formants(int v, float *f) { for (int k = 0; k < 5; k++) f[k] = v >= 0 && v < MV_N ? vo[v].f_now[k] : 0.0f; }
void  mv_test_reset(void)
{
    for (int v = 0; v < MV_N; v++) { mv_flush(v); Q[v].r = Q[v].w; fl_seen[v] = fl_seq[v]; vo[v].has = false; vo[v].pend = false; }
}
// the tract's gain at f (dB), from its coefficients
float mv_test_tract_db(int v, float f)
{
    const tract_t *t = &vo[v].tr;
    float w = TWO_PI * f * ISR, c1 = cosf(w), s1 = sinf(w), c2 = cosf(2.0f * w), s2 = sinf(2.0f * w), g = 1.0f;
    for (int k = t->nas ? 0 : 2; k < TS; k++) {
        float re = 1.0f - t->B[k] * c1 - t->C[k] * c2, im = t->B[k] * s1 + t->C[k] * s2;
        float m = t->A[k] / sqrtf(re * re + im * im);
        if (k == 1) { float a = 1.0f / t->A[1], b = -t->B[1] * a, c = -t->C[1] * a; re = 1.0f - b * c1 - c * c2; im = b * s1 + c * s2; m = sqrtf(re * re + im * im) / a; }
        g *= m;
    }
    return 20.0f * log10f(g + 1e-20f);
}
#endif
