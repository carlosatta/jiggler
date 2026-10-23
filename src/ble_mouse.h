#pragma once

#include <NimBLEDevice.h>

#include <atomic>

#include "devices.h"
#include "platform.h"

// One BLE HID device exposing a mouse and/or keyboard collection, with the identity (name, vendor,
// VID/PID, Bluetooth address) of the first enabled device in the registry.
class BleMouse : public jig::MouseSink, public jig::KeyboardSink {
 public:
  void begin(const jig::ResolvedDevice& device);
  bool connected() const { return connected_; }
  void move(int8_t dx, int8_t dy) override;
  void click() override;
  void scroll(int8_t notches) override;
  void keyDown(uint8_t modifiers, uint8_t usage) override;
  void keyUp() override;

 private:
  void sendMouse(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel);
  void sendKeyboard(uint8_t modifiers, uint8_t usage);

  std::atomic<bool> connected_{false};
  NimBLECharacteristic* mouseInput_ = nullptr;
  NimBLECharacteristic* keyboardInput_ = nullptr;
  std::vector<uint8_t> reportMap_;
};
