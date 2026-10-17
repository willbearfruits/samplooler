// SNIFF's radio: the 2.4 GHz Wi-Fi in promiscuous mode, plus channel state information (CSI). Only what
// each frame looks like leaves this file: its kind, length, signal strength, the radio's noise floor, the
// channel, and a salted hash of the sender (a new salt every boot), so a device keeps one voice for a
// session while no address, network name or payload is ever kept or shown.
//
// Device: inst_radio.c (the Wi-Fi task's callbacks only push into lock-free queues). Host: tools/sim's
// radio_host.c makes up a room of devices, a person walking about, and a microwave oven.
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { PKT_MGMT, PKT_CTRL, PKT_DATA, PKT_BAD };     // PKT_BAD: the frame arrived broken (interference)
typedef struct {
    uint32_t who;        // salted hash of the sender; 0 when the frame names none (ACK, CTS) or is broken
    uint32_t frame;      // audio_frames() when it arrived (2 ms steps)
    uint16_t len;        // bytes on air
    int8_t   rssi;       // dBm
    int8_t   noise;      // the radio's noise floor, dBm (0: not known)
    uint8_t  chan;
    uint8_t  kind;       // PKT_*
    uint8_t  sub;        // 802.11 subtype (mgmt: 4 probe request, 5 probe response, 8 beacon)
    uint8_t  ofdm;       // 1: OFDM (11g/n), 0: DSSS (11b)
} radio_pkt_t;

#define CSI_BANDS 13     // the 52 data subcarriers of a 20 MHz channel, in groups of four
typedef struct {
    uint32_t who;        // salted hash of the sender
    int8_t   rssi;
    uint8_t  amp[CSI_BANDS];   // the channel's amplitude in each group, 0..255 (relative)
} radio_csi_t;

bool     radio_start(void);          // false if Wi-Fi would not start
void     radio_stop(void);
void     radio_set_channel(int ch);  // 1..13
void     radio_set_broken(bool on);  // also deliver frames that arrived broken (for STORM)
bool     radio_pkt(radio_pkt_t *p);  // the next frame heard (UI task); false: none waiting
bool     radio_csi(radio_csi_t *c);  // the next CSI frame (UI task)
void     radio_follow(uint32_t who); // CSI from this sender only; 0: from anyone
uint32_t radio_dropped(void);        // frames the queue had no room for
