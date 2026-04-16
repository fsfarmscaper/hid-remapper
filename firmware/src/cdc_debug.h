#pragma once

/*
 * CDC Debug Output Handler
 *
 * Redirects printf output to USB CDC via a pico-sdk stdio driver.
 * Controlled by CFG_TUD_CDC in tusb_config.h (1 = enabled, 0 = disabled).
 */

#include <tusb.h>

#if CFG_TUD_CDC
// Initialize CDC stdio driver (call after stdio_init_all)
void cdc_debug_init(void);

// Drain buffered output to USB CDC (call from main loop after tud_task)
void cdc_debug_task(void);
#else
static inline void cdc_debug_init(void) {}
static inline void cdc_debug_task(void) {}
#endif
