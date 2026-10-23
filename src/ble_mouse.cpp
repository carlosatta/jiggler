#include "ble_mouse.h"

#include <NimBLEHIDDevice.h>
#include <esp_system.h>

#include <Arduino.h>

#include <algorithm>

#include "hid_report_map.h"

static_assert(CONFIG_BT_NIMBLE_MAX_BONDS >= jig::kStackBondSlots,
              "the bond store must have the slots bluetooth.h assumes (see platformio.ini)");

namespace {

constexpr uint8_t kMouseReportId = 1;
constexpr uint8_t kKeyboardReportId = 2;

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string addressOf(const ble_addr_t& a) { return lower(NimBLEAddress(a).toString()); }

BleMouse* gBle = nullptr;

// Receives every GAP event (NimBLE's server callbacks do not pass the disconnect reason on).
int gapListener(ble_gap_event* event, void*) {
  if (!gBle) return 0;
  if (event->type == BLE_GAP_EVENT_DISCONNECT) gBle->notePeerDisconnect(event->disconnect.reason);
  else if (event->type == BLE_GAP_EVENT_ENC_CHANGE)
    Serial.printf("BLE encryption change: status %d\n", event->enc_change.status);
  else if (event->type == BLE_GAP_EVENT_REPEAT_PAIRING) Serial.println("BLE repeat pairing requested by a known computer");
  return 0;
}

class ServerCallbacks : public NimBLEServerCallbacks {
 public:
  explicit ServerCallbacks(BleMouse& ble) : ble_(ble) {}
  void onConnect(NimBLEServer*, ble_gap_conn_desc* desc) override {
    ble_.linkUp(desc->conn_handle, addressOf(desc->peer_id_addr));
  }
  void onAuthenticationComplete(ble_gap_conn_desc* desc) override {
    ble_.linkIdentified(desc->conn_handle, addressOf(desc->peer_id_addr), desc->sec_state.bonded);
  }
  void onDisconnect(NimBLEServer*, ble_gap_conn_desc* desc) override { ble_.linkDown(desc->conn_handle); }
  // Reached only when pairing is closed (see setPairingOpen): the board then claims it can confirm a code, and
  // always says no, so even a known computer that lost its key cannot re-pair without the owner's window.
  bool onConfirmPIN(uint32_t) override {
    Serial.println("BLE pairing attempt refused before bonding (window closed)");
    return false;
  }

 private:
  BleMouse& ble_;
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

  name_ = device.identity.name;
  NimBLEDevice::init(device.identity.name);
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_PUBLIC);
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_YESNO);  // closed by default; setPairingOpen switches it

  NimBLEServer* server = NimBLEDevice::createServer();
  server_ = server;
  gBle = this;
  NimBLEDevice::setCustomGapHandler(gapListener);
  server->advertiseOnDisconnect(false);  // BluetoothManager decides when the device is visible (see linkDown/setAdvertising)
  server->setCallbacks(new ServerCallbacks(*this));

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
  if (advertise_) adv->start();
}

std::string BleMouse::address() const { return lower(NimBLEDevice::getAddress().toString()); }

void BleMouse::refreshConnected() { connected_ = !links_.empty(); }

void BleMouse::linkUp(uint16_t handle, const std::string& peerAddress) {
  Link link{handle, peerAddress, bondedAddresses()};
  std::lock_guard<std::mutex> lock(linksMutex_);
  links_.push_back(std::move(link));
  refreshConnected();
  // Fix the connection parameters (15-30 ms interval, no slave latency, 4 s timeout, within Apple's accessory
  // guidelines). Left to the central, macOS renegotiates them and the link was dropped every few minutes with
  // HCI error 0x28 (instant passed), then re-established a couple of seconds later.
  if (server_) server_->updateConnParams(handle, 12, 24, 0, 400);
  Serial.printf("BLE link up (handle %u)\n", static_cast<unsigned>(handle));
}

// Pairing finished (the identity address is now known). A computer that was not bonded before this link came
// up is refused unless the owner opened a pairing window (and there is room): its bond, if it made one, is
// removed and the link dropped. This holds even when it paired without asking to bond.
void BleMouse::linkIdentified(uint16_t handle, const std::string& idAddress, bool bonded) {
  bool refuse = false;
  {
    std::lock_guard<std::mutex> lock(linksMutex_);
    for (Link& l : links_) {
      if (l.handle != handle) continue;
      l.address = idAddress;
      const bool known = std::find(l.bondedBefore.begin(), l.bondedBefore.end(), idAddress) != l.bondedBefore.end();
      refuse = jig::refuseNewPeer(known, pairingOpen_, l.bondedBefore.size());
    }
  }
  Serial.printf("BLE identified %s: bonded=%d pairingOpen=%d -> %s\n", idAddress.c_str(), bonded ? 1 : 0, pairingOpen_.load() ? 1 : 0,
                refuse ? "REFUSE" : "accept");
  if (!refuse) return;
  Serial.printf("BLE pairing refused for %s (window closed)\n", idAddress.c_str());
  forgetBond(idAddress);
  if (server_) server_->disconnect(handle);
}

// reason is NimBLE's code: 0x200 + the HCI reason. 0x213 = the remote user terminated the connection
// (the computer's own "Disconnect"); 0x216 = we did; 0x208 = the link timed out (out of range, asleep).
void BleMouse::notePeerDisconnect(int reason) {
  Serial.printf("BLE disconnect reason 0x%03x\n", reason);
  if (reason == 0x213) peerDisconnect_ = true;
}

void BleMouse::linkDown(uint16_t handle) {
  {
    std::lock_guard<std::mutex> lock(linksMutex_);
    links_.erase(std::remove_if(links_.begin(), links_.end(), [&](const Link& l) { return l.handle == handle; }),
                 links_.end());
    refreshConnected();
  }
  Serial.printf("BLE link down (handle %u), advertising %s\n", static_cast<unsigned>(handle), advertise_ ? "on" : "off");
  if (advertise_) NimBLEDevice::startAdvertising();
}

std::vector<std::string> BleMouse::bondedAddresses() {
  std::vector<std::string> out;
  const int n = NimBLEDevice::getNumBonds();
  for (int i = 0; i < n; ++i) out.push_back(lower(NimBLEDevice::getBondedAddress(i).toString()));
  return out;
}

std::vector<std::string> BleMouse::connectedAddresses() {
  std::lock_guard<std::mutex> lock(linksMutex_);
  std::vector<std::string> out;
  for (const Link& l : links_) out.push_back(l.address);
  return out;
}

void BleMouse::disconnectAll() {
  std::vector<uint16_t> handles;
  {
    std::lock_guard<std::mutex> lock(linksMutex_);
    for (const Link& l : links_) handles.push_back(l.handle);
  }
  if (!server_) return;
  for (uint16_t h : handles) server_->disconnect(h);
}

bool BleMouse::forgetBond(const std::string& address) {
  const int n = NimBLEDevice::getNumBonds();
  for (int i = 0; i < n; ++i) {
    const NimBLEAddress bonded = NimBLEDevice::getBondedAddress(i);
    if (lower(bonded.toString()) == address) return NimBLEDevice::deleteBond(bonded);
  }
  return false;
}

void BleMouse::forgetAllBonds() { NimBLEDevice::deleteAllBonds(); }

void BleMouse::setAdvertising(bool on) {
  if (advertise_.exchange(on) == on) return;
  if (on) {
    NimBLEDevice::startAdvertising();
  } else {
    NimBLEDevice::stopAdvertising();
  }
}

void BleMouse::setPairingOpen(bool open) {
  pairingOpen_ = open;
  // Open: "Just Works" (no code). Closed: advertise a display+yes/no capability so a pairing attempt reaches
  // onConfirmPIN, where it is refused before any key is stored. Does not affect already-bonded reconnections.
  NimBLEDevice::setSecurityIOCap(open ? BLE_HS_IO_NO_INPUT_OUTPUT : BLE_HS_IO_DISPLAY_YESNO);
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
