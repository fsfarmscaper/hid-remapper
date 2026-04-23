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

// --- X52 HID Report Layout (non-Pro, 14 bytes) ---
// Byte offsets after report ID stripped by TinyUSB
// Per libx52io: axes 0-2 and hat are on the joystick grip (not present on throttle-only)
//
// Byte  Bits     Usage                       Range      Location
// 0-1   0-10     Stick X   (0x0001:0030)      0-2047     Joystick
// 1-2   11-21    Stick Y   (0x0001:0031)      0-2047     Joystick
// 2-3   22-31    Stick Rz  (0x0001:0035)      0-1023     Joystick (twist)
// 4     32-39    Throttle  (0x0001:0032)       0-255      Throttle base
// 5     40-47    Rotary X  (0x0001:0033)       0-255      Throttle base (clutch rotary)
// 6     48-55    Rotary Y  (0x0001:0034)       0-255      Throttle base (E rotary)
// 7     56-63    Slider    (0x0001:0036)       0-255      Throttle base (MFD brightness)
// 8-12  64-97    Buttons 1-34                  0/1        Mixed (see below)
// 12    98-99    Padding
// 12    100-103  Hat switch (0x0001:0039)       1-8        Joystick (8-way POV)
// 13    104-107  Mouse X   (0x0005:0024)       0-15       Throttle base (thumbstick)
// 13    108-111  Mouse Y   (0x0005:0026)       0-15       Throttle base (thumbstick)

#define X52_REPORT_LEN        14

// Axis byte offsets
#define X52_THROTTLE_BYTE     4
#define X52_ROTARY_X_BYTE     5
#define X52_ROTARY_Y_BYTE     6
#define X52_SLIDER_BYTE       7
#define X52_BRIGHTNESS_BYTE   7    // alias: slider is the MFD brightness wheel

// Buttons 1-34 span bytes 8-12
// Compute byte offset and bit mask from HID button number (1-34)
#define X52_BTN_BASE          8
#define X52_BTN_BYTE(n)       (X52_BTN_BASE + ((n) - 1) / 8)
#define X52_BTN_MASK(n)       (1 << (((n) - 1) % 8))

// HID button numbers (all 34 from descriptor, per libx52io parser)
// Buttons on the joystick grip will read 0 when grip is not connected
//
// --- Joystick Grip (not present on throttle-only setup) ---
#define X52_BTN_TRIGGER       1     // Stick: primary trigger
#define X52_BTN_FIRE          2     // Stick: fire/launch
#define X52_BTN_A             3     // Stick: A button
#define X52_BTN_B             4     // Stick: B button
#define X52_BTN_C             5     // Stick: C button
#define X52_BTN_PINKY         6     // Stick: pinky/shift trigger
#define X52_BTN_T1_UP         9     // Stick: toggle 1 up
#define X52_BTN_T1_DN         10    // Stick: toggle 1 down
#define X52_BTN_T2_UP         11    // Stick: toggle 2 up
#define X52_BTN_T2_DN         12    // Stick: toggle 2 down
#define X52_BTN_T3_UP         13    // Stick: toggle 3 up
#define X52_BTN_T3_DN         14    // Stick: toggle 3 down
#define X52_BTN_TRIGGER_2     15    // Stick: secondary trigger (stage 2)
#define X52_BTN_POV_1_N       16    // Stick: 4-way POV north
#define X52_BTN_POV_1_E       17    // Stick: 4-way POV east
#define X52_BTN_POV_1_S       18    // Stick: 4-way POV south
#define X52_BTN_POV_1_W       19    // Stick: 4-way POV west
#define X52_BTN_MODE_1        24    // Stick: mode selector position 1
#define X52_BTN_MODE_2        25    // Stick: mode selector position 2
#define X52_BTN_MODE_3        26    // Stick: mode selector position 3
//
// --- Throttle Base ---
#define X52_BTN_D             7     // Throttle: D button
#define X52_BTN_E             8     // Throttle: E button
#define X52_BTN_POV_2_N       20    // Throttle: 4-way POV north (up)
#define X52_BTN_POV_2_E       21    // Throttle: 4-way POV east (right)
#define X52_BTN_POV_2_S       22    // Throttle: 4-way POV south (down)
#define X52_BTN_POV_2_W       23    // Throttle: 4-way POV west (left)
#define X52_BTN_FUNCTION      27    // Throttle: MFD function button
#define X52_BTN_START_STOP    28    // Throttle: MFD start/stop button
#define X52_BTN_RESET         29    // Throttle: MFD reset button
#define X52_BTN_CLUTCH        30    // Throttle: clutch (i) button
#define X52_BTN_MOUSE_PRIMARY 31    // Throttle: mouse primary click
#define X52_BTN_MOUSE_SECONDARY 32  // Throttle: mouse secondary (scroll press)
#define X52_BTN_SCROLL_DN     33    // Throttle: MFD scroll wheel down
#define X52_BTN_SCROLL_UP     34    // Throttle: MFD scroll wheel up

// Scroll wheel convenience
#define X52_SCROLL_BYTE       X52_BTN_BYTE(X52_BTN_SCROLL_DN)
#define X52_SCROLL_MASK       (X52_BTN_MASK(X52_BTN_SCROLL_DN) | X52_BTN_MASK(X52_BTN_SCROLL_UP))
#define X52_SCROLL_DOWN       X52_BTN_MASK(X52_BTN_SCROLL_DN)
#define X52_SCROLL_UP         X52_BTN_MASK(X52_BTN_SCROLL_UP)

// Hat switch (byte 12, upper nibble)
#define X52_HAT_BYTE          12
#define X52_HAT_SHIFT         4
#define X52_HAT_MASK          0xF0

// Mouse ministick (byte 13)
#define X52_MOUSE_BYTE        13
#define X52_MOUSE_X_MASK      0x0F
#define X52_MOUSE_Y_SHIFT     4
#define X52_MOUSE_Y_MASK      0xF0

// Timing
#define LONG_PRESS_MS         500

// Shift mode throttle scaling
#define SHIFT_SCALE_NORMAL    255   // 100%
#define SHIFT_SCALE_LOW       64    // 25%
#define SHIFT_RAMP_STEP       4     // per report (~480ms transition at 100Hz)

// Date formats
enum x52_date_format {
    X52_DATE_FORMAT_DDMMYY,
    X52_DATE_FORMAT_MMDDYY,
    X52_DATE_FORMAT_YYMMDD,
};

// Button action results from X52 input processing
enum x52_ht_action {
    X52_HT_NONE,
    X52_HT_RESET,      // Short press D: reset head tracker
    X52_HT_PAUSE,      // Long press D: toggle pause
};

// MFD page identifiers
#define MFD_PAGE_SHIFT    0   // X52 throttle shift (default)
#define MFD_PAGE_HT       1   // Head Tracker
#define MFD_PAGE_G923     2   // G923 wheel settings
#define MFD_PAGE_ATTACK3  3   // Attack 3 Z-axis scaling
#define MFD_PAGE_RESERVED 4   // Reserved for future use
#define MFD_NUM_PAGES     5

// Device lifecycle
void x52_on_mount(uint8_t dev_addr, uint16_t vid, uint16_t pid);
void x52_on_unmount(uint8_t dev_addr);
uint8_t x52_get_dev_addr();

// Process incoming X52 HID report (brightness + button state machine)
x52_ht_action x52_process_report(const uint8_t* report, uint16_t len, bool ht_connected);

// Apply shift mode: toggle on E button, ramp-scale throttle in place
// Call BEFORE report_received_callback so games see scaled value
void x52_apply_shift(uint8_t* report, uint16_t len);

// Query current shift state
bool x52_is_shifted();

// Poll long-press timeout from main loop (independent of X52 report rate)
x52_ht_action x52_check_long_press(bool ht_connected);

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
// Set force=true to bypass rate limit (e.g. on pause toggle)
void x52_update_ht_display(uint8_t dev_addr, int16_t headX, bool paused, bool force = false);

// Initialize MFD clocks: clock 1 = 00:00, start uptime tracking
void x52_init_clocks(uint8_t dev_addr);

// Update uptime clock on MFD (call from main loop, sends only on minute change)
void x52_update_clock();

// Get current MFD page (MFD_PAGE_SHIFT, MFD_PAGE_HT, etc.)
uint8_t x52_get_mfd_page();

// Device connection tracking for MFD pages
void x52_set_ht_connected(bool connected);
void x52_set_g923_connected(bool connected);
void x52_set_attack3_connected(bool connected);

// Update cached G923 display values (shown when on G923 page)
void x52_update_g923_display(uint16_t range, uint8_t spring_pct, uint8_t sensitivity);

// Update cached Attack 3 display values (shown when on Attack3 page)
void x52_update_attack3_display(uint8_t z_val, uint16_t scale_pct);

#endif
