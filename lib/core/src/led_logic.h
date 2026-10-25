#pragma once

#include <cstdint>

#include "bluetooth.h"
#include "button_gesture.h"

namespace jig {

constexpr uint32_t kLedBlinkPeriodMs = 1000;
constexpr uint32_t kLedMoveFlashMs = 80;
constexpr uint32_t kLedConfirmMs = 600;  // three quick blinks after a tap on the button

struct LedContext {
  BluetoothState bluetooth = BluetoothState::Waiting;
  bool running = false;     // the jiggler is running (not stopped or paused)
  uint32_t nowMs = 0;
  uint32_t lastMoveMs = 0;
  bool hasMoved = false;
  HoldPhase hold = HoldPhase::None;  // the button is down and has been held this long
  bool confirming = false;
  uint32_t confirmStartMs = 0;
};

inline bool ledFastBlink(uint32_t nowMs) { return (nowMs % 250) < 125; }

// While the button is held: 5 to 10 s fast blink (let go now to open Discovery), 10 to 30 s dark (let go to do
// nothing), 30 s solid (network mode switches). Otherwise a tap's three blinks, then the Bluetooth state:
//   off: one short flash every 3 s        waiting: slow blink        discovery: fast blink
//   connected and running: solid, dark for a moment after each movement
//   connected but stopped or paused: two short flashes every 3 s
inline bool ledLevel(const LedContext& c) {
  switch (c.hold) {
    case HoldPhase::Pairing: return ledFastBlink(c.nowMs);
    case HoldPhase::Cancel: return false;
    case HoldPhase::Network: return true;
    case HoldPhase::None: break;
  }
  if (c.confirming) {
    const uint32_t elapsed = static_cast<uint32_t>(c.nowMs - c.confirmStartMs);
    if (elapsed < kLedConfirmMs) return (elapsed % 200) < 100;
  }
  switch (c.bluetooth) {
    case BluetoothState::Off: return (c.nowMs % 3000) < 100;
    case BluetoothState::Discovery: return ledFastBlink(c.nowMs);
    case BluetoothState::Waiting: return (c.nowMs % kLedBlinkPeriodMs) < kLedBlinkPeriodMs / 2;
    case BluetoothState::Connected: break;
  }
  if (!c.running) {
    const uint32_t t = c.nowMs % 3000;
    return t < 100 || (t >= 250 && t < 350);
  }
  if (c.hasMoved && static_cast<uint32_t>(c.nowMs - c.lastMoveMs) < kLedMoveFlashMs) return false;
  return true;
}

}  // namespace jig
