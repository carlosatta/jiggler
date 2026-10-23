#include "automation.h"

#include <set>

namespace jig {

namespace {

constexpr const char* kBlobKey = "autos";
constexpr int64_t kMinIntervalSec = 5;
constexpr int64_t kMaxIntervalSec = 86400;

bool validId(const std::string& id) {
  if (id.empty() || id.size() > 24) return false;
  for (char c : id)
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
  return true;
}

std::string at(size_t i, const char* field) { return "automations[" + std::to_string(i) + "]." + field; }

bool parseFail(ParseError& err, const std::string& field, const char* message) {
  err.code = "invalid_automation";
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

std::vector<Automation> defaultAutomations() {
  Automation keepalive;
  keepalive.id = "keepalive-key";
  keepalive.script = "keepalive-key";
  keepalive.minIntervalSec = 60;
  keepalive.maxIntervalSec = 180;
  keepalive.enabled = false;
#ifdef JIG_DEBUG_AUTOMATION
  keepalive.enabled = true;  // debug builds only: fast, enabled keep-alive to exercise the engine on a board
  keepalive.minIntervalSec = keepalive.maxIntervalSec = 5;
#endif
  return {keepalive};
}

std::optional<ValidationError> validateAutomations(const std::vector<Automation>& list, const ScriptLibrary* library) {
  if (list.size() > kMaxAutomations) return ValidationError{"automations", "at most 4 automations"};
  std::set<std::string> ids;
  for (size_t i = 0; i < list.size(); i++) {
    const Automation& a = list[i];
    if (!validId(a.id)) return ValidationError{at(i, "id"), "must be 1 to 24 characters of a-z 0-9 _ -"};
    if (!ids.insert(a.id).second) return ValidationError{at(i, "id"), "duplicate id"};
    if (a.script.empty() || (library && !library->get(a.script))) return ValidationError{at(i, "script"), "unknown script"};
    if (a.minIntervalSec < kMinIntervalSec || a.minIntervalSec > kMaxIntervalSec)
      return ValidationError{at(i, "minIntervalSec"), "must be between 5 and 86400"};
    if (a.maxIntervalSec < kMinIntervalSec || a.maxIntervalSec > kMaxIntervalSec)
      return ValidationError{at(i, "maxIntervalSec"), "must be between 5 and 86400"};
    if (a.maxIntervalSec < a.minIntervalSec) return ValidationError{at(i, "maxIntervalSec"), "must be >= minIntervalSec"};
  }
  return std::nullopt;
}

std::string automationsToJson(const std::vector<Automation>& list) {
  JsonDocument doc;
  JsonArray arr = doc["automations"].to<JsonArray>();
  for (const Automation& a : list) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = a.id;
    o["script"] = a.script;
    o["minIntervalSec"] = a.minIntervalSec;
    o["maxIntervalSec"] = a.maxIntervalSec;
    o["enabled"] = a.enabled;
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool parseAutomations(const std::string& body, std::vector<Automation>& out, ParseError& err) {
  JsonDocument doc;
  if (deserializeJson(doc, body) || !doc.is<JsonObject>()) {
    err.code = "bad_json";
    err.field = "";
    err.message = "body must be a JSON object";
    return false;
  }
  if (!doc["automations"].is<JsonArray>()) return parseFail(err, "automations", "must be an array");

  std::vector<Automation> parsed;
  size_t i = 0;
  for (JsonVariantConst item : doc["automations"].as<JsonArrayConst>()) {
    if (!item.is<JsonObjectConst>()) return parseFail(err, "automations[" + std::to_string(i) + "]", "must be an object");
    Automation a;
    bool hasId = false, hasScript = false, hasMin = false, hasMax = false, hasEnabled = false;
    for (JsonPairConst kv : item.as<JsonObjectConst>()) {
      const std::string key = kv.key().c_str();
      JsonVariantConst v = kv.value();
      const std::string field = at(i, key.c_str());
      if (key == "id" || key == "script") {
        if (!v.is<const char*>()) return parseFail(err, field, "must be a string");
        (key == "id" ? a.id : a.script) = v.as<std::string>();
        (key == "id" ? hasId : hasScript) = true;
      } else if (key == "minIntervalSec" || key == "maxIntervalSec") {
        if (!v.is<int64_t>() || v.as<int64_t>() < 0 || v.as<int64_t>() > kMaxIntervalSec * 2)
          return parseFail(err, field, "must be an integer");
        (key == "minIntervalSec" ? a.minIntervalSec : a.maxIntervalSec) = static_cast<uint32_t>(v.as<int64_t>());
        (key == "minIntervalSec" ? hasMin : hasMax) = true;
      } else if (key == "enabled") {
        if (!v.is<bool>()) return parseFail(err, field, "must be true or false");
        a.enabled = v.as<bool>();
        hasEnabled = true;
      } else {
        return parseFail(err, field, "unknown field");
      }
    }
    if (!hasId) return parseFail(err, at(i, "id"), "is required");
    if (!hasScript) return parseFail(err, at(i, "script"), "is required");
    if (!hasMin) return parseFail(err, at(i, "minIntervalSec"), "is required");
    if (!hasMax) return parseFail(err, at(i, "maxIntervalSec"), "is required");
    if (!hasEnabled) return parseFail(err, at(i, "enabled"), "is required");
    parsed.push_back(std::move(a));
    i++;
  }
  out = std::move(parsed);
  return true;
}

AutomationEngine::AutomationEngine(Platform& platform, Rng& rng, ScriptLibrary& scripts, BlobStore& blobs)
    : platform_(platform), rng_(rng), scripts_(scripts), blobs_(blobs) {}

void AutomationEngine::begin() {
  std::lock_guard<std::mutex> lock(mutex_);
  automations_ = defaultAutomations();
  const std::string raw = blobs_.get(kBlobKey);
  if (!raw.empty()) {
    std::vector<Automation> stored;
    ParseError err;
    if (parseAutomations(raw, stored, err) && !validateAutomations(stored, nullptr)) automations_ = std::move(stored);
  }
  scheduleAllLocked();
}

std::vector<Automation> AutomationEngine::list() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return automations_;
}

CmdResult AutomationEngine::replace(const std::vector<Automation>& automations) {
  if (auto bad = validateAutomations(automations, &scripts_)) return fail("invalid_automation", bad->field, bad->message);
  std::lock_guard<std::mutex> lock(mutex_);
  if (!blobs_.put(kBlobKey, automationsToJson(automations))) return fail("storage_error", "", "could not save the automations (storage full?)");
  automations_ = automations;
  scheduleAllLocked();
  CmdResult r;
  r.ok = true;
  return r;
}

CmdResult AutomationEngine::runNow(const std::string& script) {
  if (!scripts_.get(script)) return fail("not_found", "name", "no such script");
  if (!platform_.mouseConnected()) return fail("mouse_not_connected", "", "the mouse is not connected to the computer");
  std::lock_guard<std::mutex> lock(mutex_);
  if (pending_.size() >= kMaxPendingRuns) return fail("busy", "", "too many runs already queued");
  pending_.push_back({script, std::nullopt});
  CmdResult r;
  r.ok = true;
  return r;
}

CmdResult AutomationEngine::runInline(const std::string& scriptJson) {
  Plan plan;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (auto err = compileScript(scriptJson, rng_, plan)) return fail("invalid_script", err->path, err->message);
  }
  if (!platform_.mouseConnected()) return fail("mouse_not_connected", "", "the mouse is not connected to the computer");
  std::lock_guard<std::mutex> lock(mutex_);
  if (pending_.size() >= kMaxPendingRuns) return fail("busy", "", "too many runs already queued");
  pending_.push_back({"", std::move(plan)});
  CmdResult r;
  r.ok = true;
  return r;
}

std::optional<Plan> AutomationEngine::tick(bool running) {
  // Decide what to run under the lock, but read the script and compile it after releasing it: compiling
  // can take a while and every HTTP handler touching the engine would otherwise block behind it.
  std::string script;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pending_.empty()) {
      Pending next = std::move(pending_.front());
      pending_.pop_front();
      if (!platform_.mouseConnected()) return std::nullopt;  // an on-demand run must not fire later on reconnect
      if (next.plan) return std::move(next.plan);
      script = next.script;
    } else {
      if (!running || !platform_.mouseConnected()) return std::nullopt;
      const uint32_t now = platform_.nowMs();
      for (size_t i = 0; i < automations_.size(); i++) {
        if (!automations_[i].enabled || static_cast<int32_t>(now - dueMs_[i]) < 0) continue;
        scheduleLocked(i);  // reschedule first so a script that fails to compile is not retried every tick
        script = automations_[i].script;
        break;  // other due automations are served by the next tick
      }
    }
  }
  if (script.empty()) return std::nullopt;
  return compileNamed(script);
}

void AutomationEngine::scheduleLocked(size_t index) {
  const Automation& a = automations_[index];
  const int32_t intervalMs = randRange(rng_, static_cast<int32_t>(a.minIntervalSec * 1000), static_cast<int32_t>(a.maxIntervalSec * 1000));
  dueMs_[index] = platform_.nowMs() + static_cast<uint32_t>(intervalMs);
}

void AutomationEngine::scheduleAllLocked() {
  dueMs_.assign(automations_.size(), 0);
  for (size_t i = 0; i < automations_.size(); i++) scheduleLocked(i);
}

std::optional<Plan> AutomationEngine::compileNamed(const std::string& script) {
  auto json = scripts_.get(script);
  if (!json) return std::nullopt;
  Plan plan;
  if (compileScript(*json, rng_, plan)) return std::nullopt;
  return plan;
}

}  // namespace jig
