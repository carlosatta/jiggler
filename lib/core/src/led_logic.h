#pragma once

#include <cstdint>

namespace jig {

constexpr uint32_t kLedBlinkPeriodMs = 1000;
constexpr uint32_t kLedMoveFlashMs = 80;

// Disconnected: 1 Hz blink. Connected: solid, with a brief off-flash right after each movement.
inline bool ledLevel(bool connected, uint32_t nowMs, uint32_t lastMoveMs, bool hasMoved) {
  if (!connected) return (nowMs % kLedBlinkPeriodMs) < kLedBlinkPeriodMs / 2;
  if (hasMoved && static_cast<uint32_t>(nowMs - lastMoveMs) < kLedMoveFlashMs) return false;
  return true;
}

}  // namespace jig
