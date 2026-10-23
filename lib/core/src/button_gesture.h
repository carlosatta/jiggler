#pragma once

#include <cstdint>

namespace jig {

enum class Gesture { None, Short, Long };

// One button, two meanings: a short press (released within shortMaxMs, longer than the contact bounce) and
// a long press (held for longMs, reported once while still held). Anything in between does nothing.
class ButtonGesture {
 public:
  explicit ButtonGesture(uint32_t longMs = 3000, uint32_t shortMaxMs = 800, uint32_t bounceMs = 40)
      : longMs_(longMs), shortMaxMs_(shortMaxMs), bounceMs_(bounceMs) {}

  Gesture update(bool pressed, uint32_t nowMs) {
    if (pressed) {
      if (!down_) {
        down_ = true;
        longFired_ = false;
        since_ = nowMs;
        return Gesture::None;
      }
      if (!longFired_ && static_cast<uint32_t>(nowMs - since_) >= longMs_) {
        longFired_ = true;
        return Gesture::Long;
      }
      return Gesture::None;
    }
    if (!down_) return Gesture::None;
    down_ = false;
    const uint32_t heldMs = static_cast<uint32_t>(nowMs - since_);
    if (!longFired_ && heldMs >= bounceMs_ && heldMs <= shortMaxMs_) return Gesture::Short;
    return Gesture::None;
  }

 private:
  uint32_t longMs_, shortMaxMs_, bounceMs_;
  uint32_t since_ = 0;
  bool down_ = false;
  bool longFired_ = false;
};

}  // namespace jig
