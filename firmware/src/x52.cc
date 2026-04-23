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

// X52 device state (managed by x52_on_mount/unmount)
static uint8_t x52_dev_addr = 0;
static uint8_t last_mfd_brightness = 0xFF;
static bool x52_btn_prev = false;
static bool x52_btn_handled = false;
static absolute_time_t x52_long_press_timeout;

// Shift mode state
static bool shift_active = false;
static bool shift_btn_prev = false;
static uint8_t shift_scale = SHIFT_SCALE_NORMAL;

// MFD page state
static uint8_t mfd_page = MFD_PAGE_SHIFT;
static uint8_t scroll_prev = 0;

// Last known HT display values (for redraw on page switch)
static int16_t last_ht_x = 0;
static bool last_ht_paused = false;

// Device connection state for MFD pages
static bool ht_connected = false;
static bool g923_connected = false;
static bool attack3_connected = false;

// Cached G923 display values
static uint16_t last_g923_range = 360;
static uint8_t last_g923_spring = 50;
static uint8_t last_g923_sensitivity = 30;

// Cached Attack 3 display values
static uint8_t last_attack3_z = 0;
static uint16_t last_attack3_scale_pct = 125;

bool x52_vendor_command(uint8_t dev_addr, uint16_t index, uint16_t value) {
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

static absolute_time_t ht_mfd_next_update = {0};
static char mfd_cache[3][17] = { "", "", "" }; // Cached line contents

static void mfd_set_line_cached(uint8_t dev_addr, uint8_t line, const char* text) {
    if (memcmp(mfd_cache[line], text, 16) == 0) return; // No change, skip
    memcpy(mfd_cache[line], text, 16);
    mfd_cache[line][16] = '\0';
    x52_set_mfd_text(dev_addr, line, text, 16);
}

static void build_mfd_bar(char* buf, int16_t value, int16_t range) {
    int pos = (int)((((float)value / range) + 1.0f) * 7.5f + 0.5f);
    if (pos < 0) pos = 0;
    if (pos > 15) pos = 15;
    static const char blank_bar[] = "--------+-------";
    memcpy(buf, blank_bar, 16);
    buf[pos] = 'X'; // value position (overwrites center if at 0)
}

void x52_update_ht_display(uint8_t dev_addr, int16_t headX, bool paused, bool force) {
    // Cache latest values for redraw on page switch
    last_ht_x = headX;
    last_ht_paused = paused;

    if (dev_addr == 0 || mfd_page != MFD_PAGE_HT) return;

    if (!force && !time_reached(ht_mfd_next_update)) return;
    ht_mfd_next_update = make_timeout_time_ms(X52_HT_MFD_UPDATE_MS);

    if (!ht_connected) {
        mfd_set_line_cached(dev_addr, 0, "                ");
        mfd_set_line_cached(dev_addr, 1, "   Connect HT   ");
        mfd_set_line_cached(dev_addr, 2, "                ");
        return;
    }

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

static absolute_time_t x52_mount_time = {0};
static uint8_t last_clock_minute = 0xFF;

void x52_init_clocks(uint8_t dev_addr) {
    x52_mount_time = get_absolute_time();
    last_clock_minute = 0xFF;
    x52_set_time(dev_addr, 0, 0, true);
}

void x52_update_clock() {
    if (x52_dev_addr == 0) return;

    uint32_t elapsed_s = absolute_time_diff_us(x52_mount_time, get_absolute_time()) / 1000000;
    uint8_t minutes = (elapsed_s / 60) % 60;
    uint8_t hours = (elapsed_s / 3600) % 24;

    // Only send vendor command when the minute changes
    if (minutes != last_clock_minute) {
        last_clock_minute = minutes;
        x52_set_time(x52_dev_addr, hours, minutes, true);
    }
}

// --- Device Lifecycle ---

void x52_on_mount(uint8_t dev_addr, uint16_t vid, uint16_t pid) {
    if (vid != X52_VENDOR_ID) return;
    if (pid != X52_PRODUCT_ID_V1 && pid != X52_PRODUCT_ID_V2) return;

    x52_dev_addr = dev_addr;
    last_mfd_brightness = 0xFF;
    x52_init_clocks(dev_addr);
#if CFG_TUD_CDC
    printf("X52 detected (PID: 0x%04X)\n", pid);
#endif
}

void x52_on_unmount(uint8_t dev_addr) {
    if (dev_addr == x52_dev_addr) {
        x52_dev_addr = 0;
        x52_btn_prev = false;
        shift_active = false;
        shift_btn_prev = false;
        shift_scale = SHIFT_SCALE_NORMAL;
        mfd_page = MFD_PAGE_SHIFT;
        scroll_prev = 0;
    }
}

uint8_t x52_get_dev_addr() {
    return x52_dev_addr;
}

// --- Shift Mode ---

void x52_apply_shift(uint8_t* report, uint16_t len) {
    if (x52_dev_addr == 0 || len <= X52_BTN_BASE) return;

    // Detect E button rising edge — toggle shift
    bool btn_now = (report[X52_BTN_BYTE(X52_BTN_E)] & X52_BTN_MASK(X52_BTN_E)) != 0;
    if (btn_now && !shift_btn_prev) {
        shift_active = !shift_active;
        x52_set_shift(x52_dev_addr, shift_active);
    }
    shift_btn_prev = btn_now;

    // Ramp scale toward target
    uint8_t target = shift_active ? SHIFT_SCALE_LOW : SHIFT_SCALE_NORMAL;
    if (shift_scale < target) {
        shift_scale = (target - shift_scale > SHIFT_RAMP_STEP)
            ? shift_scale + SHIFT_RAMP_STEP : target;
    } else if (shift_scale > target) {
        shift_scale = (shift_scale - target > SHIFT_RAMP_STEP)
            ? shift_scale - SHIFT_RAMP_STEP : target;
    }

    // Scale throttle in place
    if (len > X52_THROTTLE_BYTE) {
        report[X52_THROTTLE_BYTE] =
            (uint8_t)((report[X52_THROTTLE_BYTE] * shift_scale) / 255);
    }
}

bool x52_is_shifted() {
    return shift_active;
}

// --- MFD Paging ---

static void x52_update_shift_display(bool force) {
    if (x52_dev_addr == 0 || mfd_page != MFD_PAGE_SHIFT) return;
    if (!force && !time_reached(ht_mfd_next_update)) return;
    ht_mfd_next_update = make_timeout_time_ms(X52_HT_MFD_UPDATE_MS);

    mfd_set_line_cached(x52_dev_addr, 0, "   Shift Mode   ");

    char line1[17];
    snprintf(line1, 17, "  State: %-7s", shift_active ? "ON" : "OFF");
    mfd_set_line_cached(x52_dev_addr, 1, line1);

    char line2[17];
    snprintf(line2, 17, " Throttle: %3u%% ", (unsigned)(shift_scale * 100 / 255));
    mfd_set_line_cached(x52_dev_addr, 2, line2);
}

static void x52_update_g923_page(bool force) {
    if (x52_dev_addr == 0 || mfd_page != MFD_PAGE_G923) return;
    if (!force && !time_reached(ht_mfd_next_update)) return;
    ht_mfd_next_update = make_timeout_time_ms(X52_HT_MFD_UPDATE_MS);

    if (!g923_connected) {
        mfd_set_line_cached(x52_dev_addr, 0, "                ");
        mfd_set_line_cached(x52_dev_addr, 1, "  Connect G923  ");
        mfd_set_line_cached(x52_dev_addr, 2, "                ");
        return;
    }

    char line0[17];
    snprintf(line0, 17, " G923  Rng:%3u%c ", last_g923_range, 0x7F);  // degree symbol approximation
    mfd_set_line_cached(x52_dev_addr, 0, line0);

    char line1[17];
    snprintf(line1, 17, " Spring:  %3u%%  ", last_g923_spring);
    mfd_set_line_cached(x52_dev_addr, 1, line1);

    char line2[17];
    snprintf(line2, 17, " Sensit:  %3u%%  ", last_g923_sensitivity);
    mfd_set_line_cached(x52_dev_addr, 2, line2);
}

static void x52_update_attack3_page(bool force) {
    if (x52_dev_addr == 0 || mfd_page != MFD_PAGE_ATTACK3) return;
    if (!force && !time_reached(ht_mfd_next_update)) return;
    ht_mfd_next_update = make_timeout_time_ms(X52_HT_MFD_UPDATE_MS);

    if (!attack3_connected) {
        mfd_set_line_cached(x52_dev_addr, 0, "                ");
        mfd_set_line_cached(x52_dev_addr, 1, " Connect Attk 3 ");
        mfd_set_line_cached(x52_dev_addr, 2, "                ");
        return;
    }

    mfd_set_line_cached(x52_dev_addr, 0, " Attack3 Z-Axis ");

    char bar[17];
    // Z 0=released (scale 125%), 255=pressed (scale 25%), show as bar
    build_mfd_bar(bar, (int16_t)(255 - last_attack3_z) - 128, 128);
    bar[16] = '\0';
    mfd_set_line_cached(x52_dev_addr, 1, bar);

    char line2[17];
    snprintf(line2, 17, "  Scale: %3u%%   ", last_attack3_scale_pct);
    mfd_set_line_cached(x52_dev_addr, 2, line2);
}

static void x52_update_reserved_page(bool force) {
    if (x52_dev_addr == 0 || mfd_page != MFD_PAGE_RESERVED) return;
    if (!force && !time_reached(ht_mfd_next_update)) return;
    ht_mfd_next_update = make_timeout_time_ms(X52_HT_MFD_UPDATE_MS);

    mfd_set_line_cached(x52_dev_addr, 0, "                ");
    mfd_set_line_cached(x52_dev_addr, 1, "    Reserved    ");
    mfd_set_line_cached(x52_dev_addr, 2, "                ");
}

static void x52_refresh_current_page() {
    switch (mfd_page) {
        case MFD_PAGE_SHIFT:   x52_update_shift_display(true); break;
        case MFD_PAGE_HT:     x52_update_ht_display(x52_dev_addr, last_ht_x, last_ht_paused, true); break;
        case MFD_PAGE_G923:    x52_update_g923_page(true); break;
        case MFD_PAGE_ATTACK3: x52_update_attack3_page(true); break;
        case MFD_PAGE_RESERVED: x52_update_reserved_page(true); break;
    }
}

// --- Device connection tracking ---

void x52_set_ht_connected(bool connected) {
    ht_connected = connected;
}

void x52_set_g923_connected(bool connected) {
    g923_connected = connected;
}

void x52_set_attack3_connected(bool connected) {
    attack3_connected = connected;
}

void x52_update_g923_display(uint16_t range, uint8_t spring_pct, uint8_t sensitivity) {
    last_g923_range = range;
    last_g923_spring = spring_pct;
    last_g923_sensitivity = sensitivity;
    if (mfd_page == MFD_PAGE_G923) {
        x52_update_g923_page(false);
    }
}

void x52_update_attack3_display(uint8_t z_val, uint16_t scale_pct) {
    last_attack3_z = z_val;
    last_attack3_scale_pct = scale_pct;
    if (mfd_page == MFD_PAGE_ATTACK3) {
        x52_update_attack3_page(false);
    }
}

uint8_t x52_get_mfd_page() {
    return mfd_page;
}

// --- X52 Report Processing ---

x52_ht_action x52_process_report(const uint8_t* report, uint16_t len, bool ht_is_connected) {
    // MFD brightness wheel (byte 7, 0-255 -> 0-128)
    if (len > X52_BRIGHTNESS_BYTE) {
        uint8_t brightness = report[X52_BRIGHTNESS_BYTE] >> 1;
        if (brightness != last_mfd_brightness) {
            last_mfd_brightness = brightness;
            x52_set_brightness(x52_dev_addr, true, brightness);
        }
    }

    // Scroll wheel page cycling (byte 12, buttons 33/34)
    if (len > X52_SCROLL_BYTE) {
        uint8_t scroll = report[X52_SCROLL_BYTE] & X52_SCROLL_MASK;
        if (scroll != 0 && scroll_prev == 0) {
            if (scroll == X52_SCROLL_DOWN) {
                mfd_page = (mfd_page + 1) % MFD_NUM_PAGES;
            } else {
                mfd_page = (mfd_page + MFD_NUM_PAGES - 1) % MFD_NUM_PAGES;
            }
            memset(mfd_cache, 0, sizeof(mfd_cache));
            ht_mfd_next_update = get_absolute_time();

            // Force immediate display update for the new page
            x52_refresh_current_page();
        }
        scroll_prev = scroll;
    }

    // Update current page display
    switch (mfd_page) {
        case MFD_PAGE_SHIFT:    x52_update_shift_display(false); break;
        case MFD_PAGE_HT:       break; // Updated from report callback
        case MFD_PAGE_G923:     x52_update_g923_page(false); break;
        case MFD_PAGE_ATTACK3:  x52_update_attack3_page(false); break;
        case MFD_PAGE_RESERVED: x52_update_reserved_page(false); break;
    }

    if (!ht_is_connected || len <= X52_BTN_BASE) return X52_HT_NONE;

    bool btn_now = (report[X52_BTN_BYTE(X52_BTN_D)] & X52_BTN_MASK(X52_BTN_D)) != 0;
    x52_ht_action action = X52_HT_NONE;

    if (btn_now && !x52_btn_prev) {
        // Button just pressed — start timer
        x52_long_press_timeout = make_timeout_time_ms(LONG_PRESS_MS);
        x52_btn_handled = false;
    }
    else if (btn_now && !x52_btn_handled) {
        // Button held — check for long press
        if (time_reached(x52_long_press_timeout)) {
            x52_btn_handled = true;
            action = X52_HT_PAUSE;
        }
    }
    else if (!btn_now && x52_btn_prev) {
        // Button released — short press if not already handled
        if (!x52_btn_handled) {
            action = X52_HT_RESET;
        }
    }
    x52_btn_prev = btn_now;

    return action;
}

x52_ht_action x52_check_long_press(bool ht_is_connected) {
    if (!ht_is_connected || !x52_btn_prev || x52_btn_handled) return X52_HT_NONE;
    if (time_reached(x52_long_press_timeout)) {
        x52_btn_handled = true;
        return X52_HT_PAUSE;
    }
    return X52_HT_NONE;
}
