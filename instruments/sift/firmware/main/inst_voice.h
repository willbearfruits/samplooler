// VOICE (inst_voice.c): the formant voices. Owned by the VOICE builder; refine it, but report every change.
//
// Each voice: a glottal source (a smooth band-limited pulse with its open quotient, jitter, shimmer, breath noise,
// creak; period doubling for the undertone) through a vocal tract: five formants in cascade (F1..F5, their
// bandwidths), a nasal branch (pole and zero) for M, N, NG, a parallel noise branch for S, SH, F, TH, H, bursts for
// P, T, K, B, D, G. Phonemes have targets and move to them as a mouth does (tens of ms; consonants faster). The
// overtone whistle: F2 and F3 pulled together into one very narrow peak sitting on a harmonic of the voice's own
// pitch (khoomei; narrower and higher: sygyt). The choir: voices spread in pitch and time. The 8-bit robot (SAM's
// era, our own engine): a buzzy source, coarse steps, a lower rate.
#pragma once
#include "core.h"

#define MV_N 6                 // voices: the lead (0) and five more (monks, a drone, a harmony)
enum {
    PH_SIL,                                                              // silence: the voice stops
    PH_AA, PH_AE, PH_AH, PH_AO, PH_EH, PH_ER, PH_IH, PH_IY, PH_OW, PH_UH, PH_UW,   // vowels
    PH_AY, PH_EY, PH_AW, PH_OY,                                          // diphthongs
    PH_M, PH_N, PH_NG, PH_L, PH_R, PH_W, PH_Y,                           // nasals, liquids, glides
    PH_F, PH_V, PH_TH, PH_DH, PH_S, PH_Z, PH_SH, PH_ZH, PH_HH,           // fricatives
    PH_P, PH_B, PH_T, PH_D, PH_K, PH_G, PH_CH, PH_JH,                    // stops, affricates
    PH_N_
};
const char *mv_ph_name(int ph);                  // "AA" ...
int   mv_ph_parse(const char *s);                // "AA" -> PH_AA; -1 unknown (ARPABET, upper case)
int   mv_parse(const char *s, uint8_t *ph, int max);   // "D EY Z IY" -> phonemes; how many (unknown ones skipped)
bool  mv_ph_vowel(int ph);                       // a vowel or a diphthong (the syllable's nucleus: it carries the note)
float mv_ph_len(int ph);                         // how long it lasts spoken, seconds (a stop: its closure, burst
                                                 // and aspiration together)

// A phoneme for a voice. It starts at audio frame `at` (0: right after the one before; a voice that is free or
// silent starts it at once), lasts dur_s, at `note` (MIDI, with a fraction; 0: keep the last), `level` 0..1.
// An `at` already past starts at once. An event with an `at` starts then even if the one before is still on (it
// cuts it). After the last event's end, with nothing queued, the voice goes back to its mv_free_t (or falls
// silent). The choir delays events with an `at` (and events starting a silent voice) by up to 35 ms per voice.
// A stop (P T K B D G CH JH) spends its length on the closure, then the burst and the aspiration at its end; it
// looks at the event queued after it to move to that vowel. PH_SIL rests (the voice falls silent for dur_s).
enum { MVF_GLIDE = 1, MVF_ACCENT = 2, MVF_HOLD = 4 };   // glide from the last pitch (over VOICE 2 > GLIDE, or a third
                                                        // of dur_s); a harder start; hold until the next is queued
typedef struct { uint32_t at; uint8_t ph, flags; float dur_s, note, level; } mv_ev_t;
bool  mv_push(int v, const mv_ev_t *e);          // UI task: queue it (32 per voice); false when full
void  mv_flush(int v);                           // UI task: drop what voice v has queued (it falls silent smoothly)
int   mv_queued(int v);

// What a voice does when nothing is queued: a free voice holds a vowel (the hands' jaw) at a note: a drone, a monk,
// the vowel player. The global controls shape every voice; the VOICE params (TRACT, BREATH, ROUGH ...) add to them.
typedef struct { bool on; float note, level; } mv_free_t;
typedef struct {
    mv_free_t v[MV_N];
    float jaw;                 // 0..1: OO OH AH EH EE, for free voices and the effect
    float hum;                 // 0..1: the mouth shut to M (OM's end) / NG: every voice, free or not
    float tract;               // -1..1: a shorter tract (higher formants, a child) .. longer (deeper, a giant)
    float breath;              // 0..1 aspiration (scaled by VOICE > BREATH: 50 % leaves it as it is)
    float vib_depth, vib_rate; // semitones (added to VOICE > VIBRATO), Hz (scaled by VIB RATE / 5.5)
    float rough;               // 0..1 jitter, shimmer, creak
    float sub;                 // 0..1 the undertone (kargyraa, the monks' octave below): period doubling
    float overtone;            // 0: off; else the harmonic (4..16, a fraction glides) the lead's whistle sits on
    float sharp;               // 0..1 how narrow and loud the whistle is (khoomei .. sygyt)
    float choir;               // 0..1 how far apart the voices are (cents, onset times)
    float c64;                 // 0..1 the 8-bit robot
    float bend;                // semitones on every voice (the stick)
    float level;               // 0..1 master of the voices
    float send;                // 0..1 into the echo and reverb
} mv_ctl_t;
void  mv_init(void);                             // boot, UI task
void  mv_set(const mv_ctl_t *c);                 // UI task; picked up at the next block, smoothed
void  mv_process(float *out, float *send, int n);   // audio: ADD every voice (dry, and the reverb send)

// The effect: the mic through the mouth: the jaw's vowel as a formant filter, the undertone and the whistle added,
// amount 0 = nothing. The mic's pitch is tracked (for HUM ALONG: the host makes a voice follow it): the tracking
// runs only while mv_effect is called, so call it every block in HUM ALONG even with amount 0 (it then only listens).
typedef struct { float amount, jaw, hum, tract, sub, overtone, sharp; } mv_fx_t;
void  mv_effect(const float *mic, float *out, float *send, int n, const mv_fx_t *p);   // audio: ADD
float mv_track(void);                            // the mic's pitch now (MIDI, 0: none), any task

// For the screen (UI task)
typedef struct {
    float level[MV_N], note[MV_N];
    int   ph[MV_N];            // the phoneme each voice is on (a free voice: the vowel nearest its jaw; PH_SIL silent)
    float mouth;               // 0 shut .. 1 wide: the lead's jaw now (for the Buddha's mouth)
    float round;               // 0..1 lips rounded (OO, OW, W)
    float f1, f2, f3;          // the lead's formants now, Hz
    float whistle;             // 0..1 how loud the overtone whistle is
    int   harmonic;            // the harmonic it sits on (0 none)
} mv_view_t;
void  mv_view(mv_view_t *v);
