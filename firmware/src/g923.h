#ifndef _G923_H_
#define _G923_H_

#include <stdint.h>

// Device identification
#define G923_VENDOR_ID             0x046D
#define G923_PID_XBOXVAR_XBOXMODE  0xC26D  // Xbox/PC variant in Xbox mode before mode switch
#define G923_PID_XBOXVAR_PCMODE    0xC26E  // Xbox/PC variant in PC mode after mode (HID++ 2.0)
#define G923_PID_PSVAR             0xC266  // PlayStation/PC variant (raw HID)

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
#define HIDPP_PAGE_PEDAL_STATUS     0x8060  // Dual-clutch mode control
#define HIDPP_PAGE_DEVICE_READY     0x8100  // Device Ready notification (fires after POST)

// Confirmed runtime feature indices (from IFeatureSet enumeration)
// These are fixed for the G923 Xbox firmware — no discovery needed
// All confirmed from pcap IFeatureSet enumeration:
#define G923_FIDX_IFEATURESET       0x02  // IFeatureSet
#define G923_FIDX_IFIRMWAREINFO     0x03  // IFirmwareInfo
#define G923_FIDX_AXIS_MODE         0x0A  // Feature 0x8120
#define G923_FIDX_FORCE_FEEDBACK    0x0B  // Feature 0x8123
#define G923_FIDX_PEDAL_STATUS      0x0D  // Feature 0x8060 (dual-clutch)
#define G923_FIDX_DEVICE_READY      0x11  // Device ready notification
#define G923_FIDX_LED_CTRL          0x12  // Feature 0x807A ✅ confirmed
#define G923_FIDX_AXIS_SENSITIVITY  0x14  // Feature 0x80A3
#define G923_PEDAL_FUNC_SET_MODE    1

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

// ============================================================
// HID++ interface identification (Phase 2)
// ============================================================

// G923 PC mode HID++ channel — confirmed from Python hidapi capture
// Col02 (write): Usage Page 0xFF43, Usage 0x0602 — send 0x11 Long reports here
// Col03 (read):  Usage Page 0xFF43, Usage 0x0604 — 0x12 VLong responses arrive here
#define G923_HIDPP_USAGE_PAGE    0xFF43
#define G923_HIDPP_USAGE_WRITE   0x0602   // Col02 — the interface to match and store
#define G923_HIDPP_USAGE_READ    0x0604   // Col03 — informational, same iface/instance



// Stored at mount time by g923_check_hidpp_interface()
extern uint8_t g923_hidpp_dev_addr;
extern uint8_t g923_hidpp_instance;
extern uint8_t g923_hidpp_itf_num;

// Called from tuh_hid_mount_cb for every G923 interface mount.
// Returns true if this interface is the HID++ write channel (Col02).
bool g923_check_hidpp_interface(uint8_t dev_addr, uint8_t instance,
                                uint8_t itf_num,
                                const uint8_t* desc_report, uint16_t desc_len);

// ============================================================
// HID++ init state machine (Phase 3)
// ============================================================

typedef enum {
    G923_INIT_IDLE              = 0,
    G923_INIT_MOUNTED           = 1,  // waiting for first IN to arm receive path
    G923_INIT_WAIT_READY        = 2,  // waiting for feat=0x11 POST-complete event
    G923_INIT_WAIT_PEDAL_RESET  = 3,  // sent dual-clutch disable, waiting for ack
    G923_INIT_WAIT_IROOT        = 4,  // sent IRoot query, waiting for feat_idx
    G923_INIT_WAIT_LED_ENABLE   = 5,  // sent func3 enable, waiting for ack
    G923_INIT_WAIT_LED_SET      = 6,  // sent func6 set, waiting for ack
    G923_INIT_DONE              = 7,
} g923_init_state_t;

// Called from tuh_hid_report_received_cb for HID++ responses on the HID++ instance.
// 0x11 requests always return 0x12 VLong responses (confirmed from Python capture).
// TinyUSB strips the report ID before the callback so report[0] = device_index (0xFF).
void g923_on_hidpp_response(const uint8_t* report, uint16_t len);

// Test accessors — only compiled when UNIT_TEST is defined
#ifdef UNIT_TEST
g923_init_state_t g923_init_state_get(void);
uint8_t           g923_led_feat_idx_get(void);
#endif

// Device lifecycle
void g923_on_mount(uint8_t dev_addr, uint8_t instance, uint16_t vid, uint16_t pid,
                   uint8_t itf_num,
                   const uint8_t* desc_report, uint16_t desc_len);
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
