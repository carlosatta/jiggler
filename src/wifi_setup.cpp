#include "wifi_setup.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_system.h>

namespace {
constexpr const char* kApName = "Jiggler-Setup";
constexpr uint32_t kReconnectIntervalMs = 30000;
constexpr uint16_t kDnsPort = 53;

std::string randomHex(size_t digits) {
  std::string out;
  while (out.size() < digits) {
    char buf[9];
    snprintf(buf, sizeof(buf), "%08x", (unsigned)esp_random());
    out += buf;
  }
  out.resize(digits);
  return out;
}

std::string initialToken(TokenStore& tokens) {
  std::string t = tokens.load();
  return t.empty() ? randomHex(16) : t;
}
}  // namespace

struct WifiSetup::Impl {
  Impl(TokenStore& t, jig::NetworkStore& n) : tokens(t), network(n) {}

  void persistToken() {
    std::string token = tokenParam->getValue();
    if (token.empty() || token.size() > 40) return;
    tokens.save(token);
    Serial.printf("API token saved: %s\n", token.c_str());
  }

  void startAccessPoint() {
    jig::NetworkSettings stored = network.load();
    jig::NetworkSettings active = stored;
#ifdef JIG_FORCE_AP
    active.mode = jig::NetworkMode::Ap;
#endif
    if (active.apPassword.empty()) {
      active.apPassword = randomHex(10);
      stored.apPassword = active.apPassword;  // persist the password only, never a forced mode
      network.save(stored);
    }
    WiFi.mode(WIFI_AP);
    WiFi.softAP(active.apSsid.c_str(), active.apPassword.c_str());
    dns.start(kDnsPort, "*", WiFi.softAPIP());
    MDNS.begin("jiggler");
    MDNS.addService("http", "tcp", 80);
    if (tokens.load().empty()) tokens.save(active.apPassword);  // the AP password doubles as the first token
    ap = true;
    Serial.printf("AP mode: SSID '%s' password '%s' IP %s (captive portal on)\n", active.apSsid.c_str(),
                  active.apPassword.c_str(), WiFi.softAPIP().toString().c_str());
  }

  TokenStore& tokens;
  jig::NetworkStore& network;
  WiFiManager wm;
  DNSServer dns;
  std::unique_ptr<WiFiManagerParameter> tokenParam;  // created in begin(): NVS is not ready during static init
  bool ap = false;
  bool announced = false;
  uint32_t lastReconnectMs = 0;
};

WifiSetup::WifiSetup(TokenStore& tokens, jig::NetworkStore& network) : impl_(new Impl(tokens, network)) {}
WifiSetup::~WifiSetup() = default;

bool WifiSetup::apMode() const { return impl_->ap; }

void WifiSetup::begin() {
  bool wantAp = impl_->network.load().mode == jig::NetworkMode::Ap;
#ifdef JIG_FORCE_AP
  wantAp = true;
#endif
  if (wantAp) {
    impl_->startAccessPoint();
    return;
  }

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
  if (s.ap) {
    s.dns.processNextRequest();
    if (!s.announced) {
      s.announced = true;
      return true;
    }
    return false;
  }

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
