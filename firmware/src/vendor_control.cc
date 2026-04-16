#include <cstdio>
#include <cstring>

#include <tusb.h>

#include "vendor_control.h"

/*
 * Vendor Control Transfer Queue
 * 
 * This implementation follows the pattern established in out_report.cc
 * for queueing and processing asynchronous USB operations.
 */

struct vendor_control_request_t {
    uint8_t dev_addr;
    tusb_control_request_t setup;
    uint8_t data[64];
};

#define VENDOR_CONTROL_BUFSIZE 32
static vendor_control_request_t vendor_control_queue[VENDOR_CONTROL_BUFSIZE];
static uint8_t vcq_head = 0;
static uint8_t vcq_tail = 0;
static uint8_t vcq_items = 0;

static bool vendor_control_ready_flag = true;

static void vendor_control_xfer_complete(tuh_xfer_t* xfer) {
    (void)xfer;
    vendor_control_ready_flag = true;
}

bool queue_vendor_control_transfer(
    uint8_t dev_addr,
    uint8_t bRequest,
    uint16_t wValue,
    uint16_t wIndex,
    const uint8_t* data,
    uint16_t wLength,
    uint32_t timeout_ms
) {
    if (vcq_items == VENDOR_CONTROL_BUFSIZE) {
        printf("vendor_control queue overflow!\n");
        return false;
    }
    
    if (wLength > sizeof(vendor_control_queue[vcq_tail].data)) {
        printf("vendor_control data too large (%u > %zu)\n", wLength, sizeof(vendor_control_queue[vcq_tail].data));
        return false;
    }
    
    vendor_control_queue[vcq_tail].dev_addr = dev_addr;
    memset(&vendor_control_queue[vcq_tail].setup, 0, sizeof(tusb_control_request_t));
    vendor_control_queue[vcq_tail].setup.bmRequestType_bit.recipient = TUSB_REQ_RCPT_DEVICE;
    vendor_control_queue[vcq_tail].setup.bmRequestType_bit.type = TUSB_REQ_TYPE_VENDOR;
    vendor_control_queue[vcq_tail].setup.bmRequestType_bit.direction = TUSB_DIR_OUT;
    vendor_control_queue[vcq_tail].setup.bRequest = bRequest;
    vendor_control_queue[vcq_tail].setup.wValue = wValue;
    vendor_control_queue[vcq_tail].setup.wIndex = wIndex;
    vendor_control_queue[vcq_tail].setup.wLength = wLength;
    
    if (data != NULL && wLength > 0) {
        memcpy(vendor_control_queue[vcq_tail].data, data, wLength);
    }
    
    vcq_tail = (vcq_tail + 1) % VENDOR_CONTROL_BUFSIZE;
    vcq_items++;
    
    return true;
}

void process_vendor_control_transfers() {
    if ((vcq_items > 0) && vendor_control_ready_flag) {
        vendor_control_request_t* req = &(vendor_control_queue[vcq_head]);
        
        tuh_xfer_t xfer = {};
        xfer.daddr = req->dev_addr;
        xfer.ep_addr = 0;
        xfer.setup = &req->setup;
        xfer.buffer = req->setup.wLength > 0 ? req->data : NULL;
        xfer.complete_cb = vendor_control_xfer_complete;
        xfer.user_data = 0;
        
        if (tuh_control_xfer(&xfer)) {
            vendor_control_ready_flag = false;
            vcq_head = (vcq_head + 1) % VENDOR_CONTROL_BUFSIZE;
            vcq_items--;
        } else {
            printf("process_vendor_control: tuh_control_xfer failed!\n");
        }
    }
}

bool vendor_control_ready() {
    return vendor_control_ready_flag;
}
