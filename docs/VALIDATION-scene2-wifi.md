# scene2-wifi validation

Version: `v1.5.0-smalltv-scene2-wifi`.

Exact deployed image: **567,664 bytes**, SHA256 `493cc76fa28e72ffde3b554fb25a7c726104ca6fa3aed52b48a4407c1b9dfacb`.

## Build and host checks

- Build passed: static RAM 38,616 bytes; application flash 563,511 bytes.
- [Exact binary structural review](binary-wifi-review.json) passed.
- Six Node/VM web tests passed: authorization normalization and safe errors, bounded asynchronous scan, busy responses, three-profile save preserving omitted credentials, accepted connection behavior and explicit token validation.

## Device and browser checks

- Firmware-only OTA uploaded the full image and returned `Update OK`.
- A temporary token change survived reboot; the original token was subsequently restored.
- Two ordered network profiles were saved. Listing returned SSIDs and password-presence flags, never passwords.
- Scan returned HTTP 202 while pending, then HTTP 200 with 20 discovered entries.
- With an unavailable first-priority network, reboot selected the available second-priority network after approximately 23.4 seconds total.
- Chrome successfully validated the token for the current origin, saved the network list and displayed scan results.
- Invalid four-profile lists, duplicate SSIDs and short passwords returned HTTP 400; invalid authentication returned HTTP 401. The original saved profiles remained unchanged.

## Short concurrent-rendering observation

With the 12-node panel active, four samples showed free heap of 17,592, 17,496, 17,496 and 17,496 bytes; the largest free block stayed at 10,536 bytes. Across three asynchronous scans, uptime advanced from 186,261 to 195,545 ms and the frame counter from 367 to 460. Each scan returned 20 entries in approximately 2.2–3.8 seconds. No reset or frozen animation was observed in this short window.

Scene state occupied 7,528 bytes. Sampled rendering took approximately 14–15 ms, with a recorded maximum near 108 ms. These observations cover a short run only; they do not establish a long-term leak bound, worst-case heap requirement or maximum rendering latency.

An initial attempt did not upload because the access point was unavailable; a later fresh Wi-Fi scan restored access and the upload above completed. No filesystem image was uploaded.

These are targeted functional checks, not a long-duration stability or power-loss study. Host simulation does not emulate ESP8266 radio/SPI/watchdog behavior. Corrupt nonblank secure storage is intentionally not overwritten; recovery from that case may require serial access and a backup.
