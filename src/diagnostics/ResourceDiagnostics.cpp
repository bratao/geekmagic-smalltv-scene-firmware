#include "diagnostics/ResourceDiagnostics.h"

namespace {
struct Counters {
    uint64_t workUs = 0;
    uint64_t idleUs = 0;
    uint64_t renderUs = 0;
    uint64_t pixels = 0;
    uint32_t loops = 0;
    uint32_t renders = 0;
    uint32_t maxWorkUs = 0;
    uint32_t maxLoopUs = 0;
    uint32_t maxRenderUs = 0;
    uint32_t heapStart = 0;
    uint32_t heapLatest = 0;
    uint32_t heapMin = 0;
    uint32_t blockMin = 0;
    uint32_t heapSamples = 0;
    uint8_t fragmentationMax = 0;
};
Counters counters;
uint32_t startedMs = 0;
uint32_t elapsedMs = 0;
uint32_t lastHeapMs = 0;
uint32_t durationMs = ResourceDiagnostics::DEFAULT_DURATION_SECONDS * 1000;
bool hasCapture = false;
bool expired = false;

void sampleHeap() {
    uint32_t freeHeap;
    uint32_t maxBlock;
    uint8_t fragmentation;
    ESP.getHeapStats(&freeHeap, &maxBlock, &fragmentation);
    if (!counters.heapSamples) {
        counters.heapStart = freeHeap;
        counters.heapMin = freeHeap;
        counters.blockMin = maxBlock;
    }
    counters.heapLatest = freeHeap;
    if (freeHeap < counters.heapMin) counters.heapMin = freeHeap;
    if (maxBlock < counters.blockMin) counters.blockMin = maxBlock;
    if (fragmentation > counters.fragmentationMax) counters.fragmentationMax = fragmentation;
    ++counters.heapSamples;
}
}

bool ResourceDiagnostics::collecting = false;

bool ResourceDiagnostics::start(uint32_t durationSeconds) {
    if (!durationSeconds || durationSeconds > MAX_DURATION_SECONDS) return false;
    counters = Counters{};
    durationMs = durationSeconds * 1000;
    startedMs = lastHeapMs = millis();
    elapsedMs = 0;
    expired = false;
    hasCapture = true;
    sampleHeap();
    collecting = true;
    return true;
}

void ResourceDiagnostics::stop() {
    if (!collecting) return;
    elapsedMs = uint32_t(millis() - startedMs);
    collecting = false;
}

void ResourceDiagnostics::reset() {
    if (collecting) {
        start(durationMs / 1000);
        return;
    }
    counters = Counters{};
    elapsedMs = 0;
    hasCapture = false;
    expired = false;
}

void ResourceDiagnostics::recordLoop(uint32_t workUs, uint32_t idleUs) {
    if (!collecting) return;
    ++counters.loops;
    counters.workUs += workUs;
    counters.idleUs += idleUs;
    if (workUs > counters.maxWorkUs) counters.maxWorkUs = workUs;
    const uint64_t loopUs = uint64_t(workUs) + idleUs;
    if (loopUs > counters.maxLoopUs) {
        counters.maxLoopUs = loopUs > UINT32_MAX ? UINT32_MAX : uint32_t(loopUs);
    }
    const uint32_t now = millis();
    elapsedMs = uint32_t(now - startedMs);
    if (uint32_t(now - lastHeapMs) >= 1000) {
        lastHeapMs = now;
        sampleHeap();
    }
    if (elapsedMs >= durationMs) {
        expired = true;
        collecting = false;
    }
}

void ResourceDiagnostics::recordRender(uint32_t durationUs, uint32_t pixels) {
    if (!collecting) return;
    ++counters.renders;
    counters.renderUs += durationUs;
    counters.pixels += pixels;
    if (durationUs > counters.maxRenderUs) counters.maxRenderUs = durationUs;
}

void ResourceDiagnostics::snapshot(JsonObject result) {
    // Heap values are the last periodic sample, not a sample polluted by this
    // JSON response allocation. This function deliberately does not sample.
    const uint32_t elapsed = collecting ? uint32_t(millis() - startedMs) : elapsedMs;
    result["enabled"] = collecting;
    result["has_capture"] = hasCapture;
    result["expired"] = expired;
    result["duration_limit_s"] = durationMs / 1000;
    result["elapsed_ms"] = elapsed;
    result["loops"] = counters.loops;
    result["work_us"] = counters.workUs;
    result["idle_us"] = counters.idleUs;
    result["max_work_us"] = counters.maxWorkUs;
    result["max_loop_us"] = counters.maxLoopUs;
    result["render_calls"] = counters.renders;
    result["render_us"] = counters.renderUs;
    result["max_render_us"] = counters.maxRenderUs;
    result["pixels_written"] = counters.pixels;
    result["rgb565_bytes"] = counters.pixels * 2;
    result["heap_start"] = counters.heapStart;
    result["heap_latest"] = counters.heapLatest;
    result["heap_min"] = counters.heapMin;
    result["max_block_min"] = counters.blockMin;
    result["fragmentation_max_pct"] = counters.fragmentationMax;
    result["heap_samples"] = counters.heapSamples;
    // No floating point or divisions in the hot path. Wall-time fraction is an
    // cooperative activity indicator, not measured silicon utilization. SDK
    // processing may happen during either interval, so this is not a CPU bound.
    result["cooperative_work_pct"] = elapsed
        ? double(counters.workUs) / (double(elapsed) * 10.0) : 0.0;
    result["timing_kind"] = "cooperative_wall_time_not_cpu_utilization";
}
