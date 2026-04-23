#include <tusb.h>

#include "pio_usb.h"
#include "usb_midi_host.h"
#include "pico/platform.h"
#include "pico/time.h"

#include "cdc_debug.h"
#include "descriptor_parser.h"
#include "out_report.h"
#include "remapper.h"
#include "tick.h"
#include "vendor_control.h"
#include "x52.h"
#include "g923.h"

// Head Tracker (Arduino Nano ESP32)
#define HT_VENDOR_ID   0x2341
#define HT_PRODUCT_ID  0x8070
#define HT_RESET_CMD   0x01
#define HT_PAUSE_CMD   0x02
#define HT_REPORT_ID   2

// Logitech Attack 3 joystick
#define ATTACK3_VENDOR_ID   0x046D
#define ATTACK3_PRODUCT_ID  0xC214

static uint8_t ht_dev_addr = 0;
static uint8_t ht_instance = 0;
static bool ht_paused = false;
static int16_t last_ht_x = 0;

static uint8_t attack3_dev_addr = 0;

static void handle_ht_action(x52_ht_action action) {
    if (action == X52_HT_NONE || ht_dev_addr == 0) return;
    if (action == X52_HT_PAUSE) {
        ht_paused = !ht_paused;
        uint8_t cmd = HT_PAUSE_CMD;
#if CFG_TUD_CDC
        printf("X52 btn long -> HT %s\n", ht_paused ? "paused" : "resumed");
#endif
        queue_out_report((uint16_t)(ht_dev_addr << 8) | ht_instance, HT_REPORT_ID, &cmd, 1);
        x52_update_ht_display(x52_get_dev_addr(), last_ht_x, ht_paused, true);
    } else if (action == X52_HT_RESET) {
        uint8_t cmd = HT_RESET_CMD;
#if CFG_TUD_CDC
        printf("X52 btn short -> HT reset\n");
#endif
        queue_out_report((uint16_t)(ht_dev_addr << 8) | ht_instance, HT_REPORT_ID, &cmd, 1);
    }
}


static bool __no_inline_not_in_flash_func(manual_sof)(repeating_timer_t* rt) {
    pio_usb_host_frame();
    set_tick_pending();
    return true;
}

static repeating_timer_t sof_timer;

void extra_init() {
    pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
    pio_cfg.pin_dp = PICO_DEFAULT_PIO_USB_DP_PIN;
    pio_cfg.skip_alarm_pool = true;
    tuh_configure(BOARD_TUH_RHPORT, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);
    add_repeating_timer_us(-1000, manual_sof, NULL, &sof_timer);
}

uint32_t get_gpio_valid_pins_mask() {
    return GPIO_VALID_PINS_BASE & ~(
#ifdef PICO_DEFAULT_UART_TX_PIN
                                      (1 << PICO_DEFAULT_UART_TX_PIN) |
#endif
#ifdef PICO_DEFAULT_UART_RX_PIN
                                      (1 << PICO_DEFAULT_UART_RX_PIN) |
#endif
                                      (1 << PICO_DEFAULT_PIO_USB_DP_PIN) |
                                      (1 << (PICO_DEFAULT_PIO_USB_DP_PIN + 1)));
}

static bool reports_received;

void read_report(bool* new_report, bool* tick) {
    *tick = get_and_clear_tick_pending();

    reports_received = false;
    tuh_task();
    
    // Service device mode (CDC for debug output)
    tud_task();
    cdc_debug_task();
    
    // Process pending vendor control transfers
    process_vendor_control_transfers();

    // Update X52 uptime clock (sends vendor cmd only on minute change)
    x52_update_clock();

    // Poll X52 long-press timeout (independent of X52 report rate)
    handle_ht_action(x52_check_long_press(ht_dev_addr != 0));

    *new_report = reports_received;
}

void interval_override_updated() {
}

void flash_b_side() {
}

void descriptor_received_callback(uint16_t vendor_id, uint16_t product_id, const uint8_t* report_descriptor, int len, uint16_t interface, uint8_t hub_port, uint8_t itf_num) {
    parse_descriptor(vendor_id, product_id, report_descriptor, len, interface, itf_num);

    device_connected_callback(interface, vendor_id, product_id, hub_port);
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len) {
#if CFG_TUD_CDC
    printf("tuh_hid_mount_cb\n");
#endif

    uint8_t hub_addr;
    uint8_t hub_port;
    tuh_get_hub_addr_port(dev_addr, &hub_addr, &hub_port);

    uint16_t vid;
    uint16_t pid;
    tuh_vid_pid_get(dev_addr, &vid, &pid);

    tuh_itf_info_t itf_info;
    tuh_hid_itf_get_info(dev_addr, instance, &itf_info);
    uint8_t itf_num = itf_info.desc.bInterfaceNumber;

    descriptor_received_callback(vid, pid, desc_report, desc_len, (uint16_t) (dev_addr << 8) | instance, hub_port, itf_num);

    device_connected_callback((uint16_t) (dev_addr << 8) | instance, vid, pid, hub_port);

    x52_on_mount(dev_addr, vid, pid);

    // Set connection flags BEFORE on_mount calls so display updates
    // from g923_apply_defaults() see the correct connected state
    if (vid == HT_VENDOR_ID && pid == HT_PRODUCT_ID) {
        ht_dev_addr = dev_addr;
        ht_instance = instance;
        x52_set_ht_connected(true);
#if CFG_TUD_CDC
        printf("Head Tracker detected (addr=%d, inst=%d, itf_num=%d)\n", ht_dev_addr, ht_instance, itf_num);
#endif
    }

    if (vid == G923_VENDOR_ID && pid == G923_PID_XBOX) {
        x52_set_g923_connected(true);
    }

    if (vid == ATTACK3_VENDOR_ID && pid == ATTACK3_PRODUCT_ID) {
        attack3_dev_addr = dev_addr;
        x52_set_attack3_connected(true);
#if CFG_TUD_CDC
        printf("Attack 3 detected (addr=%d)\n", dev_addr);
#endif
    }

    // on_mount after connection flags so MFD display sees correct state
    g923_on_mount(dev_addr, instance, vid, pid);

    tuh_hid_receive_report(dev_addr, instance);
}

void umount_callback(uint8_t dev_addr, uint8_t instance) {
    device_disconnected_callback(dev_addr);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
#if CFG_TUD_CDC
    printf("tuh_hid_umount_cb\n");
#endif
    if (dev_addr == ht_dev_addr) {
        ht_dev_addr = 0;
        x52_set_ht_connected(false);
    }
    if (dev_addr == g923_get_dev_addr()) {
        x52_set_g923_connected(false);
    }
    if (dev_addr == attack3_dev_addr) {
        attack3_dev_addr = 0;
        x52_set_attack3_connected(false);
    }
    x52_on_unmount(dev_addr);
    g923_on_unmount(dev_addr);
    umount_callback(dev_addr, instance);
}

void report_received_callback(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
    if (len > 0) {
        handle_received_report(report, len, (uint16_t) (dev_addr << 8) | instance);

        reports_received = true;
    }
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
    // Apply shift mode throttle scaling before remapper sees the report
    if (dev_addr == x52_get_dev_addr()) {
        x52_apply_shift((uint8_t*)report, len);
    }

    report_received_callback(dev_addr, instance, report, len);


    // Update head tracker MFD display
    if (dev_addr == ht_dev_addr && len >= 4) {
        int16_t headX_centered = (int16_t)(report[2] | (report[3] << 8));
        x52_update_ht_display(x52_get_dev_addr(), headX_centered, ht_paused);
    }

    // Update Attack 3 Z-axis display
    // Z is byte 2 (bits 16-23), 0-255; expression:
    //   abs(z - 255) / 255 + 0.25  → scale 0.25 (z=255) to 1.25 (z=0)
    //   Display as percentage: (255-z)*100/255 + 25
    if (dev_addr == attack3_dev_addr && len >= 3) {
        uint8_t z_val = report[2];
        uint16_t scale_pct = (uint16_t)(((255 - z_val) * 100) / 255 + 25);
        x52_update_attack3_display(z_val, scale_pct);
    }

    // Process X52 reports (brightness, button state machine)
    if (dev_addr == x52_get_dev_addr()) {
        handle_ht_action(x52_process_report(report, len, ht_dev_addr != 0));
    }

    tuh_hid_receive_report(dev_addr, instance);
}

void tuh_midi_rx_cb(uint8_t dev_addr, uint32_t num_packets) {
    uint8_t hub_addr;
    uint8_t hub_port;
    tuh_get_hub_addr_port(dev_addr, &hub_addr, &hub_port);

    uint8_t buf[4];
    while (tuh_midi_packet_read(dev_addr, buf)) {
        handle_received_midi(hub_port, buf);
    }
    reports_received = true;
}

void queue_out_report(uint16_t interface, uint8_t report_id, const uint8_t* buffer, uint8_t len) {
    do_queue_out_report(buffer, len, report_id, interface >> 8, interface & 0xFF, OutType::OUTPUT);
}

void queue_set_feature_report(uint16_t interface, uint8_t report_id, const uint8_t* buffer, uint8_t len) {
    do_queue_out_report(buffer, len, report_id, interface >> 8, interface & 0xFF, OutType::SET_FEATURE);
}

void queue_get_feature_report(uint16_t interface, uint8_t report_id, uint8_t len) {
    do_queue_get_report(report_id, interface >> 8, interface & 0xFF, len);
}

void send_out_report() {
    do_send_out_report();
}

void __no_inline_not_in_flash_func(sof_callback)() {
}

void get_report_cb(uint8_t dev_addr, uint8_t interface, uint8_t report_id, uint8_t report_type, uint8_t* report, uint16_t len) {
    handle_get_report_response((uint16_t) (dev_addr << 8) | interface, report_id, report, len);
}

void set_report_complete_cb(uint8_t dev_addr, uint8_t interface, uint8_t report_id) {
    handle_set_report_complete((uint16_t) (dev_addr << 8) | interface, report_id);
}
