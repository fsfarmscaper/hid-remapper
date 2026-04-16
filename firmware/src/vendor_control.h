#ifndef _VENDOR_CONTROL_H_
#define _VENDOR_CONTROL_H_

#include <stdint.h>

/*
 * USB HID Vendor Control Transfer Interface
 * 
 * This module provides functionality to send vendor-specific control transfers
 * to connected USB devices from the firmware acting as a USB host.
 */

/**
 * @brief Queue a vendor-specific control transfer to a connected USB device
 * 
 * @param dev_addr      Device address (from TinyUSB)
 * @param bRequest      Vendor-specific request code (0x00-0xFF)
 * @param wValue        Request value parameter
 * @param wIndex        Request index parameter
 * @param data          Pointer to data buffer (can be NULL for transfers with no data stage)
 * @param wLength       Length of data to transfer (0 for no data stage)
 * @param timeout_ms    Timeout in milliseconds (typically 1000)
 * 
 * @return true if queued successfully, false if queue is full
 */
bool queue_vendor_control_transfer(
    uint8_t dev_addr,
    uint8_t bRequest,
    uint16_t wValue,
    uint16_t wIndex,
    const uint8_t* data,
    uint16_t wLength,
    uint32_t timeout_ms
);

/**
 * @brief Process pending vendor control transfers
 * 
 * This should be called regularly (e.g., in the main loop) to send queued transfers.
 */
void process_vendor_control_transfers();

/**
 * @brief Check if vendor control queue is ready to send
 * 
 * @return true if no transfer is currently in progress
 */
bool vendor_control_ready();

#endif
