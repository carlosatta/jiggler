#pragma once

#include <NimBLEDevice.h>

#include <atomic>

#include "platform.h"

class BleMouse : public jig::MouseSink {
 public:
  void begin();
  bool connected() const { return connected_; }
  void move(int8_t dx, int8_t dy) override;
  void click() override;
  void scroll(int8_t notches) override;

 private:
  void sendReport(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel);

  std::atomic<bool> connected_{false};
  NimBLECharacteristic* input_ = nullptr;
};
