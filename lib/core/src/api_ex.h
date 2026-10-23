#pragma once

#include <functional>
#include <string>

#include "api_router.h"
#include "controller.h"
#include "devices.h"
#include "network_settings.h"
#include "script.h"

namespace jig {

// Services are optional (nullptr = routes answer 404) so the router can grow without breaking callers.
struct ApiContext {
  Controller& controller;
  NetworkStore* network = nullptr;
  DeviceRegistry* devices = nullptr;
  ScriptLibrary* scripts = nullptr;
  std::function<void()> restart;
};

// Extended router: new routes first, everything else falls through to handleApi().
HttpResponse handleApiEx(ApiContext& ctx, const std::string& method, const std::string& path,
                         const std::string& body);

}  // namespace jig
