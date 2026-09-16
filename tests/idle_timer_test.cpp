#include "display/IdleTimer.h"
#include <cassert>
#include <cstdint>
int main() {
    IdleTimer timer;
    timer.activity(100);
    assert(!timer.expire(600099));
    assert(timer.expire(600100));
    assert(timer.sleeping());
    assert(!timer.expire(600101));
    timer.activity(600102);
    assert(!timer.sleeping());
    assert(!timer.expire(1200101));
    assert(timer.expire(1200102));
    timer.activity(UINT32_MAX - 100);
    assert(!timer.expire(599898));
    assert(timer.expire(599899));
}
