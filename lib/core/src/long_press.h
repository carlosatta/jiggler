#pragma once

#include <cstdint>

namespace jig {

// Debounce-free long-press detector: update() returns true exactly once per press, when the
// button has been held for `holdMs`. Wrap-safe.
class LongPress {
 public:
  explicit LongPress(uint32_t holdMs) : holdMs_(holdMs) {}

  bool update(bool pressed, uint32_t nowMs) {
    if (!pressed) {
      down_ = false;
      fired_ = false;
      return false;
    }
    if (!down_) {
      down_ = true;
      since_ = nowMs;
      return false;
    }
    if (!fired_ && static_cast<uint32_t>(nowMs - since_) >= holdMs_) {
      fired_ = true;
      return true;
    }
    return false;
  }

 private:
  uint32_t holdMs_;
  uint32_t since_ = 0;
  bool down_ = false;
  bool fired_ = false;
};

}  // namespace jig
