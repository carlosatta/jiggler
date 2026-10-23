#pragma once

#include <memory>

#include "network_settings.h"
#include "token_store.h"

// Network bring-up. `auto` mode: home WiFi with the "Jiggler-Setup" portal as fallback (WiFi + API
// token). `ap` mode: own WPA2 network with a DNS-based captive portal. Never touches BLE/movement.
class WifiSetup {
 public:
  WifiSetup(TokenStore& tokens, jig::NetworkStore& network);
  ~WifiSetup();

  void begin();

  // Call from loop(). Returns true exactly once: when the HTTP server may bind port 80
  // (home WiFi connected and portal closed, or the AP is up).
  bool loop();

  bool apMode() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
