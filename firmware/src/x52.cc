#include <cstdio>

#include "vendor_control.h"
#include "x52.h"

// X52 vendor protocol constants (from libx52/libx52/commands.h)
#define X52_VENDOR_REQUEST 0x91

// MFD Text commands
#define X52_MFD_LINE1 0xd1
#define X52_MFD_LINE2 0xd2
#define X52_MFD_LINE3 0xd4
#define X52_MFD_CLEAR_LINE 0x08
#define X52_MFD_WRITE_LINE 0x00

// Brightness commands
#define X52_MFD_BRIGHTNESS 0xb1
#define X52_LED_BRIGHTNESS 0xb2

// LED set command
#define X52_LED 0xb8

// Time commands
#define X52_TIME_CLOCK1 0xc0
#define X52_OFFS_CLOCK2 0xc1
#define X52_OFFS_CLOCK3 0xc2

// Date commands
#define X52_DATE_DDMM 0xc4
#define X52_DATE_YEAR  0xc8

// Shift indicator on MFD
#define X52_SHIFT_INDICATOR 0xfd
#define X52_SHIFT_ON  0x51
#define X52_SHIFT_OFF 0x50

// Blink throttle & POV LED
#define X52_BLINK_INDICATOR 0xb4
#define X52_BLINK_ON  0x51
#define X52_BLINK_OFF 0x50

bool x52_vendor_command(uint8_t dev_addr, uint16_t index, uint16_t value) {
    printf("x52_vendor_command: dev_addr=%u, index=0x%04x, value=0x%04x\n", dev_addr, index, value);
    return queue_vendor_control_transfer(
        dev_addr,
        X52_VENDOR_REQUEST,  // bRequest = 0x91
        value,               // wValue
        index,               // wIndex
        NULL,                // no data stage
        0,                   // no data
        1000                 // timeout
    );
}

bool x52_set_mfd_text(uint8_t dev_addr, uint8_t line, const char* text, uint8_t length) {
    if (!text || line > 2) {
        printf("x52_set_mfd_text: invalid args (text=%p, line=%u)\n", text, line);
        return false;
    }

    if (length > 16) {
        length = 16;
    }

    printf("x52_set_mfd_text: dev_addr=%u, line=%u, text='%s', length=%u\n", dev_addr, line, text, length);

    const uint16_t line_map[3] = { X52_MFD_LINE1, X52_MFD_LINE2, X52_MFD_LINE3 };
    uint16_t line_index = line_map[line];

    // 1. Clear the line first
    x52_vendor_command(dev_addr, line_index | X52_MFD_CLEAR_LINE, 0);

    // 2. Pad text with spaces to 16 chars
    uint8_t padded[16];
    for (int i = 0; i < 16; i++) {
        padded[i] = (i < length) ? text[i] : ' ';
    }

    // 3. Write text in 2-character chunks
    for (int i = 0; i < 16; i += 2) {
        uint16_t value = (padded[i + 1] << 8) | padded[i];
        x52_vendor_command(dev_addr, line_index | X52_MFD_WRITE_LINE, value);
    }

    return true;
}

bool x52_set_brightness(uint8_t dev_addr, bool mfd, uint16_t brightness) {
    uint16_t index = mfd ? X52_MFD_BRIGHTNESS : X52_LED_BRIGHTNESS;
    return x52_vendor_command(dev_addr, index, brightness);
}

bool x52_set_shift(uint8_t dev_addr, bool on) {
    return x52_vendor_command(dev_addr, X52_SHIFT_INDICATOR, on ? X52_SHIFT_ON : X52_SHIFT_OFF);
}

bool x52_set_led(uint8_t dev_addr, uint8_t led_bit, bool on) {
    uint16_t value = (on ? 1 : 0) | (led_bit << 8);
    return x52_vendor_command(dev_addr, X52_LED, value);
}

bool x52_set_blink(uint8_t dev_addr, bool on) {
    return x52_vendor_command(dev_addr, X52_BLINK_INDICATOR, on ? X52_BLINK_ON : X52_BLINK_OFF);
}

bool x52_set_date(uint8_t dev_addr, uint8_t day, uint8_t month, uint8_t year, x52_date_format format) {
    uint16_t value1, value2;

    switch (format) {
    case X52_DATE_FORMAT_YYMMDD:
        value1 = (month << 8) | year;
        value2 = day;
        break;
    case X52_DATE_FORMAT_MMDDYY:
        value1 = (day << 8) | month;
        value2 = year;
        break;
    case X52_DATE_FORMAT_DDMMYY:
    default:
        value1 = (month << 8) | day;
        value2 = year;
        break;
    }

    x52_vendor_command(dev_addr, X52_DATE_DDMM, value1);
    x52_vendor_command(dev_addr, X52_DATE_YEAR, value2);
    return true;
}

bool x52_set_time(uint8_t dev_addr, uint8_t hour, uint8_t minute, bool h24) {
    uint16_t value = ((h24 ? 1 : 0) << 15) | ((hour & 0x7F) << 8) | (minute & 0xFF);
    return x52_vendor_command(dev_addr, X52_TIME_CLOCK1, value);
}

bool x52_set_clock_offset(uint8_t dev_addr, uint8_t clock, int16_t offset_minutes, bool h24) {
    if (clock < 2 || clock > 3) return false;

    uint16_t index = (clock == 2) ? X52_OFFS_CLOCK2 : X52_OFFS_CLOCK3;
    uint16_t negative = 0;
    int offset = offset_minutes;

    if (offset < 0) {
        negative = 1;
        offset = -offset;
    }

    // Clamp to 10-bit range
    if (offset > 1023) offset = 1023;

    uint16_t value = ((h24 ? 1 : 0) << 15) | (negative << 10) | (offset & 0x3FF);
    return x52_vendor_command(dev_addr, index, value);
}
