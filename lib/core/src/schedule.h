#pragma once

#include <ArduinoJson.h>

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "bluetooth_control.h"
#include "clock_settings.h"
#include "config.h"
#include "controller.h"
#include "json_codec.h"
#include "platform.h"

namespace jig {

constexpr size_t kMaxSchedules = 8;
constexpr uint16_t kMaxRepeat = 999;
constexpr uint16_t kMaxJitterMin = 120;

enum class ScheduleKind { Window, Action };
enum class ScheduleAction { Start, Stop, Pause, Resume, BluetoothOn, BluetoothOff, Discovery };

const char* scheduleActionName(ScheduleAction action);  // "start", "stop", "pause", "resume", "bluetooth_on", "bluetooth_off", "discovery"
std::optional<ScheduleAction> parseScheduleAction(const std::string& name);

// One weekly entry, on the selected days:
//  - Window: from `startMinute` for `durationMin` minutes (it may run past midnight). While it is open the
//    jiggler runs: it starts when the window opens and stops when it closes.
//  - Action: at `startMinute`, do `action` once.
// `jitterMin` shifts every time of the entry by a random amount between minus and plus that many minutes, chosen
// afresh each day (a window's start and end are shifted independently).
struct Schedule {
  std::string id;
  bool enabled = true;
  ScheduleKind kind = ScheduleKind::Window;
  uint8_t days = 0;           // bit 0 = Monday ... bit 6 = Sunday
  uint16_t startMinute = 0;   // minutes after local midnight, 0..1439
  uint16_t durationMin = 60;  // Window only, 1..1440
  ScheduleAction action = ScheduleAction::Start;  // Action only
  uint16_t jitterMin = 0;     // 0..120
  uint16_t times = 0;         // how many windows/actions it runs before turning itself off; 0 = forever (999 at most)
  uint16_t remaining = 0;     // left while `times` is set; saved so a restart does not reset it
};

struct ScheduleSettings {
  std::vector<Schedule> schedules;
};

struct ScheduleStatus {
  bool active = false;  // some enabled window is open right now
  size_t enabledCount = 0;
  std::string blocked;  // "no_computer_paired" while an open window cannot start the jiggler yet; else empty
};

// The random shift (minutes, -jitterMin..+jitterMin) of one time of one entry on one day. Deterministic, so a
// restart in the middle of a day agrees with what it did before. slot: 0 = action time, 1 = window start, 2 = window end.
int scheduleJitterOffset(const std::string& id, int64_t day, int slot, int jitterMin);
bool scheduleWindowContains(const Schedule& schedule, const LocalTime& now);
// An action entry's time falls in (fromAbs, toAbs], counted in minutes since 1970-01-01 local time.
bool scheduleActionDue(const Schedule& schedule, const LocalTime& now, int64_t fromAbs, int64_t toAbs);
std::optional<ValidationError> validateSchedules(const ScheduleSettings& settings);
std::string schedulesToJson(const ScheduleSettings& settings);
// Only the keys present in `body` overwrite `out`. Errors carry the offending field path.
bool parseSchedules(const std::string& body, ScheduleSettings& out, ParseError& err);
std::string scheduleStatusToJson(const ScheduleStatus& status);

// Runs the schedules by the clock. Edge-driven, so a manual start/stop in the middle of a window is respected
// until the next window opens or closes. Two moments are authoritative instead: the first look at the clock after
// boot, and a schedule that was just saved or changed: if its window is open at that moment the jiggler starts.
// Needs a synced wall clock. Thread-safe.
class ScheduleEngine {
 public:
  ScheduleEngine(Platform& platform, Controller& controller, BlobStore& blobs, ClockStore& clock);

  void setBluetooth(BluetoothControl* bluetooth) { bluetooth_ = bluetooth; }
  void begin();
  ScheduleSettings settings() const;
  CmdResult replace(const ScheduleSettings& settings);  // errors: invalid_schedule, storage_error
  void tick();                                          // call about once a second
  ScheduleStatus status() const;

 private:
  bool anyWindowOpenLocked(const LocalTime& now) const;
  size_t enabledCountLocked() const;
  void saveLocked();
  void runLocked(ScheduleAction action);

  Platform& platform_;
  Controller& controller_;
  BlobStore& blobs_;
  ClockStore& clock_;
  BluetoothControl* bluetooth_ = nullptr;
  mutable std::mutex mutex_;
  ScheduleSettings settings_;
  bool first_ = true;       // no evaluation yet since boot / since the clock arrived
  bool wasOpen_ = false;    // a window was open at the previous evaluation
  std::vector<bool> open_;  // per schedule: its window was open at the previous evaluation (to spot openings and closings)
  std::optional<int64_t> lastAbs_;  // the minute of the previous evaluation, for the actions
  bool unsaved_ = false;    // a countdown changed and could not be stored yet; tick() retries
  bool startPending_ = false;  // a window opened but the jiggler could not start (no computer paired yet): retry while it is open
};

}  // namespace jig
