#pragma once
#include <cstdint>
uint32_t millis();
struct FakeESP {
 void getHeapStats(uint32_t* free, uint32_t* block, uint8_t* frag);
};
extern FakeESP ESP;
