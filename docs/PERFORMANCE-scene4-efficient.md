# Report-monitor performance — scene4-efficient

The optimized firmware was installed on a SmallTV-Ultra. The matched animated workload sent **94.76% fewer RGB565 bytes**, spent **85.93% less measured main-loop work time**, and reduced mean render time by **21.19%**. A static report reduced main-loop work time by **98.77%**, retaining zero screen transfers.

These are software-work and screen-traffic measurements. No thermometer or USB power meter was available: this report does **not** establish a temperature, wattage, or true CPU-utilization reduction. The always-on backlight remains unchanged.

## Matched device measurements

| Metric | Instrumented previous firmware | Optimized firmware | Change |
| --- | ---: | ---: | ---: |
| Animated capture | 60.000 s | 60.004 s | Same duration target |
| Main-loop iterations/s | 10,825.72 | 88.04 | −99.19% |
| Main-loop work / elapsed time | 76.30% | 10.74% | −85.93% relative |
| RGB565 bytes sent/s | 67,328.00 | 3,526.96 | −94.76% |
| Pixels sent/s | 33,664.00 | 1,763.48 | −94.76% |
| Render wall time per second | 127.15 ms | 98.20 ms | −22.77% |
| Mean render wall time | 12.72 ms | 10.02 ms | −21.19% |
| Render passes/s | 10.00 | 9.80 | −2.01% |
| Latest sampled free heap | 19,024 B | 18,936 B | −88 B |
| Native scene allocation | 7,528 B | 7,536 B | +8 B |
| Static capture | 15.000 s | 15.005 s | Same duration target |
| Static main-loop iterations/s | 12,451.07 | 97.77 | −99.21% |
| Static work / elapsed time | 73.03% | 0.90% | −98.77% relative |
| Static screen writes | 0 | 0 | Preserved |

Raw counters: [performance-captures.json](performance-captures.json). Independent calculations: [performance-measurement-review.md](performance-measurement-review.md).

## What made the difference

1. **Stop spinning between deadlines.** The old loop had no positive wait, even on a static screen. The new loop yields a bounded 10 ms to the SDK for native/static reports and 1 ms during GIF playback. In the static control, no rendering occurred in either capture, yet measured work fell from 73.03% to 0.90%. This is cooperative scheduling; idle time can still include SDK/radio activity.
2. **Write only the colon columns during a pulse.** Previously, a changed colon row sent all 240 pixels. Its actual glyph is 12 pixels wide: 95% fewer pixel bytes per changed row. Only bands intersecting the colon are rasterized. Full row hashes and composition remain for overlapping content. Minute changes, scene replacement and general pulsing nodes retain conservative bounds. The measured 60-second reduction is 94.76%, including clock changes and a slightly different render cadence.
3. **Keep the small compositor.** No full framebuffer, extra animation image, reduced resolution or large cache was added. The existing 240×8 RGB565 scratch band is packed in place for narrower transfers. The scene adds eight bytes for cumulative pixel/transfer counters; dirty bounds use small stack arrays.

The pulse follows absolute time with the same four-second period and 40 animation steps per cycle. Render cadence measured 9.80 rather than 10.00 passes/s because bounded waits introduce scheduling granularity. Minute changes remain checked before the animation limiter; the idle wait adds at most 10 ms of scheduling delay in an otherwise available loop. HTTP handlers, SDK work and SPI transfers can add latency, so this is not a hard real-time guarantee. No screen-clear operation was added.

## Method and limitations

Same device, native 240×240 resolution, twelve-node report, black background, clock font and pulse settings; same CPU clock (**80 MHz**) and SDK sleep mode (**2, MODEM_SLEEP**). Wi-Fi sleep policy and backlight brightness were unchanged. Both builds received identical commands except for a fresh fallback epoch. The initial full render was excluded. Each animated capture ran for a minute, followed by a static control replacing the clock with fixed text. Captures were started, left unpolled, and fetched once after automatic expiry. Counters were then cleared and disabled.

The original uninstrumented firmware was sampled first: approximately 33,659 pixels/s and 12.75 ms median last-render time, consistent with the instrumented baseline. Diagnostics have overhead. Neither work time nor idle time equals CPU cycles; SDK work outside the application loop is not counted. Render time overlaps work time and must not be added to it. Heap minima include request-boundary allocations. Short samples do not establish leak freedom or long-term stability.

A test-script encoding error was found when the user reported incorrect accents: Windows decoded the UTF-8 fixture as CP1252. Both matched captures used that same payload, so their commands remained identical. The script and fixture now explicitly use UTF-8, corrected text was resent, and the user confirmed the accents display correctly. Font source is unchanged from scene3-web. This was input corruption, not a firmware font change.

There was one matched capture per mode, not a statistical series or electrical/thermal experiment. Both optimizations were applied together; the static control supports the scheduling explanation, but there was no separate idle-only animated firmware ablation. Modem sleep leaves the CPU available; reducing application activity does not prove proportional power savings. Cooling requires measurement under comparable ambient conditions. Backlight power remains unchanged.

## Optional resource log

Disabled after boot. Enable a bounded **1–600 second** capture through the authenticated API. Fixed RAM counters sample heap once per second. No flash/EEPROM writes, serial spam, persistent history, or background timer is added. Disabled hooks check a flag without timer calls, heap sampling or allocations. The code and counters still occupy firmware/static RAM; disabled does not mean zero compiled bytes.

[API and field definitions](RESOURCE-DIAGNOSTICS.md). Set `SMALLTV_TOKEN` in the environment and save a capture on the computer:

```sh
python tools/profile_resources.py --url http://TV_IP --seconds 60 --output resource-capture.json
```

The tool saves JSON locally, stops collection on exit, and does not follow redirects or environment proxies. Automatic expiry protects against a disconnected monitor. Existing application logs are separate and were not enabled.

## Build and safety checks

- Version: `v1.5.0-smalltv-scene4-efficient`.
- Image: **580,208 bytes**; SHA256 `2b829127427c2fcf8277d0abb2a7433532f3affbe2c4434b6f6eec000119d2d3`.
- Static RAM **39,520 / 81,920 B**; application flash **576,063 / 1,044,464 B**.
- Versus scene3-web: +636 B static RAM and +5,024 B OTA image. Previous binary preserved in project recovery directory.
- Structural checks verified CRC, segment checksums, flash mode, memory bounds, unchanged LittleFS/EEPROM layout, and 6,804 bytes of decoder tables still in flash.
- Three agents reviewed rendering, diagnostics, scheduling and binary safety. ASan covered 625 glyph-bound cases, 2,040 packing cases, 160 full-frame comparisons (9,216,000 pixels), and diagnostics bounds, disabled behavior, cadence, expiry and rollover. Twelve browser-handler tests passed.
- Firmware-only OTA succeeded for the exact image. No filesystem flash, erase or Wi-Fi credential change. Captures began disabled after both boots and automatically expired. Web/recovery/OTA code paths were retained; deliberate corruption/recovery failure was not induced.

Host tests run the portable renderer and mocked diagnostics; they are **not full ESP8266 emulation**. Primary references: [Arduino ESP8266 3.1.2 Wi-Fi documentation](https://arduino-esp8266.readthedocs.io/en/3.1.2/esp8266wifi/generic-class.html) and [Espressif low-power guidance](https://documentation.espressif.com/9b-esp8266-low_power_solutions__en.html). See [independent source review](power-source-review.md) and [binary review](binary-efficient-review.json).

## Final device checks

After the UTF-8 correction, the user confirmed both Portuguese labels display correctly. On the final firmware, automatic web authorization, both saved Wi-Fi profiles, and asynchronous scan (202 then 200, 20 results) passed. Invalid diagnostic durations/types returned 400, oversized input 413, and invalid authorization 401. Animation frames and uptime continued advancing. Diagnostics remained off during these checks. The optional PC logging tool was exercised with a one-second capture and automatic expiry.

A pooled HTTP test encountered a peer-closed connection without a device reset. Repeating the checks with fresh HTTP connections passed; 21 status requests measured a median 219 ms and maximum 2,094 ms. These include Wi-Fi/network effects and are not a claim of sub-10-ms API responses. This short test cannot establish long-term network reliability. Evidence: [efficient-live-validation.json](efficient-live-validation.json).
