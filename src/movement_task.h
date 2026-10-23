#pragma once

#include "arduino_platform.h"
#include "ble_mouse.h"
#include "controller.h"

// Runs on core 1 at priority 2, independent of WiFi/HTTP code.
void startMovementTask(jig::Controller& controller, BleMouse& mouse, ArduinoPlatform& platform);
