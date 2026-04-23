// G923 Racing Wheel support for HID Remapper
//
// Xbox/PC variant (0xC26E): HID++ 2.0 FAP — force feedback, sensitivity, wheel range
// PlayStation/PC variant (0xC266): raw HID output reports — LEDs only
//
// HID++ commands are sent as Report ID 0x11 (Long, 20 bytes) via queue_out_report().
// For Very Long payloads (spring effect), Report ID 0x12 (64 bytes) is used.
// Responses arrive as HID input reports and are parsed in g923_process_response().

#include <cstdio>
#include <cstring>

#include "g923.h"
#include "out_report.h"
#include "remapper.h"
#include "x52.h"

// Device state
static uint8_t g923_dev_addr = 0;
static uint8_t g923_instance = 0;
static bool g923_xbox = false;   // true = Xbox/PC (HID++), false = PS/PC (raw)

// Current settings (tracked for MFD display)
static uint16_t g923_cur_range = 360;
static uint8_t g923_cur_spring_pct = 50;
static uint8_t g923_cur_sensitivity = 30;

// Composite interface handle for queue_out_report()
static uint16_t g923_iface() {
    return (uint16_t)(g923_dev_addr << 8) | g923_instance;
}

// ============================================================
// HID++ 2.0 helpers
// ============================================================

// Build and queue a HID++ Long report (Report ID 0x11, 20 bytes total).
// queue_out_report prepends the report_id byte, so we pass 19 bytes.
static bool hidpp_send_long(uint8_t feat_idx, uint8_t func_id, const uint8_t* params, uint8_t param_len) {
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
// Xbox mode switch (0xC26D → 0xC26E)
// ============================================================

// Send the 5-byte mode switch payload via interrupt OUT.
// The device will disconnect and re-enumerate as PID 0xC26E.
static void g923_send_mode_switch() {
    // Payload from usb_modeswitch / Wireshark capture:
    // MessageContent="0f00010142", sent on interrupt OUT endpoint 0x01
    uint8_t cmd[5] = { 0x0F, 0x00, 0x01, 0x01, 0x42 };

#if CFG_TUD_CDC
    printf("G923: sending Xbox→PC mode switch (0xC26D→0xC26E)\n");
#endif
    // report_id = 0 so queue_out_report sends raw bytes without prepending
    queue_out_report(g923_iface(), 0, cmd, sizeof(cmd));
}

// ============================================================
// Device lifecycle
// ============================================================

void g923_on_mount(uint8_t dev_addr, uint8_t instance, uint16_t vid, uint16_t pid) {
    if (vid != G923_VENDOR_ID) return;

    if (pid == G923_PID_XBOX) {
        g923_dev_addr = dev_addr;
        g923_instance = instance;
        g923_xbox = true;
#if CFG_TUD_CDC
        printf("G923 Xbox/PC mounted (addr=%d, inst=%d)\n", dev_addr, instance);
#endif
        g923_apply_defaults();
    } else if (pid == G923_PID_XBOX_PRE) {
        // Device is in Xbox mode — send mode switch, it will re-enumerate as 0xC26E
        g923_dev_addr = dev_addr;
        g923_instance = instance;
        g923_xbox = false;  // not usable yet
#if CFG_TUD_CDC
        printf("G923 Xbox mode detected (addr=%d, inst=%d), switching to PC mode...\n", dev_addr, instance);
#endif
        g923_send_mode_switch();
    } else if (pid == G923_PID_PS) {
        g923_dev_addr = dev_addr;
        g923_instance = instance;
        g923_xbox = false;
#if CFG_TUD_CDC
        printf("G923 PS/PC mounted (addr=%d, inst=%d)\n", dev_addr, instance);
#endif
    }
}

void g923_on_unmount(uint8_t dev_addr) {
    if (dev_addr == g923_dev_addr) {
        g923_dev_addr = 0;
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
// Default settings applied on connect
// ============================================================

void g923_apply_defaults() {
    // Wheel range: 360 degrees
    g923_set_wheel_range(360);

    // Steering sensitivity: 30%
    g923_set_sensitivity(G923_AXIS_STEERING, 30);

    // Spring effect: 50% coefficient on slot 0
    // Scale: percentage * 0x7FFF / 100  (20%=0x1999, 50%=0x3FFF, 100%=0x7FFF)
    // Full saturation (0x7FFF) = max centering force at full deflection
    // Deadband=0 (no dead zone), center=0 (wheel midpoint)
    g923_set_spring(0,
                    0x3FFF, 0x3FFF,   // left/right coefficient (50%)
                    0x7FFF, 0x7FFF,   // left/right saturation (full)
                    0x0000, 0x0000);  // deadband=0, center=0 (midpoint)
}
