#include <cstdio>
#include <cstring>

#include <tusb.h>
#include "pico/time.h"

#include "out_report.h"

struct outgoing_out_report_t {
    uint8_t dev_addr;
    uint8_t interface;
    uint8_t report_id;
    uint16_t len;
    OutType type;
    uint8_t report[64];  // XXX
};

#define OOR_BUFSIZE 8
static outgoing_out_report_t outgoing_out_reports[OOR_BUFSIZE];
static uint8_t oor_head = 0;
static uint8_t oor_tail = 0;
static uint8_t oor_items = 0;

static uint8_t get_buffer[64];

static bool ready_to_send = true;
static absolute_time_t last_send_time;
#define SEND_TIMEOUT_MS 1000

static absolute_time_t retry_after_time;
static bool retry_pending = false;
#define RETRY_DELAY_MS 50

void do_queue_out_report(const uint8_t* report, uint16_t len, uint8_t report_id, uint8_t dev_addr, uint8_t interface, OutType type) {
    // TODO: PA Buffer overflow handling — currently just drops new reports when full, so needs rate limiting on the caller side to avoid silent drops. Could return false here and let caller decide whether to retry or drop. For GET_REPORT, caller must retry since there's no report data in the queue.
    if (oor_items == OOR_BUFSIZE) {
        printf("out overflow!\n");
        return;
    }
    if ((len + ((report_id != 0) ? 1 : 0)) > sizeof(outgoing_out_reports[oor_tail].report)) {
        return;
    }
    outgoing_out_reports[oor_tail].dev_addr = dev_addr;
    outgoing_out_reports[oor_tail].interface = interface;
    outgoing_out_reports[oor_tail].report_id = report_id;
    outgoing_out_reports[oor_tail].len = len + ((report_id != 0) ? 1 : 0);
    outgoing_out_reports[oor_tail].type = type;
    if (report_id != 0) {
        outgoing_out_reports[oor_tail].report[0] = report_id;
    }
    memcpy(outgoing_out_reports[oor_tail].report + ((report_id != 0) ? 1 : 0), report, len);
    oor_tail = (oor_tail + 1) % OOR_BUFSIZE;
    oor_items++;
}

void do_queue_get_report(uint8_t report_id, uint8_t dev_addr, uint8_t interface, uint8_t len) {
    if (oor_items == OOR_BUFSIZE) {
        printf("out overflow!\n");
        return;
    }
    outgoing_out_reports[oor_tail].dev_addr = dev_addr;
    outgoing_out_reports[oor_tail].interface = interface;
    outgoing_out_reports[oor_tail].report_id = report_id;
    outgoing_out_reports[oor_tail].type = OutType::GET_FEATURE;
    outgoing_out_reports[oor_tail].len = len;
    oor_tail = (oor_tail + 1) % OOR_BUFSIZE;
    oor_items++;
}

void do_send_out_report() {
    // Recover from stuck transfers (completion callback never fired)
    if (!ready_to_send && time_reached(last_send_time)) {
        printf("out_report: send timeout, recovering\n");
        ready_to_send = true;
    }

    if ((oor_items > 0) && ready_to_send) {
        // Back off after a failed send to avoid hammering the USB stack
        if (retry_pending && !time_reached(retry_after_time)) {
            return;
        }
        retry_pending = false;

        outgoing_out_report_t* out = &(outgoing_out_reports[oor_head]);
        if ((out->type == OutType::OUTPUT) || (out->type == OutType::SET_FEATURE)) {
            bool ok = tuh_hid_set_report(out->dev_addr, out->interface, out->report_id, (out->type == OutType::OUTPUT) ? HID_REPORT_TYPE_OUTPUT : HID_REPORT_TYPE_FEATURE, out->report, out->len);
#if CFG_TUD_CDC
            printf("set_report(addr=%d,inst=%d,rid=%d,len=%d): %s\n",
                   out->dev_addr, out->interface, out->report_id, out->len, ok ? "OK" : "FAIL");
#endif
            if (ok) {
                ready_to_send = false;
                last_send_time = make_timeout_time_ms(SEND_TIMEOUT_MS);
                oor_head = (oor_head + 1) % OOR_BUFSIZE;
                oor_items--;
            } else {
                retry_pending = true;
                retry_after_time = make_timeout_time_ms(RETRY_DELAY_MS);
            }
        } else if (out->type == OutType::GET_FEATURE) {
            if (tuh_hid_get_report(out->dev_addr, out->interface, out->report_id, HID_REPORT_TYPE_FEATURE, get_buffer, out->len)) {
                ready_to_send = false;
                last_send_time = make_timeout_time_ms(SEND_TIMEOUT_MS);
                oor_head = (oor_head + 1) % OOR_BUFSIZE;
                oor_items--;
            }
        }
    }
}

void tuh_hid_set_report_complete_cb(uint8_t dev_addr, uint8_t instance, uint8_t report_id, uint8_t report_type, uint16_t len) {
    ready_to_send = true;
    set_report_complete_cb(dev_addr, instance, report_id);
}

void tuh_hid_get_report_complete_cb(uint8_t dev_addr, uint8_t idx, uint8_t report_id, uint8_t report_type, uint16_t len) {
    ready_to_send = true;
    get_report_cb(dev_addr, idx, report_id, report_type, get_buffer, len);
}
