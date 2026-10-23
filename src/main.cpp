#include <Arduino.h>

#include "arduino_platform.h"
#include "ble_mouse.h"
#include "controller.h"
#include "http_server.h"
#include "led.h"
#include "movement_task.h"
#include "nvs_config_store.h"
#include "token_store.h"
#include "wifi_setup.h"

BleMouse ble;
ArduinoPlatform platform(ble);
EspRng rng;
NvsConfigStore store;
TokenStore tokens;
jig::Controller controller(platform, rng, store);
WifiSetup wifi(tokens);
HttpServer http(controller, tokens);
Led led;

void setup() {
  Serial.begin(115200);
  led.begin();
  ble.begin();
  controller.begin();
  startMovementTask(controller, ble, platform);  // from here on the mouse works without WiFi
  Serial.printf("jiggler started, BLE address %s\n", NimBLEDevice::getAddress().toString().c_str());
  wifi.begin();
}

void loop() {
  led.update(controller.status(), millis());
  if (wifi.loop()) http.begin();
  delay(10);
}
