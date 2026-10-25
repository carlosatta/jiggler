#include "button_settings.h"

#include <ArduinoJson.h>

#include <algorithm>

namespace jig {

namespace {
constexpr const char* kBlobKey = "button";
}

const std::vector<uint8_t>& allowedButtonGpios() {
  static const std::vector<uint8_t> pins = {0, 4, 5, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33};
  return pins;
}

bool validButtonGpio(int gpio) {
  const auto& pins = allowedButtonGpios();
  return gpio >= 0 && gpio <= 255 && std::find(pins.begin(), pins.end(), static_cast<uint8_t>(gpio)) != pins.end();
}

std::string buttonToJson(uint8_t gpio) {
  JsonDocument doc;
  doc["gpio"] = gpio;
  doc["default"] = kDefaultButtonGpio;
  JsonArray pins = doc["gpios"].to<JsonArray>();
  for (uint8_t p : allowedButtonGpios()) pins.add(p);
  std::string out;
  serializeJson(doc, out);
  return out;
}

void ButtonStore::begin() {
  std::lock_guard<std::mutex> lock(mutex_);
  gpio_ = kDefaultButtonGpio;
  const std::string raw = blobs_.get(kBlobKey);
  JsonDocument doc;
  if (!raw.empty() && !deserializeJson(doc, raw) && doc["gpio"].is<int>() && validButtonGpio(doc["gpio"].as<int>()))
    gpio_ = static_cast<uint8_t>(doc["gpio"].as<int>());
}

uint8_t ButtonStore::gpio() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return gpio_;
}

CmdResult ButtonStore::setGpio(int gpio) {
  CmdResult r;
  if (!validButtonGpio(gpio)) {
    r.error = "invalid_gpio";
    r.field = "gpio";
    r.message = "pick one of the listed pins (0 is the BOOT button)";
    return r;
  }
  JsonDocument doc;
  doc["gpio"] = gpio;
  std::string raw;
  serializeJson(doc, raw);
  std::lock_guard<std::mutex> lock(mutex_);
  if (!blobs_.put(kBlobKey, raw)) {
    r.error = "storage_error";
    r.message = "could not save the setting (storage full?)";
    return r;
  }
  gpio_ = static_cast<uint8_t>(gpio);
  r.ok = true;
  return r;
}

}  // namespace jig
