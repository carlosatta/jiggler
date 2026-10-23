#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

#include "config.h"
#include "movement.h"
#include "platform.h"

namespace jig {

enum class RunState { Stopped, Running, Paused };

constexpr uint32_t kMaxRunMinutes = 1440;

struct Status {
  RunState runState = RunState::Stopped;
  bool mouseConnected = false;
  bool wifiConnected = false;
  std::optional<uint32_t> msToNextMove;  // only while Running
  std::optional<uint32_t> msToAutoStop;  // only while Running with a time limit
  uint32_t moveCount = 0;
  std::optional<uint32_t> lastMoveAgoMs;
  std::optional<uint32_t> lastMoveUnix;  // only when the wall clock is synced
  Config config;
};

struct CmdResult {
  bool ok = false;
  std::string error;  // machine-readable code: mouse_not_connected, invalid_config
  std::string field;
  std::string message;
};

// Single control surface shared by dashboard, REST and MCP. Thread-safe.
class Controller {
 public:
  Controller(Platform& platform, Rng& rng, ConfigStore& store);

  void begin();
  Status status() const;
  void start();                        // run until told otherwise (clears any time limit)
  CmdResult startFor(uint32_t minutes);  // run, then stop by itself; 1..kMaxRunMinutes (invalid_minutes)
  void pause();
  void stop();
  CmdResult moveNow();
  CmdResult updateConfig(const ConfigPatch& patch);

  // Called every ~50 ms by the movement task; returns the plan to execute, if any.
  std::optional<Plan> tick();

 private:
  void startLocked();
  void scheduleNextLocked();
  uint32_t remainingMsLocked() const;
  std::optional<Plan> fireLocked();

  Platform& platform_;
  Rng& rng_;
  ConfigStore& store_;
  mutable std::mutex mutex_;

  Config config_;
  RunState state_ = RunState::Stopped;
  uint32_t dueMs_ = 0;
  uint32_t stopAtMs_ = 0;
  bool hasStopAt_ = false;
  uint32_t pausedRemainingMs_ = 0;
  bool pendingMoveNow_ = false;
  uint32_t moveCount_ = 0;
  bool hasMoved_ = false;
  uint32_t lastMoveMs_ = 0;
};

}  // namespace jig
