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

// Head Tracker (Arduino Nano ESP32)
#define HT_VENDOR_ID   0x2341
#define HT_PRODUCT_ID  0x8070
#define HT_RESET_CMD   0x01
#define HT_PAUSE_CMD   0x02
#define HT_REPORT_ID   2

// X52 MFD brightness wheel (byte 9, 0-255)
#define X52_BRIGHTNESS_BYTE 8

// X52 button detection (byte 10, bit 6 = HID Button 15)
#define X52_BTN_BYTE   9
#define X52_BTN_MASK   0x40
#define LONG_PRESS_MS  1000


static uint8_t ht_dev_addr = 0;
static uint8_t ht_instance = 0;
static uint8_t x52_dev_addr = 0;
static bool x52_btn_prev = false;
static uint32_t x52_btn_press_time = 0;
static bool x52_btn_handled = false;
static bool ht_paused = false;
static int16_t last_ht_x = 0;
static uint8_t last_mfd_brightness = 0xFF; // Invalid initial to force first update

static uint8_t last_report_copy[64] = {0};
static bool first_run = true;


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

    // If this is an X52 device, save address
    if (vid == X52_VENDOR_ID && (pid == X52_PRODUCT_ID_V1 || pid == X52_PRODUCT_ID_V2)) {
        x52_dev_addr = dev_addr;
        last_mfd_brightness = 0xFF; // Force brightness update on first report
        x52_init_clocks(x52_dev_addr);
        #if CFG_TUD_CDC
            printf("X52 detected (PID: 0x%04X)\n", pid);
        #endif
    }

    // Head Tracker detection
    if (vid == HT_VENDOR_ID && pid == HT_PRODUCT_ID) {
        ht_dev_addr = dev_addr;
        ht_instance = instance;
        #if CFG_TUD_CDC
            printf("Head Tracker detected (addr=%d, inst=%d)\n", ht_dev_addr, ht_instance);
        #endif
    }

    tuh_hid_receive_report(dev_addr, instance);
}

void umount_callback(uint8_t dev_addr, uint8_t instance) {
    device_disconnected_callback(dev_addr);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
#if CFG_TUD_CDC
    printf("tuh_hid_umount_cb\n");
#endif
    if (dev_addr == ht_dev_addr) { ht_dev_addr = 0; }
    if (dev_addr == x52_dev_addr) { x52_dev_addr = 0; x52_btn_prev = false; }
    umount_callback(dev_addr, instance);
}

void report_received_callback(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
    if (len > 0) {
        handle_received_report(report, len, (uint16_t) (dev_addr << 8) | instance);

        reports_received = true;
    }
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
    report_received_callback(dev_addr, instance, report, len);

    // Extract head tracker X for MFD display
    if (dev_addr == ht_dev_addr && len >= 12) {
        uint16_t twoBytes = report[2] | (report[3] << 8);
        uint16_t raw = twoBytes & 0x03FF;
        last_ht_x = (raw > 511) ? (int16_t)raw - 1024 : (int16_t)raw;
        x52_update_ht_display(x52_dev_addr, last_ht_x, ht_paused);
    }

    // Process X52 device reports
    if (dev_addr == x52_dev_addr) {

#if CFG_TUD_CDC
        printf("tuh_hid_report_received_cb: dev_addr=%u, len=%u\n", dev_addr, len);


        if (!first_run) {
            for (uint16_t i = 0; i < len; i++) {
                if (report[i] != last_report_copy[i]) {
                    // Log which byte changed and its new hex/dec value
                    printf("Diff at Byte [%d]: %d (0x%02X)\n", i, report[i], report[i]);
                }
            }
        }

        // Update the copy for the next comparison
        memcpy(last_report_copy, report, len);
        first_run = false;        
#endif

        // MFD brightness wheel (byte 8, 0-255 -> 0-128)
        if (len > X52_BRIGHTNESS_BYTE) {
            uint8_t brightness = report[X52_BRIGHTNESS_BYTE] >> 1;
            if (brightness != last_mfd_brightness) {
                last_mfd_brightness = brightness;
                x52_set_brightness(x52_dev_addr, true, brightness);
            }
        }

        if (ht_dev_addr != 0) {
            // X52 button: short press = reset, long press = pause toggle
            bool btn_now = (report[X52_BTN_BYTE] & X52_BTN_MASK) != 0;
            uint32_t now = to_ms_since_boot(get_absolute_time());

            if (btn_now && !x52_btn_prev) {
                // Button just pressed — start timer
                x52_btn_press_time = now;
                x52_btn_handled = false;
            }
            else if (btn_now && !x52_btn_handled) {
                // Button held — check for long press
                if (now - x52_btn_press_time >= LONG_PRESS_MS) {
                    ht_paused = !ht_paused;
                    uint8_t cmd = HT_PAUSE_CMD;
                    queue_out_report((uint16_t)(ht_dev_addr << 8) | ht_instance, HT_REPORT_ID, &cmd, 1);
                    x52_btn_handled = true;
                    #if CFG_TUD_CDC
                        printf("X52 btn long -> HT %s\n", ht_paused ? "paused" : "resumed");
                    #endif
                    x52_update_ht_display(x52_dev_addr, last_ht_x, ht_paused);
                }
            }
            else if (!btn_now && x52_btn_prev) {
                // Button released — short press if not already handled
                if (!x52_btn_handled) {
                    uint8_t cmd = HT_RESET_CMD;
                    queue_out_report((uint16_t)(ht_dev_addr << 8) | ht_instance, HT_REPORT_ID, &cmd, 1);
                    #if CFG_TUD_CDC
                        printf("X52 btn short -> HT reset\n");
                    #endif
                }
            }
            x52_btn_prev = btn_now;
        } // ht_dev_addr != 0
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
