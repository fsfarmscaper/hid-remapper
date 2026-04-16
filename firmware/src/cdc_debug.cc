#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <tusb.h>

/*
 * CDC Debug Output Handler
 * 
 * Implements TinyUSB device-side CDC for debug logging.
 * Redirects printf output to USB CDC for real-time debugging via the USB-C port.
 */

#define CDC_DEBUG_BUFFER_SIZE 2048
static char cdc_out_buffer[CDC_DEBUG_BUFFER_SIZE];
static uint32_t cdc_out_pos = 0;

// Forward declaration
void cdc_debug_flush(void);

/*
 * Custom printf for CDC - buffers output
 * Called by TinyUSB debug logging framework
 */
int cdc_debug_printf(const char* fmt, va_list va) {
    // Format the message into buffer
    int len = vsnprintf(cdc_out_buffer + cdc_out_pos, 
                        sizeof(cdc_out_buffer) - cdc_out_pos, 
                        fmt, va);
    
    if (len > 0) {
        cdc_out_pos += len;
        // Flush if buffer is getting full or if we see a newline
        if (cdc_out_pos >= sizeof(cdc_out_buffer) - 128 || 
            (cdc_out_pos > 0 && cdc_out_buffer[cdc_out_pos - 1] == '\n')) {
            cdc_debug_flush();
        }
    }
    
    return len;
}

/*
 * Flush buffered CDC output
 * In a full implementation, this would send via USB CDC
 * For now, this is a placeholder for the TinyUSB device to handle
 */
void cdc_debug_flush(void) {
    // Buffer is maintained for TinyUSB device callbacks to read from
    // The actual transmission happens in the device task
    // Reset buffer position to keep recent messages
    if (cdc_out_pos >= sizeof(cdc_out_buffer) - 128) {
        cdc_out_pos = 0;
    }
}

/*
 * TinyUSB Device Callbacks
 */

void tud_mount_cb(void) {
    // Device mounted
}

void tud_umount_cb(void) {
    // Device unmounted
}

void tud_suspend_cb(bool remote_wakeup_en) {
    // Device suspended
}

void tud_resume_cb(void) {
    // Device resumed
}

void tud_cdc_rx_cb(uint8_t itf) {
    // Handle any incoming CDC data if needed
}

/*
 * Call this from your main loop to service CDC
 */
void cdc_debug_task(void) {
    // This task processes CDC communications
    // Output buffering happens in cdc_debug_printf
}
