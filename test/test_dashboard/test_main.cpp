#include <unity.h>

#include <set>
#include <string>

#include "api_ex.h"
#include "appearance.h"
#include "automation.h"
#include "controller.h"
#include "bluetooth.h"
#include "dashboard_html.h"
#include "schedule.h"
#include "web_assets.h"
#include "fakes.h"

using namespace jig;

void setUp() {}
void tearDown() {}

namespace {

struct MemNetworkStore : NetworkStore {
  NetworkSettings stored;
  NetworkSettings load() override { return stored; }
  void save(const NetworkSettings& s) override { stored = s; }
};

struct FakeLink : BleLink {
  std::vector<std::string> bonded{"aa:bb:cc:dd:ee:ff"};
  std::vector<std::string> bondedAddresses() override { return bonded; }
  std::vector<std::string> connectedAddresses() override { return {}; }
  void disconnectAll() override {}
  bool forgetBond(const std::string& a) override {
    for (auto it = bonded.begin(); it != bonded.end(); ++it)
      if (*it == a) { bonded.erase(it); return true; }
    return false;
  }
  void forgetAllBonds() override { bonded.clear(); }
  void setAdvertising(bool) override {}
  void setPairingOpen(bool) override {}
  bool takePeerDisconnectRequest() override { return false; }
};

struct MemTokens : TokenVault {
  std::string token = "tok";
  std::string load() override { return token; }
  bool save(const std::string& t) override { token = t; return true; }
};

struct Route {
  const char* method;
  const char* path;
  const char* body;
};

// Every (method, path) the dashboard page calls, in an order that works against the stateful router
// (a script is created before it is read, run and deleted).
const Route kPageRoutes[] = {
    {"GET", "/api/status", ""},
    {"POST", "/api/start", ""},
    {"POST", "/api/start", "{\"minutes\":30}"},
    {"POST", "/api/pause", "{\"minutes\":15}"},
    {"POST", "/api/resume", ""},
    {"POST", "/api/stop", ""},
    {"POST", "/api/move", ""},
    {"PUT", "/api/config", "{}"},
    {"GET", "/api/appearance", ""},
    {"PUT", "/api/appearance", "{\"profile\":\"logitech-mx-master-3s\"}"},
    {"GET", "/api/schedules", ""},
    {"PUT", "/api/schedules", "{\"timezone\":\"Europe/Rome\",\"schedules\":[]}"},
    {"GET", "/api/network", ""},
    {"PUT", "/api/network", "{}"},
    {"GET", "/api/bluetooth", ""},
    {"POST", "/api/bluetooth/pairing", "{\"seconds\":120}"},
    {"DELETE", "/api/bluetooth/pairing", ""},
    {"POST", "/api/bluetooth/disconnect", ""},
    {"POST", "/api/bluetooth/reconnect", ""},
    {"PUT", "/api/bluetooth/peers/aabbccddeeff", "{\"label\":\"Mac\"}"},
    {"DELETE", "/api/bluetooth/peers/aabbccddeeff", ""},
    {"DELETE", "/api/bluetooth/peers", ""},
    {"PUT", "/api/token", "{\"current\":\"tok\",\"token\":\"newtoken99\"}"},
    {"POST", "/api/restart", ""},
};

// First path segment after /api/ for every "/api/<name>" occurrence in `text`.
std::set<std::string> apiPathsIn(const std::string& text) {
  std::set<std::string> out;
  size_t pos = 0;
  while ((pos = text.find("/api/", pos)) != std::string::npos) {
    size_t end = pos + 5;
    while (end < text.size() && ((text[end] >= 'a' && text[end] <= 'z') || text[end] == '_')) end++;
    out.insert(text.substr(pos, end - pos));
    pos = end;
  }
  return out;
}

}  // namespace

void test_dashboard_only_references_real_routes() {
  const std::string html = kDashboardHtml;
  const std::set<std::string> referenced = apiPathsIn(html);

  std::set<std::string> known;
  for (const Route& r : kPageRoutes)
    for (const std::string& p : apiPathsIn(r.path)) known.insert(p);
  // Routes served by the HTTP transport itself (a streamed upload cannot go through handleApiEx).
  const std::set<std::string> transportRoutes = {"/api/update"};
  for (const std::string& p : referenced)
    TEST_ASSERT_TRUE_MESSAGE(known.count(p) == 1 || transportRoutes.count(p) == 1, p.c_str());
  for (const std::string& p : known) TEST_ASSERT_TRUE_MESSAGE(referenced.count(p) == 1, p.c_str());

  FakeClock clock;
  XorShiftRng rng(1);
  MemStore config;
  Controller controller(clock, rng, config);
  controller.begin();
  MemBlobStore blobs;
  MemNetworkStore net;
  AppearanceStore appearance(blobs);
  appearance.begin();
  ScriptLibrary scripts(blobs);
  AutomationEngine automations(clock, rng, scripts, blobs);
  automations.begin();
  ScheduleEngine schedules(clock, controller, blobs);
  schedules.begin();
  FakeLink link;
  MemBlobStore peerBlobs;
  BluetoothManager bluetooth(clock, link, peerBlobs);
  bluetooth.begin();
  MemTokens tokens;
  int restarts = 0;
  ApiContext ctx{controller};
  ctx.bluetooth = &bluetooth;
  ctx.schedules = &schedules;
  ctx.tokens = &tokens;
  ctx.network = &net;
  ctx.appearance = &appearance;
  ctx.scripts = &scripts;
  ctx.automations = &automations;
  ctx.restart = [&] { restarts++; };

  for (const Route& r : kPageRoutes) {
    HttpResponse res = handleApiEx(ctx, r.method, r.path, r.body);
    const std::string what = std::string(r.method) + " " + r.path;
    TEST_ASSERT_TRUE_MESSAGE(res.status != 404 && res.status != 405 && res.status < 400, what.c_str());
  }
  TEST_ASSERT_EQUAL_INT(1, restarts);
}

void test_dashboard_is_self_contained_html() {
  const std::string html = kDashboardHtml;
  TEST_ASSERT_TRUE(html.find("<!doctype html>") == 0);
  TEST_ASSERT_TRUE(html.find("http://") == std::string::npos);
  TEST_ASSERT_TRUE(html.find("https://") == std::string::npos);
  TEST_ASSERT_TRUE(html.find("Authorization") != std::string::npos);
  TEST_ASSERT_TRUE(html.find("localStorage") != std::string::npos);
}

void test_dashboard_never_injects_html() {
  const std::string html = kDashboardHtml;
  for (const char* sink : {"innerHTML", "outerHTML", "insertAdjacentHTML", "document.write", "eval(", "new Function"})
    TEST_ASSERT_TRUE_MESSAGE(html.find(sink) == std::string::npos, sink);
}

void test_pwa_assets_are_served_and_referenced() {
  const std::string html = kDashboardHtml;
  for (const char* ref : {"/manifest.webmanifest", "/icon-192.png", "/apple-touch-icon.png"})
    TEST_ASSERT_TRUE_MESSAGE(html.find(ref) != std::string::npos, ref);
  const std::string manifest = webassets::kManifest;
  for (const char* key : {"\"display\": \"standalone\"", "\"start_url\"", "/icon-192.png", "/icon-512.png"})
    TEST_ASSERT_TRUE_MESSAGE(manifest.find(key) != std::string::npos, key);
  const uint8_t* icons[] = {webassets::kIcon192, webassets::kIcon512, webassets::kAppleTouchIcon};
  const size_t sizes[] = {webassets::kIcon192Size, webassets::kIcon512Size, webassets::kAppleTouchIconSize};
  for (int i = 0; i < 3; ++i) {
    TEST_ASSERT_TRUE(sizes[i] > 1000);
    TEST_ASSERT_EQUAL_HEX8(0x89, icons[i][0]);
    TEST_ASSERT_EQUAL_HEX8('P', icons[i][1]);
    TEST_ASSERT_EQUAL_HEX8('N', icons[i][2]);
    TEST_ASSERT_EQUAL_HEX8('G', icons[i][3]);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_dashboard_only_references_real_routes);
  RUN_TEST(test_dashboard_is_self_contained_html);
  RUN_TEST(test_dashboard_never_injects_html);
  RUN_TEST(test_pwa_assets_are_served_and_referenced);
  return UNITY_END();
}
