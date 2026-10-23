#include <unity.h>

#include <ArduinoJson.h>

#include <string>

#include "api_ex.h"
#include "controller.h"
#include "fakes.h"
#include "network_settings.h"

using namespace jig;

void setUp() {}
void tearDown() {}

namespace {

struct MemNetworkStore : NetworkStore {
  NetworkSettings stored;
  int saves = 0;
  NetworkSettings load() override { return stored; }
  void save(const NetworkSettings& s) override {
    stored = s;
    saves++;
  }
};

struct Env {
  FakeClock clock;
  XorShiftRng rng{1};
  MemStore config;
  Controller controller{clock, rng, config};
  MemNetworkStore net;
  int restarts = 0;
  ApiContext ctx{controller};

  Env() {
    controller.begin();
    ctx.network = &net;
    ctx.restart = [this] { restarts++; };
  }
  HttpResponse call(const std::string& method, const std::string& path, const std::string& body = "") {
    return handleApiEx(ctx, method, path, body);
  }
};

JsonDocument parse(const std::string& s) {
  JsonDocument doc;
  TEST_ASSERT_TRUE_MESSAGE(!deserializeJson(doc, s), s.c_str());
  return doc;
}

}  // namespace

void test_defaults() {
  NetworkSettings n;
  TEST_ASSERT_TRUE(n.mode == NetworkMode::Auto);
  TEST_ASSERT_EQUAL_STRING("Jiggler", n.apSsid.c_str());
  TEST_ASSERT_EQUAL_STRING("", n.apPassword.c_str());
  TEST_ASSERT_FALSE(validateNetwork(n).has_value());
}

void test_validate() {
  NetworkSettings n;
  n.apSsid = "";
  TEST_ASSERT_EQUAL_STRING("apSsid", validateNetwork(n)->field.c_str());
  n.apSsid = std::string(33, 'a');
  TEST_ASSERT_TRUE(validateNetwork(n).has_value());
  n.apSsid = std::string(32, 'a');
  TEST_ASSERT_FALSE(validateNetwork(n).has_value());
  n.apPassword = "1234567";
  TEST_ASSERT_EQUAL_STRING("apPassword", validateNetwork(n)->field.c_str());
  n.apPassword = "12345678";
  TEST_ASSERT_FALSE(validateNetwork(n).has_value());
  n.apPassword = std::string(63, 'p');
  TEST_ASSERT_FALSE(validateNetwork(n).has_value());
  n.apPassword = std::string(64, 'p');
  TEST_ASSERT_TRUE(validateNetwork(n).has_value());
}

void test_patch_partial_and_atomic() {
  NetworkSettings base;
  ParseError err;
  TEST_ASSERT_TRUE(parseNetworkPatch("{\"mode\":\"ap\"}", base, err));
  TEST_ASSERT_TRUE(base.mode == NetworkMode::Ap);
  TEST_ASSERT_EQUAL_STRING("Jiggler", base.apSsid.c_str());
  NetworkSettings before = base;
  TEST_ASSERT_FALSE(parseNetworkPatch("{\"mode\":\"auto\",\"apSsid\":\"Nuova\",\"apPassword\":\"short\"}", base, err));
  TEST_ASSERT_EQUAL_STRING("apPassword", err.field.c_str());
  TEST_ASSERT_TRUE(base.mode == before.mode);
  TEST_ASSERT_EQUAL_STRING(before.apSsid.c_str(), base.apSsid.c_str());
}

void test_bad_input_table() {  // RF5
  const std::string big = "{\"apSsid\":\"" + std::string(1100, 'a') + "\"}";
  const struct {
    std::string body;
    int status;
  } cases[] = {
      {"", 400},          {"{", 400},
      {"[]", 400},        {"{\"mode\":\"x\"}", 400},
      {"{\"mode\":5}", 400},
      {"{\"apSsid\":null}", 400},
      {"{\"apPassword\":\"short\"}", 400},
      {"{\"bogus\":1}", 400},
      {big, 413},
  };
  for (const auto& tc : cases) {
    Env e;
    HttpResponse r = e.call("PUT", "/api/network", tc.body);
    TEST_ASSERT_EQUAL_INT_MESSAGE(tc.status, r.status, tc.body.substr(0, 40).c_str());
    TEST_ASSERT_EQUAL_INT(0, e.net.saves);
  }
}

void test_get_put_network_routes() {
  Env e;
  HttpResponse g = e.call("GET", "/api/network");
  TEST_ASSERT_EQUAL_INT(200, g.status);
  JsonDocument d = parse(g.body);
  TEST_ASSERT_EQUAL_STRING("auto", d["mode"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("Jiggler", d["apSsid"].as<const char*>());

  HttpResponse p = e.call("PUT", "/api/network", "{\"mode\":\"ap\",\"apPassword\":\"supersegreta\"}");
  TEST_ASSERT_EQUAL_INT(200, p.status);
  TEST_ASSERT_EQUAL_STRING("ap", parse(p.body)["mode"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(1, e.net.saves);
  TEST_ASSERT_TRUE(e.net.stored.mode == NetworkMode::Ap);
  TEST_ASSERT_EQUAL_STRING("supersegreta", e.net.stored.apPassword.c_str());

  HttpResponse bad = e.call("PUT", "/api/network", "{\"apPassword\":\"short\"}");
  TEST_ASSERT_EQUAL_INT(400, bad.status);
  JsonDocument bd = parse(bad.body);
  TEST_ASSERT_EQUAL_STRING("invalid_network", bd["error"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("apPassword", bd["field"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(405, e.call("DELETE", "/api/network").status);
}

void test_restart_route_calls_callback_once_and_returns_202() {
  Env e;
  HttpResponse r = e.call("POST", "/api/restart");
  TEST_ASSERT_EQUAL_INT(202, r.status);
  TEST_ASSERT_TRUE(parse(r.body)["restarting"].as<bool>());
  TEST_ASSERT_EQUAL_INT(1, e.restarts);
  TEST_ASSERT_EQUAL_INT(405, e.call("GET", "/api/restart").status);
  TEST_ASSERT_EQUAL_INT(1, e.restarts);
}

void test_legacy_routes_pass_through() {
  Env e;
  HttpResponse s = e.call("GET", "/api/status");
  TEST_ASSERT_EQUAL_INT(200, s.status);
  TEST_ASSERT_EQUAL_STRING("running", parse(s.body)["state"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(404, e.call("GET", "/api/nope").status);
  TEST_ASSERT_EQUAL_INT(405, e.call("GET", "/api/start").status);
}

void test_network_routes_404_when_service_missing() {
  Env e;
  e.ctx.network = nullptr;
  TEST_ASSERT_EQUAL_INT(404, e.call("GET", "/api/network").status);
  TEST_ASSERT_EQUAL_INT(404, e.call("PUT", "/api/network", "{}").status);
}

void test_captive_probe_paths() {
  for (const char* p : {"/generate_204", "/gen_204", "/hotspot-detect.html", "/library/test/success.html",
                        "/ncsi.txt", "/connecttest.txt", "/redirect", "/canonical.html", "/success.txt"})
    TEST_ASSERT_TRUE_MESSAGE(isCaptiveProbePath(p), p);
  for (const char* p : {"/", "/api/status", "/mcp", "/favicon.ico", ""})
    TEST_ASSERT_FALSE_MESSAGE(isCaptiveProbePath(p), p);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_defaults);
  RUN_TEST(test_validate);
  RUN_TEST(test_patch_partial_and_atomic);
  RUN_TEST(test_bad_input_table);
  RUN_TEST(test_get_put_network_routes);
  RUN_TEST(test_restart_route_calls_callback_once_and_returns_202);
  RUN_TEST(test_legacy_routes_pass_through);
  RUN_TEST(test_network_routes_404_when_service_missing);
  RUN_TEST(test_captive_probe_paths);
  return UNITY_END();
}
