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
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
    uint32_t timeout_ms;
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
    
    vendor_control_queue[vcq_tail].dev_addr = dev_addr;
    vendor_control_queue[vcq_tail].bRequest = bRequest;
    vendor_control_queue[vcq_tail].wValue = wValue;
    vendor_control_queue[vcq_tail].wIndex = wIndex;
    vendor_control_queue[vcq_tail].wLength = wLength;
    vendor_control_queue[vcq_tail].timeout_ms = timeout_ms;
    
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
        
        // Build the control request
        tusb_control_request_t ctrl_req = {
            .bmRequestType_bit = {
                .recipient = TUSB_REQ_RCPT_DEVICE,
                .type = TUSB_REQ_TYPE_VENDOR,
                .direction = TUSB_DIR_OUT  // Host to Device (vendor OUT request)
            },
            .bRequest = req->bRequest,
            .wValue = req->wValue,
            .wIndex = req->wIndex,
            .wLength = req->wLength
        };
        
        // Send the control transfer
        // For transfers with data, pass the data pointer; for transfers without data, pass NULL
        if (tuh_control_xfer(req->dev_addr, &ctrl_req, req->wLength > 0 ? req->data : NULL, req->timeout_ms)) {
            vendor_control_ready_flag = false;
            vcq_head = (vcq_head + 1) % VENDOR_CONTROL_BUFSIZE;
            vcq_items--;
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
        if (result == XFER_RESULT_SUCCESS) {
            printf("Vendor control transfer completed successfully (%lu bytes)\n", xferred_bytes);
        } else if (result == XFER_RESULT_FAILED) {
            printf("Vendor control transfer failed\n");
        } else if (result == XFER_RESULT_STALLED) {
            printf("Vendor control transfer stalled\n");
        } else if (result == XFER_RESULT_TIMEOUT) {
            printf("Vendor control transfer timeout\n");
        }
        
        vendor_control_ready_flag = true;
    }
}
