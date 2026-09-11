#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Retained scenes own bounded RAM only while active. All validation is atomic.
class SceneRenderer {
 public:
    static bool submit(JsonObjectConst document, String& error);
    static void stop();
    static bool active();
    static void update();
    static void status(JsonObject result);
};
