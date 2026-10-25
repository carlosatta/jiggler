#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "controller.h"
#include "platform.h"

namespace jig {

constexpr uint8_t kDefaultButtonGpio = 0;  // the BOOT button

// Pins a button can use: input-capable, with an internal pull-up, and not taken by the flash (6-11), the console
// (1, 3), the status LED (2) or the boot-mode strapping (12).
const std::vector<uint8_t>& allowedButtonGpios();
bool validButtonGpio(int gpio);

std::string buttonToJson(uint8_t gpio);

// Which GPIO the jiggler button is wired to (to ground, with the internal pull-up). Thread-safe.
class ButtonStore {
 public:
  explicit ButtonStore(BlobStore& blobs) : blobs_(blobs) {}

  void begin();
  uint8_t gpio() const;
  CmdResult setGpio(int gpio);  // errors: invalid_gpio, storage_error

 private:
  BlobStore& blobs_;
  mutable std::mutex mutex_;
  uint8_t gpio_ = kDefaultButtonGpio;
};

}  // namespace jig
