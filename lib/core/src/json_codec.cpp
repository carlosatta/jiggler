#include "json_codec.h"

namespace jig {

namespace {

void fillConfig(JsonObject obj, const Config& c) {
  obj["minIntervalSec"] = c.minIntervalSec;
  obj["maxIntervalSec"] = c.maxIntervalSec;
  obj["amplitudePx"] = c.amplitudePx;
  obj["clickProbability"] = c.clickProbability;
  obj["scrollProbability"] = c.scrollProbability;
}

const char* stateName(RunState s) {
  switch (s) {
    case RunState::Running: return "running";
    case RunState::Paused: return "paused";
    default: return "stopped";
  }
}

bool fail(ParseError& err, const char* code, const std::string& field, const char* message) {
  err.code = code;
  err.field = field;
  err.message = message;
  return false;
}

}  // namespace

std::string statusToJson(const Status& s) {
  JsonDocument doc;
  doc["state"] = stateName(s.runState);
  doc["mouseConnected"] = s.mouseConnected;
  doc["wifiConnected"] = s.wifiConnected;
  if (s.msToNextMove) doc["msToNextMove"] = *s.msToNextMove;
  else doc["msToNextMove"] = nullptr;
  if (s.msToAutoStop) doc["stopsInMs"] = *s.msToAutoStop;
  else doc["stopsInMs"] = nullptr;
  if (s.msToResume) doc["resumesInMs"] = *s.msToResume;
  else doc["resumesInMs"] = nullptr;
  doc["moveCount"] = s.moveCount;
  if (s.lastMoveAgoMs) doc["lastMoveAgoMs"] = *s.lastMoveAgoMs;
  else doc["lastMoveAgoMs"] = nullptr;
  if (s.lastMoveUnix) doc["lastMoveAt"] = *s.lastMoveUnix;
  else doc["lastMoveAt"] = nullptr;
  fillConfig(doc["config"].to<JsonObject>(), s.config);
  std::string out;
  serializeJson(doc, out);
  return out;
}

std::string configToJson(const Config& config) {
  JsonDocument doc;
  fillConfig(doc.to<JsonObject>(), config);
  std::string out;
  serializeJson(doc, out);
  return out;
}

std::string errorJson(const std::string& code, const std::string& field, const std::string& message) {
  JsonDocument doc;
  doc["error"] = code;
  if (!field.empty()) doc["field"] = field;
  if (!message.empty()) doc["message"] = message;
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool parseConfigPatch(JsonObjectConst obj, ConfigPatch& out, ParseError& err) {
  ConfigPatch patch;
  for (JsonPairConst kv : obj) {
    const std::string key = kv.key().c_str();
    JsonVariantConst v = kv.value();
    if (key == "minIntervalSec" || key == "maxIntervalSec" || key == "amplitudePx") {
      if (!v.is<int64_t>()) return fail(err, "invalid_config", key, "must be an integer");
      const int64_t n = v.as<int64_t>();
      if (key == "minIntervalSec") patch.minIntervalSec = n;
      else if (key == "maxIntervalSec") patch.maxIntervalSec = n;
      else patch.amplitudePx = n;
    } else if (key == "clickProbability" || key == "scrollProbability") {
      if (!v.is<double>()) return fail(err, "invalid_config", key, "must be a number");
      const double d = v.as<double>();
      if (key == "clickProbability") patch.clickProbability = d;
      else patch.scrollProbability = d;
    } else {
      return fail(err, "invalid_config", key, "unknown field");
    }
  }
  out = patch;
  return true;
}

bool parseStartRequest(JsonObjectConst obj, std::optional<uint32_t>& minutes, ParseError& err) {
  minutes.reset();
  for (JsonPairConst kv : obj) {
    const std::string key = kv.key().c_str();
    if (key != "minutes") return fail(err, "invalid_start", key, "unknown field");
    JsonVariantConst v = kv.value();
    if (!v.is<int64_t>()) return fail(err, "invalid_minutes", key, "must be an integer");
    const int64_t n = v.as<int64_t>();
    if (n < 1 || n > kMaxRunMinutes) return fail(err, "invalid_minutes", key, "must be between 1 and 1440");
    minutes = static_cast<uint32_t>(n);
  }
  return true;
}

bool parseStartRequest(const std::string& body, std::optional<uint32_t>& minutes, ParseError& err) {
  minutes.reset();
  if (body.empty()) return true;
  JsonDocument doc;
  if (deserializeJson(doc, body) || !doc.is<JsonObject>()) return fail(err, "bad_json", "", "body must be a JSON object");
  return parseStartRequest(doc.as<JsonObjectConst>(), minutes, err);
}

bool parseConfigPatch(const std::string& body, ConfigPatch& out, ParseError& err) {
  JsonDocument doc;
  if (deserializeJson(doc, body) || !doc.is<JsonObject>())
    return fail(err, "bad_json", "", "body must be a JSON object");
  return parseConfigPatch(doc.as<JsonObjectConst>(), out, err);
}

}  // namespace jig
