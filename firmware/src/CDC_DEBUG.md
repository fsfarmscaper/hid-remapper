# CDC Debug Console

The firmware includes a USB CDC (serial port) debug interface that
redirects all `printf()` output over USB. When enabled, the device
exposes an extra COM port (Windows) or `/dev/ttyACM*` (Linux).

Controlled by `CFG_TUD_CDC` in `tusb_config_both/tusb_config.h`.

## What it prints

`print_stats()` in `remapper.cc` outputs a line every second:

```
rx:<reports_received> tx:<reports_sent> proc_us:<processing_time_us>
```

Example: `rx:0 tx:0 proc_us:12345` means no HID reports in/out,
12345 µs spent in remapper processing during that 1-second window.

## Cost of leaving CDC enabled

- ~2.4 KB RAM (2 KB ring buffer + TinyUSB CDC buffers)
- 3 extra USB endpoints, 2 extra interfaces in the descriptor
- Host OS sees an extra COM port
- `cdc_debug_task()` runs each loop iteration (returns immediately
  if no terminal is connected)

## How to enable / disable

One-line change in `tusb_config_both/tusb_config.h`:

```c
#define CFG_TUD_CDC 1   // 1 = enabled (default), 0 = disabled
```

All other code is guarded by `#if CFG_TUD_CDC`:

| File | What's guarded |
|------|----------------|
| `cdc_debug.h` | Function declarations become empty inline stubs when 0 |
| `cdc_debug.cc` | Entire implementation compiled out when 0 |
| `tinyusb_stuff.cc` | CDC descriptor, interface count, and string descriptor |
| `remapper.cc` | Labeled stats output (`rx: tx: proc_us:`) vs bare numbers |
| `remapper_single.cc` | HID mount/unmount and X52 detection printf calls |
| `x52.cc` | Vendor command and MFD text debug printf calls |

No other files need changes. Calls to `cdc_debug_init()` and
`cdc_debug_task()` compile to nothing via the inline stubs.
