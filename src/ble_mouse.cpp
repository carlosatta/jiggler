#include "ble_mouse.h"

#include <NimBLEHIDDevice.h>
#include <esp_system.h>

#include "hid_report_map.h"

namespace {

constexpr uint8_t kMouseReportId = 1;
constexpr uint8_t kKeyboardReportId = 2;

class ServerCallbacks : public NimBLEServerCallbacks {
 public:
  explicit ServerCallbacks(std::atomic<bool>& connected) : connected_(connected) {}
  void onConnect(NimBLEServer*, ble_gap_conn_desc*) override { connected_ = true; }
  void onDisconnect(NimBLEServer*, ble_gap_conn_desc*) override {
    connected_ = false;
    NimBLEDevice::startAdvertising();
  }

 private:
  std::atomic<bool>& connected_;
};

}  // namespace

void BleMouse::begin(const jig::ResolvedDevice& device) {
  if (device.mac) {
    std::array<uint8_t, 6> base = *device.mac;
    // The Bluetooth address is the base MAC with the last byte + 2 (universal MAC layout). A profile-derived
    // address keeps that offset (it only matters that the vendor prefix survives); a user-typed address
    // must come out exactly as typed.
    if (device.macExplicit) base[5] = static_cast<uint8_t>(base[5] - 2);
    esp_base_mac_addr_set(base.data());
  }

  NimBLEDevice::init(device.identity.name);
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_PUBLIC);
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks(connected_));

  reportMap_ = jig::buildReportMap(device.mouse, device.keyboard);

  auto* hid = new NimBLEHIDDevice(server);
  if (device.mouse) mouseInput_ = hid->inputReport(kMouseReportId);
  if (device.keyboard) keyboardInput_ = hid->inputReport(kKeyboardReportId);
  hid->manufacturer(device.identity.manufacturer);
  hid->pnp(device.identity.vidSource, device.identity.vid, device.identity.pid, device.identity.version);
  hid->hidInfo(0x00, 0x01);
  hid->reportMap(reportMap_.data(), reportMap_.size());
  hid->setBatteryLevel(100);
  hid->startServices();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setAppearance(device.identity.appearance);
  adv->addServiceUUID(hid->hidService()->getUUID());
  adv->start();
}

void BleMouse::sendMouse(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel) {
  if (!connected_ || !mouseInput_) return;
  const uint8_t report[4] = {buttons, static_cast<uint8_t>(dx), static_cast<uint8_t>(dy),
                             static_cast<uint8_t>(wheel)};
  mouseInput_->setValue(report, sizeof(report));
  mouseInput_->notify();
}

void BleMouse::sendKeyboard(uint8_t modifiers, uint8_t usage) {
  if (!connected_ || !keyboardInput_) return;
  const uint8_t report[8] = {modifiers, 0, usage, 0, 0, 0, 0, 0};
  keyboardInput_->setValue(report, sizeof(report));
  keyboardInput_->notify();
}

void BleMouse::move(int8_t dx, int8_t dy) { sendMouse(0, dx, dy, 0); }

void BleMouse::scroll(int8_t notches) { sendMouse(0, 0, 0, notches); }

void BleMouse::click() {
  sendMouse(0x01, 0, 0, 0);
  vTaskDelay(pdMS_TO_TICKS(20));
  sendMouse(0x00, 0, 0, 0);
}

void BleMouse::keyDown(uint8_t modifiers, uint8_t usage) { sendKeyboard(modifiers, usage); }

void BleMouse::keyUp() { sendKeyboard(0, 0); }
