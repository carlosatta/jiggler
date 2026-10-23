#pragma once

#include <cstdint>
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

struct Platform {
  virtual ~Platform() = default;
  virtual uint32_t nowMs() const = 0;
  virtual uint32_t unixSeconds() const = 0;  // 0 = clock not synced
  virtual bool mouseConnected() const = 0;
  virtual bool wifiConnected() const = 0;
};

struct MouseSink {
  virtual ~MouseSink() = default;
  virtual void move(int8_t dx, int8_t dy) = 0;
  virtual void click() = 0;
  virtual void scroll(int8_t notches) = 0;
};

struct ConfigStore {
  virtual ~ConfigStore() = default;
  virtual Config load() = 0;
  virtual void save(const Config& config) = 0;
};

// Small key/value persistence for JSON blobs (devices, scripts, automations). get() returns "" if absent.
struct BlobStore {
  virtual ~BlobStore() = default;
  virtual std::string get(const std::string& key) = 0;
  virtual void put(const std::string& key, const std::string& value) = 0;
  virtual void remove(const std::string& key) = 0;
  virtual std::vector<std::string> keys(const std::string& prefix) = 0;
};

}  // namespace jig
