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

struct LedContext {
  bool connected;
  bool pairingOpen;
  bool held;
  uint32_t nowMs;
  uint32_t lastMoveMs;
  bool hasMoved;
};

// Pairing open: fast blink (the owner can see the window is open). Held (hidden on request): a short flash every
// 3 s. Otherwise the connection pattern above.
inline bool ledLevel(const LedContext& c) {
  if (c.pairingOpen) return (c.nowMs % 250) < 125;
  if (c.held) return (c.nowMs % 3000) < 100;
  return ledLevel(c.connected, c.nowMs, c.lastMoveMs, c.hasMoved);
}

}  // namespace jig
