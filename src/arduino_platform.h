#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>

#include <cstdlib>
#include <ctime>
#include <mutex>
#include <optional>

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

  bool computerPaired() const override {
#ifdef JIG_FAKE_CONNECTED
    return true;
#else
    return ble_.pairedCount() > 0;
#endif
  }

  // The loop task (schedule tick) and the HTTP task (status) both call this, and TZ is process-wide: serialise.
  std::optional<jig::LocalTime> localTime(const std::string& posixTz) const override {
    const time_t now = time(nullptr);
    if (now < 1700000000) return std::nullopt;  // SNTP has not set the clock yet
    static std::mutex tzMutex;
    std::lock_guard<std::mutex> lock(tzMutex);
    setenv("TZ", posixTz.c_str(), 1);
    tzset();
    struct tm local;
    localtime_r(&now, &local);
    jig::LocalTime out;
    out.weekday = static_cast<uint8_t>((local.tm_wday + 6) % 7);  // tm_wday: 0 = Sunday
    out.minuteOfDay = static_cast<uint16_t>(local.tm_hour * 60 + local.tm_min);
    return out;
  }

 private:
  const BleMouse& ble_;
};

class EspRng : public jig::Rng {
 public:
  uint32_t next() override { return esp_random(); }
};
