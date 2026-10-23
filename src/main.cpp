#include <Arduino.h>

#include "api_ex.h"
#include "arduino_platform.h"
#include "ble_mouse.h"
#include "controller.h"
#include "http_server.h"
#include "led.h"
#include "long_press.h"
#include "movement_task.h"
#include "nvs_config_store.h"
#include "nvs_network_store.h"
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
TokenStore tokens;
jig::Controller controller(platform, rng, store);
jig::ApiContext api{controller};
WifiSetup wifi(tokens, networkStore);
HttpServer http(api, tokens);
Led led;
jig::LongPress bootButton(kBootHoldMs);

void setup() {
  Serial.begin(115200);
  pinMode(kBootButtonPin, INPUT_PULLUP);
  led.begin();
  ble.begin();
  controller.begin();
  startMovementTask(controller, ble, platform);  // from here on the mouse works without WiFi
  api.network = &networkStore;
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
