// G923 Racing Wheel support for HID Remapper
//
// Xbox/PC variant (0xC26E): HID++ 2.0 FAP — force feedback, sensitivity, wheel range
// PlayStation/PC variant (0xC266): raw HID output reports — LEDs only
//
// HID++ commands are sent as Report ID 0x11 (Long, 20 bytes) via queue_out_report().
// For Very Long payloads (spring effect), Report ID 0x12 (64 bytes) is used.
// Responses arrive as HID input reports.

#include <tusb.h>
#include <cstdio>
#include <cstring>

#include "g923.h"
#include "out_report.h"
#include "remapper.h"
#include "x52.h"
#include "pico/time.h"

// Device state
static uint8_t g923_dev_addr = 0;
static uint8_t g923_instance = 0;
static bool g923_xbox = false;   // true = Xbox/PC (HID++), false = PS/PC (raw)

// Current settings (tracked for MFD display)
static uint16_t g923_cur_range = 360;
static uint8_t g923_cur_spring_pct = 50;
static uint8_t g923_cur_sensitivity = 30;

// LED state (Feature 0x807A)
static uint8_t g923_led_feat_idx = 0;  // runtime index (typically G923_FIDX_LED_CTRL)
static bool g923_led_enabled = false;

// LED level map: 16-bit values for bytes[8:9] of func 6 payload
// Base bytes[4:5] = 0x12,0x49 and mask bytes[6:7] = 0xFF,0xFF for RPM bar mode
// Level 0 uses alternate "all off" base: 0x00,0x01
static const uint16_t g923_led_levels[] = {
    0x0000,  // 0: Off
    0x2492,  // 1: 1x green each side
    0x6DB6,  // 2: 2x green each side
    0xB6DA,  // 3: 2x green + 1x red each side
    0xEDB5,  // 4: 2x green + 2x red each side
    0xFFFF,  // 5: All on (2x blue + 2x green + 2x red)
};

// Initialize to an invalid level to ensure the first update goes through
static uint8_t g923_led_last_level = 0xFF;

// HID++ init state machine
static g923_init_state_t g923_init_state = G923_INIT_IDLE;

// Composite interface handle for queue_out_report()
static uint16_t g923_iface() {
    return (uint16_t)(g923_hidpp_dev_addr << 8) | g923_hidpp_instance;
}

// ============================================================
// HID++ 2.0 helpers
// ============================================================

// Build and queue a HID++ Long report (Report ID 0x11, 20 bytes total).
// queue_out_report prepends the report_id byte, so we pass 19 bytes.
static bool hidpp_send_long(uint8_t feat_idx, uint8_t func_id, const uint8_t* params, uint8_t param_len) {
#if CFG_TUD_CDC
    printf("hidpp_send_long (feat_idx=%d, func_id=%d, param_len=%d)\n", feat_idx, func_id, param_len);
#endif

    if (!g923_dev_addr || !g923_xbox) return false;
    if (param_len > 16) return false;  // 19 - 3 header bytes = 16 max params

    uint8_t buf[19] = {};
    buf[0] = HIDPP_DEVICE_INDEX;
    buf[1] = feat_idx;
    buf[2] = (func_id << 4) | HIDPP_SW_ID;
    if (params && param_len > 0) {
        memcpy(&buf[3], params, param_len);
    }

    queue_out_report(g923_iface(), HIDPP_REPORT_LONG, buf, sizeof(buf));
    return true;
}

// Build and queue a HID++ Very Long report (Report ID 0x12, 64 bytes total).
// queue_out_report prepends the report_id byte, so we pass 63 bytes.
static bool hidpp_send_vlong(uint8_t feat_idx, uint8_t func_id, const uint8_t* params, uint8_t param_len) {
    if (!g923_dev_addr || !g923_xbox) return false;
    if (param_len > 60) return false;  // 63 - 3 header bytes = 60 max params

    uint8_t buf[63] = {};
    buf[0] = HIDPP_DEVICE_INDEX;
    buf[1] = feat_idx;
    buf[2] = (func_id << 4) | HIDPP_SW_ID;
    if (params && param_len > 0) {
        memcpy(&buf[3], params, param_len);
    }

    queue_out_report(g923_iface(), HIDPP_REPORT_VLONG, buf, sizeof(buf));
    return true;
}

// ============================================================
// HID++ interface identification via report descriptor
// ============================================================

uint8_t g923_hidpp_dev_addr = 0;
uint8_t g923_hidpp_instance = 0;
uint8_t g923_hidpp_itf_num  = 0;

// Returns the byte size of a short HID descriptor item (tag byte + data).
// Size field bits [1:0]: 0=0bytes, 1=1byte, 2=2bytes, 3=4bytes.
static uint8_t hid_item_size(uint8_t tag_byte) {
    uint8_t sz = tag_byte & 0x03;
    return (sz == 3) ? 5 : (1 + sz);
}

// Reads the unsigned value from a short HID item's data bytes (little-endian).
static uint32_t hid_item_value(const uint8_t* p, uint8_t tag_byte) {
    switch (tag_byte & 0x03) {
        case 1: return p[1];
        case 2: return (uint32_t)(p[1] | ((uint32_t)p[2] << 8));
        case 3: return (uint32_t)(p[1] | ((uint32_t)p[2] << 8) |
                                  ((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 24));
        default: return 0;
    }
}

// Walk the descriptor and extract the top-level Usage Page and first Usage
// (before the first Collection). Returns true if they match the HID++ write
// channel (0xFF43 / 0x0602 — Col02 confirmed from Python hidapi capture).
static bool descriptor_is_hidpp(const uint8_t* desc, uint16_t len) {
    uint32_t usage_page   = 0;
    uint32_t usage        = 0;
    bool     got_up       = false;
    bool     got_usage    = false;
    int      depth        = 0;  // collection nesting depth

    for (uint16_t i = 0; i < len; ) {
        if (i + 1 > len) break;
        uint8_t tag_byte = desc[i];
        uint8_t item_sz  = hid_item_size(tag_byte);
        uint8_t item_tag = tag_byte & 0xFC;

        if (i + item_sz > len) break;

        switch (item_tag) {
            case 0x04:  // Usage Page (global) — applies until next Usage Page
                if (depth == 0) {
                    usage_page = hid_item_value(desc + i, tag_byte);
                    got_up     = true;
                    got_usage  = false;  // reset — Usage Page resets pending Usage
                    usage      = 0;
                }
                break;

            case 0x08:  // Usage (local, 1-byte)
            case 0x0A:  // Usage (local, extended)
                if (depth == 0 && !got_usage) {
                    usage     = hid_item_value(desc + i, tag_byte);
                    got_usage = true;
                }
                break;

            case 0xA0:  // Collection — check the pending usage before descending
                if (depth == 0 && got_up && got_usage) {
#if CFG_TUD_CDC
                    printf("G923 descriptor collection: UsagePage=0x%04lX Usage=0x%04lX\n",
                           (unsigned long)usage_page, (unsigned long)usage);
#endif
                    if (usage_page == G923_HIDPP_USAGE_PAGE &&
                        usage      == G923_HIDPP_USAGE_WRITE) {
                        return true;  // found FF43:0602 — this is the HID++ write channel
                    }
                    // Reset for next top-level collection
                    got_usage = false;
                    usage     = 0;
                }
                depth++;
                break;

            case 0xC0:  // End Collection
                if (depth > 0) depth--;
                break;
        }
        i += item_sz;
    }
    return false;
}

bool g923_check_hidpp_interface(uint8_t dev_addr, uint8_t instance,
                                uint8_t itf_num,
                                const uint8_t* desc_report, uint16_t desc_len) {
    if (desc_report == nullptr || desc_len == 0) return false;
    if (!descriptor_is_hidpp(desc_report, desc_len)) return false;

    g923_hidpp_dev_addr = dev_addr;
    g923_hidpp_instance = instance;
    g923_hidpp_itf_num  = itf_num;

#if CFG_TUD_CDC
    printf("G923 HID++ interface found: dev=%d inst=%d itf=%d\n",
           dev_addr, instance, itf_num);
#endif
    return true;
}


// ============================================================
// Xbox mode switch (0xC26D → 0xC26E)
// ============================================================
// NOTE: The mode switch is handled by the Xbox driver in xbox.cc (XType::G923_PRE).
// The pre-switch device (0xC26D) uses vendor-class interfaces that TinyUSB's HID
// driver ignores, so tuh_hid_mount_cb never fires for it. The Xbox driver detects
// it by VID/PID and sends the 5-byte mode switch on the interrupt OUT endpoint.
// After re-enumeration as 0xC26E, the HID driver mounts it and g923_on_mount()
// is called with G923_PID_XBOXVAR_PCMODE.

// ============================================================
// Device lifecycle
// ============================================================

void g923_on_mount(uint8_t dev_addr, uint8_t instance, uint16_t vid, uint16_t pid,
                   uint8_t itf_num,
                   const uint8_t* desc_report, uint16_t desc_len) {
#if CFG_TUD_CDC
    printf("g923_on_mount (addr=%d, inst=%d, vid=0x%04X, pid=0x%04X, itf=%d)\n",
           dev_addr, instance, vid, pid, itf_num);
#endif
    if (vid != G923_VENDOR_ID) return;

    if (pid == G923_PID_XBOXVAR_PCMODE) {
        g923_dev_addr = dev_addr;
        g923_xbox     = true;
        g923_led_feat_idx = 0;
        g923_led_enabled  = false;

        // Identify which instance is the HID++ write channel (Col02)
        // by scanning the report descriptor for Usage Page 0xFF43, Usage 0x0602.
        // g923_apply_defaults() is only called on that instance — not the
        // gamepad interface — so HID++ init doesn't fire on the wrong endpoint.
        bool is_hidpp = g923_check_hidpp_interface(dev_addr, instance,
                                                    itf_num, desc_report, desc_len);
#if CFG_TUD_CDC
        printf("G923 Xbox/PC mounted (addr=%d, inst=%d, itf=%d, is_hidpp=%d)\n",
               dev_addr, instance, itf_num, is_hidpp);
#endif
        if (is_hidpp) {
            g923_apply_defaults();
        }

    } else if (pid == G923_PID_XBOXVAR_XBOXMODE) {
        // Pre-switch Xbox mode — handled by xbox.cc, should not reach here.
#if CFG_TUD_CDC
        printf("G923 Xbox mode (0xC26D) in HID mount — unexpected\n");
#endif
    } else if (pid == G923_PID_PSVAR) {
        g923_dev_addr = dev_addr;
        g923_instance = instance;
        g923_xbox     = false;
#if CFG_TUD_CDC
        printf("G923 PS/PC mounted (addr=%d, inst=%d)\n", dev_addr, instance);
#endif
    }
}

void g923_on_unmount(uint8_t dev_addr) {
    if (dev_addr == g923_dev_addr) {
        g923_dev_addr       = 0;
        g923_led_feat_idx   = 0;
        g923_led_enabled    = false;
        // Clear HID++ interface tracking
        g923_hidpp_dev_addr = 0;
        g923_hidpp_instance = 0;
        g923_hidpp_itf_num  = 0;
        g923_init_state = G923_INIT_IDLE;        
#if CFG_TUD_CDC
        printf("G923 unmounted\n");
#endif
    }
}

uint8_t g923_get_dev_addr() {
    return g923_dev_addr;
}

bool g923_is_xbox() {
    return g923_xbox;
}

// ============================================================
// Force Feedback — Wheel Range (Feature 0x8123, Func 6)
// ============================================================

bool g923_set_wheel_range(uint16_t degrees) {
    if (!g923_dev_addr) return false;

    if (g923_xbox) {
        // HID++ 2.0: Feature 0x8123, Function 6 (SetWheelRange)
        // Params: [angle_hi, angle_lo] big-endian
        uint8_t params[2] = {
            (uint8_t)(degrees >> 8),
            (uint8_t)(degrees & 0xFF)
        };
#if CFG_TUD_CDC
        printf("G923: set wheel range %d deg\n", degrees);
#endif
        bool ok = hidpp_send_long(G923_FIDX_FORCE_FEEDBACK, G923_FFB_FUNC_SET_RANGE, params, 2);
        if (ok) {
            g923_cur_range = degrees;
            x52_update_g923_display(g923_cur_range, g923_cur_spring_pct, g923_cur_sensitivity);
        }
        return ok;
    }

    return false;  // PS variant doesn't use HID++ for range
}

// ============================================================
// Force Feedback — Spring Effect (Feature 0x8123, Func 2)
// ============================================================

bool g923_set_spring(uint8_t slot,
                     uint16_t left_coeff, uint16_t right_coeff,
                     uint16_t left_sat, uint16_t right_sat,
                     uint16_t deadband, uint16_t center) {
    if (!g923_dev_addr || !g923_xbox) return false;

    // Very Long report required — 18 bytes of spring data
    // Layout from capture: [slot, 0x86, left_sat_hi, left_sat_lo, right_sat_hi, right_sat_lo,
    //   left_max_hi, left_max_lo, left_coeff_hi, left_coeff_lo,
    //   deadband_hi, deadband_lo, center_hi, center_lo,
    //   right_coeff_hi, right_coeff_lo, right_max_hi, right_max_lo]
    // G HUB always uses: sat=0x0000, max=0x7FFF, deadband=0, center=0
    uint8_t params[18] = {};
    params[0]  = slot;
    params[1]  = 0x86;  // effect type: spring
    params[2]  = (uint8_t)(left_sat >> 8);
    params[3]  = (uint8_t)(left_sat & 0xFF);
    params[4]  = (uint8_t)(right_sat >> 8);
    params[5]  = (uint8_t)(right_sat & 0xFF);
    params[6]  = 0x7F;  // left_max always 0x7FFF
    params[7]  = 0xFF;
    params[8]  = (uint8_t)(left_coeff >> 8);
    params[9]  = (uint8_t)(left_coeff & 0xFF);
    params[10] = (uint8_t)(deadband >> 8);
    params[11] = (uint8_t)(deadband & 0xFF);
    params[12] = (uint8_t)(center >> 8);
    params[13] = (uint8_t)(center & 0xFF);
    params[14] = (uint8_t)(right_coeff >> 8);
    params[15] = (uint8_t)(right_coeff & 0xFF);
    params[16] = 0x7F;  // right_max always 0x7FFF
    params[17] = 0xFF;

#if CFG_TUD_CDC
    printf("G923: set spring slot=%d L=%04x R=%04x\n", slot, left_coeff, right_coeff);
#endif
    bool ok = hidpp_send_vlong(G923_FIDX_FORCE_FEEDBACK, G923_FFB_FUNC_SET_SPRING, params, sizeof(params));
    if (ok) {
        g923_cur_spring_pct = (uint8_t)((uint32_t)left_coeff * 100 / 0x7FFF);
        x52_update_g923_display(g923_cur_range, g923_cur_spring_pct, g923_cur_sensitivity);
    }
    return ok;
}

bool g923_stop_effect(uint8_t slot) {
    if (!g923_dev_addr || !g923_xbox) return false;

    uint8_t params[1] = { slot };
#if CFG_TUD_CDC
    printf("G923: stop effect slot=%d\n", slot);
#endif
    return hidpp_send_long(G923_FIDX_FORCE_FEEDBACK, G923_FFB_FUNC_STOP_EFFECT, params, 1);
}

// ============================================================
// Axis Sensitivity (Feature 0x80A3, Func 3)
// ============================================================

bool g923_set_sensitivity(uint8_t axis, uint8_t sensitivity) {
    if (!g923_dev_addr || !g923_xbox) return false;
    if (axis > G923_AXIS_CLUTCH) return false;
    if (sensitivity < G923_SENS_MIN || sensitivity > G923_SENS_MAX) return false;

    // From capture: Func 3 params = [axis_idx, param_A, sensitivity]
    // param_A is always 0x64 (100) in all G HUB captures
    uint8_t params[3] = { axis, 0x64, sensitivity };

#if CFG_TUD_CDC
    printf("G923: set sensitivity axis=%d sens=%d%%\n", axis, sensitivity);
#endif
    bool ok = hidpp_send_long(G923_FIDX_AXIS_SENSITIVITY, G923_SENS_FUNC_SET_SENS, params, 3);
    if (ok && axis == G923_AXIS_STEERING) {
        g923_cur_sensitivity = sensitivity;
        x52_update_g923_display(g923_cur_range, g923_cur_spring_pct, g923_cur_sensitivity);
    }
    return ok;
}

// ============================================================
// PlayStation/PC variant — Raw LED control
// ============================================================

bool g923_set_leds_ps(uint8_t setting) {
    if (!g923_dev_addr || g923_xbox) return false;

    // 7-byte raw HID output report, report_id = 0 (no report ID prefix)
    uint8_t cmd[7] = { 0xF8, 0x12, (uint8_t)(setting & 0x1F), 0x00, 0x00, 0x00, 0x01 };

#if CFG_TUD_CDC
    printf("G923 PS: set LEDs 0x%02x\n", setting & 0x1F);
#endif
    queue_out_report(g923_iface(), 0, cmd, sizeof(cmd));
    return true;
}

// ============================================================
// Xbox/PC LED control (Feature 0x807A)
// ============================================================

// Discover feature 0x807A via IRoot (index 0x00, func 0)
// Response byte[4] = runtime feature index
// TODO: parse IRoot response to extract runtime index dynamically
static bool g923_discover_led_feature(void) {
#if CFG_TUD_CDC
    printf("g923_discover_led_feature: sending IRoot query for page 0x%04X\n",
           HIDPP_PAGE_LED_CTRL);
#endif
    uint8_t params[] = {
        (uint8_t)(HIDPP_PAGE_LED_CTRL >> 8),
        (uint8_t)(HIDPP_PAGE_LED_CTRL & 0xFF)
    };
    // Do NOT set g923_led_feat_idx here — extracted from IRoot response in
    // g923_on_hidpp_response() state G923_INIT_WAIT_IROOT
    return hidpp_send_long(0x00, 0x00, params, sizeof(params));
}

// Enable LED mode — func 3, param 0x02
// This is the critical unlock found ONLY in the working pcap capture.
// Without this, func 6 LED commands return NOT_ALLOWED (0x05).
bool g923_enable_leds(void) {
#if CFG_TUD_CDC
    printf("g923_enable_leds: sending func3 unlock (feat_idx=0x%02X)\n",
           g923_led_feat_idx);
#endif
    if (!g923_led_feat_idx) return false;
    uint8_t params[] = { 0x02, 0x00 };
    // Do NOT set g923_led_enabled here — set after ack in G923_INIT_WAIT_LED_ENABLE
    return hidpp_send_long(g923_led_feat_idx, G923_LED_FUNC_SET_MODE, params, sizeof(params));
}

// Set LED level (0-5)
// Func 6 payload: [base_hi, base_lo, mask_hi, mask_lo, level_hi, level_lo]
bool g923_set_leds(uint8_t level) {
#if CFG_TUD_CDC
    printf("g923_set_leds (level=%d)\n", level);
#endif
    if (!g923_led_feat_idx) return false;
    if (level > 5) level = 5;

    uint16_t val = g923_led_levels[level];

    if (level == 0) {
        // All-off uses alternate base 0x00,0x01 (from pcap)
        uint8_t params[] = { 0x00, 0x01, 0xFF, 0xFF, 0x00, 0x00 };
        return hidpp_send_long(g923_led_feat_idx, G923_LED_FUNC_SET_LEDS, params, sizeof(params));
    } else {
        // Standard RPM bar: base 0x12,0x49 / mask 0xFF,0xFF
        uint8_t params[] = { 0x12, 0x49, 0xFF, 0xFF, (uint8_t)(val >> 8), (uint8_t)(val & 0xFF) };
        return hidpp_send_long(g923_led_feat_idx, G923_LED_FUNC_SET_LEDS, params, sizeof(params));
    }
}

// ============================================================
// HID++ init state machine response handler
// ============================================================

void g923_on_hidpp_response(const uint8_t* report, uint16_t len) {

    // After TinyUSB strips report ID:
    // report[0] = device_index (0xFF)
    // report[1] = feat_idx echo
    // report[2] = func|sw_id echo  (func in upper nibble, sw_id in lower)
    // report[3+] = response params

    if (g923_init_state == G923_INIT_MOUNTED) {
#if CFG_TUD_CDC
        printf("g923: first IN received (len=%d), starting IRoot discovery\n", len);
#endif
        g923_init_state = G923_INIT_WAIT_IROOT;
        g923_discover_led_feature();
        return;
    }

    // All other states: must be a valid HID++ response
    //if (len < 4 || report[0] != HIDPP_DEVICE_INDEX) return;
    if (len < 6 ) return;

#if CFG_TUD_CDC
    // Safe to read [1],[2],[3] now — len >= 4 guaranteed above
    printf("g923_on_hidpp_response: state=%d, idx=0x%02X feat=0x%02X func=0x%02X p[3]=0x%02X p[4]=0x%02X p[5]=0x%02X\n",
           g923_init_state, report[0], report[1], report[2], report[3], report[4], report[5]);
#endif

    switch (g923_init_state) {

        case G923_INIT_WAIT_IROOT:
            // IRoot response: report[3] = runtime feature index for queried page
            // Confirmed from Python script: runtime index for 0x807A = 0x12
            g923_led_feat_idx = report[3];
#if CFG_TUD_CDC
            printf("g923: IRoot response — LED feat_idx=0x%02X\n", g923_led_feat_idx);
#endif
            if (g923_led_feat_idx == 0) {
#if CFG_TUD_CDC
                printf("g923: LED feature 0x807A not found on device, aborting init\n");
#endif
                g923_init_state = G923_INIT_IDLE;
                return;
            }
            g923_init_state = G923_INIT_WAIT_LED_ENABLE;
            g923_enable_leds();
            break;

        case G923_INIT_WAIT_LED_ENABLE:
            // HID++ error response: feat_idx=0xFF, func=0xFF, error code in report[3]
            if (report[1] == 0xFF && report[2] == 0xFF) {
#if CFG_TUD_CDC
                printf("g923: LED enable error 0x%02X (NOT_ALLOWED=0x05)\n", report[3]);
#endif
                g923_init_state = G923_INIT_IDLE;
                return;
            }
            g923_led_enabled = true;
#if CFG_TUD_CDC
            printf("g923: LED enable ack — sending func6 set\n");
#endif
            g923_init_state = G923_INIT_WAIT_LED_SET;
            g923_set_leds(1);
            break;

        case G923_INIT_WAIT_LED_SET:
#if CFG_TUD_CDC
            printf("g923: LED set ack — init complete\n");
#endif
            g923_init_state = G923_INIT_DONE;
            break;

        case G923_INIT_DONE:
            // Post-init responses (FFB acks etc.) — no handling needed yet
            break;

        case G923_INIT_IDLE:
        default:
            break;
    }
}

// Full LED init — discover feature + enable
void g923_init_leds(void) {
#if CFG_TUD_CDC
    printf("g923_init_leds: waiting for first IN report\n");
#endif
    if (!g923_dev_addr || !g923_xbox) return;
    g923_init_state = G923_INIT_MOUNTED;  // was G923_INIT_WAIT_IROOT
    // Do NOT send IRoot query here — wait for first IN in g923_on_hidpp_response
}

// Simulate RPM from pedal inputs (call from report callback)
// The longer the accelerator is held, the higher the RPM percentage.
// Brake quickly drops RPM. Natural decay when both pedals are released.
//
// Example: wire up in tuh_hid_report_received_cb (remapper_single.cc):
//
//   // G923 report 0x01 layout (after TinyUSB strips report ID byte):
//   //   [0]     hat(4b) + buttons(4b)    bits 0-7
//   //   [1-2]   buttons 5-20             bits 8-23
//   //   [3]     buttons 21-23 + padding  bits 24-31
//   //   [4-5]   X  steering   (16-bit)   bits 32-47
//   //   [6]     Y  accelerator (8-bit)   bits 48-55
//   //   [7]     Z  brake       (8-bit)   bits 56-63
//   //   [8]     Rz clutch      (8-bit)   bits 64-71
//   //
//   if (dev_addr == g923_get_dev_addr() && len >= 8) {
//       g923_simulate_rev_counter(report[6], report[7], G923_STAGE_AUTO);
//   }
//
void g923_simulate_rev_counter(uint8_t accelerator, uint8_t brake, uint8_t stage_input) {
    if (!g923_dev_addr || !g923_xbox || !g923_led_enabled) return;

    static uint64_t last_tick_us = 0;
    static int16_t rpm_accum = 0;  // 0-10000 (x100 for precision)
    static uint8_t stage = 0;      // 0-7 gear stage

    uint64_t now_us = time_us_64();
    uint32_t dt_ms = (last_tick_us == 0) ? 0 : (uint32_t)((now_us - last_tick_us) / 1000);
    last_tick_us = now_us;

    // Accelerator builds RPM: ~2 seconds 0→100% at full throttle
    if (accelerator > 0) {
        rpm_accum += (int16_t)((uint32_t)accelerator * dt_ms / 50);
    }

    // Brake drops RPM: ~0.5 seconds 100%→0 at full brake
    if (brake > 0) {
        rpm_accum -= (int16_t)((uint32_t)brake * dt_ms / 12);
    }

    // Natural decay when off throttle
    if (accelerator == 0 && brake == 0 && rpm_accum > 0) {
        rpm_accum -= (int16_t)(dt_ms * 2);
    }

    // Gear stage simulation (0-7)
    if (stage_input == G923_STAGE_AUTO) {
        // Auto-shift based on RPM thresholds
        if (rpm_accum >= 9500 && stage < 7) {
            stage++;
            rpm_accum = (accelerator > 128) ? 2500 : 5000;
        }
        if (rpm_accum <= 1000 && stage > 0) {
            stage--;
            rpm_accum = (accelerator > 128) ? 8000 : 5000;
        }
        if (rpm_accum <= 0) {
            stage = 0;
        }
    } else if (stage_input <= 7 && stage_input != stage) {
        // Manual stage set via button (0-7, usage mappings TBD)
        uint8_t prev = stage;
        stage = stage_input;
        if (stage > prev) {
            // Upshift: RPM drops (high throttle = sharper drop)
            rpm_accum = (accelerator > 128) ? 2500 : 5000;
        } else {
            // Downshift: RPM jumps up (high throttle = higher rev match)
            rpm_accum = (accelerator > 128) ? 8000 : 5000;
        }
    }

    // Clamp
    if (rpm_accum < 0) rpm_accum = 0;
    if (rpm_accum > 10000) rpm_accum = 10000;

    // Map 0-10000 → 0-5 LED level
    uint8_t rpm_pct = (uint8_t)(rpm_accum / 100);
    uint8_t level;
    if (rpm_pct == 0)       level = 0;
    else if (rpm_pct <= 20) level = 1;
    else if (rpm_pct <= 40) level = 2;
    else if (rpm_pct <= 60) level = 3;
    else if (rpm_pct <= 80) level = 4;
    else                    level = 5;

    if (level != g923_led_last_level) {
        g923_set_leds(level);
        g923_led_last_level = level;
    }
}

// ============================================================
// Default settings applied on connect
// ============================================================
void g923_apply_defaults() {
#if CFG_TUD_CDC
    printf("g923_apply_defaults\n");
#endif
    g923_init_state   = G923_INIT_IDLE;
    g923_led_feat_idx = 0;
    g923_led_enabled  = false;

    // TODO: PA check if updating these settings requires a mode unlock first.

    // // Wheel range: 360 degrees
    // g923_set_wheel_range(360);

    // // Steering sensitivity: 30%
    // g923_set_sensitivity(G923_AXIS_STEERING, 30);

    // // Spring effect: 50% coefficient on slot 0
    // // Scale: percentage * 0x7FFF / 100  (20%=0x1999, 50%=0x3FFF, 100%=0x7FFF)
    // // Full saturation (0x7FFF) = max centering force at full deflection
    // // Deadband=0 (no dead zone), center=0 (wheel midpoint)
    // g923_set_spring(0,
    //                 0x3FFF, 0x3FFF,   // left/right coefficient (50%)
    //                 0x7FFF, 0x7FFF,   // left/right saturation (full)
    //                 0x0000, 0x0000);  // deadband=0, center=0 (midpoint)

    // LED unlock: discover feature 0x807A + enable LED writes
    g923_init_leds();

    // FFB and sensitivity left commented out pending LED init confirmation
}

// ============================================================
// Test accessors (UNIT_TEST builds only)
// ============================================================
#ifdef UNIT_TEST
g923_init_state_t g923_init_state_get() { return g923_init_state; }
uint8_t           g923_led_feat_idx_get() { return g923_led_feat_idx; }
#endif