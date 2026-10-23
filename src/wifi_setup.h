#pragma once

#include <memory>

#include "token_store.h"

// WiFi provisioning (captive portal "Jiggler-Setup") plus the API token field.
// The portal is non-blocking and never touches the BLE/movement side.
class WifiSetup {
 public:
  explicit WifiSetup(TokenStore& tokens);
  ~WifiSetup();

  void begin();

  // Call from loop(). Returns true exactly once: when WiFi is connected and the
  // config portal is closed, i.e. when the HTTP server may bind port 80.
  bool loop();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
