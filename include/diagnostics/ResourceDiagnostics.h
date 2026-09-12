#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Opt-in, bounded RAM counters. No filesystem writes, logs, or sampling task.
// Times include interrupt/SDK work and are NOT hardware CPU-utilization samples.
class ResourceDiagnostics {
 public:
    static constexpr uint32_t DEFAULT_DURATION_SECONDS = 60;
    static constexpr uint32_t MAX_DURATION_SECONDS = 600;

    // This inline flag is the entire cost of a disabled instrumentation hook.
    static bool enabled() { return collecting; }
    static bool start(uint32_t durationSeconds = DEFAULT_DURATION_SECONDS);
    static void stop();
    static void reset();
    static void recordLoop(uint32_t workUs, uint32_t idleUs);
    // Count actual transmitted pixels, including repeated transfers. Duration is
    // render + transfer wall time and overlaps loop work; do not add both totals.
    static void recordRender(uint32_t durationUs, uint32_t pixels);
    static void snapshot(JsonObject result);

 private:
    static bool collecting;
};
