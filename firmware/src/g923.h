#ifndef _G923_H_
#define _G923_H_

#include <stdint.h>

// Device identification
#define G923_VENDOR_ID     0x046D
#define G923_PID_XBOX      0xC26E  // Xbox/PC variant in PC mode (HID++ 2.0)
#define G923_PID_XBOX_PRE  0xC26D  // Xbox/PC variant before mode switch
#define G923_PID_PS        0xC266  // PlayStation/PC variant (raw HID)

// HID++ 2.0 constants
#define HIDPP_DEVICE_INDEX 0xFF   // USB-connected (not via receiver)
#define HIDPP_SW_ID        0x0D   // Software ID (echoed in responses)
#define HIDPP_REPORT_LONG  0x11   // Long report: 20 bytes total
#define HIDPP_REPORT_VLONG 0x12   // Very Long report: 64 bytes total

// Known HID++ feature page IDs (from capture analysis)
#define HIDPP_PAGE_IROOT             0x0000
#define HIDPP_PAGE_FORCE_FEEDBACK    0x8123  // Wheel range, spring, damper
#define HIDPP_PAGE_AXIS_SENSITIVITY  0x80A3  // Per-axis sensitivity
#define HIDPP_PAGE_AXIS_MODE         0x8120  // Axis response curve / profile
#define HIDPP_PAGE_LED_CTRL          0x807A  // "Adjustable Configuration" — LED control

// Confirmed runtime feature indices (from IFeatureSet enumeration)
// These are fixed for the G923 Xbox firmware — no discovery needed
#define G923_FIDX_FORCE_FEEDBACK    0x0B  // Feature 0x8123
#define G923_FIDX_AXIS_SENSITIVITY  0x14  // Feature 0x80A3
#define G923_FIDX_AXIS_MODE         0x0A  // Feature 0x8120
#define G923_FIDX_LED_CTRL          0x12  // Feature 0x807A

// Force Feedback functions (Feature 0x8123)
#define G923_FFB_FUNC_SET_SPRING    2  // Set spring effect (Very Long)
#define G923_FFB_FUNC_STOP_EFFECT   4  // Stop/destroy FFB effect
#define G923_FFB_FUNC_SET_RANGE     6  // Set/get wheel rotation range

// Axis Sensitivity functions (Feature 0x80A3)
#define G923_SENS_FUNC_GET_COUNT    0  // Get axis count
#define G923_SENS_FUNC_GET_INFO     1  // Get axis info
#define G923_SENS_FUNC_GET_SENS     2  // Get sensitivity
#define G923_SENS_FUNC_SET_SENS     3  // Set sensitivity

// LED Control functions (Feature 0x807A)
#define G923_LED_FUNC_GET_INFO      0  // -> [03:05:02]
#define G923_LED_FUNC_GET_STATE     1  // -> [00:02]
#define G923_LED_FUNC_RESET         2
#define G923_LED_FUNC_SET_MODE      3  // param 0x02 = enable LED writes (THE UNLOCK)
#define G923_LED_FUNC_SET_CONFIG    4  // -> [00:05]
#define G923_LED_FUNC_SET_LEDS      6  // LED data payload
#define G923_LED_FUNC_GET_DETAILS   7  // -> [00:01:00:05]

// Axis indices (from capture analysis)
#define G923_AXIS_STEERING    0  // X, 16-bit
#define G923_AXIS_ACCELERATOR 1  // Y, 8-bit
#define G923_AXIS_BRAKE       2  // Z, 8-bit
#define G923_AXIS_CLUTCH      3  // Rz, 8-bit

// Wheel range presets (big-endian degrees)
#define G923_RANGE_180   180
#define G923_RANGE_360   360
#define G923_RANGE_900   900   // Default

// Sensitivity range
#define G923_SENS_MIN    0x01  // 1%
#define G923_SENS_DEFAULT 0x32 // 50%
#define G923_SENS_MAX    0x64  // 100%

// Rev counter stage input
#define G923_STAGE_AUTO  0xFF  // auto-shift mode (no manual override)

// Device lifecycle
void g923_on_mount(uint8_t dev_addr, uint8_t instance, uint16_t vid, uint16_t pid);
void g923_on_unmount(uint8_t dev_addr);
uint8_t g923_get_dev_addr();
bool g923_is_xbox();

// Force feedback: wheel range
bool g923_set_wheel_range(uint16_t degrees);

// Force feedback: spring effect
// Coefficients are 0x0000–0x7FFF (0–100%).  deadband/center in device units.
bool g923_set_spring(uint8_t slot,
                     uint16_t left_coeff, uint16_t right_coeff,
                     uint16_t left_sat, uint16_t right_sat,
                     uint16_t deadband, uint16_t center);
bool g923_stop_effect(uint8_t slot);

// Axis sensitivity (0x01–0x64 = 1%–100%)
bool g923_set_sensitivity(uint8_t axis, uint8_t sensitivity);

// PlayStation/PC variant: raw LED control
bool g923_set_leds_ps(uint8_t setting);

// Xbox/PC LED control (Feature 0x807A)
bool g923_enable_leds();
bool g923_set_leds(uint8_t level);  // 0=off, 1-5=progressive RPM bar
void g923_init_leds();
void g923_simulate_rev_counter(uint8_t accelerator, uint8_t brake, uint8_t stage_input);

// Apply default settings on connect
void g923_apply_defaults();

#endif // _G923_H_
