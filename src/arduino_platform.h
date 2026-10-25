#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <esp_system.h>
#include <sys/time.h>

#include <atomic>
#include <cstdlib>
#include <string>
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

  const char* timeSource() const override {
    if (unixSeconds() == 0) return "none";
    return manual().load() ? "manual" : "ntp";
  }

  // SNTP keeps the server name by pointer, so it lives in this object. An empty name switches NTP off. The
  // network stack may not be up yet (the setting is read at boot): the change waits for applyNtp() in loop().
  void setNtpServer(const std::string& server) override {
    std::lock_guard<std::mutex> lock(ntpMutex());
    ntpServer_ = server;
    ntpPending_ = true;
    applyNtpLocked();
  }

  void applyNtp() {
    std::lock_guard<std::mutex> lock(ntpMutex());
    applyNtpLocked();
  }

  bool setUnixTime(uint32_t epochSeconds) override {
    struct timeval tv;
    tv.tv_sec = static_cast<time_t>(epochSeconds);
    tv.tv_usec = 0;
    if (settimeofday(&tv, nullptr) != 0) return false;
    manual().store(true);
    return true;
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
    out.day = daysFromCivil(local.tm_year + 1900, static_cast<unsigned>(local.tm_mon + 1), static_cast<unsigned>(local.tm_mday));
    return out;
  }

 private:
  // Whole days since 1970-01-01 of a calendar date (Howard Hinnant's algorithm).
  static uint32_t daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<uint32_t>(era * 146097 + static_cast<int>(doe) - 719468);
  }
  static std::atomic<bool>& manual() {
    static std::atomic<bool> flag{false};
    return flag;
  }
  static std::mutex& ntpMutex() {
    static std::mutex m;
    return m;
  }
  void applyNtpLocked() {
    if (!ntpPending_ || WiFi.status() != WL_CONNECTED) return;
    ntpPending_ = false;
    sntp_stop();
    if (ntpServer_.empty()) return;
    sntp_set_time_sync_notification_cb([](struct timeval*) { manual().store(false); });
    configTime(0, 0, ntpServer_.c_str());
  }
  std::string ntpServer_;
  bool ntpPending_ = false;
  const BleMouse& ble_;
};

class EspRng : public jig::Rng {
 public:
  uint32_t next() override { return esp_random(); }
};
