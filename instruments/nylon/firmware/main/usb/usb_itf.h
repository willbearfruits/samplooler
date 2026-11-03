// The S3 has EP0 plus four IN endpoints. MIDI takes one, the audio path takes two
// (feedback and capture), so there is no room for a CDC console: logs and the
// reboot-to-flash command travel over MIDI SysEx instead (see usb.c).
#pragma once
enum { ITF_MIDI = 0, ITF_MIDI_STREAM, ITF_AUDIO_CONTROL, ITF_AUDIO_SPK, ITF_AUDIO_MIC, ITF_TOTAL };
#define EP_MIDI_OUT   0x01
#define EP_MIDI_IN    0x81
#define EP_AUDIO_OUT  0x02
#define EP_AUDIO_FB   0x82
#define EP_AUDIO_IN   0x83
