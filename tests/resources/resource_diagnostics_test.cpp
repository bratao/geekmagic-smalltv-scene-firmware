#include "diagnostics/ResourceDiagnostics.h"
#include <cassert>
#include <iostream>
uint32_t nowMs=0, samples=0, heap=30000;
FakeESP ESP;
uint32_t millis() { return nowMs; }
void FakeESP::getHeapStats(uint32_t* f,uint32_t* b,uint8_t* g) { ++samples; *f=heap; *b=heap-100; *g=2; }
JsonDocument state() { JsonDocument d; ResourceDiagnostics::snapshot(d.to<JsonObject>()); return d; }
int main() {
 assert(!ResourceDiagnostics::enabled());
 ResourceDiagnostics::recordLoop(1,2); ResourceDiagnostics::recordRender(100,240*240);
 assert(samples==0 && state()["loops"]==0);
 assert(!ResourceDiagnostics::start(0)); assert(!ResourceDiagnostics::start(601));
 assert(ResourceDiagnostics::start(2)); assert(samples==1);
 nowMs=500; ResourceDiagnostics::recordLoop(100,490000); ResourceDiagnostics::recordRender(50,100);
 auto d=state(); assert(d["loops"]==1 && d["pixels_written"]==100 && d["rgb565_bytes"]==200);
 assert(d["render_us"]==50 && samples==1);
 nowMs=1000; heap=29000; ResourceDiagnostics::recordLoop(200,400000);
 assert(samples==2 && state()["heap_min"]==29000);
 nowMs=2000; ResourceDiagnostics::recordLoop(300,990000);
 assert(!ResourceDiagnostics::enabled() && state()["expired"]==true);
 const auto count=samples; nowMs=9999; ResourceDiagnostics::recordLoop(123,456);
 assert(samples==count && state()["loops"]==3 && state()["elapsed_ms"]==2000);
 ResourceDiagnostics::reset(); assert(state()["has_capture"]==false && state()["loops"]==0);
 nowMs=UINT32_MAX-500; assert(ResourceDiagnostics::start(1));
 nowMs=499; ResourceDiagnostics::recordLoop(100,999900);
 assert(!ResourceDiagnostics::enabled() && state()["elapsed_ms"]==1000);
 nowMs=10000; assert(ResourceDiagnostics::start(60)); nowMs=11000;
 ResourceDiagnostics::reset(); assert(ResourceDiagnostics::enabled() && state()["loops"]==0);
 nowMs=12000; ResourceDiagnostics::stop(); assert(state()["elapsed_ms"]==1000);
 std::cout << "PASS: disabled, bounds, counters, sampling, expiry, reset, millis rollover, stop\n";
}
