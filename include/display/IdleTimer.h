#pragma once
#include <stdint.h>

// Unsigned elapsed time remains correct across millis() rollover.
class IdleTimer {
 public:
    static constexpr uint32_t TimeoutMs = 10u * 60u * 1000u;
    void activity(uint32_t now) { last_ = now; sleeping_ = false; }
    bool expire(uint32_t now) {
        if (sleeping_ || uint32_t(now - last_) < TimeoutMs) return false;
        sleeping_ = true;
        return true;
    }
    bool sleeping() const { return sleeping_; }
    uint32_t elapsed(uint32_t now) const { return uint32_t(now - last_); }
 private:
    uint32_t last_ = 0;
    bool sleeping_ = false;
};
