#include "log_hook.h"

#include <mutex>

namespace jig {

namespace {
std::mutex gMutex;
std::function<void(const std::string&)> gSink;
}  // namespace

void setLogSink(std::function<void(const std::string&)> sink) {
  std::lock_guard<std::mutex> lock(gMutex);
  gSink = std::move(sink);
}

void logEvent(const std::string& message) {
  std::function<void(const std::string&)> sink;
  {
    std::lock_guard<std::mutex> lock(gMutex);
    sink = gSink;
  }
  if (sink) sink(message);
}

}  // namespace jig
