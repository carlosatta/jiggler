#include "schedule.h"

#include <algorithm>
#include <cstdio>

namespace jig {

namespace {

constexpr const char* kBlobKey = "schedules";
constexpr const char* kDayNames[7] = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};

struct Zone {
  const char* name;
  const char* posix;
};

const Zone kZones[] = {
    {"UTC", "UTC0"},
    {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Lisbon", "WET0WEST,M3.5.0/1,M10.5.0"},
    {"Europe/Rome", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Paris", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Berlin", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Madrid", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Athens", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Moscow", "MSK-3"},
    {"Asia/Dubai", "<+04>-4"},
    {"Asia/Kolkata", "IST-5:30"},
    {"Asia/Singapore", "<+08>-8"},
    {"Asia/Tokyo", "JST-9"},
    {"Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"America/New_York", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Chicago", "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Denver", "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Sao_Paulo", "<-03>3"},
};

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

const std::vector<std::string>& timezoneNames() {
  static const std::vector<std::string> names = [] {
    std::vector<std::string> v;
    for (const Zone& z : kZones) v.push_back(z.name);
    return v;
  }();
  return names;
}

std::optional<std::string> posixTimezone(const std::string& name) {
  for (const Zone& z : kZones)
    if (name == z.name) return std::string(z.posix);
  return std::nullopt;
}

bool scheduleWindowContains(const Schedule& s, const LocalTime& now) {
  const int minute = now.minuteOfDay;
  if (s.days & (1 << now.weekday)) {
    const int sinceStart = minute - s.startMinute;
    if (sinceStart >= 0 && sinceStart < s.durationMin) return true;
  }
  const int yesterday = (now.weekday + 6) % 7;  // a window that started yesterday and runs past midnight
  if (s.days & (1 << yesterday)) {
    const int sinceStart = minute + 1440 - s.startMinute;
    if (sinceStart >= 0 && sinceStart < s.durationMin) return true;
  }
  return false;
}

std::optional<ValidationError> validateSchedules(const ScheduleSettings& settings) {
  if (!posixTimezone(settings.timezone)) return ValidationError{"timezone", "unknown timezone"};
  if (settings.schedules.size() > kMaxSchedules) return ValidationError{"schedules", "at most 8 schedules"};
  std::vector<std::string> seen;
  for (size_t i = 0; i < settings.schedules.size(); ++i) {
    const Schedule& s = settings.schedules[i];
    if (!validId(s.id)) return ValidationError{at(i, "id"), "must be 1 to 24 characters of a-z 0-9 _ -"};
    if (std::find(seen.begin(), seen.end(), s.id) != seen.end()) return ValidationError{at(i, "id"), "duplicate id"};
    seen.push_back(s.id);
    if (s.days == 0 || (s.days & 0x80)) return ValidationError{at(i, "days"), "pick at least one day"};
    if (s.startMinute >= 1440) return ValidationError{at(i, "start"), "must be a time between 00:00 and 23:59"};
    if (s.durationMin < 1 || s.durationMin > 1440) return ValidationError{at(i, "durationMin"), "must be between 1 and 1440 minutes"};
  }
  return std::nullopt;
}

std::string schedulesToJson(const ScheduleSettings& settings) {
  JsonDocument doc;
  doc["timezone"] = settings.timezone;
  JsonArray arr = doc["schedules"].to<JsonArray>();
  for (const Schedule& s : settings.schedules) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = s.id;
    o["enabled"] = s.enabled;
    JsonArray days = o["days"].to<JsonArray>();
    for (int d = 0; d < 7; ++d)
      if (s.days & (1 << d)) days.add(kDayNames[d]);
    o["start"] = hhmm(s.startMinute);
    o["durationMin"] = s.durationMin;
  }
  JsonArray zones = doc["timezones"].to<JsonArray>();
  for (const std::string& z : timezoneNames()) zones.add(z);
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
    if (key == "timezones") {
      continue;  // read-only list that GET adds; ignoring it lets a GET body be sent back unchanged
    } else if (key == "timezone") {
      if (!v.is<const char*>()) return parseFail(err, "timezone", "must be a string");
      parsed.timezone = v.as<std::string>();
    } else if (key == "schedules") {
      if (!v.is<JsonArrayConst>()) return parseFail(err, "schedules", "must be an array");
      parsed.schedules.clear();
      size_t i = 0;
      for (JsonVariantConst item : v.as<JsonArrayConst>()) {
        if (!item.is<JsonObjectConst>()) return parseFail(err, "schedules[" + std::to_string(i) + "]", "must be an object");
        Schedule s;
        bool hasId = false, hasDays = false, hasStart = false, hasDur = false;
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
          } else {
            return parseFail(err, field, "unknown field");
          }
        }
        if (!hasId) return parseFail(err, at(i, "id"), "is required");
        if (!hasDays) return parseFail(err, at(i, "days"), "is required");
        if (!hasStart) return parseFail(err, at(i, "start"), "is required");
        if (!hasDur) return parseFail(err, at(i, "durationMin"), "is required");
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
  doc["timeSynced"] = s.timeSynced;
  if (s.timeSynced) {
    doc["localTime"] = s.localTime;
    doc["weekday"] = s.weekday;
  } else {
    doc["localTime"] = nullptr;
    doc["weekday"] = nullptr;
  }
  doc["active"] = s.active;
  doc["enabledCount"] = s.enabledCount;
  doc["timezone"] = s.timezone;
  std::string out;
  serializeJson(doc, out);
  return out;
}

ScheduleEngine::ScheduleEngine(Platform& platform, Controller& controller, BlobStore& blobs)
    : platform_(platform), controller_(controller), blobs_(blobs) {}

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
}

ScheduleSettings ScheduleEngine::settings() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return settings_;
}

CmdResult ScheduleEngine::replace(const ScheduleSettings& next) {
  if (auto bad = validateSchedules(next)) return fail("invalid_schedule", bad->field, bad->message);
  std::lock_guard<std::mutex> lock(mutex_);
  std::string stored = schedulesToJson(next);
  if (!blobs_.put(kBlobKey, stored)) return fail("storage_error", "", "could not save the schedules (storage full?)");
  settings_ = next;
  // Saving must not start or stop anything: remember what the clock says now so only later changes count.
  const auto now = platform_.localTime(*posixTimezone(settings_.timezone));
  first_ = !now;
  wasOpen_ = now ? anyWindowOpenLocked(*now) : false;
  CmdResult r;
  r.ok = true;
  return r;
}

bool ScheduleEngine::anyWindowOpenLocked(const LocalTime& now) const {
  for (const Schedule& s : settings_.schedules)
    if (s.enabled && scheduleWindowContains(s, now)) return true;
  return false;
}

size_t ScheduleEngine::enabledCountLocked() const {
  return static_cast<size_t>(std::count_if(settings_.schedules.begin(), settings_.schedules.end(), [](const Schedule& s) { return s.enabled; }));
}

void ScheduleEngine::tick() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (enabledCountLocked() == 0) {
    first_ = true;  // the next schedule that gets enabled starts from a clean look at the clock
    wasOpen_ = false;
    return;
  }
  const auto now = platform_.localTime(*posixTimezone(settings_.timezone));
  if (!now) return;
  const bool open = anyWindowOpenLocked(*now);
  if (first_ || open != wasOpen_) {
    if (open) controller_.start();
    else controller_.stop();
  }
  first_ = false;
  wasOpen_ = open;
}

ScheduleStatus ScheduleEngine::status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  ScheduleStatus s;
  s.timezone = settings_.timezone;
  s.enabledCount = enabledCountLocked();
  const auto now = platform_.localTime(*posixTimezone(settings_.timezone));
  if (now) {
    s.timeSynced = true;
    s.localTime = hhmm(now->minuteOfDay);
    s.weekday = kDayNames[now->weekday % 7];
    s.active = anyWindowOpenLocked(*now);
  }
  return s;
}

}  // namespace jig
