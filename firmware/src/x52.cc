#include <cstdio>

#include <tusb.h>
#include "pico/time.h"

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
#if CFG_TUD_CDC
    printf("x52_vendor_command: dev_addr=%u, index=0x%04x, value=0x%04x\n", dev_addr, index, value);
#endif
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
#if CFG_TUD_CDC
        printf("x52_set_mfd_text: invalid args (text=%p, line=%u)\n", text, line);
#endif
        return false;
    }

    if (length > 16) {
        length = 16;
    }

#if CFG_TUD_CDC
    printf("x52_set_mfd_text: dev_addr=%u, line=%u, text='%s', length=%u\n", dev_addr, line, text, length);
#endif

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

// --- Head Tracker MFD Display ---

#define X52_HT_MFD_UPDATE_MS 100

static uint32_t ht_mfd_last_update = 0;
static char mfd_cache[3][17] = { "", "", "" }; // Cached line contents

static void mfd_set_line_cached(uint8_t dev_addr, uint8_t line, const char* text) {
    if (memcmp(mfd_cache[line], text, 16) == 0) return; // No change, skip
    memcpy(mfd_cache[line], text, 16);
    mfd_cache[line][16] = '\0';
    x52_set_mfd_text(dev_addr, line, text, 16);
}

static void build_mfd_bar(char* buf, int16_t value, int16_t range) {

    float ratio = (float)value / (float)range;
    float scaled = (ratio + 1.0f) * 7.5f;
    int pos_1 = (int)(scaled + 0.5f);

    // DEBUG PRINT: This tells us if the math is actually reaching 0 or 15
    printf("build_mfd_bar: value=%d, ratio=%.2f, scaled=%.2f, pos=%d\n", value, ratio, scaled, pos_1);

    int pos = (int)((((float)value / range) + 1.0f) * 7.5f + 0.5f);
    if (pos < 0) pos = 0;
    if (pos > 15) pos = 15;
    static const char blank_bar[] = "--------+-------";
    memcpy(buf, blank_bar, 16);
    buf[pos] = 'X'; // value position (overwrites center if at 0)
}

void x52_update_ht_display(uint8_t dev_addr, int16_t headX, bool paused) {
    if (dev_addr == 0) return;

    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - ht_mfd_last_update < X52_HT_MFD_UPDATE_MS) return;
    ht_mfd_last_update = now;

#if CFG_TUD_CDC
    printf("x52_update_ht_display: dev_addr=%u, headX=%d, paused=%s\n", dev_addr, headX, paused ? "true" : "false");
#endif

    if (paused) {
        mfd_set_line_cached(dev_addr, 0, "  Head Tracker  ");
        mfd_set_line_cached(dev_addr, 1, "  -- PAUSED --  ");
        mfd_set_line_cached(dev_addr, 2, "  Long-Press D  ");
        return;
    }

    mfd_set_line_cached(dev_addr, 0, " Head Tracker X ");

    char bar[17];
    build_mfd_bar(bar, headX, 512);
    bar[16] = '\0';
    mfd_set_line_cached(dev_addr, 1, bar);

    char val[17];
    snprintf(val, 17, "     %4d       ", headX);
    mfd_set_line_cached(dev_addr, 2, val);
}

// --- Uptime Clock ---

void x52_init_clocks(uint8_t dev_addr) {
    x52_set_time(dev_addr, 0, 0, true);
}
