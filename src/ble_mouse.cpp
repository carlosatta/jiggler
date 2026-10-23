#include "ble_mouse.h"

#include <NimBLEHIDDevice.h>
#include <esp_system.h>

#include "identity.h"

namespace {

// Report ID 1, input: 3 buttons + padding, int8 X, int8 Y, int8 wheel (4 bytes).
const uint8_t kReportMap[] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x01, 0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03,
    0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05, 0x81, 0x03, 0x05, 0x01,
    0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08,
    0x95, 0x03, 0x81, 0x06, 0xC0, 0xC0,
};

constexpr uint16_t kAppearanceHidMouse = 0x03C2;
constexpr uint8_t kPnpVendorSourceUsb = 0x02;
constexpr uint16_t kDeviceVersion = 0x0100;

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

void BleMouse::begin() {
  uint8_t efuse[6];
  esp_efuse_mac_get_default(efuse);
  const auto mac = jig::deriveLogitechMac(efuse);
  esp_base_mac_addr_set(mac.data());

  NimBLEDevice::init(jig::kBleName);
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_PUBLIC);
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks(connected_));

  auto* hid = new NimBLEHIDDevice(server);
  input_ = hid->inputReport(1);
  hid->manufacturer(jig::kManufacturer);
  hid->pnp(kPnpVendorSourceUsb, jig::kVid, jig::kPid, kDeviceVersion);
  hid->hidInfo(0x00, 0x01);
  hid->reportMap(const_cast<uint8_t*>(kReportMap), sizeof(kReportMap));
  hid->setBatteryLevel(100);
  hid->startServices();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setAppearance(kAppearanceHidMouse);
  adv->addServiceUUID(hid->hidService()->getUUID());
  adv->start();
}

void BleMouse::sendReport(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel) {
  if (!connected_ || !input_) return;
  const uint8_t report[4] = {buttons, static_cast<uint8_t>(dx), static_cast<uint8_t>(dy),
                             static_cast<uint8_t>(wheel)};
  input_->setValue(report, sizeof(report));
  input_->notify();
}

void BleMouse::move(int8_t dx, int8_t dy) { sendReport(0, dx, dy, 0); }

void BleMouse::scroll(int8_t notches) { sendReport(0, 0, 0, notches); }

void BleMouse::click() {
  sendReport(0x01, 0, 0, 0);
  vTaskDelay(pdMS_TO_TICKS(20));
  sendReport(0x00, 0, 0, 0);
}
