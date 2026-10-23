#include "led.h"

#include <Arduino.h>

#include "led_logic.h"

namespace {
constexpr uint8_t kLedPin = 2;
}

void Led::begin() { pinMode(kLedPin, OUTPUT); }

void Led::update(const jig::Status& s, uint32_t nowMs) {
  const bool hasMoved = s.lastMoveAgoMs.has_value();
  const uint32_t lastMoveMs = hasMoved ? nowMs - *s.lastMoveAgoMs : 0;
  digitalWrite(kLedPin, jig::ledLevel(s.mouseConnected, nowMs, lastMoveMs, hasMoved) ? HIGH : LOW);
}
