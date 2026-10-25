#pragma once

#include <cstdint>

namespace jig {

// Tap: released within 0.8 s. Pairing: released after 5 to 10 s. Network: held for 30 s (reported once, while held).
// Any other length does nothing, so a long press that was not meant can be cancelled by holding on past 10 s.
enum class Gesture { None, Tap, Pairing, Network };

// What letting go of the button right now would do, so the LED can tell the owner while the button is down.
enum class HoldPhase { None, Pairing, Cancel, Network };

class ButtonGesture {
 public:
  struct Timing {
    uint32_t tapMaxMs = 800;
    uint32_t pairingMinMs = 5000;
    uint32_t pairingMaxMs = 10000;
    uint32_t networkMs = 30000;
    uint32_t bounceMs = 40;
  };

  ButtonGesture() = default;
  explicit ButtonGesture(Timing timing) : t_(timing) {}

  Gesture update(bool pressed, uint32_t nowMs) {
    if (pressed) {
      if (!down_) {
        down_ = true;
        networkFired_ = false;
        since_ = nowMs;
        return Gesture::None;
      }
      if (!networkFired_ && static_cast<uint32_t>(nowMs - since_) >= t_.networkMs) {
        networkFired_ = true;
        return Gesture::Network;
      }
      return Gesture::None;
    }
    if (!down_) return Gesture::None;
    down_ = false;
    if (networkFired_) return Gesture::None;
    const uint32_t heldMs = static_cast<uint32_t>(nowMs - since_);
    if (heldMs >= t_.bounceMs && heldMs <= t_.tapMaxMs) return Gesture::Tap;
    if (heldMs >= t_.pairingMinMs && heldMs <= t_.pairingMaxMs) return Gesture::Pairing;
    return Gesture::None;
  }

  HoldPhase phase(uint32_t nowMs) const {
    if (!down_) return HoldPhase::None;
    const uint32_t heldMs = static_cast<uint32_t>(nowMs - since_);
    if (heldMs >= t_.networkMs) return HoldPhase::Network;
    if (heldMs > t_.pairingMaxMs) return HoldPhase::Cancel;
    if (heldMs >= t_.pairingMinMs) return HoldPhase::Pairing;
    return HoldPhase::None;
  }

 private:
  Timing t_;
  uint32_t since_ = 0;
  bool down_ = false;
  bool networkFired_ = false;
};

}  // namespace jig
