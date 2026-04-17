#ifndef _X52_H_
#define _X52_H_

#include <stdint.h>

// Device identification
#define X52_VENDOR_ID 0x06A3
#define X52_PRODUCT_ID_V1 0x0255      // X52 revision 1
#define X52_PRODUCT_ID_V2 0x075C      // X52 revision 2

// LED identifiers (bit positions used in vendor command value field)
#define X52_LED_FIRE      1
#define X52_LED_A_RED     2
#define X52_LED_A_GREEN   3
#define X52_LED_B_RED     4
#define X52_LED_B_GREEN   5
#define X52_LED_D_RED     6
#define X52_LED_D_GREEN   7
#define X52_LED_E_RED     8
#define X52_LED_E_GREEN   9
#define X52_LED_T1_RED    10
#define X52_LED_T1_GREEN  11
#define X52_LED_T2_RED    12
#define X52_LED_T2_GREEN  13
#define X52_LED_T3_RED    14
#define X52_LED_T3_GREEN  15
#define X52_LED_POV_RED   16
#define X52_LED_POV_GREEN 17
#define X52_LED_I_RED     18
#define X52_LED_I_GREEN   19
#define X52_LED_THROTTLE  20

// Date formats
enum x52_date_format {
    X52_DATE_FORMAT_DDMMYY,
    X52_DATE_FORMAT_MMDDYY,
    X52_DATE_FORMAT_YYMMDD,
};

// Send a raw X52 vendor command (index + value)
bool x52_vendor_command(uint8_t dev_addr, uint16_t index, uint16_t value);

// Write text to an MFD line (0-2), max 16 chars
bool x52_set_mfd_text(uint8_t dev_addr, uint8_t line, const char* text, uint8_t length);

// Set MFD (mfd=true) or LED (mfd=false) brightness (0-128)
bool x52_set_brightness(uint8_t dev_addr, bool mfd, uint16_t brightness);

// Set shift indicator on/off
bool x52_set_shift(uint8_t dev_addr, bool on);

// Set individual LED on/off by bit id (X52_LED_FIRE, X52_LED_A_RED, etc.)
bool x52_set_led(uint8_t dev_addr, uint8_t led_bit, bool on);

// Set POV/throttle blink on/off
bool x52_set_blink(uint8_t dev_addr, bool on);

// Set date on MFD
bool x52_set_date(uint8_t dev_addr, uint8_t day, uint8_t month, uint8_t year, x52_date_format format);

// Set primary clock time on MFD
bool x52_set_time(uint8_t dev_addr, uint8_t hour, uint8_t minute, bool h24);

// Set secondary clock offset (clock 2 or 3, offset in minutes from clock 1)
bool x52_set_clock_offset(uint8_t dev_addr, uint8_t clock, int16_t offset_minutes, bool h24);

// Head tracker MFD display
// Update rate-limited display showing X axis bar graph, or PAUSED state
void x52_update_ht_display(uint8_t dev_addr, int16_t headX, bool paused);

// Initialize MFD clocks: clock 1 = 00:00
void x52_init_clocks(uint8_t dev_addr);

#endif
