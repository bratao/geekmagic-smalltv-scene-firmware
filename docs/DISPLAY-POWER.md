# Automatic display power saving

Scene5-idle switches off the SmallTV-Ultra backlight (GPIO 5, active low) and sends the ST7789 sleep-in command after **600,000 milliseconds without an incoming HTTP request**. Native-scene and GIF rendering pause. The ESP8266 and Wi-Fi remain available; this is not deep sleep or a device shutdown.

The next parsed request sends sleep-out, waits the controller's 120 ms wake delay, and restores the backlight. Requests to pages, drawing APIs, status APIs, unknown routes, and unauthorized routes all count. Authorization requirements for the actual action remain unchanged. Local animations, clock ticks, and outgoing NTP synchronization do not count as incoming activity.

Scenes remain in RAM. Their local clock catches up on the next update. GIF playback timing is rebased on wake so ten minutes of sleep does not trip the decoder's per-file duration limit. No EEPROM or filesystem writes are introduced by the timer. It uses unsigned elapsed-time arithmetic across the 32-bit millis rollover.

The normal firmware loop checks the deadline; network operations that block that loop can delay the transition. Rescue mode deliberately retains its recovery screen. Optional custom builds enabling the legacy PC metrics dashboard are outside this release's validation scope.

## Status

`GET /api/v1/display/power` requires the normal Bearer token and returns:

```json
{
  "awake": true,
  "idle_timeout_ms": 600000,
  "idle_ms": 3,
  "sleep_count": 1,
  "request_wakes_display": true
}
```

The request itself wakes the display and resets idle time. `sleep_count` is cumulative since boot and makes an idle transition observable after wake. Do not poll this endpoint to wait for sleep: polling intentionally prevents it. Power savings in watts have not been measured.

## Verification

`tests/idle_timer_test.cpp` covers the exact 600,000 ms boundary, no repeated transitions, activity reset, wake state, and millis rollover. Compile with assertions enabled from a Visual Studio x64 developer shell:

```bat
cl /nologo /std:c++17 /EHsc /Iinclude tests\idle_timer_test.cpp /Fe:idle_timer_test.exe
idle_timer_test.exe
```

The release also passes the pinned ESP8266 build, binary checksum/layout/flash-table review in `binary-idle-review.json`, and 12 web regression tests. Host tests do not verify physical LCD output.

For a device test, stop publishing clients, close polling browser tabs, submit a scene, record the power counter, and send no requests for at least 610 seconds. Then request power status once: the counter should increase and `awake` should be true. Check retained scene status and confirm the physical display is visible. Raw device evidence is recorded separately when the test completes.

## Device result — 2026-09-16

Firmware-only OTA accepted the exact 581,472-byte scene5-idle image. The first 610-second observation did not enter sleep because another client continued sending scenes (revision reached 309). The user closed that sender. The repeated timed process was interrupted, but the later follow-up returned `sleep_count: 1`, `awake: true`, and an active retained scene with approximately 61 minutes of uptime. This confirms a recorded sleep transition and an awake, responsive display state after the request. It does not measure the exact physical shutoff time, backlight current, or prove visually correct pixels. The exact timeout boundary is covered by the host timer test.

Raw result: [idle-live-validation.json](idle-live-validation.json).
