#include <Arduino.h>

#include "arduino_platform.h"
#include "ble_mouse.h"
#include "controller.h"
#include "led.h"
#include "movement_task.h"
#include "nvs_config_store.h"

BleMouse ble;
ArduinoPlatform platform(ble);
EspRng rng;
NvsConfigStore store;
jig::Controller controller(platform, rng, store);
Led led;

void setup() {
  Serial.begin(115200);
  led.begin();
  ble.begin();
  controller.begin();
  startMovementTask(controller, ble, platform);
  Serial.printf("jiggler started, BLE address %s\n", NimBLEDevice::getAddress().toString().c_str());
}

void loop() {
  led.update(controller.status(), millis());
  delay(10);
}
