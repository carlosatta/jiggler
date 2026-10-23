#pragma once

#include <cstdint>
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
  uint32_t unix = 1760000000;
  bool mouse = true;
  bool wifi = true;
  uint32_t nowMs() const override { return now; }
  uint32_t unixSeconds() const override { return unix; }
  bool mouseConnected() const override { return mouse; }
  bool wifiConnected() const override { return wifi; }
};

struct FakeMouse : MouseSink {
  std::vector<std::string> calls;
  void move(int8_t dx, int8_t dy) override {
    calls.push_back("move " + std::to_string(dx) + "," + std::to_string(dy));
  }
  void click() override { calls.push_back("click"); }
  void scroll(int8_t notches) override { calls.push_back("scroll " + std::to_string(notches)); }
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
