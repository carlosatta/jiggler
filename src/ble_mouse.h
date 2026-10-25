#pragma once

#include <NimBLEDevice.h>

#include <atomic>

#include <mutex>
#include <string>
#include <vector>

#include "bluetooth.h"
#include "appearance.h"
#include "platform.h"

// One BLE HID device exposing a mouse and/or keyboard collection, with the identity (name, vendor,
// VID/PID, Bluetooth address) of the first enabled device in the registry.
class BleMouse : public jig::MouseSink, public jig::KeyboardSink, public jig::BleLink {
 public:
  void begin(const jig::ResolvedAppearance& device);
  bool connected() const { return connected_; }
  std::string address() const;
  std::string name() const { return name_; }
  int pairedCount() const { return NimBLEDevice::getNumBonds(); }

  // jig::BleLink
  std::vector<std::string> bondedAddresses() override;
  std::vector<std::string> connectedAddresses() override;
  void disconnectAll() override;
  bool forgetBond(const std::string& address) override;
  void forgetAllBonds() override;
  void setEnabled(bool on) override;
  void setPairingOpen(bool open) override;
  // While a firmware upload runs the Bluetooth radio stays quiet (no advertising, no link): it shares the antenna
  // with WiFi, and its packets made the upload connection drop.
  void setQuiet(bool quiet);
  bool takePeerDisconnectRequest() override { return peerDisconnect_.exchange(false); }
  void notePeerDisconnect(int reason);  // from the NimBLE gap listener

  // Called by the server callbacks.
  void linkUp(uint16_t handle, const std::string& peerAddress);
  void linkIdentified(uint16_t handle, const std::string& idAddress, bool bonded);
  void linkDown(uint16_t handle);
  void move(int8_t dx, int8_t dy) override;
  void click() override;
  void scroll(int8_t notches) override;
  void keyDown(uint8_t modifiers, uint8_t usage) override;
  void keyUp() override;

 private:
  void refreshConnected();
  void refreshAdvertising();  // (re)builds the advertisement for the current state and starts it
  void sendMouse(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel);
  void sendKeyboard(uint8_t modifiers, uint8_t usage);

  struct Link {
    uint16_t handle;
    std::string address;
    std::vector<std::string> bondedBefore;  // bonds that existed when this link came up
  };

  std::atomic<bool> connected_{false};
  std::atomic<bool> enabled_{true};
  uint16_t appearance_ = 0;
  std::atomic<bool> pairingOpen_{false};
  std::atomic<bool> peerDisconnect_{false};
  std::atomic<bool> quiet_{false};
  std::mutex linksMutex_;
  std::vector<Link> links_;
  std::string name_;
  NimBLEServer* server_ = nullptr;
  NimBLECharacteristic* mouseInput_ = nullptr;
  NimBLECharacteristic* keyboardInput_ = nullptr;
  std::vector<uint8_t> reportMap_;
};
