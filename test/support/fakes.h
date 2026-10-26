#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "platform.h"

namespace jig {

class XorShiftRng : public Rng {
 public:
  explicit XorShiftRng(uint32_t seed) : state_(seed ? seed : 0x9E3779B9u) {}
  uint32_t next() override {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return state_;
  }

 private:
  uint32_t state_;
};

struct FakeClock : Platform {
  uint32_t now = 0;
  uint32_t unixTime = 1760000000;
  bool mouse = true;
  bool wifi = true;
  bool paired = true;
  std::optional<LocalTime> local;           // what localTime() reports; nullopt = clock not synced
  mutable std::string lastTz;               // the POSIX TZ string the last localTime() call asked for
  std::string ntp = "unset";                // what setNtpServer() was last given
  bool canSetTime = true;
  const char* source = "ntp";
  const char* timeSource() const override { return unixTime ? source : "none"; }
  void setNtpServer(const std::string& server) override { ntp = server; }
  bool setUnixTime(uint32_t epoch) override {
    if (!canSetTime) return false;
    unixTime = epoch;
    source = "manual";
    return true;
  }
  uint32_t nowMs() const override { return now; }
  uint32_t unixSeconds() const override { return unixTime; }
  bool mouseConnected() const override { return mouse; }
  bool wifiConnected() const override { return wifi; }
  bool computerPaired() const override { return paired; }
  std::optional<LocalTime> localTime(const std::string& posixTz) const override {
    lastTz = posixTz;
    return local;
  }
};

struct FakeMouse : MouseSink {
  std::vector<std::string> calls;
  void move(int8_t dx, int8_t dy) override {
    calls.push_back("move " + std::to_string(dx) + "," + std::to_string(dy));
  }
  void click() override { calls.push_back("click"); }
  void scroll(int8_t notches) override { calls.push_back("scroll " + std::to_string(notches)); }
};

struct FakeKeyboard : KeyboardSink {
  std::vector<std::string> calls;
  void keyDown(uint8_t modifiers, uint8_t usage) override {
    calls.push_back("down " + std::to_string(modifiers) + "," + std::to_string(usage));
  }
  void keyUp() override { calls.push_back("up"); }
};

struct MemBlobStore : BlobStore {
  std::map<std::string, std::string> data;
  std::string get(const std::string& key) override {
    auto it = data.find(key);
    return it == data.end() ? std::string() : it->second;
  }
  bool put(const std::string& key, const std::string& value) override {
    data[key] = value;
    return true;
  }
  void remove(const std::string& key) override { data.erase(key); }
  std::vector<std::string> keys(const std::string& prefix) override {
    std::vector<std::string> out;
    for (const auto& kv : data)
      if (kv.first.rfind(prefix, 0) == 0) out.push_back(kv.first);
    return out;
  }
};

// A store that can be told to refuse writes, like a full NVS.
struct FlakyBlobStore : MemBlobStore {
  bool failPuts = false;
  bool put(const std::string& key, const std::string& value) override {
    if (failPuts) return false;
    return MemBlobStore::put(key, value);
  }
};

struct MemStore : ConfigStore {
  Config stored;
  int saves = 0;
  Config load() override { return stored; }
  void save(const Config& config) override {
    stored = config;
    saves++;
  }
};

}  // namespace jig
