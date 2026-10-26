#pragma once

#include <functional>
#include <string>

#include "api_router.h"
#include "appearance.h"
#include "automation.h"
#include "bluetooth.h"
#include "button_settings.h"
#include "clock_settings.h"
#include "controller.h"
#include "event_log.h"
#include "network_settings.h"
#include "schedule.h"
#include "script.h"
#include "update_manager.h"

namespace jig {

struct DeviceInfo {
  std::string version;
  std::string bleName;
  std::string bleAddress;
};

// Services are optional (nullptr = routes answer 404) so the router can grow without breaking callers.
struct ApiContext {
  Controller& controller;
  NetworkStore* network = nullptr;
  AppearanceStore* appearance = nullptr;
  ScriptLibrary* scripts = nullptr;
  AutomationEngine* automations = nullptr;
  BluetoothManager* bluetooth = nullptr;
  ScheduleEngine* schedules = nullptr;
  ClockStore* clock = nullptr;
  ButtonStore* button = nullptr;
  EventLog* log = nullptr;
  UpdateManager* updates = nullptr;
  std::function<uint32_t()> nowMs;  // the device clock in ms, for the event log
  TokenVault* tokens = nullptr;
  DeviceInfo info;  // shown in /api/status when its version is set
  std::function<void()> restart;
  std::function<void(bool)> updateActive;  // a firmware upload started (true) or ended without installing (false)
};

// Extended router: new routes first, everything else falls through to handleApi().
HttpResponse handleApiEx(ApiContext& ctx, const std::string& method, const std::string& path,
                         const std::string& body);

}  // namespace jig
