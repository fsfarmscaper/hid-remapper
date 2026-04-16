#include <cstring>

#include <tusb.h>
#include <pico/stdio.h>
#include <pico/stdio/driver.h>

#include "cdc_debug.h"

/*
 * CDC Debug Output Handler
 *
 * Registers a pico-sdk stdio driver that buffers printf output
 * into a ring buffer. cdc_debug_task() drains the buffer to
 * USB CDC when a host terminal is connected.
 */

#define CDC_RING_SIZE 2048
static char cdc_ring[CDC_RING_SIZE];
static volatile uint32_t cdc_head = 0;  // write position (producer: printf context)
static volatile uint32_t cdc_tail = 0;  // read position  (consumer: cdc_debug_task)

static void cdc_out_chars(const char *buf, int len) {
    for (int i = 0; i < len; i++) {
        uint32_t next = (cdc_head + 1) % CDC_RING_SIZE;
        if (next != cdc_tail) {
            cdc_ring[cdc_head] = buf[i];
            cdc_head = next;
        }
        // else: ring full, drop character
    }
}

static stdio_driver_t cdc_stdio;

void cdc_debug_init(void) {
    memset(&cdc_stdio, 0, sizeof(cdc_stdio));
    cdc_stdio.out_chars = cdc_out_chars;
    stdio_set_driver_enabled(&cdc_stdio, true);
}

void cdc_debug_task(void) {
    if (!tud_cdc_connected()) return;

    uint32_t avail = tud_cdc_write_available();
    while (avail > 0 && cdc_tail != cdc_head) {
        tud_cdc_write_char(cdc_ring[cdc_tail]);
        cdc_tail = (cdc_tail + 1) % CDC_RING_SIZE;
        avail--;
    }
    tud_cdc_write_flush();
}
