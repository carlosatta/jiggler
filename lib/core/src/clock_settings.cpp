#include "clock_settings.h"

#include <cctype>
#include <cstdio>

namespace jig {

namespace {

constexpr const char* kBlobKey = "clock";
constexpr const char* kLegacyScheduleKey = "schedules";  // older versions kept the timezone with the schedules
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


std::string hhmm(unsigned minutes) {
  char buf[8];
  std::snprintf(buf, sizeof buf, "%02u:%02u", minutes / 60, minutes % 60);
  return buf;
}

bool parseFail(ParseError& err, const std::string& field, const char* message) {
  err.code = "invalid_clock";
  err.field = field;
  err.message = message;
  return false;
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

const char* weekdayName(unsigned weekday) { return kDayNames[weekday % 7]; }

bool validNtpServer(const std::string& server) {
  if (server.empty()) return true;
  if (server.size() > 63) return false;
  for (unsigned char c : server)
    if (!(std::isalnum(c) || c == '.' || c == '-')) return false;
  return server.front() != '.' && server.front() != '-' && server.back() != '-';
}

std::string clockStatusToJson(const ClockStatus& s) {
  JsonDocument doc;
  doc["synced"] = s.synced;
  doc["source"] = s.source;
  doc["timezone"] = s.timezone;
  if (s.synced) {
    doc["localTime"] = s.localTime;
    doc["weekday"] = s.weekday;
    doc["epoch"] = s.epoch;
  } else {
    doc["localTime"] = nullptr;
    doc["weekday"] = nullptr;
    doc["epoch"] = nullptr;
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

std::string clockToJson(const ClockSettings& settings, const ClockStatus& status) {
  JsonDocument doc;
  doc["timezone"] = settings.timezone;
  doc["ntpServer"] = settings.ntpServer;
  JsonDocument st;
  deserializeJson(st, clockStatusToJson(status));
  doc["status"] = st;
  JsonArray zones = doc["timezones"].to<JsonArray>();
  for (const std::string& z : timezoneNames()) zones.add(z);
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool parseClockSettings(const std::string& body, ClockSettings& out, ParseError& err) {
  JsonDocument doc;
  if (deserializeJson(doc, body) || !doc.is<JsonObject>()) {
    err.code = "bad_json";
    err.field = "";
    err.message = "body must be a JSON object";
    return false;
  }
  ClockSettings parsed = out;
  for (JsonPairConst kv : doc.as<JsonObjectConst>()) {
    const std::string key = kv.key().c_str();
    if (key == "timezones" || key == "status") continue;  // read-only parts of a GET body
    if (key == "timezone") {
      if (!kv.value().is<const char*>() || !posixTimezone(kv.value().as<std::string>())) return parseFail(err, key, "unknown timezone");
      parsed.timezone = kv.value().as<std::string>();
    } else if (key == "ntpServer") {
      if (!kv.value().is<const char*>() || !validNtpServer(kv.value().as<std::string>()))
        return parseFail(err, key, "must be a host name such as pool.ntp.org, or empty for no NTP");
      parsed.ntpServer = kv.value().as<std::string>();
    } else {
      return parseFail(err, key, "unknown field");
    }
  }
  out = std::move(parsed);
  return true;
}

ClockStore::ClockStore(Platform& platform, BlobStore& blobs) : platform_(platform), blobs_(blobs) {}

void ClockStore::begin() {
  std::lock_guard<std::mutex> lock(mutex_);
  settings_ = ClockSettings{};
  std::string raw = blobs_.get(kBlobKey);
  if (raw.empty()) raw = blobs_.get(kLegacyScheduleKey);  // a timezone chosen on the old Schedule page
  if (!raw.empty()) {
    ClockSettings stored;
    JsonDocument doc;
    if (!deserializeJson(doc, raw) && doc.is<JsonObject>()) {
      if (doc["timezone"].is<const char*>() && posixTimezone(doc["timezone"].as<std::string>())) stored.timezone = doc["timezone"].as<std::string>();
      if (doc["ntpServer"].is<const char*>() && validNtpServer(doc["ntpServer"].as<std::string>())) stored.ntpServer = doc["ntpServer"].as<std::string>();
      settings_ = stored;
    }
  }
  platform_.setNtpServer(settings_.ntpServer);
}

ClockSettings ClockStore::settings() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return settings_;
}

std::string ClockStore::posixTz() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return posixTimezone(settings_.timezone).value_or("UTC0");
}

CmdResult ClockStore::replace(const ClockSettings& next) {
  if (!posixTimezone(next.timezone)) return fail("invalid_clock", "timezone", "unknown timezone");
  if (!validNtpServer(next.ntpServer)) return fail("invalid_clock", "ntpServer", "must be a host name such as pool.ntp.org, or empty for no NTP");
  JsonDocument doc;
  doc["timezone"] = next.timezone;
  doc["ntpServer"] = next.ntpServer;
  std::string raw;
  serializeJson(doc, raw);
  std::lock_guard<std::mutex> lock(mutex_);
  if (!blobs_.put(kBlobKey, raw)) return fail("storage_error", "", "could not save the clock settings (storage full?)");
  settings_ = next;
  platform_.setNtpServer(settings_.ntpServer);
  CmdResult r;
  r.ok = true;
  return r;
}

CmdResult ClockStore::setTime(uint32_t epoch) {
  if (epoch < 1700000000u || epoch > 4102444800u) return fail("invalid_time", "epoch", "must be a Unix time in seconds between 2023 and 2100");
  if (!platform_.setUnixTime(epoch)) return fail("not_supported", "", "this device cannot set the time");
  CmdResult r;
  r.ok = true;
  return r;
}

ClockStatus ClockStore::status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  ClockStatus s;
  s.timezone = settings_.timezone;
  const auto now = platform_.localTime(posixTimezone(settings_.timezone).value_or("UTC0"));
  s.source = platform_.timeSource();
  if (now) {
    s.synced = true;
    s.localTime = hhmm(now->minuteOfDay);
    s.weekday = kDayNames[now->weekday % 7];
    s.epoch = platform_.unixSeconds();
  } else {
    s.source = "none";
  }
  return s;
}

}  // namespace jig
