#include <Arduino.h>

#include "api_ex.h"
#include "arduino_platform.h"
#include "ble_mouse.h"
#include "controller.h"
#include "http_server.h"
#include "led.h"
#include "bluetooth.h"
#include "button_gesture.h"
#include "button_settings.h"
#include "clock_settings.h"
#include "movement_task.h"
#include "appearance.h"
#include "nvs_blob_store.h"
#include "nvs_config_store.h"
#include "nvs_network_store.h"
#include "schedule.h"
#include "token_store.h"
#include "wifi_setup.h"

namespace {
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
jig::AppearanceStore appearance(blobs);
TokenStore tokens;
jig::Controller controller(platform, rng, store);
jig::ClockStore clockStore(platform, blobs);
jig::ButtonStore buttonStore(blobs);
jig::ScheduleEngine schedules(platform, controller, blobs, clockStore);
jig::ApiContext api{controller};
WifiSetup wifi(tokens, networkStore);
HttpServer http(api, tokens);
Led led;
jig::ButtonGesture button;
uint8_t buttonPin = jig::kDefaultButtonGpio;
jig::BluetoothManager bluetooth(platform, ble, blobs);
jig::BluetoothState bluetoothState = jig::BluetoothState::Waiting;
bool confirming = false;
uint32_t confirmStartMs = 0;
uint32_t lastBluetoothTick = 0;
uint32_t lastScheduleTick = 0;

// A tap on the button: start the jiggler, or stop it if it is running or paused.
void toggleJiggler() {
  const jig::RunState state = controller.status().runState;
  const bool active = state == jig::RunState::Running || state == jig::RunState::Paused;
  jig::CmdResult r;
  r.ok = true;
  if (active) controller.stop();
  else r = controller.start();
  Serial.printf("Button tap: %s %s\n", active ? "stop" : "start", r.ok ? "done" : r.error.c_str());
  if (r.ok) {
    confirming = true;
    confirmStartMs = millis();
  }
}

void setup() {
  Serial.begin(115200);
  led.begin();
  appearance.begin();
  buttonStore.begin();
  buttonPin = buttonStore.gpio();
  pinMode(buttonPin, INPUT_PULLUP);
  uint8_t efuse[6];
  esp_efuse_mac_get_default(efuse);
  jig::ResolvedAppearance resolved = appearance.resolved(efuse);
  ble.begin(resolved);
  bluetooth.begin();
  controller.begin();
  clockStore.begin();
  schedules.begin();
  schedules.setBluetooth(&bluetooth);
  startMovementTask(controller, ble, platform);  // from here on the mouse works without WiFi
  api.network = &networkStore;
  api.appearance = &appearance;
  api.bluetooth = &bluetooth;
  api.schedules = &schedules;
  api.clock = &clockStore;
  api.button = &buttonStore;
  api.tokens = &tokens;
  api.info = {JIG_VERSION, ble.name(), ble.address()};
  api.restart = scheduleRestart;
  api.updateActive = [](bool active) { ble.setQuiet(active); };
  Serial.printf("jiggler %s started, BLE address %s\n", JIG_VERSION, NimBLEDevice::getAddress().toString().c_str());
  wifi.begin();
}

void loop() {
  const uint32_t now = millis();
  if (now - lastBluetoothTick >= kBluetoothTickMs) {
    lastBluetoothTick = now;
    bluetooth.tick();
    bluetoothState = bluetooth.status().state;
  }
  if (now - lastScheduleTick >= 1000) {
    lastScheduleTick = now;
    schedules.tick();
    platform.applyNtp();
  }

  const uint8_t wantedPin = buttonStore.gpio();
  if (wantedPin != buttonPin) {  // the owner picked another pin on the dashboard
    buttonPin = wantedPin;
    pinMode(buttonPin, INPUT_PULLUP);
    button = jig::ButtonGesture();
    Serial.printf("Button moved to GPIO %u\n", static_cast<unsigned>(buttonPin));
  }
  switch (button.update(digitalRead(buttonPin) == LOW, now)) {
    case jig::Gesture::Tap:
      toggleJiggler();
      break;
    case jig::Gesture::Pairing:
      bluetooth.startPairing(0);
      Serial.println("Button held 5-10 s: Discovery open");
      break;
    case jig::Gesture::Network: {
      jig::NetworkSettings s = networkStore.load();
      s.mode = s.mode == jig::NetworkMode::Ap ? jig::NetworkMode::Auto : jig::NetworkMode::Ap;
      networkStore.save(s);
      Serial.printf("Button held 30 s: network mode -> %s, restarting\n", s.mode == jig::NetworkMode::Ap ? "ap" : "auto");
      scheduleRestart();
      break;
    }
    case jig::Gesture::None:
      break;
  }

  const jig::Status status = controller.status();
  jig::LedContext led_ctx;
  led_ctx.bluetooth = bluetoothState;
  led_ctx.running = status.runState == jig::RunState::Running;
  led_ctx.nowMs = now;
  led_ctx.hasMoved = status.lastMoveAgoMs.has_value();
  led_ctx.lastMoveMs = led_ctx.hasMoved ? now - *status.lastMoveAgoMs : 0;
  led_ctx.hold = button.phase(now);
  led_ctx.confirming = confirming;
  led_ctx.confirmStartMs = confirmStartMs;
  led.update(led_ctx);

  if (wifi.loop()) http.begin(wifi.apMode());
  if (wifi.needsRestart() && restartAtMs == 0) scheduleRestart();
  if (restartAtMs && static_cast<int32_t>(now - restartAtMs) >= 0) ESP.restart();
  delay(10);
}
