#include "controller.h"

namespace jig {

Controller::Controller(Platform& platform, Rng& rng, ConfigStore& store)
    : platform_(platform), rng_(rng), store_(store) {}

void Controller::begin() {
  std::lock_guard<std::mutex> lock(mutex_);
  const Config loaded = store_.load();
  Config validated;
  config_ = applyPatch(loaded, ConfigPatch{}, validated) ? Config{} : validated;
  state_ = platform_.computerPaired() ? RunState::Running : RunState::Stopped;
  if (state_ == RunState::Running) scheduleNextLocked();
}

Status Controller::status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  Status s;
  s.runState = state_;
  s.mouseConnected = platform_.mouseConnected();
  s.wifiConnected = platform_.wifiConnected();
  if (state_ == RunState::Running) s.msToNextMove = remainingMsLocked();
  if (state_ != RunState::Stopped && hasStopAt_) {
    const int32_t left = static_cast<int32_t>(stopAtMs_ - platform_.nowMs());
    s.msToAutoStop = left > 0 ? static_cast<uint32_t>(left) : 0;
  }
  if (state_ == RunState::Paused && hasResumeAt_) {
    const int32_t left = static_cast<int32_t>(resumeAtMs_ - platform_.nowMs());
    s.msToResume = left > 0 ? static_cast<uint32_t>(left) : 0;
  }
  s.moveCount = moveCount_;
  if (hasMoved_) {
    const uint32_t ago = platform_.nowMs() - lastMoveMs_;
    s.lastMoveAgoMs = ago;
    const uint32_t unix = platform_.unixSeconds();
    if (unix != 0 && unix >= ago / 1000) s.lastMoveUnix = unix - ago / 1000;
  }
  s.config = config_;
  return s;
}

namespace {
CmdResult noComputer() {
  CmdResult r;
  r.error = "no_computer_paired";
  r.message = "pair a computer first: there is nothing to keep awake yet";
  return r;
}
}  // namespace

CmdResult Controller::start() {
  std::lock_guard<std::mutex> lock(mutex_);
  CmdResult r = startLocked();
  if (r.ok) {
    hasStopAt_ = false;
    hasResumeAt_ = false;
  }
  return r;
}

CmdResult Controller::startFor(uint32_t minutes) {
  CmdResult r;
  if (minutes < 1 || minutes > kMaxRunMinutes) {
    r.error = "invalid_minutes";
    r.field = "minutes";
    r.message = "must be between 1 and 1440";
    return r;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  r = startLocked();
  if (!r.ok) return r;
  hasResumeAt_ = false;
  stopAtMs_ = platform_.nowMs() + minutes * 60000u;
  hasStopAt_ = true;
  return r;
}

CmdResult Controller::startLocked() {
  if (!platform_.computerPaired()) return noComputer();
  CmdResult ok;
  ok.ok = true;
  if (state_ == RunState::Running) return ok;
  if (state_ == RunState::Paused) {
    dueMs_ = platform_.nowMs() + pausedRemainingMs_;
    state_ = RunState::Running;
  } else {
    state_ = RunState::Running;
    scheduleNextLocked();
  }
  return ok;
}

void Controller::pause() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ != RunState::Running) return;
  pausedRemainingMs_ = remainingMsLocked();
  state_ = RunState::Paused;
  hasResumeAt_ = false;  // a time limit, if any, keeps running: it is a moment, not a stopwatch
}

CmdResult Controller::pauseFor(uint32_t minutes) {
  CmdResult r;
  if (minutes < 1 || minutes > kMaxRunMinutes) {
    r.error = "invalid_minutes";
    r.field = "minutes";
    r.message = "must be between 1 and 1440";
    return r;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ != RunState::Running) {
    r.error = "not_running";
    r.message = "only a running jiggler can be paused";
    return r;
  }
  pausedRemainingMs_ = remainingMsLocked();
  state_ = RunState::Paused;
  resumeAtMs_ = platform_.nowMs() + minutes * 60000u;
  hasResumeAt_ = true;
  r.ok = true;
  return r;
}

CmdResult Controller::resume() {
  std::lock_guard<std::mutex> lock(mutex_);
  CmdResult r;
  if (state_ != RunState::Paused) {
    r.error = "not_paused";
    r.message = "the jiggler is not paused";
    return r;
  }
  hasResumeAt_ = false;
  return startLocked();
}

void Controller::stop() {
  std::lock_guard<std::mutex> lock(mutex_);
  state_ = RunState::Stopped;
  hasStopAt_ = false;
  hasResumeAt_ = false;
}

CmdResult Controller::moveNow() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!platform_.mouseConnected()) {
    CmdResult r;
    r.error = "mouse_not_connected";
    r.message = "the mouse is not connected to the computer";
    return r;
  }
  pendingMoveNow_ = true;
  CmdResult r;
  r.ok = true;
  return r;
}

CmdResult Controller::updateConfig(const ConfigPatch& patch) {
  std::lock_guard<std::mutex> lock(mutex_);
  Config next;
  if (auto err = applyPatch(config_, patch, next)) {
    CmdResult r;
    r.error = "invalid_config";
    r.field = err->field;
    r.message = err->message;
    return r;
  }
  config_ = next;
  store_.save(config_);
  if (state_ == RunState::Running) scheduleNextLocked();
  CmdResult r;
  r.ok = true;
  return r;
}

std::optional<Plan> Controller::tick() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (hasStopAt_ && static_cast<int32_t>(platform_.nowMs() - stopAtMs_) >= 0) {
    state_ = RunState::Stopped;
    hasStopAt_ = false;
    hasResumeAt_ = false;
  }
  if (state_ == RunState::Paused && hasResumeAt_ && static_cast<int32_t>(platform_.nowMs() - resumeAtMs_) >= 0) {
    hasResumeAt_ = false;
    startLocked();  // the pause is over
  }
  if (state_ != RunState::Stopped && !platform_.computerPaired()) {  // every computer was forgotten
    state_ = RunState::Stopped;
    hasStopAt_ = false;
    hasResumeAt_ = false;
  }
  if (pendingMoveNow_) {
    pendingMoveNow_ = false;
    if (platform_.mouseConnected()) return fireLocked();
    return std::nullopt;
  }
  if (state_ == RunState::Running && platform_.mouseConnected() &&
      static_cast<int32_t>(platform_.nowMs() - dueMs_) >= 0) {
    return fireLocked();
  }
  return std::nullopt;
}

void Controller::scheduleNextLocked() {
  const int32_t intervalMs = randRange(rng_, static_cast<int32_t>(config_.minIntervalSec * 1000),
                                       static_cast<int32_t>(config_.maxIntervalSec * 1000));
  dueMs_ = platform_.nowMs() + static_cast<uint32_t>(intervalMs);
}

uint32_t Controller::remainingMsLocked() const {
  const int32_t remaining = static_cast<int32_t>(dueMs_ - platform_.nowMs());
  return remaining > 0 ? static_cast<uint32_t>(remaining) : 0;
}

std::optional<Plan> Controller::fireLocked() {
  Plan plan = planActivity(config_, rng_);
  moveCount_++;
  hasMoved_ = true;
  lastMoveMs_ = platform_.nowMs();
  if (state_ == RunState::Running) scheduleNextLocked();
  return plan;
}

}  // namespace jig
