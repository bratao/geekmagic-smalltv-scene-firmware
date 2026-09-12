# Independent efficiency review

Reviewed 2026-09-12, before OTA. No firmware upload was performed by this reviewer.

## Binary gate

The final image is 580,208 bytes, SHA-256 `2b829127427c2fcf8277d0abb2a7433532f3affbe2c4434b6f6eec000119d2d3`. The independent structural result is in `binary-efficient-review.json`: image length, CRC, segment checksums, DIO/4 MB/40 MHz settings, segment address bounds, filesystem/EEPROM layout, and 6,804 bytes of decoder tables remaining in flash all passed. These checks do not emulate the device.

The instrumented baseline was independently checked as 579,200 bytes, SHA-256 `75aebffe92d0d33cd92e829f5e46744eb205428801d98f6f01f3f827ec48030f`. Its portable scene rasterizer matched the previous committed source. Its scene wrapper differed only by the diagnostics include and the enabled render-counter hook. `SMALLTV_IDLE_WAIT=0` disabled the new wait in that baseline.

## Source and host checks

- Main-loop measurement is enabled at loop entry; timer reads are skipped while disabled. Work and cooperative waiting are separate unsigned, wrap-safe intervals. Native/static content waits 10 ms and actively playing GIFs wait 1 ms. Rescue handling is unchanged.
- Full invalidation and minute changes keep full-width output. A different pulsing node sharing a band unions its full width with the colon rectangle. Colon coordinates follow the same glyph advances, baseline, and horizontal clipping as rendering. Truncated clocks use the conservative path.
- A positive clipped colon rectangle fits the 0..240 coordinate range before conversion to the byte-sized band bounds. The renderer composes all overlapping nodes and computes full-row hashes before packing cropped output. Forward packing stays inside the current consecutive row run and does not overwrite later source rows.
- Resource collection is off at boot, bounded to 600 seconds, and stores counters in RAM. It samples heap once per second while enabled. The endpoint validates body size, types and duration; collection performs no flash logging. Timing is explicitly identified as cooperative wall time, not CPU utilization.
- Independently reran the AddressSanitizer host programs: 625 clock bounds cases, 2,040 row-packing cases, and diagnostics disabled/bounds/counters/sampling/expiry/reset/millisecond-rollover/stop cases passed.

No blocking source or binary findings remained at this revision. Hardware networking, actual display output and OTA recovery still require the separate on-device checks.

## Power interpretation

The pinned `esp12e` board already selects an 80 MHz CPU. Backlight GPIO 5 is held active; this revision does not change brightness or Wi-Fi sleep policy. Pure black LCD pixels do not disable that backlight.

In the pinned Arduino core 3.1.2, returning from `loop()` or calling `yield()` reschedules the application; a positive `delay()` arms a timer and suspends its continuation. This reduces repeated application polling, but is not evidence that the silicon sleeps for the entire reported wait interval. SDK work can occur during either measured interval.

[Arduino's Wi-Fi documentation](https://arduino-esp8266.readthedocs.io/en/3.1.2/esp8266wifi/generic-class.html) explains that the default listen interval wakes on each DTIM, whereas longer intervals can miss broadcasts. [Espressif's low-power guide](https://documentation.espressif.com/9b-esp8266-low_power_solutions__en.html) distinguishes modem sleep, which leaves the CPU running, from light sleep, which suspends it; the SDK determines actual sleep entry. Therefore the live `wifi_sleep_mode` value should be recorded, and datasheet chip currents must not be presented as measurements of this complete display.

The firmware has no temperature measurement in this experiment. A measured reduction in render time or transmitted pixels establishes less software/display work, not a measured reduction in enclosure temperature or electrical power.
