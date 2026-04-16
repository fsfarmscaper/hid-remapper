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

#define CDC_DEBUG_BUFFER_SIZE 256
static char cdc_buffer[CDC_DEBUG_BUFFER_SIZE];

/*
 * Custom printf for CDC - formats output and sends via USB CDC
 * Called by TinyUSB debug logging framework
 */
int cdc_debug_printf(const char* fmt, va_list va) {
    // Format the message
    int len = vsnprintf(cdc_buffer, sizeof(cdc_buffer), fmt, va);
    
    if (len > 0 && tud_cdc_connected()) {
        // Send to CDC if connected
        uint32_t written = tud_cdc_write(cdc_buffer, len);
        tud_cdc_write_flush();
        return written;
    }
    
    return len;
}

/*
 * TinyUSB Device Callbacks
 */

void tud_mount_cb(void) {
    printf("USB Device mounted\n");
}

void tud_umount_cb(void) {
    printf("USB Device unmounted\n");
}

void tud_suspend_cb(bool remote_wakeup_en) {
    printf("USB Device suspended\n");
}

void tud_resume_cb(void) {
    printf("USB Device resumed\n");
}

void tud_cdc_rx_cb(uint8_t itf) {
    // Handle any incoming CDC data if needed
    // For now, we're just using CDC for output only
    uint8_t buf[64];
    uint32_t count = tud_cdc_read(buf, sizeof(buf));
    // Could implement serial command handling here if desired
}

/*
 * Call this from your main loop to service CDC
 */
void cdc_debug_task(void) {
    if (tud_cdc_connected()) {
        // Process any pending writes
        tud_cdc_write_flush();
    }
}
