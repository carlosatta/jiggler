#include <Arduino.h>

#include "api_ex.h"
#include "arduino_platform.h"
#include "ble_mouse.h"
#include "controller.h"
#include "http_server.h"
#include "led.h"
#include "long_press.h"
#include "movement_task.h"
#include "automation.h"
#include "devices.h"
#include "nvs_blob_store.h"
#include "nvs_config_store.h"
#include "nvs_network_store.h"
#include "script.h"
#include "token_store.h"
#include "wifi_setup.h"

namespace {
constexpr uint8_t kBootButtonPin = 0;
constexpr uint32_t kBootHoldMs = 3000;
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
jig::ApiContext api{controller};
WifiSetup wifi(tokens, networkStore);
HttpServer http(api, tokens);
Led led;
jig::LongPress bootButton(kBootHoldMs);

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
  controller.begin();
  automations.begin();
  startMovementTask(controller, automations, ble, platform);  // from here on the mouse works without WiFi
  api.network = &networkStore;
  api.devices = &devices;
  api.scripts = &scripts;
  api.automations = &automations;
  api.restart = scheduleRestart;
  Serial.printf("jiggler started, BLE address %s\n", NimBLEDevice::getAddress().toString().c_str());
  wifi.begin();
}

void loop() {
  const uint32_t now = millis();
  led.update(controller.status(), now);
  if (wifi.loop()) http.begin(wifi.apMode());

  if (bootButton.update(digitalRead(kBootButtonPin) == LOW, now)) {
    jig::NetworkSettings s = networkStore.load();
    s.mode = s.mode == jig::NetworkMode::Ap ? jig::NetworkMode::Auto : jig::NetworkMode::Ap;
    networkStore.save(s);
    Serial.printf("BOOT held: network mode -> %s, restarting\n", s.mode == jig::NetworkMode::Ap ? "ap" : "auto");
    scheduleRestart();
  }
  if (restartAtMs && static_cast<int32_t>(now - restartAtMs) >= 0) ESP.restart();
  delay(10);
}
