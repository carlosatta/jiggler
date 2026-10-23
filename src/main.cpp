#include <Arduino.h>

#include "api_ex.h"
#include "arduino_platform.h"
#include "ble_mouse.h"
#include "controller.h"
#include "http_server.h"
#include "led.h"
#include "bluetooth.h"
#include "button_gesture.h"
#include "movement_task.h"
#include "automation.h"
#include "devices.h"
#include "nvs_blob_store.h"
#include "nvs_config_store.h"
#include "nvs_network_store.h"
#include "schedule.h"
#include "script.h"
#include "token_store.h"
#include "wifi_setup.h"

namespace {
constexpr uint8_t kBootButtonPin = 0;
constexpr uint32_t kPairingWindowSec = jig::kDefaultPairingSec;
constexpr uint32_t kBluetoothTickMs = 200;
volatile uint32_t restartAtMs = 0;

void scheduleRestart() {
  restartAtMs = millis() + 500;  // lets the HTTP response go out first
  if (restartAtMs == 0) restartAtMs = 1;
}
}  // namespace

BleMouse ble;
ArduinoPlatform platform(ble);
EspRng rng;
NvsConfigStore store;
NvsNetworkStore networkStore;
NvsBlobStore blobs;
jig::DeviceRegistry devices(blobs);
jig::ScriptLibrary scripts(blobs);
TokenStore tokens;
jig::Controller controller(platform, rng, store);
jig::AutomationEngine automations(platform, rng, scripts, blobs);
jig::ScheduleEngine schedules(platform, controller, blobs);
jig::ApiContext api{controller};
WifiSetup wifi(tokens, networkStore);
HttpServer http(api, tokens);
Led led;
jig::ButtonGesture bootButton;
jig::BluetoothManager bluetooth(platform, ble, blobs);
bool pairingOpen = true;
bool held = false;
uint32_t lastBluetoothTick = 0;
uint32_t lastScheduleTick = 0;

void setup() {
  Serial.begin(115200);
  pinMode(kBootButtonPin, INPUT_PULLUP);
  led.begin();
  devices.begin();
  uint8_t efuse[6];
  esp_efuse_mac_get_default(efuse);
  jig::ResolvedDevice resolved = devices.resolved(efuse);
#ifdef JIG_FORCE_KEYBOARD
  resolved.keyboard = true;  // debug builds only: in memory, never persisted
#endif
  ble.begin(resolved);
  bluetooth.begin();
  controller.begin();
  automations.begin();
  schedules.begin();
  startMovementTask(controller, automations, ble, platform);  // from here on the mouse works without WiFi
  api.network = &networkStore;
  api.devices = &devices;
  api.scripts = &scripts;
  api.automations = &automations;
  api.bluetooth = &bluetooth;
  api.schedules = &schedules;
  api.tokens = &tokens;
  api.info = {JIG_VERSION, ble.name(), ble.address()};
  api.restart = scheduleRestart;
  Serial.printf("jiggler %s started, BLE address %s\n", JIG_VERSION, NimBLEDevice::getAddress().toString().c_str());
  wifi.begin();
}

void loop() {
  const uint32_t now = millis();
  if (now - lastBluetoothTick >= kBluetoothTickMs) {
    lastBluetoothTick = now;
    bluetooth.tick();
    const jig::BluetoothStatus bt = bluetooth.status();
    pairingOpen = bt.pairingOpen;
    held = bt.held;
  }
  if (now - lastScheduleTick >= 1000) {
    lastScheduleTick = now;
    schedules.tick();
  }
  led.update(controller.status(), pairingOpen, held, now);
  if (wifi.loop()) http.begin(wifi.apMode());
  if (wifi.needsRestart() && restartAtMs == 0) scheduleRestart();

  switch (bootButton.update(digitalRead(kBootButtonPin) == LOW, now)) {
    case jig::Gesture::Short:
      bluetooth.startPairing(kPairingWindowSec);
      Serial.printf("BOOT pressed: pairing open for %u s\n", static_cast<unsigned>(kPairingWindowSec));
      break;
    case jig::Gesture::Long: {
      jig::NetworkSettings s = networkStore.load();
      s.mode = s.mode == jig::NetworkMode::Ap ? jig::NetworkMode::Auto : jig::NetworkMode::Ap;
      networkStore.save(s);
      Serial.printf("BOOT held: network mode -> %s, restarting\n", s.mode == jig::NetworkMode::Ap ? "ap" : "auto");
      scheduleRestart();
      break;
    }
    case jig::Gesture::None:
      break;
  }
  if (restartAtMs && static_cast<int32_t>(now - restartAtMs) >= 0) ESP.restart();
  delay(10);
}
