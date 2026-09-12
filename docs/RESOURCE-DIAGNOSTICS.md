# Optional resource diagnostics

Resource collection is disabled after every boot. Enabling it records fixed-size aggregate counters in RAM for 1–600 seconds (default 60), then stops automatically. It does not write files, EEPROM, serial logs, or a persistent sample history. Results remain available until reset, a new capture, or reboot. Save JSON responses on the monitoring computer when a permanent report is needed.

Disabled instrumentation checks a boolean and performs no timer calls, heap sampling, or allocations. Enabled collection samples heap once per second and accumulates integer counters. Generating an explicitly requested JSON response allocates temporary response storage; repeated polling can influence measurements. Prefer starting a capture, waiting for it to expire, and then fetching the result once.

## API

All endpoints require the device's bearer token. The local web interface can obtain its configured token through its existing automatic login mechanism; diagnostics do not change authentication.

`POST /api/v1/diagnostics/resources` starts a fresh capture:

```json
{"enabled": true, "duration_s": 60}
```

Starting again discards the previous capture. The duration is an integer from 1 through 600; omitting it selects 60. A valid request returns HTTP 200 with `{"status":"ok"}`. Invalid bodies return 400; bodies larger than 256 bytes return 413.

Stop early, preserving the counters:

```json
{"enabled": false}
```

`GET /api/v1/diagnostics/resources` returns the current or completed capture. `DELETE /api/v1/diagnostics/resources` stops collection and clears it, returning `{"status":"reset"}`.

| Fields | Meaning |
| --- | --- |
| `enabled`, `has_capture`, `expired` | Collection active, results exist, and capture ended at its duration limit. |
| `duration_limit_s`, `elapsed_ms` | Requested limit and measured capture wall time. Expiry is checked at loop completion, so a long loop can overshoot the limit. |
| `loops`, `work_us`, `idle_us` | Completed instrumented loops, their cooperative work interval, and their explicit idle interval. |
| `max_work_us`, `max_loop_us` | Longest measured work interval and work-plus-idle interval. |
| `cooperative_work_pct` | `work_us / (elapsed_ms * 10)`. An activity indicator, not hardware CPU utilization. |
| `render_calls`, `render_us`, `max_render_us` | Native-scene render passes with dirty bands, their cumulative wall time, and longest pass. A pass can find unchanged pixels and transmit nothing. |
| `pixels_written`, `rgb565_bytes` | Native-scene pixels actually submitted to the display, and twice that count. Excludes SPI commands and non-scene drawing/GIF transfers. |
| `heap_start`, `heap_latest`, `heap_min` | First, latest, and lowest periodically sampled free heap, in bytes. The start sample occurs in the start-request handler and includes its temporary memory. |
| `max_block_min`, `fragmentation_max_pct`, `heap_samples` | Smallest observed largest free block, highest observed fragmentation, and sample count. These are periodic observations, not continuous allocator peaks. |
| `cpu_mhz`, `wifi_sleep_mode` | Device CPU clock and ESP8266 SDK sleep-mode enum when the GET request is handled. |
| `timing_kind` | Explicitly identifies `cooperative_wall_time_not_cpu_utilization`. |

## Interpreting comparisons

The work interval includes synchronous web handlers, network/display work, and any interrupts or SDK processing that occur during that interval. The explicit idle interval can also execute SDK work. Neither interval is a direct measure of CPU active cycles, physical sleep, power draw, or temperature, and neither provides a strict bound for actual CPU utilization. Render time overlaps work time: do not add them together.

The Arduino/SDK work between calls to the application loop is outside these intervals. Diagnostic bookkeeping itself is also outside the reported work interval. Therefore, `work_us + idle_us` need not equal elapsed wall time. An API request that starts or restarts a capture introduces a small boundary discrepancy; use sustained captures rather than sub-second readings.

Compare the same native scene and animation settings, similar Wi-Fi conditions, equal capture durations, and the same request load. Report transferred RGB565 bytes per second, render wall time, loop activity, heap minima, and response latency separately. Distinguish warm-up/full-screen uploads and minute changes from steady animation. A reduction in display transfers or polling proves less application work; claims about heat or electrical power require external temperature or power measurements.

Existing application logs and compile-time heap logging are separate. This resource collector does not enable them and does not retain private report text, Wi-Fi credentials, or tokens in its counters.
