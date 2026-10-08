// Composite: MIDI + UAC2 speaker/mic. (No CDC: the S3 has only four IN endpoints.)
#include <string.h>
#include "tusb.h"
#include "uac_descriptors.h"
#include "usb_itf.h"

static const tusb_desc_device_t dev_desc = {
    .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE, .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC, .bDeviceSubClass = MISC_SUBCLASS_COMMON, .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE, .idVendor = 0x303A, .idProduct = 0x4020, .bcdDevice = 0x0100,
    .iManufacturer = 1, .iProduct = 2, .iSerialNumber = 3, .bNumConfigurations = 1,
};
uint8_t const *tud_descriptor_device_cb(void) { return (uint8_t const *)&dev_desc; }

#define CFG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN + TUD_AUDIO_DEVICE_DESC_LEN)
static const uint8_t cfg_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_TOTAL, 0, CFG_TOTAL_LEN, 0, 250),
    TUD_MIDI_DESCRIPTOR(ITF_MIDI, 4, EP_MIDI_OUT, EP_MIDI_IN, 64),
    TUD_AUDIO_DESCRIPTOR(ITF_AUDIO_CONTROL, 5, EP_AUDIO_OUT, EP_AUDIO_IN, EP_AUDIO_FB),
};
uint8_t const *tud_descriptor_configuration_cb(uint8_t index) { (void)index; return cfg_desc; }

char usb_serial[16] = "SL-000000";    // set from the chip's MAC in usb_start()
static const char *strings[] = {
    (const char[]){ 0x09, 0x04 },
    "willbearfruits",            // 1
    "Samplooler",                // 2
    usb_serial,                  // 3
    "Samplooler MIDI",           // 4
    "Samplooler audio",          // 5
    "Samplooler in",             // 6 speaker (host -> device)
    "Samplooler out",            // 7 mic (device -> host)
};
static uint16_t desc_str[32];
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    uint8_t n;
    if (index == 0) { memcpy(&desc_str[1], strings[0], 2); n = 1; }
    else {
        if (index >= sizeof strings / sizeof strings[0]) return NULL;
        const char *s = strings[index];
        n = (uint8_t)strlen(s); if (n > 31) n = 31;
        for (uint8_t i = 0; i < n; i++) desc_str[1 + i] = s[i];
    }
    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * n + 2));
    return desc_str;
}
