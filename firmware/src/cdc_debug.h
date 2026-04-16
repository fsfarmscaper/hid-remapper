#pragma once

/*
 * CDC Debug Output Handler
 *
 * Redirects printf output to USB CDC via a pico-sdk stdio driver.
 */

// Initialize CDC stdio driver (call after stdio_init_all)
void cdc_debug_init(void);

// Drain buffered output to USB CDC (call from main loop after tud_task)
void cdc_debug_task(void);
