#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

namespace jig {

constexpr size_t kEventLogCapacity = 80;

// The last few things the device did (Bluetooth links, pairing decisions, the button), kept in memory so they can
// be read over WiFi when no USB cable is attached. Thread-safe. Newest last.
class EventLog {
 public:
  void add(uint32_t atMs, const std::string& message);
  std::string toJson(uint32_t nowMs) const;  // {"uptimeMs":..., "events":[{"agoMs":..., "message":"..."}]}

 private:
  struct Entry {
    uint32_t atMs;
    std::string message;
  };
  mutable std::mutex mutex_;
  std::deque<Entry> entries_;
};

}  // namespace jig
