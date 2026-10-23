#pragma once

#include <ArduinoJson.h>

#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "config.h"
#include "controller.h"
#include "json_codec.h"
#include "movement.h"
#include "platform.h"
#include "script.h"

namespace jig {

constexpr size_t kMaxAutomations = 4;
constexpr size_t kMaxPendingRuns = 4;

struct Automation {
  std::string id;
  std::string script;
  uint32_t minIntervalSec = 60;
  uint32_t maxIntervalSec = 180;
  bool enabled = false;
};

std::vector<Automation> defaultAutomations();
// `library` may be null (structure-only validation, used when loading stored data).
std::optional<ValidationError> validateAutomations(const std::vector<Automation>& list, const ScriptLibrary* library);
std::string automationsToJson(const std::vector<Automation>& list);
bool parseAutomations(const std::string& body, std::vector<Automation>& out, ParseError& err);

// Runs scripts on schedule (random interval in [min,max] seconds) and on demand. Scheduled runs need the
// global run state to be Running, an enabled automation and a connected HID; on-demand runs need only
// the connection. Thread-safe.
class AutomationEngine {
 public:
  AutomationEngine(Platform& platform, Rng& rng, ScriptLibrary& scripts, BlobStore& blobs);

  void begin();
  std::vector<Automation> list() const;
  CmdResult replace(const std::vector<Automation>& automations);  // error: invalid_automation
  CmdResult runNow(const std::string& script);                     // errors: not_found, mouse_not_connected, busy
  std::optional<Plan> tick(bool running);

 private:
  void scheduleLocked(size_t index);
  void scheduleAllLocked();
  std::optional<Plan> compileLocked(const std::string& script);

  Platform& platform_;
  Rng& rng_;
  ScriptLibrary& scripts_;
  BlobStore& blobs_;
  mutable std::mutex mutex_;
  std::vector<Automation> automations_;
  std::vector<uint32_t> dueMs_;
  std::deque<std::string> pending_;
};

}  // namespace jig
