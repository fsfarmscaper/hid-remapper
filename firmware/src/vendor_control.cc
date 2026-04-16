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
    uint8_t data[64];  // XXX: Adjust size if needed for larger transfers
};

#define VENDOR_CONTROL_BUFSIZE 8
static vendor_control_request_t vendor_control_queue[VENDOR_CONTROL_BUFSIZE];
static uint8_t vcq_head = 0;
static uint8_t vcq_tail = 0;
static uint8_t vcq_items = 0;

static bool vendor_control_ready_flag = true;

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
    
    printf("queue_vendor_control: dev_addr=%u, bRequest=0x%02x, wValue=0x%04x, wIndex=0x%04x, wLength=%u\n", 
           dev_addr, bRequest, wValue, wIndex, wLength);
    
    vendor_control_queue[vcq_tail].dev_addr = dev_addr;
    vendor_control_queue[vcq_tail].setup = {
        .bmRequestType_bit = {
            .recipient = TUSB_REQ_RCPT_DEVICE,
            .type = TUSB_REQ_TYPE_VENDOR,
            .direction = TUSB_DIR_OUT  // Host to Device (vendor OUT request)
        },
        .bRequest = bRequest,
        .wValue = wValue,
        .wIndex = wIndex,
        .wLength = wLength
    };
    
    if (data != NULL && wLength > 0) {
        memcpy(vendor_control_queue[vcq_tail].data, data, wLength);
    }
    
    vcq_tail = (vcq_tail + 1) % VENDOR_CONTROL_BUFSIZE;
    vcq_items++;
    
    printf("vendor_control queue: %d items queued\n", vcq_items);
    return true;
}

void process_vendor_control_transfers() {
    if ((vcq_items > 0) && vendor_control_ready_flag) {
        vendor_control_request_t* req = &(vendor_control_queue[vcq_head]);
        
        printf("process_vendor_control: sending transfer for dev_addr=%u, wLength=%u\n", 
               req->dev_addr, req->setup.wLength);
        
        // Build the transfer request using the new tuh_control_xfer API
        tuh_xfer_t xfer = {};
        xfer.daddr = req->dev_addr;
        xfer.ep_addr = 0;
        xfer.setup = &req->setup;
        xfer.buffer = req->setup.wLength > 0 ? req->data : NULL;
        xfer.buflen = req->setup.wLength;
        // Note: complete_cb is in a union with buffer/buflen, so we don't set it here.
        // TinyUSB will invoke the global tuh_xfer_cb callback for all transfers.
        xfer.user_data = 0;
        
        // Send the control transfer
        if (tuh_control_xfer(&xfer)) {
            printf("process_vendor_control: transfer submitted successfully\n");
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

/*
 * Callback for control transfer completion
 * This is called by TinyUSB when a control transfer completes
 */
void tuh_xfer_cb(uint8_t daddr, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes) {
    // Only process control transfers (ep_addr == 0)
    if (ep_addr == 0) {
        switch (result) {
            case XFER_RESULT_SUCCESS:
                printf("tuh_xfer_cb: control transfer completed successfully (%lu bytes)\n", xferred_bytes);
                break;
            case XFER_RESULT_FAILED:
                printf("tuh_xfer_cb: control transfer failed\n");
                break;
            case XFER_RESULT_STALLED:
                printf("tuh_xfer_cb: control transfer stalled\n");
                break;
            case XFER_RESULT_TIMEOUT:
                printf("tuh_xfer_cb: control transfer timeout\n");
                break;
            default:
                printf("tuh_xfer_cb: control transfer result %d\n", result);
        }
        
        vendor_control_ready_flag = true;
    }
}
