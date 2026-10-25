#include "led.h"

#include <Arduino.h>

namespace {
constexpr uint8_t kLedPin = 2;
}

void Led::begin() { pinMode(kLedPin, OUTPUT); }

void Led::update(const jig::LedContext& context) { digitalWrite(kLedPin, jig::ledLevel(context) ? HIGH : LOW); }
