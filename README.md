# GeekMagic SmallTV Scene Firmware

### Tiny screen. Native intelligence. A whole new way to build desktop displays.

Turn a **240×240 SmallTV-Ultra** into a programmable, always-ready display for calendars, focus timers, notifications, home automation, and live dashboards. Send a compact JSON scene once: the ESP8266 takes care of crisp text, a local clock, and animation—without uploading a new image for every tick.

**Smooth updates · Native resolution · Local rendering · Automatic screen sleep · Open source**

[Get the firmware](artifacts/firmware.bin) · [Try the Drawing API](docs/API.md) · [See measured improvements](docs/PERFORMANCE-scene4-efficient.md) · [Build it yourself](#building)

## Small hardware. Big improvements.

| Upgrade | What you get |
| --- | --- |
| **A scene engine on an ESP8266** | Retained text, shapes, and clocks with a 3,840-byte compositing buffer—no full-screen framebuffer required. |
| **Draw only what changes** | Dirty regions and cropped colon transfers keep updates smooth at the full native 240×240 resolution. |
| **A clock that lives on the TV** | Minute changes and animation continue locally, without a network round trip for each tick. |
| **Sleep when nobody needs it** | After 10 minutes without an HTTP request, the backlight and LCD controller switch off. The next request wakes the screen. |
| **Readable, multilingual text** | Antialiased Noto Sans, Portuguese accents, five sizes, clipping, and automatic ellipsis. |
| **Less work, more headroom** | 6,804 bytes moved from RAM to flash; bounded rendering and optional, self-expiring diagnostics. |
| **An easier device to live with** | Three Wi-Fi profiles, asynchronous connection management, automatic local-web authorization, and firmware-only updates. |

The scene4 benchmark measured **94.76% fewer transmitted RGB565 bytes** and **85.93% less main-loop work time** for its animated report; the static report measured **98.77% less main-loop work time**. These are workload-specific transfer and cooperative timing results—not measured electrical power, temperature, or CPU utilization. [Read the methodology and limits](docs/PERFORMANCE-scene4-efficient.md).

This fork builds on [Times-Z/GeekMagic-Open-Firmware](https://github.com/Times-Z/GeekMagic-Open-Firmware), v1.5.0, commit `9d31738bd653ca69b2fae979fec31bce9d20664f`, with traditional drawing commands adapted from [HoloClawd](https://github.com/andrewjiang/HoloClawd-Open-Firmware). Credit to those projects for the foundation that makes these improvements possible.

This repository contains firmware, build tools, documentation, and release images. Calendar and automation integrations are clients you connect to it; no Google account or cloud service is built in.

## New in scene5: automatic display sleep

Leave the screen unattended and it switches off after **600,000 ms without an incoming HTTP request**. Wi-Fi stays connected, so the next request—including a draw command—turns it back on. Scene/GIF updates pause while sleeping; retained content remains available. Internal clock ticks, GIF frames, and NTP traffic do not keep it awake.

Every parsed HTTP request counts, including web pages, status checks, and rejected requests. A polling browser or an application sending updates every two minutes will therefore keep it awake. Close polling pages and pause your client to let the timer expire. This is display power saving, not ESP deep sleep; electrical savings have not been measured.

`GET /api/v1/display/power` reports the 10-minute timeout and cumulative `sleep_count`. **Reading it wakes the screen**, so check the counter after an idle test rather than polling during it. The timeout is fixed in this release. Device validation recorded a completed sleep cycle and an awake, active scene after the next check. [Behavior and verification](docs/DISPLAY-POWER.md).

## What's changed

- **Native scenes:** compact JSON describes text, a clock, and shapes. The device retains the scene and animates it without a new request for each frame.
- **No blank screen between updates:** offscreen composition in 240×8 strips; changed rows are transmitted without clearing the screen. Clock-colon animation now crops SPI transfers horizontally to its native-pixel bounds.
- **Autonomous clock:** uses NTP, with a supplied epoch as a fallback. Minute changes are checked before the animation rate limiter.
- **Smooth pulsing:** 40 intensity levels, a configurable period, and a 100 ms update interval. The example uses a four-second cycle without abrupt blinking.
- **Readable text:** antialiased 4 bpp Noto Sans, five sizes, UTF-8 support including Portuguese characters, width clipping, and ellipsis truncation.
- **HoloClawd Drawing API:** nine individual primitives and traditional batch drawing, plus the optimized scene mode. Drawing routes require Bearer authentication.
- **GIF playback without mandatory clearing:** `keep_screen:true` for stop/play. The final GIF frame's timing is also fixed.
- **Lower RAM usage:** 6,804 bytes of YCbCr tables moved from DRAM to flash while preserving signed values and color conversion.
- **Lower polling overhead:** a bounded 10 ms cooperative wait between native/static report loops, or 1 ms during GIF playback, gives the SDK time between display deadlines. CPU frequency and Wi-Fi sleep policy are unchanged.
- **Optional resource capture:** disabled by default, fixed RAM counters for loop work/idle timing, rendering, transmitted pixels, and once-per-second heap sampling. Captures expire automatically; no diagnostic files or serial output. Existing status diagnostics remain available.
- **Three saved Wi-Fi profiles:** edit priority order without reconnecting; explicitly connect using saved credentials. Scan and connection requests are asynchronous.
- **Automatic web authorization:** the browser loads the TV's saved token automatically, including after an IP change; the token page prefills it masked. Explicit token changes still persist across reboot.
- **Firmware-delivered Wi-Fi/token pages:** updated management assets are embedded, so this upgrade needs only the firmware image, not a LittleFS upload.

Preserved: web interface, Wi-Fi/AP, NTP, LittleFS, configuration, authentication, GIF, OTA, and RescueMode. Disabled in this build: the PC CPU/GPU metrics dashboard and periodic heap logging; API diagnostics remain available.

## Hardware and limits

| Item | Configuration |
| --- | --- |
| Validated device | SmallTV-Ultra |
| MCU / environment | ESP8266 ESP-12E / `esp12e` |
| LCD | ST7789, **native 240×240** |
| Flash | **4 MB, DIO, 40 MHz** |
| Linker | `eagle.flash.4m2m.ld` |
| Static RAM, scene4-efficient | **39,520 / 81,920 bytes** |
| Application flash, scene4-efficient | **576,063 / 1,044,464 bytes** |
| OTA image, scene4-efficient | **580,208 bytes** |
| Scene state on the heap | **7,536 bytes**, allocated only while active |
| Compositing buffer | **3,840 bytes** |

The previous scene1 build reduced static RAM to 37,464 bytes; scene2-wifi added networking and storage changes; scene3-web used 38,884 bytes, and scene4-efficient uses 39,520 bytes (+636). Scene state grew by 8 bytes for cumulative transfer counters. Available runtime heap is a separate metric: the approximately **22 KB** physical measurement belongs to scene1, not the new release.

A full RGB565 framebuffer would require 115,200 bytes. Instead, the renderer uses strips, per-row hashes, and grouped transfers. Colon-only animation retains full native resolution while sending a narrow horizontal crop; minute changes and general scene changes retain the full affected-row path. Scene replacement is not an atomic framebuffer swap: a large change may appear progressively during the SPI transfer. The recommended background is **pure black `#000000`**; resolution is never reduced. The example reserves 5 px at the top and 10 px at the bottom.

## Building

Requirements: Python 3.11+ and internet access for the initial tool installation. Neither Poetry nor the calendar application is required.

```sh
git clone https://github.com/bratao/geekmagic-smalltv-scene-firmware.git
cd geekmagic-smalltv-scene-firmware
python -m venv .venv
```

Activate the environment:

```powershell
# Windows PowerShell
.\.venv\Scripts\Activate.ps1
```

```sh
# Linux / macOS
source .venv/bin/activate
```

Install the pinned versions and build:

```sh
python -m pip install -r requirements-build.txt
python tools/build.py
```

Output: `.pio/build/esp12e/firmware.bin` and `firmware.elf`. The script **does not flash the device**. It resolves dependencies, applies the SHA256-verified GFX patch, generates fonts, and performs a clean build. Do not skip the patch by running only `pio run` on a fresh installation: doing so would lose the RAM savings.

Versions: PlatformIO 6.2.0; espressif8266 platform 4.2.1; Arduino Core 3.1.2; ArduinoJson 7.4.3; Arduino_GFX 1.6.7; AnimatedGIF 2.2.3; Pillow 12.3.0. The current source version is `v1.5.0-smalltv-scene5-idle` in `firmware_version.txt`. The packaged image and structural review below match this source version. Physical checks remain separate from build validation.

Generated fonts are included; the TTF and its license are in `assets/`. To re-enable PC metrics, change `SMALLTV_ENABLE_METRICS` and remove `-<dashboard/>` from `build_src_filter`; for periodic logging, change `SMALLTV_HEAP_LOG`.

## Binary and updates

[**Download scene5-idle firmware**](artifacts/firmware.bin) · [SHA256](artifacts/SHA256SUMS) · [Structural review](docs/binary-idle-review.json)

SHA256 of the packaged scene5-idle binary:

```text
38bce50a98e2c56865f67ac8bde344be5cfd83a10b2732966eabd60a6818d302
```

The scene5-idle structural review passed for the exact image above. It uses 39,644 bytes of static RAM, 577,319 bytes of application flash, and a 581,472-byte OTA image. The following performance results belong to the prior scene4 release. Measurement results and verification scope are recorded in the [performance report](docs/PERFORMANCE-scene4-efficient.md).

Historical scene3-web validation: the build, exact-binary structural review, 12 mocked web tests and targeted live checks passed. Firmware-only OTA reported `Update OK` for all 575,184 bytes. The token and two saved profiles survived the upgrade; Chrome loaded authorization automatically after its browser token copy was cleared. Local bootstrap restrictions and masked prefill were verified. See [scene3-web validation](docs/VALIDATION-scene3-web.md). This is not a universal migration package for stock firmware; check the model and flash configuration and follow the upstream migration procedure where applicable.

To update a compatible installation, stop clients that send display content and upload **only the firmware** through `/api/v1/ota/fw`, keeping power and network connectivity stable:

```sh
curl -H "Authorization: Bearer YOUR_TOKEN" \
  -F "file=@artifacts/firmware.bin" \
  http://TV_IP/api/v1/ota/fw
```

Do not upload a LittleFS image or erase flash to install this update: this preserves the web interface, configuration, and files. Inspect the JSON response, because the updater may return HTTP 200 even on failure. Success must contain `"status":"Upload successful"` and `"message":"Update OK (...)"`; the device then restarts.

The scene3-web Wi-Fi/token/logs/OTA management assets are served from the firmware, overriding older filesystem copies for those routes. Existing LittleFS files and configuration remain in place. After upgrading, reload the Wi-Fi page; if the browser retains old scripts, perform a hard refresh. Control APIs retain Bearer authentication; the local web UI obtains that token automatically as described below. Saved Wi-Fi passwords are not returned.

After boot, check `/api/v1/display/capabilities`, `/api/v1/draw/status`, the web interface, and `/api/v1/ota/status`. Keep your previous firmware and an appropriate backup before modifying another device. RescueMode/OTA are preserved; a device that cannot boot may require physical serial recovery.

Corrupt, nonblank secure NVS storage is deliberately not overwritten automatically. Token reset also cannot persist through `secure.put` while that corruption remains. This is not a self-recovering case: retain a backup and use explicit physical serial recovery to diagnose and repair the storage. Do not assume a web token reset or ordinary firmware-only update repairs all configuration corruption.

## Using the API

Full documentation: **[docs/API.md](docs/API.md)**. Example: **[examples/scene.json](examples/scene.json)**.

```sh
curl -H "Authorization: Bearer YOUR_TOKEN" \
  http://TV_IP/api/v1/display/capabilities

curl -H "Authorization: Bearer YOUR_TOKEN" \
  -H "Content-Type: application/json" \
  --data-binary @examples/scene.json \
  http://TV_IP/api/v1/draw/batch
```

Replace `epoch` in the example with the current Unix timestamp if you need the NTP fallback. `tz_offset` is the offset in seconds. The example draws a screen; it does not fetch calendars or tasks.

`POST /ntp/sync` schedules synchronization and returns HTTP 202 with `{"status":"ok","pending":true}`; it returns HTTP 503 when offline. Acceptance is not confirmation that the clock has synchronized; check `/ntp/status` later.

## Measuring a report monitor

Keep a native scene resident and send new content only when the report changes; the clock and pulse run locally. Do not upload an image or poll status for every animation frame. The screen stays at **240×240**, with **100% black `#000000`** for black backgrounds.

Start an optional 60-second capture with `POST /api/v1/diagnostics/resources` and `{"enabled":true,"duration_s":60}`, then fetch it once with GET after it expires. DELETE stops and clears it. All three operations use Bearer authentication. Full fields and examples: [resource diagnostics](docs/RESOURCE-DIAGNOSTICS.md).

Reported work/idle intervals measure cooperative wall time, not actual CPU utilization, physical sleep, power consumption, or temperature. Rendering time overlaps loop work; do not add the two. See the [performance report](docs/PERFORMANCE-scene4-efficient.md) for numerical comparisons and limitations.

## Wi-Fi profiles and browser authorization

Open `/wifi.html` to manage up to three saved networks in priority order. **Save networks** persists the list without switching the connection. Leave an existing profile's password unchanged to preserve it; explicitly select replacement to change it, including an empty password for an open network. **Connect saved network** starts a separate connection request. The response is sent before switching Wi-Fi, and the device's IP may change.

Scanning returns HTTP 202 while pending; the page polls once per second with a bounded retry window and a cancel control. HTTP 409 means a connection/scan is busy; retry after it finishes. The page distinguishes a pending operation from an access-point fallback instead of remaining indefinitely busy.

Wi-Fi and other control APIs still require `Authorization: Bearer YOUR_TOKEN`, but the web interface obtains the saved token automatically from `GET /api/v1/web/token`. No manual copy/paste is needed when changing IP addresses. Open the UI using the device's literal local IP address; the bootstrap route checks the local IP Host and same-origin request context, requires `X-SmallTV-Web: 1`, sends no CORS permission and disables caching. A token changed in another tab is refreshed once after HTTP 401. Returning through browser history refreshes it too.

This deliberately trusts people who can open the device's local web UI: they can obtain its token and control it. The custom header/origin checks protect browser cross-origin access; they do not authenticate a separate user or block a local HTTP client that supplies the required headers. Keep this device interface on a trusted local network. Wi-Fi passwords are still not returned by profile listing. The browser never sends the TV token to external API URLs. Explicit token editing remains available at `/token.html`.

On startup, saved SSIDs are tried in priority order; an unavailable first profile can fall back to the next. After connecting, open the IP shown by the TV.

The API contract, including preservation rules and asynchronous responses, is documented in [Wi-Fi profiles](docs/API.md#wifi-profiles--scene2-wifi).

## Validation and evidence limits

Current efficiency measurements are in the [scene4-efficient performance report](docs/PERFORMANCE-scene4-efficient.md). The renderer measurements below describe scene1. Networking checks are recorded in [scene2-wifi validation](docs/VALIDATION-scene2-wifi.md), and automatic-authorization checks in [scene3-web validation](docs/VALIDATION-scene3-web.md). The web management tests can be run independently with `node --test tests/web-wifi.test.cjs`.

The optional capture implementation has [host AddressSanitizer regression tests](tests/resources/README.md) for disabled behavior, sampling, duration limits, reset/stop, and timer rollover. These use the actual diagnostics source with mocked hardware functions; they are not an ESP8266 emulator.

- Code review by agents and an independent review of the binary's **exact hash**.
- Both eboot/application headers, segments, XOR checksums, full CRC, DIO/4 MB configuration, and partitions verified. All YCbCr tables confirmed in flash.
- The same C++ core executed on the host with AddressSanitizer: 1,000 frames and 128 scenes of up to 32 nodes, testing bounds, UTF-8, canaries, margins, and minute/midnight transitions.
- Physical device tests: individual drawing commands, batch and scene modes, rejected requests preserving the scene, authentication, web, NTP, and OTA; ten scene replacements and 30 samples without heap decline or restarts.
- First measured composition: approximately 111 ms; sampled animated frames: up to 16.8 ms. Animation and the absence of black-screen transitions confirmed visually.

The simulation covered the renderer; it was **not a complete ESP8266/Wi-Fi/ST7789 emulator**. It does not establish watchdog behavior, SPI reliability, fragmentation behavior, or indefinite operation. See [the validation summary](docs/VALIDATION.md).

## Repository layout and licensing

`src/`, `include/`, and `lib/`: firmware; `data/web/`: preserved web interface; `tools/`: build, fonts, patching, and validation; `artifacts/`: binary; `examples/`: scene; `docs/`: API, changes, and validation.

GPL-3.0-or-later under the upstream [LICENSE](LICENSE). HoloClawd adaptations retain attribution and the [MIT license](LICENSE-HoloClawd). Noto Sans: [SIL OFL 1.1](assets/OFL.txt). [docs/changes.patch](docs/changes.patch) is a historical patch against the base commit and does not include all subsequent firmware changes. Build the current `src/`, `include/`, and supporting files in this repository; do not use that legacy patch to reconstruct the current release.
