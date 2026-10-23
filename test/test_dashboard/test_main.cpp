#include <unity.h>

#include <set>
#include <string>

#include "api_ex.h"
#include "automation.h"
#include "controller.h"
#include "dashboard_html.h"
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
    {"POST", "/api/pause", ""},
    {"POST", "/api/stop", ""},
    {"POST", "/api/move", ""},
    {"PUT", "/api/config", "{}"},
    {"GET", "/api/profiles", ""},
    {"GET", "/api/devices", ""},
    {"PUT", "/api/devices", "{\"devices\":[{\"id\":\"mouse\",\"kind\":\"mouse\",\"enabled\":true,\"profileId\":\"logitech-mx-master-3s\"}]}"},
    {"GET", "/api/scripts", ""},
    {"PUT", "/api/scripts/demo", "{\"steps\":[{\"op\":\"click\"}]}"},
    {"GET", "/api/scripts/demo", ""},
    {"POST", "/api/scripts/demo/run", ""},
    {"DELETE", "/api/scripts/demo", ""},
    {"GET", "/api/automations", ""},
    {"PUT", "/api/automations", "{\"automations\":[]}"},
    {"GET", "/api/network", ""},
    {"PUT", "/api/network", "{}"},
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
  for (const std::string& p : referenced) TEST_ASSERT_TRUE_MESSAGE(known.count(p) == 1, p.c_str());
  for (const std::string& p : known) TEST_ASSERT_TRUE_MESSAGE(referenced.count(p) == 1, p.c_str());

  FakeClock clock;
  XorShiftRng rng(1);
  MemStore config;
  Controller controller(clock, rng, config);
  controller.begin();
  MemBlobStore blobs;
  MemNetworkStore net;
  DeviceRegistry devices(blobs);
  devices.begin();
  ScriptLibrary scripts(blobs);
  AutomationEngine automations(clock, rng, scripts, blobs);
  automations.begin();
  int restarts = 0;
  ApiContext ctx{controller};
  ctx.network = &net;
  ctx.devices = &devices;
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

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_dashboard_only_references_real_routes);
  RUN_TEST(test_dashboard_is_self_contained_html);
  RUN_TEST(test_dashboard_never_injects_html);
  return UNITY_END();
}
