#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "config.h"

namespace jig {

struct Rng {
  virtual ~Rng() = default;
  virtual uint32_t next() = 0;
};

// [0, 1)
inline double rand01(Rng& rng) { return rng.next() / 4294967296.0; }

// inclusive on both ends
inline int32_t randRange(Rng& rng, int32_t lo, int32_t hi) {
  if (hi <= lo) return lo;
  const uint64_t span = static_cast<uint64_t>(static_cast<int64_t>(hi) - lo) + 1;
  return static_cast<int32_t>(lo + static_cast<int64_t>(rng.next() % span));
}

// Wall-clock time in the device's timezone. weekday: 0 = Monday ... 6 = Sunday.
struct LocalTime {
  uint8_t weekday = 0;
  uint16_t minuteOfDay = 0;  // 0..1439
};

struct Platform {
  virtual ~Platform() = default;
  virtual uint32_t nowMs() const = 0;
  virtual uint32_t unixSeconds() const = 0;  // 0 = clock not synced
  virtual bool mouseConnected() const = 0;
  virtual bool wifiConnected() const = 0;
  // At least one computer is paired. Without one the jiggler has nothing to keep awake and must not run.
  virtual bool computerPaired() const { return true; }
  // `posixTz` is a POSIX TZ string such as "CET-1CEST,M3.5.0,M10.5.0/3". nullopt = clock not synced.
  virtual std::optional<LocalTime> localTime(const std::string& posixTz) const {
    (void)posixTz;
    return std::nullopt;
  }
};

struct MouseSink {
  virtual ~MouseSink() = default;
  virtual void move(int8_t dx, int8_t dy) = 0;
  virtual void click() = 0;
  virtual void scroll(int8_t notches) = 0;
};

struct KeyboardSink {
  virtual ~KeyboardSink() = default;
  virtual void keyDown(uint8_t modifiers, uint8_t usage) = 0;  // HID usage page 0x07
  virtual void keyUp() = 0;                                    // releases everything
};

struct ConfigStore {
  virtual ~ConfigStore() = default;
  virtual Config load() = 0;
  virtual void save(const Config& config) = 0;
};

// Where the API token lives (NVS on the device).
struct TokenVault {
  virtual ~TokenVault() = default;
  virtual std::string load() = 0;                      // "" when never set
  virtual bool save(const std::string& token) = 0;     // false = not stored
};

// Small key/value persistence for JSON blobs (devices, scripts, automations). get() returns "" if absent.
struct BlobStore {
  virtual ~BlobStore() = default;
  virtual std::string get(const std::string& key) = 0;
  virtual bool put(const std::string& key, const std::string& value) = 0;  // false = not stored (e.g. storage full)
  virtual void remove(const std::string& key) = 0;
  virtual std::vector<std::string> keys(const std::string& prefix) = 0;
};

}  // namespace jig
