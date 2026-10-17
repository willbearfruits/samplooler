// USB on the S3: class compliant MIDI and a UAC2 sound card at the engine's sample rate.
// Logs go out as SysEx (F0 7D 4C <text> F7). In: F0 7D 52 F7 ('R') reboots into the ROM download mode
// so `tools/flash.sh` can reflash without touching BOOT; F0 7D 53 F7 ('S') logs the status and crash report.
#pragma once
#include <stdbool.h>
#include <stdint.h>

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
