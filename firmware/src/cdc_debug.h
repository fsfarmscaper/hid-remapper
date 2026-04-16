#pragma once

#include <cstdarg>

/*
 * CDC Debug Output Handler
 * 
 * Provides USB CDC debug logging via the USB-C device port
 */

// Custom printf for CDC - formats and sends output via USB CDC
int cdc_debug_printf(const char* fmt, va_list va);

// Call this from main loop to service CDC
void cdc_debug_task(void);
