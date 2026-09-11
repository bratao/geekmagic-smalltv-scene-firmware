# scene3-web validation

Version: `v1.5.0-smalltv-scene3-web`.

Exact deployed image: **575,184 bytes**, SHA256 `40384ba31ddb519ba1e647b86b2d975502081989a2953adbded6c068b7e8accf`.

## Build and host checks

- Build passed: static RAM 38,884 bytes; application flash 571,035 bytes.
- [Exact binary structural review](binary-web-review.json) passed.
- Twelve Node/VM tests passed, covering automatic token bootstrap, shared concurrent loading, one refresh after 401, external-origin rejection, masked token prefill, browser-history refresh, Wi-Fi scans/profile saves, logs and OTA authorization.

## Device and browser checks

- Firmware-only OTA uploaded all 575,184 bytes and returned `Update OK`.
- Device capabilities identified scene3-web / wifi3 after boot.
- The existing token and two saved Wi-Fi profiles were preserved.
- The local bootstrap request without Bearer authentication, but with its required custom header, returned the saved token. The response had `no-store` and no CORS permission. Token values were compared without publishing them in this report.
- Missing custom header, foreign Origin, foreign Host and cross-site requests each returned HTTP 403.
- Chrome's Token page loaded the current token automatically and kept the field masked.
- After clearing the browser's token copy, opening the Wi-Fi page automatically loaded the two saved profiles and current network without typing a token.

The user visually confirmed the connected SSID above the IP on the startup screen, with no clipping. Chrome also successfully saved the network list after the browser token copy was cleared. The panel service was restarted after validation. These targeted checks are not a long-duration stability or power-loss study. Earlier renderer/memory observations belong to their recorded release versions.

The bootstrap intentionally trusts clients able to access the local device interface. Header/origin checks limit browser cross-origin access; they are not independent user authentication against a local HTTP client. Other control APIs retain Bearer authentication. No saved Wi-Fi password is returned by profile listing.
