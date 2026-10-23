#include <Arduino.h>

#include "ble_mouse.h"

BleMouse mouse;

void setup() {
  Serial.begin(115200);
  mouse.begin();
  Serial.printf("BLE mouse started, address %s\n", NimBLEDevice::getAddress().toString().c_str());
}

// Temporary verification loop (Task 6); replaced by the movement task in Task 7.
void loop() {
  delay(5000);
  if (!mouse.connected()) return;
  Serial.println("test move");
  mouse.move(20, 0);
  delay(100);
  mouse.move(-20, 0);
}
