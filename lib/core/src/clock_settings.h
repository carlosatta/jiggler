#pragma once

#include <ArduinoJson.h>

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "controller.h"
#include "json_codec.h"
#include "platform.h"

namespace jig {

struct ClockSettings {
  std::string timezone = "Europe/Rome";  // one of timezoneNames()
  std::string ntpServer = "pool.ntp.org";  // empty = no NTP (set the time by hand)
};

struct ClockStatus {
  bool synced = false;
  std::string source;     // "ntp", "manual" or "none"
  std::string localTime;  // "HH:MM" in the configured timezone, empty when not synced
  std::string weekday;    // "mon".."sun", empty when not synced
  uint32_t epoch = 0;     // UTC seconds, 0 when not synced
  std::string timezone;
};

const std::vector<std::string>& timezoneNames();
std::optional<std::string> posixTimezone(const std::string& name);  // IANA-style name -> POSIX TZ rule
const char* weekdayName(unsigned weekday);                           // 0 = "mon" ... 6 = "sun"
bool validNtpServer(const std::string& server);                      // empty, or a host name / address

std::string clockToJson(const ClockSettings& settings, const ClockStatus& status);
// Only the keys present in `body` overwrite `out`. Read-only keys a GET returns are ignored.
bool parseClockSettings(const std::string& body, ClockSettings& out, ParseError& err);
std::string clockStatusToJson(const ClockStatus& status);  // the object embedded in /api/status

// Timezone, NTP server and the manual "set the time" action. Thread-safe.
class ClockStore {
 public:
  ClockStore(Platform& platform, BlobStore& blobs);

  void begin();  // loads the settings (a timezone saved by an older version is kept) and applies the NTP server
  ClockSettings settings() const;
  CmdResult replace(const ClockSettings& settings);  // errors: invalid_clock, storage_error
  CmdResult setTime(uint32_t epochSeconds);          // errors: invalid_time, not_supported
  std::string posixTz() const;
  ClockStatus status() const;

 private:
  Platform& platform_;
  BlobStore& blobs_;
  mutable std::mutex mutex_;
  ClockSettings settings_;
};

}  // namespace jig
