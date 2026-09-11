# Storage regression review

`json_storage_regression.cpp` uses only synthetic data and the pinned ArduinoJson 7.4.3 headers. Compile natively with AddressSanitizer where available. Example after the Visual Studio x64 developer environment is loaded:

```powershell
cl /std:c++17 /EHsc /Zi /fsanitize=address /I.pio\libdeps\esp12e\ArduinoJson\src tests\storage\json_storage_regression.cpp /Fe:tests\storage\json_storage_regression.exe
```

It checks that parsing a mutable input buffer and subsequently destroying it is safe with this library version; dynamic key/value assignment is also copied. It demonstrates why EEPROM capacity must use `measureJson`, including escaping.

Verified locally with MSVC 14.44 and AddressSanitizer: all three probes passed. The firmware implementation is not linked by this small semantic probe.

Required implementation-level regression cases for the storage change:

- Load the unchanged NVS1 format, unchanged salt/key derivation and legacy single-network keys; migrate to one saved profile without losing API token.
- Save three networks, reboot/reload, confirm order and selected network; preserve legacy SSID/password for firmware rollback.
- Change API token, reload, verify the new token and reject the old token; never print either.
- Simulate allocation failure, oversized serialized payload, truncated JSON and EEPROM commit failure: return failure without falsely acknowledging persistence or erasing previously valid state.
- Existing storage payload is bounded by 2042 bytes for compatibility with the previous 2048-byte EEPROM view. Include quoted/backslash/control-character SSIDs in capacity tests.
- Failed config-file write/rename must not silently report successful complete settings save. Successful migration must remove legacy plaintext credentials only after durable storage succeeds.
- API responses, logs and Wi-Fi network listing must never include stored passwords or bearer tokens.

One EEPROM sector is not power-loss atomic. Software rollback can restore RAM after a failed operation, but cannot guarantee recovery after power interruption during sector erase/write.
