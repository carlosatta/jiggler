#pragma once

#include <ArduinoJson.h>

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "config.h"
#include "controller.h"
#include "json_codec.h"
#include "platform.h"

namespace jig {

constexpr size_t kMaxSchedules = 8;

// One weekly window: on the selected days, from `startMinute` for `durationMin` minutes (it may run past
// midnight into the next day). While a window is open the jiggler runs.
struct Schedule {
  std::string id;
  bool enabled = true;
  uint8_t days = 0;           // bit 0 = Monday ... bit 6 = Sunday
  uint16_t startMinute = 0;   // minutes after local midnight, 0..1439
  uint16_t durationMin = 60;  // 1..1440
};

struct ScheduleSettings {
  std::string timezone = "Europe/Rome";  // one of timezoneNames()
  std::vector<Schedule> schedules;
};

struct ScheduleStatus {
  bool timeSynced = false;
  std::string localTime;  // "HH:MM" in the configured timezone, empty when not synced
  std::string weekday;    // "mon".."sun", empty when not synced
  bool active = false;    // some enabled window is open right now
  size_t enabledCount = 0;
  std::string timezone;
};

const std::vector<std::string>& timezoneNames();
std::optional<std::string> posixTimezone(const std::string& name);  // IANA-style name -> POSIX TZ rule

bool scheduleWindowContains(const Schedule& schedule, const LocalTime& now);
std::optional<ValidationError> validateSchedules(const ScheduleSettings& settings);
std::string schedulesToJson(const ScheduleSettings& settings);  // also lists the timezones the device knows
// Only the keys present in `body` overwrite `out`. Errors carry the offending field path.
bool parseSchedules(const std::string& body, ScheduleSettings& out, ParseError& err);
std::string scheduleStatusToJson(const ScheduleStatus& status);

// Starts and stops the jiggler by the clock. Edge-driven, so a manual start/stop in the middle of a window is
// respected until the next window opens or closes. The one exception is the first look at the clock after
// boot (or after the time arrives): then the schedule is in charge and the jiggler matches the window.
// Saving schedules never starts or stops anything by itself. Needs a synced wall clock. Thread-safe.
class ScheduleEngine {
 public:
  ScheduleEngine(Platform& platform, Controller& controller, BlobStore& blobs);

  void begin();
  ScheduleSettings settings() const;
  CmdResult replace(const ScheduleSettings& settings);  // errors: invalid_schedule, storage_error
  void tick();                                          // call about once a second
  ScheduleStatus status() const;

 private:
  bool anyWindowOpenLocked(const LocalTime& now) const;
  size_t enabledCountLocked() const;

  Platform& platform_;
  Controller& controller_;
  BlobStore& blobs_;
  mutable std::mutex mutex_;
  ScheduleSettings settings_;
  bool first_ = true;     // no evaluation yet since boot / since the clock arrived
  bool wasOpen_ = false;  // a window was open at the previous evaluation
};

}  // namespace jig
