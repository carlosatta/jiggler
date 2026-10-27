#pragma once

#include <functional>
#include <string>

namespace jig {

// Where the core writes what it decides (the jiggler starting, stopping, a schedule opening...), so the device can
// keep it in its event log. Nothing happens until something installs a sink.
void setLogSink(std::function<void(const std::string&)> sink);
void logEvent(const std::string& message);

}  // namespace jig
