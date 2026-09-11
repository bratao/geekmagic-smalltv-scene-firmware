# Independent storage/Wi-Fi safety review

Reviewer: `firmware_binary_review`, 2026-09-11. No device access or firmware build performed by this reviewer. Final binary measurements are recorded separately in `binary-wifi-review.json` after the last build.

## Result

No blocking defect found in the reviewed valid-storage migration, token persistence, or three-network routes.

- Token and network changes are persisted together in a bounded candidate document. In-memory storage is replaced only after successful commit; token/network HTTP handlers restore their previous runtime configuration when saving fails.
- NVS1 framing, existing salt/key derivation, 2048-byte EEPROM view and 2042-byte serialized payload limit remain unchanged. Legacy first-network keys remain available to the preceding firmware.
- Three-network limit, duplicate SSIDs, credential byte lengths, embedded NUL and token character restrictions are validated. Routes require bearer authentication. Network listings expose only SSID and a password-present flag.
- Network connection is deferred until after the HTTP response; scheduled connection strings are owned rather than borrowed from request/configuration allocations.
- Metadata is staged in a temporary file before credential commit. If its rename later fails, the credentials remain persisted and the firmware logs a metadata warning rather than rolling back credentials only in RAM.

## Verified hypothesis

The previous suspicion that freeing the deserialization input causes dangling strings is not applicable to pinned ArduinoJson 7.4.3. A native AddressSanitizer probe passed for mutable-input lifetime and dynamic key/value ownership. The actual token defect was the absent token write in the previous ConfigManager save method.

## Remaining limitations

- A single EEPROM erase/write sector is not power-loss atomic. Returning failure and retaining the old JSON document in RAM cannot undo physical flash damage from a failed commit.
- Nonblank unreadable storage is now preserved instead of silently erased. Normal storage writes, including a rescue token reset that uses the same storage API, cannot repair that condition without a separate explicit reset/recovery procedure. This preserves evidence but is a recovery limitation.
- Native semantic tests and binary integrity checks do not prove physical radio behavior, asynchronous scan performance, heap availability or post-reboot credential persistence. Those require device validation with no credential logging.
- The firmware can be rolled back with its first network and token intact. Subsequent edits by older firmware only update its legacy fields; returning to the newer firmware may continue to use its previously stored multi-network list.
