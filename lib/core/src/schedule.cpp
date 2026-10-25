#include "schedule.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace jig {

namespace {

constexpr const char* kBlobKey = "schedules";
constexpr const char* kDayNames[7] = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};
constexpr const char* kActionNames[] = {"start", "stop", "pause", "resume", "bluetooth_on", "bluetooth_off", "discovery"};

bool validId(const std::string& id) {
  if (id.empty() || id.size() > 24) return false;
  for (char c : id)
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
  return true;
}

std::string at(size_t i, const char* field) { return "schedules[" + std::to_string(i) + "]." + field; }

bool parseFail(ParseError& err, const std::string& field, const char* message) {
  err.code = "invalid_schedule";
  err.field = field;
  err.message = message;
  return false;
}

std::string hhmm(unsigned minutes) {
  char buf[8];
  std::snprintf(buf, sizeof buf, "%02u:%02u", minutes / 60, minutes % 60);
  return buf;
}

CmdResult fail(const char* error, const std::string& field, const std::string& message) {
  CmdResult r;
  r.error = error;
  r.field = field;
  r.message = message;
  return r;
}

}  // namespace


const char* scheduleActionName(ScheduleAction action) { return kActionNames[static_cast<int>(action)]; }

std::optional<ScheduleAction> parseScheduleAction(const std::string& name) {
  for (size_t i = 0; i < sizeof(kActionNames) / sizeof(kActionNames[0]); ++i)
    if (name == kActionNames[i]) return static_cast<ScheduleAction>(i);
  return std::nullopt;
}

int scheduleJitterOffset(const std::string& id, int64_t day, int slot, int jitterMin) {
  if (jitterMin <= 0) return 0;
  uint32_t h = 2166136261u;
  for (unsigned char c : id) {
    h ^= c;
    h *= 16777619u;
  }
  h ^= static_cast<uint32_t>(day);
  h *= 16777619u;
  h ^= static_cast<uint32_t>(slot) + 0x9e3779b9u;
  h *= 16777619u;
  h ^= h >> 15;
  h *= 0x2c1b3c6du;
  h ^= h >> 12;
  return static_cast<int>(h % static_cast<uint32_t>(2 * jitterMin + 1)) - jitterMin;
}

bool scheduleWindowContains(const Schedule& s, const LocalTime& now) {
  if (s.kind != ScheduleKind::Window) return false;
  // Anchor days: two days back (a 24 h window shifted late), yesterday, today and tomorrow (a start shifted
  // to before midnight). `rel` is the minute counted from that anchor day's midnight.
  for (int k = -2; k <= 1; ++k) {
    const int wd = ((now.weekday + k) % 7 + 7) % 7;
    if (!(s.days & (1 << wd))) continue;
    const int64_t anchor = static_cast<int64_t>(now.day) + k;
    const int start = s.startMinute + scheduleJitterOffset(s.id, anchor, 1, s.jitterMin);
    int end = s.startMinute + s.durationMin + scheduleJitterOffset(s.id, anchor, 2, s.jitterMin);
    if (end <= start) end = start + 1;
    const int rel = now.minuteOfDay - k * 1440;
    if (rel >= start && rel < end) return true;
  }
  return false;
}

bool scheduleActionDue(const Schedule& s, const LocalTime& now, int64_t fromAbs, int64_t toAbs) {
  if (s.kind != ScheduleKind::Action) return false;
  for (int k = -1; k <= 1; ++k) {
    const int wd = ((now.weekday + k) % 7 + 7) % 7;
    if (!(s.days & (1 << wd))) continue;
    const int64_t anchor = static_cast<int64_t>(now.day) + k;
    const int64_t at = anchor * 1440 + s.startMinute + scheduleJitterOffset(s.id, anchor, 0, s.jitterMin);
    if (at > fromAbs && at <= toAbs) return true;
  }
  return false;
}

std::optional<ValidationError> validateSchedules(const ScheduleSettings& settings) {
  if (settings.schedules.size() > kMaxSchedules) return ValidationError{"schedules", "at most 8 schedules"};
  std::vector<std::string> seen;
  for (size_t i = 0; i < settings.schedules.size(); ++i) {
    const Schedule& s = settings.schedules[i];
    if (!validId(s.id)) return ValidationError{at(i, "id"), "must be 1 to 24 characters of a-z 0-9 _ -"};
    if (std::find(seen.begin(), seen.end(), s.id) != seen.end()) return ValidationError{at(i, "id"), "duplicate id"};
    seen.push_back(s.id);
    if (s.days == 0 || (s.days & 0x80)) return ValidationError{at(i, "days"), "pick at least one day"};
    if (s.startMinute >= 1440) return ValidationError{at(i, "start"), "must be a time between 00:00 and 23:59"};
    if (s.kind == ScheduleKind::Window && (s.durationMin < 1 || s.durationMin > 1440))
      return ValidationError{at(i, "durationMin"), "must be between 1 and 1440 minutes"};
    if (s.jitterMin > kMaxJitterMin) return ValidationError{at(i, "jitterMin"), "must be between 0 and 120 minutes"};
    if (s.times > kMaxRepeat) return ValidationError{at(i, "times"), "must be between 1 and 999, or empty for no limit"};
    if (s.times > 0 && s.remaining > s.times) return ValidationError{at(i, "remaining"), "cannot be more than times"};
  }
  return std::nullopt;
}

std::string schedulesToJson(const ScheduleSettings& settings) {
  JsonDocument doc;
  JsonArray arr = doc["schedules"].to<JsonArray>();
  for (const Schedule& s : settings.schedules) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = s.id;
    o["enabled"] = s.enabled;
    o["kind"] = s.kind == ScheduleKind::Window ? "window" : "action";
    JsonArray days = o["days"].to<JsonArray>();
    for (int d = 0; d < 7; ++d)
      if (s.days & (1 << d)) days.add(kDayNames[d]);
    o["start"] = hhmm(s.startMinute);
    if (s.kind == ScheduleKind::Window) o["durationMin"] = s.durationMin;
    else o["action"] = scheduleActionName(s.action);
    o["jitterMin"] = s.jitterMin;
    if (s.times) {
      o["times"] = s.times;
      o["remaining"] = s.remaining;
    } else {
      o["times"] = nullptr;
      o["remaining"] = nullptr;
    }
  }
  JsonArray actions = doc["actions"].to<JsonArray>();
  for (const char* a : kActionNames) actions.add(a);
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool parseSchedules(const std::string& body, ScheduleSettings& out, ParseError& err) {
  JsonDocument doc;
  if (deserializeJson(doc, body) || !doc.is<JsonObject>()) {
    err.code = "bad_json";
    err.field = "";
    err.message = "body must be a JSON object";
    return false;
  }
  ScheduleSettings parsed = out;
  for (JsonPairConst kv : doc.as<JsonObjectConst>()) {
    const std::string key = kv.key().c_str();
    JsonVariantConst v = kv.value();
    if (key == "actions" || key == "timezone" || key == "timezones") {
      continue;  // read-only list that GET adds, and the timezone older versions kept here (it lives in /api/time now)
    } else if (key == "schedules") {
      if (!v.is<JsonArrayConst>()) return parseFail(err, "schedules", "must be an array");
      parsed.schedules.clear();
      size_t i = 0;
      for (JsonVariantConst item : v.as<JsonArrayConst>()) {
        if (!item.is<JsonObjectConst>()) return parseFail(err, "schedules[" + std::to_string(i) + "]", "must be an object");
        Schedule s;
        bool hasId = false, hasDays = false, hasStart = false, hasDur = false, hasRemaining = false, hasAction = false;
        for (JsonPairConst f : item.as<JsonObjectConst>()) {
          const std::string name = f.key().c_str();
          JsonVariantConst fv = f.value();
          const std::string field = at(i, name.c_str());
          if (name == "id") {
            if (!fv.is<const char*>()) return parseFail(err, field, "must be a string");
            s.id = fv.as<std::string>();
            hasId = true;
          } else if (name == "enabled") {
            if (!fv.is<bool>()) return parseFail(err, field, "must be true or false");
            s.enabled = fv.as<bool>();
          } else if (name == "kind") {
            const std::string k = fv.is<const char*>() ? fv.as<std::string>() : std::string();
            if (k == "window") s.kind = ScheduleKind::Window;
            else if (k == "action") s.kind = ScheduleKind::Action;
            else return parseFail(err, field, "must be window or action");
          } else if (name == "action") {
            const auto a = fv.is<const char*>() ? parseScheduleAction(fv.as<std::string>()) : std::nullopt;
            if (!a) return parseFail(err, field, "must be start, stop, pause, resume, bluetooth_on, bluetooth_off or discovery");
            s.action = *a;
            hasAction = true;
          } else if (name == "days") {
            if (!fv.is<JsonArrayConst>()) return parseFail(err, field, "must be an array of day names");
            s.days = 0;
            for (JsonVariantConst d : fv.as<JsonArrayConst>()) {
              int idx = -1;
              if (d.is<const char*>())
                for (int k = 0; k < 7; ++k)
                  if (d.as<std::string>() == kDayNames[k]) idx = k;
              if (idx < 0) return parseFail(err, field, "days must be mon, tue, wed, thu, fri, sat or sun");
              s.days |= static_cast<uint8_t>(1 << idx);
            }
            hasDays = true;
          } else if (name == "start") {
            const std::string t = fv.is<const char*>() ? fv.as<std::string>() : std::string();
            const bool shape = t.size() == 5 && t[2] == ':' && std::isdigit(static_cast<unsigned char>(t[0])) &&
                               std::isdigit(static_cast<unsigned char>(t[1])) && std::isdigit(static_cast<unsigned char>(t[3])) &&
                               std::isdigit(static_cast<unsigned char>(t[4]));
            const int h = shape ? (t[0] - '0') * 10 + (t[1] - '0') : 99;
            const int m = shape ? (t[3] - '0') * 10 + (t[4] - '0') : 99;
            if (!shape || h > 23 || m > 59) return parseFail(err, field, "must be a time like 09:30");
            s.startMinute = static_cast<uint16_t>(h * 60 + m);
            hasStart = true;
          } else if (name == "durationMin") {
            if (!fv.is<int64_t>() || fv.as<int64_t>() < 1 || fv.as<int64_t>() > 1440)
              return parseFail(err, field, "must be an integer between 1 and 1440");
            s.durationMin = static_cast<uint16_t>(fv.as<int64_t>());
            hasDur = true;
          } else if (name == "jitterMin") {
            if (!fv.is<int64_t>() || fv.as<int64_t>() < 0 || fv.as<int64_t>() > kMaxJitterMin)
              return parseFail(err, field, "must be a whole number of minutes from 0 to 120");
            s.jitterMin = static_cast<uint16_t>(fv.as<int64_t>());
          } else if (name == "times" || name == "remaining") {
            if (fv.isNull()) continue;
            const bool ok = fv.is<int64_t>() && fv.as<int64_t>() >= (name == "times" ? 1 : 0) && fv.as<int64_t>() <= kMaxRepeat;
            if (!ok) return parseFail(err, field, name == "times" ? "must be a whole number from 1 to 999, or empty for no limit" : "must be a whole number from 0 to 999");
            (name == "times" ? s.times : s.remaining) = static_cast<uint16_t>(fv.as<int64_t>());
            if (name == "remaining") hasRemaining = true;
          } else {
            return parseFail(err, field, "unknown field");
          }
        }
        if (s.times > 0 && !hasRemaining) s.remaining = s.times;
        if (!hasId) return parseFail(err, at(i, "id"), "is required");
        if (!hasDays) return parseFail(err, at(i, "days"), "is required");
        if (!hasStart) return parseFail(err, at(i, "start"), "is required");
        if (s.kind == ScheduleKind::Window && !hasDur) return parseFail(err, at(i, "durationMin"), "is required for a window");
        if (s.kind == ScheduleKind::Action && !hasAction) return parseFail(err, at(i, "action"), "is required for an action");
        parsed.schedules.push_back(s);
        ++i;
      }
    } else {
      return parseFail(err, key, "unknown field");
    }
  }
  out = std::move(parsed);
  return true;
}

std::string scheduleStatusToJson(const ScheduleStatus& s) {
  JsonDocument doc;
  doc["active"] = s.active;
  doc["enabledCount"] = s.enabledCount;
  if (s.blocked.empty()) doc["blocked"] = nullptr;
  else doc["blocked"] = s.blocked;
  std::string out;
  serializeJson(doc, out);
  return out;
}

namespace {

bool sameSchedule(const Schedule& a, const Schedule& b) {
  return a.id == b.id && a.enabled == b.enabled && a.kind == b.kind && a.days == b.days && a.startMinute == b.startMinute &&
         a.durationMin == b.durationMin && a.action == b.action && a.jitterMin == b.jitterMin && a.times == b.times;
}

}  // namespace

ScheduleEngine::ScheduleEngine(Platform& platform, Controller& controller, BlobStore& blobs, ClockStore& clock)
    : platform_(platform), controller_(controller), blobs_(blobs), clock_(clock) {}

void ScheduleEngine::begin() {
  std::lock_guard<std::mutex> lock(mutex_);
  settings_ = ScheduleSettings{};
  const std::string raw = blobs_.get(kBlobKey);
  if (!raw.empty()) {
    ScheduleSettings stored;
    ParseError err;
    if (parseSchedules(raw, stored, err) && !validateSchedules(stored)) settings_ = std::move(stored);
  }
  first_ = true;
  wasOpen_ = false;
  startPending_ = false;
  unsaved_ = false;
  lastAbs_.reset();
  open_.assign(settings_.schedules.size(), false);
}

ScheduleSettings ScheduleEngine::settings() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return settings_;
}

CmdResult ScheduleEngine::replace(const ScheduleSettings& requested) {
  if (auto bad = validateSchedules(requested)) return fail("invalid_schedule", bad->field, bad->message);
  ScheduleSettings next = requested;
  for (Schedule& s : next.schedules) {
    if (s.times == 0) s.remaining = 0;                       // a count that is not set has nothing to count down
    else if (s.enabled && s.remaining == 0) s.remaining = s.times;  // switched back on after finishing: a fresh run
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (!blobs_.put(kBlobKey, schedulesToJson(next))) return fail("storage_error", "", "could not save the schedules (storage full?)");
  // An entry the owner did not touch keeps what the engine knew about it; a new or edited one is looked at afresh,
  // so a window that is open right now opens (and starts the jiggler) at the next tick.
  open_.resize(settings_.schedules.size(), false);
  std::vector<bool> carried(next.schedules.size(), false);
  for (size_t i = 0; i < next.schedules.size(); ++i)
    for (size_t j = 0; j < settings_.schedules.size(); ++j)
      if (sameSchedule(next.schedules[i], settings_.schedules[j])) carried[i] = open_[j];
  settings_ = next;
  unsaved_ = false;
  first_ = false;  // the owner has just looked at the schedules; from here only edges count
  open_ = carried;
  wasOpen_ = false;
  for (size_t i = 0; i < open_.size(); ++i) wasOpen_ = wasOpen_ || (open_[i] && settings_.schedules[i].enabled);
  CmdResult r;
  r.ok = true;
  return r;
}

void ScheduleEngine::saveLocked() { unsaved_ = !blobs_.put(kBlobKey, schedulesToJson(settings_)); }

bool ScheduleEngine::anyWindowOpenLocked(const LocalTime& now) const {
  for (const Schedule& s : settings_.schedules)
    if (s.enabled && scheduleWindowContains(s, now)) return true;
  return false;
}

size_t ScheduleEngine::enabledCountLocked() const {
  return static_cast<size_t>(std::count_if(settings_.schedules.begin(), settings_.schedules.end(), [](const Schedule& s) { return s.enabled; }));
}

void ScheduleEngine::runLocked(ScheduleAction action) {
  switch (action) {
    case ScheduleAction::Start: controller_.start(); break;
    case ScheduleAction::Stop: controller_.stop(); break;
    case ScheduleAction::Pause: controller_.pause(); break;
    case ScheduleAction::Resume: controller_.resume(); break;
    case ScheduleAction::BluetoothOn: if (bluetooth_) bluetooth_->enableBluetooth(); break;
    case ScheduleAction::BluetoothOff: if (bluetooth_) bluetooth_->disableBluetooth(); break;
    case ScheduleAction::Discovery: if (bluetooth_) bluetooth_->openDiscovery(); break;
  }
}

void ScheduleEngine::tick() {
  std::lock_guard<std::mutex> lock(mutex_);
  open_.resize(settings_.schedules.size(), false);
  if (unsaved_) saveLocked();
  if (enabledCountLocked() == 0) {
    first_ = true;  // the next schedule that gets enabled starts from a clean look at the clock
    wasOpen_ = false;
    startPending_ = false;
    open_.assign(open_.size(), false);
    lastAbs_.reset();
    return;
  }
  const auto now = platform_.localTime(clock_.posixTz());
  if (!now) {
    lastAbs_.reset();
    return;
  }
  const int64_t absNow = static_cast<int64_t>(now->day) * 1440 + now->minuteOfDay;
  // Actions fire when their minute arrives. After a long gap (the clock was set, or the board was busy) nothing is
  // replayed: five minutes is the most that counts as "just now".
  const bool canFire = lastAbs_ && absNow > *lastAbs_ && absNow - *lastAbs_ <= 5;

  std::vector<ScheduleAction> due;
  bool open = false, changed = false;
  for (size_t i = 0; i < settings_.schedules.size(); ++i) {
    Schedule& s = settings_.schedules[i];
    if (!s.enabled) {
      open_[i] = false;
      continue;
    }
    if (s.kind == ScheduleKind::Action) {
      open_[i] = false;
      if (canFire && scheduleActionDue(s, *now, *lastAbs_, absNow)) {
        if (s.times > 0) {
          if (s.remaining == 0) {
            s.enabled = false;
            changed = true;
            continue;
          }
          s.remaining--;
          changed = true;
          if (s.remaining == 0) s.enabled = false;  // that was the last one
        }
        due.push_back(s.action);
      }
      continue;
    }
    const bool in = scheduleWindowContains(s, *now);
    if (s.times > 0) {
      bool spent = false;
      if (first_) {
        spent = !in && s.remaining == 0;                      // used up while the board was off
      } else if (in && !open_[i]) {
        if (s.remaining == 0) spent = true;                   // switched on with nothing left
        else { s.remaining--; changed = true; }               // a new window opens: that is one run
      } else if (!in && open_[i] && s.remaining == 0) {
        spent = true;                                         // the last run just ended
      }
      if (spent) {
        s.enabled = false;
        changed = true;
        open_[i] = false;
        continue;
      }
    }
    open_[i] = in;
    open = open || in;
  }
  if (changed) saveLocked();

  if (first_ || open != wasOpen_) {
    if (open) startPending_ = !controller_.start().ok;
    else {
      controller_.stop();
      startPending_ = false;
    }
  } else if (open && startPending_) {
    startPending_ = !controller_.start().ok;
  }
  for (ScheduleAction a : due) runLocked(a);
  first_ = false;
  wasOpen_ = open;
  lastAbs_ = absNow;
}

ScheduleStatus ScheduleEngine::status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  ScheduleStatus s;
  s.enabledCount = enabledCountLocked();
  const auto now = platform_.localTime(clock_.posixTz());
  if (now) s.active = anyWindowOpenLocked(*now);
  if (s.active && startPending_) s.blocked = "no_computer_paired";
  return s;
}

}  // namespace jig
