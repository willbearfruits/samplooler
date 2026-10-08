// USB on the S3: class compliant MIDI and a UAC2 sound card at the engine's sample rate.
// Logs go out as SysEx (F0 7D 4C <text> F7). In: F0 7D 52 F7 ('R') reboots into the ROM download mode
// so `tools/flash.sh` can reflash without touching BOOT; F0 7D 53 F7 ('S') logs the status and crash report.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pins.h"

#if HAS_USB
bool usb_start(void);                        // takes the USB port away from the Serial/JTAG console
bool usb_connected(void);

// MIDI, UI task
void usb_midi_send_cc(uint8_t cc, uint8_t val);
void usb_midi_send_note(uint8_t note, uint8_t vel, bool on);
typedef void (*usb_midi_handler_t)(uint8_t status, uint8_t d1, uint8_t d2);
void usb_midi_poll(usb_midi_handler_t h);    // drains incoming messages

// Audio rings, audio task side. Mono frames; the host sees both channels.
void usb_audio_out_push(const int16_t *mono, int n);   // what the host records
int  usb_audio_in_pull(int16_t *mono, int n);          // what the host plays; returns frames delivered (zeros pad)
bool usb_audio_in_active(void);
#else
static inline bool usb_start(void) { return false; }
static inline bool usb_connected(void) { return false; }
static inline void usb_midi_send_cc(uint8_t c, uint8_t v) { (void)c; (void)v; }
static inline void usb_midi_send_note(uint8_t n, uint8_t v, bool o) { (void)n; (void)v; (void)o; }
typedef void (*usb_midi_handler_t)(uint8_t, uint8_t, uint8_t);
static inline void usb_midi_poll(usb_midi_handler_t h) { (void)h; }
static inline void usb_audio_out_push(const int16_t *m, int n) { (void)m; (void)n; }
static inline int  usb_audio_in_pull(int16_t *m, int n) { (void)m; (void)n; return 0; }
static inline bool usb_audio_in_active(void) { return false; }
#endif
