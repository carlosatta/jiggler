#include "event_log.h"

#include <ArduinoJson.h>

namespace jig {

void EventLog::add(uint32_t atMs, const std::string& message) {
  std::lock_guard<std::mutex> lock(mutex_);
  entries_.push_back({atMs, message.substr(0, 160)});
  while (entries_.size() > kEventLogCapacity) entries_.pop_front();
}

std::string EventLog::toJson(uint32_t nowMs) const {
  std::lock_guard<std::mutex> lock(mutex_);
  JsonDocument doc;
  doc["uptimeMs"] = nowMs;
  JsonArray arr = doc["events"].to<JsonArray>();
  for (const Entry& e : entries_) {
    JsonObject o = arr.add<JsonObject>();
    o["agoMs"] = static_cast<uint32_t>(nowMs - e.atMs);
    o["message"] = e.message;
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

}  // namespace jig
