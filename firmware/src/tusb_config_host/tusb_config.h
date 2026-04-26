#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

// Native USB port (RHPORT0) configured as DEVICE for CDC debug output to PC
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))

// Enable TinyUSB debug logging (level 2 = info + warnings)
#define CFG_TUSB_DEBUG 3

// Redirect debug logging to custom CDC function
#define CFG_TUSB_DEBUG_PRINTF cdc_debug_printf

// Device-side configuration (for CDC debug console on USB-C to PC)
#define CFG_TUD_ENDPOINT0_SIZE 64
#define CFG_TUD_CDC 1
#define CFG_TUD_HID 0
#define CFG_TUD_MSC 0
#define CFG_TUD_MIDI 0
#define CFG_TUD_VENDOR 0

#define CFG_TUD_CDC_RX_BUFSIZE 64
#define CFG_TUD_CDC_TX_BUFSIZE 64

// Host-side configuration (for GPIO PIO-USB on USB-A for X52)
#define CFG_TUH_ENUMERATION_BUFSIZE 512

#define CFG_TUH_HUB 1
#define CFG_TUH_CDC 0
#define CFG_TUH_HID 16
#define CFG_TUH_MSC 0
#define CFG_TUH_VENDOR 0

#define CFG_TUH_DEVICE_MAX 16

#define CFG_TUH_HID_EPIN_BUFSIZE 64
#define CFG_TUH_HID_EPOUT_BUFSIZE 64

// Enable PIO-USB for host on GPIO 16/17
#define CFG_TUH_RPI_PIO_USB 1

#endif
