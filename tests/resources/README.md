# Resource diagnostics host regression

This compiles the actual firmware `ResourceDiagnostics.cpp` with a minimal mock of `millis()` and `ESP.getHeapStats()`, using the pinned ArduinoJson headers. The test covers disabled collection, duration bounds, counters, periodic heap sampling, automatic expiry, reset, stop, and 32-bit `millis()` rollover.

From a Visual Studio x64 developer shell, with PlatformIO dependencies already installed:

```powershell
cd tests/resources
cl /nologo /std:c++17 /EHsc /Zi /fsanitize=address /Istubs /I../../include /I../../.pio/libdeps/esp12e/ArduinoJson/src resource_diagnostics_test.cpp ../../src/diagnostics/ResourceDiagnostics.cpp /Fe:resource_diagnostics_test.exe
./resource_diagnostics_test.exe
```

Do not define `NDEBUG`: the regression assertions must remain enabled.

This is a host behavioral test, not ESP8266 emulation. Hardware timers, Wi-Fi/SDK scheduling, heap allocator behavior, OTA stability, and screen transfers require device measurements. The mock verifies when the firmware requests a heap sample; it does not model the ESP8266 allocator.
