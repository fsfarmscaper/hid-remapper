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

#define CDC_DEBUG_BUFFER_SIZE 512
static char cdc_out_buffer[CDC_DEBUG_BUFFER_SIZE];
static uint32_t cdc_out_pos = 0;

/*
 * Custom printf for CDC - buffers output and flushes in main task
 * Called by TinyUSB debug logging framework
 */
int cdc_debug_printf(const char* fmt, va_list va) {
    // Format the message
    int len = vsnprintf(cdc_out_buffer + cdc_out_pos, 
                        sizeof(cdc_out_buffer) - cdc_out_pos, 
                        fmt, va);
    
    if (len > 0) {
        cdc_out_pos += len;
        // Flush if buffer is getting full or if we see a newline
        if (cdc_out_pos >= sizeof(cdc_out_buffer) - 64 || 
            (cdc_out_pos > 0 && cdc_out_buffer[cdc_out_pos - 1] == '\n')) {
            cdc_debug_flush();
        }
    }
    
    return len;
}

/*
 * Flush buffered CDC output
 */
void cdc_debug_flush(void) {
    if (cdc_out_pos > 0 && tud_ready()) {
        // Try to send the buffer
        uint32_t available = tud_cdc_write_available();
        if (available > 0) {
            uint32_t to_send = (cdc_out_pos < available) ? cdc_out_pos : available;
            tud_cdc_write(cdc_out_buffer, to_send);
            
            // Shift remaining data
            if (to_send < cdc_out_pos) {
                memmove(cdc_out_buffer, cdc_out_buffer + to_send, cdc_out_pos - to_send);
            }
            cdc_out_pos -= to_send;
        }
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
    // For now, we're just using CDC for output only
}

/*
 * Call this from your main loop to service CDC
 */
void cdc_debug_task(void) {
    cdc_debug_flush();
}
