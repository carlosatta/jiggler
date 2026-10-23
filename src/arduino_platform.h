#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>

#include <ctime>

#include "ble_mouse.h"
#include "platform.h"

class ArduinoPlatform : public jig::Platform {
 public:
  explicit ArduinoPlatform(const BleMouse& ble) : ble_(ble) {}

  uint32_t nowMs() const override { return millis(); }

  uint32_t unixSeconds() const override {
    const time_t t = time(nullptr);
    return t >= 1700000000 ? static_cast<uint32_t>(t) : 0;
  }

  bool mouseConnected() const override {
#ifdef JIG_FAKE_CONNECTED
    return true;  // debug builds only: exercises the movement task without a paired host
#else
    return ble_.connected();
#endif
  }

  bool wifiConnected() const override { return WiFi.status() == WL_CONNECTED; }

 private:
  const BleMouse& ble_;
};

class EspRng : public jig::Rng {
 public:
  uint32_t next() override { return esp_random(); }
};
