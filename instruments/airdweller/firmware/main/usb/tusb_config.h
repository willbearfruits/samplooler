// TinyUSB configuration for the S3 build: MIDI + UAC2 (four IN endpoints on the S3, no room for CDC).
// The UAC part comes from espressif/usb_device_uac (CONFIG_USB_DEVICE_UAC_AS_PART).
#pragma once
#include "tusb_option.h"
#include "sdkconfig.h"

#define CFG_TUSB_RHPORT0_MODE     (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#define CFG_TUSB_OS               OPT_OS_FREERTOS
#define CFG_TUSB_OS_INC_PATH      freertos/
#ifndef ESP_PLATFORM
#define ESP_PLATFORM 1
#endif
#define CFG_TUD_ENABLED           1
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN        __attribute__((aligned(4)))
#define CFG_TUD_ENDPOINT0_SIZE    64
#define CFG_TUSB_DEBUG            0

#define CFG_TUD_CDC               0

#define CFG_TUD_MIDI              1
#define CFG_TUD_MIDI_RX_BUFSIZE   256
#define CFG_TUD_MIDI_TX_BUFSIZE   1024   // room for log lines as SysEx
#define CFG_TUD_MIDI_EP_BUFSIZE   64

#define CFG_TUD_MSC 0
#define CFG_TUD_HID 0
#define CFG_TUD_VENDOR 0

#include "uac_config.h"
#include "uac_descriptors.h"
#include "tusb_config_uac.h"
