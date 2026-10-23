#include "wifi_setup.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_system.h>

namespace {
constexpr const char* kApName = "Jiggler-Setup";
constexpr uint32_t kReconnectIntervalMs = 30000;

std::string randomToken() {
  char buf[17];
  snprintf(buf, sizeof(buf), "%08x%08x", (unsigned)esp_random(), (unsigned)esp_random());
  return buf;
}

std::string initialToken(TokenStore& tokens) {
  std::string t = tokens.load();
  return t.empty() ? randomToken() : t;
}
}  // namespace

struct WifiSetup::Impl {
  explicit Impl(TokenStore& t) : tokens(t) {}

  void persistToken() {
    std::string token = tokenParam->getValue();
    if (token.empty() || token.size() > 40) return;
    tokens.save(token);
    Serial.printf("API token saved: %s\n", token.c_str());
  }

  TokenStore& tokens;
  WiFiManager wm;
  std::unique_ptr<WiFiManagerParameter> tokenParam;  // created in begin(): NVS is not ready during static init
  bool announced = false;
  uint32_t lastReconnectMs = 0;
};

WifiSetup::WifiSetup(TokenStore& tokens) : impl_(new Impl(tokens)) {}
WifiSetup::~WifiSetup() = default;

void WifiSetup::begin() {
  WiFiManager& wm = impl_->wm;
  wm.setDebugOutput(false);
  wm.setConfigPortalBlocking(false);
  wm.setConfigPortalTimeout(0);
  wm.setConnectTimeout(15);
  impl_->tokenParam.reset(new WiFiManagerParameter(
      "apitoken", "Token API (per dashboard, REST e MCP; annotalo)", initialToken(impl_->tokens).c_str(), 40));
  wm.addParameter(impl_->tokenParam.get());
  wm.setSaveConfigCallback([this] { impl_->persistToken(); });
  wm.setSaveParamsCallback([this] { impl_->persistToken(); });

  // Without a token the API is unusable, so force the portal even if WiFi credentials exist.
  if (impl_->tokens.load().empty()) {
    wm.startConfigPortal(kApName);
    Serial.printf("No API token yet: setup portal '%s' started\n", kApName);
  } else {
    if (!wm.autoConnect(kApName)) Serial.printf("WiFi not connected: setup portal '%s' started\n", kApName);
  }
}

bool WifiSetup::loop() {
  Impl& s = *impl_;
  s.wm.process();

  const bool connected = WiFi.status() == WL_CONNECTED;
  const uint32_t now = millis();

  if (connected && !s.wm.getConfigPortalActive() && !s.announced) {
    s.announced = true;
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    MDNS.begin("jiggler");
    MDNS.addService("http", "tcp", 80);
    Serial.printf("WiFi connected, IP %s (jiggler.local)\n", WiFi.localIP().toString().c_str());
    return true;
  }

  if (!connected && s.announced && now - s.lastReconnectMs > kReconnectIntervalMs) {
    s.lastReconnectMs = now;
    WiFi.reconnect();
  }
  return false;
}
