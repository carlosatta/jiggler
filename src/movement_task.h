#pragma once

#include "arduino_platform.h"
#include "automation.h"
#include "ble_mouse.h"
#include "controller.h"

// Runs on core 1 at priority 2, independent of WiFi/HTTP code.
void startMovementTask(jig::Controller& controller, jig::AutomationEngine& automations, BleMouse& mouse,
                       ArduinoPlatform& platform);
