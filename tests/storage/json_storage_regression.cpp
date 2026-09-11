// Synthetic-only regression probe for the exact pinned ArduinoJson library.
// This is not a substitute for ConfigManager/EEPROM integration tests.
#include <ArduinoJson.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

int main() {
    static_assert(ARDUINOJSON_VERSION_MAJOR == 7 && ARDUINOJSON_VERSION_MINOR == 4 &&
                  ARDUINOJSON_VERSION_REVISION == 3, "Review copy policy after dependency changes");
    JsonDocument loaded;
    {
        const char fixture[] = "{\"api_token\":\"synthetic-token-only\",\"wifi_ssid\":\"synthetic-network\"}";
        std::unique_ptr<char[]> input(new char[sizeof(fixture)]);
        std::memcpy(input.get(), fixture, sizeof(fixture));
        assert(!deserializeJson(loaded, input.get()));
        std::memset(input.get(), 'x', sizeof(fixture));
    }
    assert(std::strcmp(loaded["api_token"].as<const char*>(), "synthetic-token-only") == 0);
    assert(std::strcmp(loaded["wifi_ssid"].as<const char*>(), "synthetic-network") == 0);

    // Dynamic const char* values and keys are copied in ArduinoJson >=7.3.
    {
        std::string key = "wifi_password", value = "synthetic-pass-only";
        const char* dynamicKey = key.c_str();
        const char* dynamicValue = value.c_str();
        loaded[dynamicKey] = dynamicValue;
        key.assign(key.size(), 'x'); value.assign(value.size(), 'x');
    }
    assert(std::strcmp(loaded["wifi_password"].as<const char*>(), "synthetic-pass-only") == 0);

    // A 2048-byte NVS sector view leaves 2042 bytes after its 6-byte header.
    // Exact serialized size matters: escaped strings can exceed raw length.
    JsonDocument escaped;
    escaped["example"] = std::string(1100, '"');
    std::string output;
    const auto written = serializeJson(escaped, output);
    assert(written == measureJson(escaped));
    assert(written > 2042);
    assert(!escaped.overflowed());
    std::cout << "PASS: mutable-input ownership, dynamic key/value ownership, escaped-capacity accounting\n";
}
