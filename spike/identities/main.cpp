// THROWAWAY SPIKE (Task 10): can a classic ESP32 present two BLE identities with different
// addresses at once? Not part of the product; built only by `pio run -e spike`.
#include <Arduino.h>
#include <NimBLEDevice.h>

namespace {

constexpr uint8_t kRandomAddrB[6] = {0xC1, 0xDE, 0x00, 0x00, 0x00, 0x02};  // LSB first: C2:00:00:00:DE:C1 style order handled below
uint32_t lastSwitchMs = 0;
bool onB = false;
bool bOnly = false;
NimBLEServer* gServer = nullptr;

std::string addrStr(const ble_addr_t& a) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%s %02x:%02x:%02x:%02x:%02x:%02x", a.type == BLE_ADDR_PUBLIC ? "public" : "random",
           a.val[5], a.val[4], a.val[3], a.val[2], a.val[1], a.val[0]);
  return buf;
}

class Callbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, ble_gap_conn_desc* d) override {
    Serial.printf("CONNECT  handle=%u our=%s peer=%s total=%u\n", d->conn_handle, addrStr(d->our_ota_addr).c_str(),
                  addrStr(d->peer_ota_addr).c_str(), (unsigned)s->getConnectedCount());
  }
  void onDisconnect(NimBLEServer* s, ble_gap_conn_desc* d) override {
    Serial.printf("DISCONN  handle=%u our=%s total=%u\n", d->conn_handle, addrStr(d->our_ota_addr).c_str(),
                  (unsigned)s->getConnectedCount());
  }
};

void advertise(bool identityB) {
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->stop();
  if (identityB) {
    uint8_t addr[6];
    for (int i = 0; i < 6; i++) addr[i] = kRandomAddrB[i];
    addr[5] |= 0xC0;  // random static: two top bits set
    NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
    int rc = ble_hs_id_set_rnd(addr);
    Serial.printf("identity B: set_rnd rc=%d\n", rc);
  } else {
    NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_PUBLIC);
  }
  NimBLEAdvertisementData data;
  data.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  data.setName(identityB ? "SpikeB" : "SpikeA");
  data.setCompleteServices(NimBLEUUID((uint16_t)0xFFF0));
  adv->setAdvertisementData(data);
  bool ok = adv->start();
  uint8_t cur[6] = {0};
  ble_hs_id_copy_addr(identityB ? BLE_ADDR_RANDOM : BLE_ADDR_PUBLIC, cur, nullptr);
  Serial.printf("advertising %s ok=%d addr=%02x:%02x:%02x:%02x:%02x:%02x\n", identityB ? "B (random)" : "A (public)", ok,
                cur[5], cur[4], cur[3], cur[2], cur[1], cur[0]);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  NimBLEDevice::init("SpikeA");
  NimBLEServer* server = NimBLEDevice::createServer();
  gServer = server;
  server->setCallbacks(new Callbacks());
  NimBLEService* svc = server->createService((uint16_t)0xFFF0);
  NimBLECharacteristic* ch = svc->createCharacteristic((uint16_t)0xFFF1, NIMBLE_PROPERTY::READ);
  ch->setValue("hello");
  svc->start();
  Serial.printf("spike up, public address %s, max connections %d\n", NimBLEDevice::getAddress().toString().c_str(),
                CONFIG_BT_NIMBLE_MAX_CONNECTIONS);
  advertise(false);
}

void loop() {
  // Variation: once a client is connected, advertise identity B continuously (no time slicing).
  if (gServer && gServer->getConnectedCount() >= 1) {
    if (!bOnly) {
      bOnly = true;
      advertise(true);
    }
    delay(50);
    return;
  }
  bOnly = false;
  if (millis() - lastSwitchMs > 800) {
    lastSwitchMs = millis();
    onB = !onB;
    advertise(onB);
  }
  delay(20);
}
