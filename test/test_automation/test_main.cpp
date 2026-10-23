#include <unity.h>

#include <ArduinoJson.h>

#include <string>

#include "api_ex.h"
#include "automation.h"
#include "controller.h"
#include "fakes.h"

using namespace jig;

void setUp() {}
void tearDown() {}

namespace {

const char* kPing = "{\"steps\":[{\"op\":\"click\"}]}";

JsonDocument parse(const std::string& s) {
  JsonDocument doc;
  TEST_ASSERT_TRUE_MESSAGE(!deserializeJson(doc, s), s.c_str());
  return doc;
}

Automation automation(const char* id, const char* script, uint32_t minSec, uint32_t maxSec, bool enabled) {
  Automation a;
  a.id = id;
  a.script = script;
  a.minIntervalSec = minSec;
  a.maxIntervalSec = maxSec;
  a.enabled = enabled;
  return a;
}

struct Env {
  FakeClock clock;
  XorShiftRng rng{1};
  MemBlobStore blobs;
  ScriptLibrary scripts{blobs};
  AutomationEngine engine{clock, rng, scripts, blobs};

  Env() {
    TEST_ASSERT_TRUE(scripts.put("ping", kPing).ok);
    engine.begin();
  }
  void useOne(uint32_t minSec = 10, uint32_t maxSec = 10, bool enabled = true) {
    TEST_ASSERT_TRUE(engine.replace({automation("a", "ping", minSec, maxSec, enabled)}).ok);
  }
};

bool isClick(const std::optional<Plan>& p) {
  return p && p->steps.size() == 1 && p->steps[0].kind == Step::Kind::Click;
}

}  // namespace

void test_default_automation_disabled() {
  Env e;
  auto list = e.engine.list();
  TEST_ASSERT_EQUAL_UINT32(1, list.size());
  TEST_ASSERT_EQUAL_STRING("keepalive-key", list[0].id.c_str());
  TEST_ASSERT_EQUAL_STRING("keepalive-key", list[0].script.c_str());
  TEST_ASSERT_EQUAL_UINT32(60, list[0].minIntervalSec);
  TEST_ASSERT_EQUAL_UINT32(180, list[0].maxIntervalSec);
  TEST_ASSERT_FALSE(list[0].enabled);
}

void test_validate_table() {
  Env e;
  std::vector<std::pair<const char*, std::vector<Automation>>> cases = {
      {"duplicate id", {automation("a", "ping", 10, 20, true), automation("a", "ping", 10, 20, true)}},
      {"unknown script", {automation("a", "nope", 10, 20, true)}},
      {"empty script", {automation("a", "", 10, 20, true)}},
      {"min 4", {automation("a", "ping", 4, 20, true)}},
      {"max below min", {automation("a", "ping", 30, 20, true)}},
      {"max 86401", {automation("a", "ping", 10, 86401, true)}},
      {"bad id", {automation("Bad Id", "ping", 10, 20, true)}},
      {"empty id", {automation("", "ping", 10, 20, true)}},
      {"5 automations", {automation("a", "ping", 10, 20, true), automation("b", "ping", 10, 20, true), automation("c", "ping", 10, 20, true),
                         automation("d", "ping", 10, 20, true), automation("e", "ping", 10, 20, true)}},
  };
  e.useOne();
  for (const auto& c : cases) {
    CmdResult r = e.engine.replace(c.second);
    TEST_ASSERT_FALSE_MESSAGE(r.ok, c.first);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("invalid_automation", r.error.c_str(), c.first);
    TEST_ASSERT_TRUE_MESSAGE(!r.field.empty(), c.first);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("a", e.engine.list()[0].id.c_str(), c.first);  // unchanged
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(10, e.engine.list()[0].minIntervalSec, c.first);
  }
  TEST_ASSERT_TRUE(e.engine.replace({}).ok);  // an empty list is allowed
  TEST_ASSERT_TRUE(e.engine.replace({automation("a", "ping", 5, 86400, false)}).ok);
}

void test_fires_when_due_running_connected_and_reschedules() {
  Env e;
  e.useOne();
  e.clock.now = 9999;
  TEST_ASSERT_FALSE(e.engine.tick(true).has_value());
  e.clock.now = 10000;
  TEST_ASSERT_TRUE(isClick(e.engine.tick(true)));
  TEST_ASSERT_FALSE(e.engine.tick(true).has_value());
  e.clock.now = 19999;
  TEST_ASSERT_FALSE(e.engine.tick(true).has_value());
  e.clock.now = 20000;
  TEST_ASSERT_TRUE(isClick(e.engine.tick(true)));
}

void test_does_not_fire_when_not_running_or_disabled_or_disconnected() {
  Env e;
  e.useOne(10, 10, true);
  e.clock.now = 10000;
  TEST_ASSERT_FALSE(e.engine.tick(false).has_value());  // global state not Running
  e.useOne(10, 10, false);
  e.clock.now = 30000;
  TEST_ASSERT_FALSE(e.engine.tick(true).has_value());   // disabled
  e.useOne(10, 10, true);
  e.clock.mouse = false;
  e.clock.now = 60000;
  TEST_ASSERT_FALSE(e.engine.tick(true).has_value());   // disconnected
}

void test_due_waits_for_connection() {
  Env e;
  e.useOne();
  e.clock.now = 10000;
  e.clock.mouse = false;
  TEST_ASSERT_FALSE(e.engine.tick(true).has_value());
  e.clock.mouse = true;
  TEST_ASSERT_TRUE(isClick(e.engine.tick(true)));
}

void test_clock_wraparound() {
  Env e;
  e.clock.now = 0xFFFFFF00u;
  e.useOne(5, 5, true);
  const uint32_t t0 = e.clock.now;
  e.clock.now = t0 + 4999;
  TEST_ASSERT_FALSE(e.engine.tick(true).has_value());
  e.clock.now = t0 + 5000;
  TEST_ASSERT_TRUE(isClick(e.engine.tick(true)));
  e.clock.now = t0 + 9999;
  TEST_ASSERT_FALSE(e.engine.tick(true).has_value());
  e.clock.now = t0 + 10000;
  TEST_ASSERT_TRUE(isClick(e.engine.tick(true)));
}

void test_run_now_queued_in_any_state_and_errors() {
  Env e;
  CmdResult unknown = e.engine.runNow("nope");
  TEST_ASSERT_FALSE(unknown.ok);
  TEST_ASSERT_EQUAL_STRING("not_found", unknown.error.c_str());
  e.clock.mouse = false;
  CmdResult off = e.engine.runNow("ping");
  TEST_ASSERT_EQUAL_STRING("mouse_not_connected", off.error.c_str());
  e.clock.mouse = true;

  TEST_ASSERT_TRUE(e.engine.runNow("ping").ok);
  TEST_ASSERT_TRUE(isClick(e.engine.tick(false)));  // served even though the global state is not Running
  TEST_ASSERT_FALSE(e.engine.tick(false).has_value());

  for (int i = 0; i < 4; i++) TEST_ASSERT_TRUE(e.engine.runNow("ping").ok);
  CmdResult busy = e.engine.runNow("ping");
  TEST_ASSERT_FALSE(busy.ok);
  TEST_ASSERT_EQUAL_STRING("busy", busy.error.c_str());
}

void test_pending_run_dropped_if_mouse_disconnects() {
  Env e;
  TEST_ASSERT_TRUE(e.engine.runNow("ping").ok);
  e.clock.mouse = false;
  TEST_ASSERT_FALSE(e.engine.tick(true).has_value());
  e.clock.mouse = true;
  TEST_ASSERT_FALSE(e.engine.tick(true).has_value());
}

void test_deleted_script_skipped_and_rescheduled() {
  Env e;
  e.useOne();
  TEST_ASSERT_TRUE(e.scripts.remove("ping").ok);
  e.clock.now = 10000;
  TEST_ASSERT_FALSE(e.engine.tick(true).has_value());
  TEST_ASSERT_TRUE(e.scripts.put("ping", kPing).ok);   // restored: not retried until the next due time
  TEST_ASSERT_FALSE(e.engine.tick(true).has_value());
  e.clock.now = 20000;
  TEST_ASSERT_TRUE(isClick(e.engine.tick(true)));
}

void test_replace_persists_and_invalid_changes_nothing() {
  Env e;
  e.useOne(15, 25, true);
  AutomationEngine again(e.clock, e.rng, e.scripts, e.blobs);
  again.begin();
  TEST_ASSERT_EQUAL_UINT32(1, again.list().size());
  TEST_ASSERT_EQUAL_STRING("a", again.list()[0].id.c_str());
  TEST_ASSERT_EQUAL_UINT32(15, again.list()[0].minIntervalSec);
  TEST_ASSERT_TRUE(again.list()[0].enabled);
}

void test_corrupt_blob_falls_back_to_defaults() {
  Env e;
  e.blobs.put("autos", "{nope");
  AutomationEngine again(e.clock, e.rng, e.scripts, e.blobs);
  again.begin();
  TEST_ASSERT_EQUAL_STRING("keepalive-key", again.list()[0].id.c_str());
  e.blobs.put("autos", "{\"automations\":[{\"id\":\"a\",\"script\":\"ping\",\"minIntervalSec\":1,\"maxIntervalSec\":2,\"enabled\":true}]}");
  AutomationEngine invalid(e.clock, e.rng, e.scripts, e.blobs);
  invalid.begin();
  TEST_ASSERT_EQUAL_STRING("keepalive-key", invalid.list()[0].id.c_str());
}

void test_automation_routes() {
  Env e;
  FakeClock clockForController;
  XorShiftRng rng(1);
  MemStore config;
  Controller controller(clockForController, rng, config);
  controller.begin();
  ApiContext ctx{controller};
  ctx.scripts = &e.scripts;
  ctx.automations = &e.engine;
  auto call = [&](const std::string& method, const std::string& path, const std::string& body = "") {
    return handleApiEx(ctx, method, path, body);
  };

  HttpResponse g = call("GET", "/api/automations");
  TEST_ASSERT_EQUAL_INT(200, g.status);
  TEST_ASSERT_EQUAL_STRING("keepalive-key", parse(g.body)["automations"][0]["id"].as<const char*>());

  HttpResponse put = call("PUT", "/api/automations",
      "{\"automations\":[{\"id\":\"ogni-tanto\",\"script\":\"ping\",\"minIntervalSec\":30,\"maxIntervalSec\":60,\"enabled\":true}]}");
  TEST_ASSERT_EQUAL_INT(200, put.status);
  TEST_ASSERT_EQUAL_STRING("ogni-tanto", e.engine.list()[0].id.c_str());

  HttpResponse bad = call("PUT", "/api/automations",
      "{\"automations\":[{\"id\":\"x\",\"script\":\"nope\",\"minIntervalSec\":30,\"maxIntervalSec\":60,\"enabled\":true}]}");
  TEST_ASSERT_EQUAL_INT(400, bad.status);
  JsonDocument bd = parse(bad.body);
  TEST_ASSERT_EQUAL_STRING("invalid_automation", bd["error"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("automations[0].script", bd["field"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("ogni-tanto", e.engine.list()[0].id.c_str());

  for (const char* body : {"", "{", "[]", "{}", "{\"automations\":5}", "{\"automations\":[5]}",
                           "{\"automations\":[{\"id\":\"a\",\"script\":\"ping\",\"minIntervalSec\":\"x\",\"maxIntervalSec\":9,\"enabled\":true}]}",
                           "{\"automations\":[{\"id\":\"a\",\"script\":\"ping\",\"minIntervalSec\":10.5,\"maxIntervalSec\":20,\"enabled\":true}]}",
                           "{\"automations\":[{\"id\":\"a\",\"script\":\"ping\",\"minIntervalSec\":10,\"maxIntervalSec\":20,\"enabled\":true,\"bogus\":1}]}"})
    TEST_ASSERT_EQUAL_INT_MESSAGE(400, call("PUT", "/api/automations", body).status, body);
  TEST_ASSERT_EQUAL_INT(413, call("PUT", "/api/automations", std::string(kMaxMediumBodyBytes + 1, ' ')).status);
  TEST_ASSERT_EQUAL_INT(405, call("DELETE", "/api/automations").status);

  HttpResponse run = call("POST", "/api/scripts/ping/run");
  TEST_ASSERT_EQUAL_INT(202, run.status);
  TEST_ASSERT_TRUE(parse(run.body)["queued"].as<bool>());
  TEST_ASSERT_TRUE(isClick(e.engine.tick(false)));
  TEST_ASSERT_EQUAL_INT(404, call("POST", "/api/scripts/nope/run").status);
  e.clock.mouse = false;
  HttpResponse off = call("POST", "/api/scripts/ping/run");
  TEST_ASSERT_EQUAL_INT(409, off.status);
  TEST_ASSERT_EQUAL_STRING("mouse_not_connected", parse(off.body)["error"].as<const char*>());
  e.clock.mouse = true;
  TEST_ASSERT_EQUAL_INT(405, call("GET", "/api/scripts/ping/run").status);
  TEST_ASSERT_EQUAL_INT(404, call("POST", "/api/scripts/ping/other").status);

  ctx.automations = nullptr;
  TEST_ASSERT_EQUAL_INT(404, call("GET", "/api/automations").status);
  TEST_ASSERT_EQUAL_INT(404, call("POST", "/api/scripts/ping/run").status);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_default_automation_disabled);
  RUN_TEST(test_validate_table);
  RUN_TEST(test_fires_when_due_running_connected_and_reschedules);
  RUN_TEST(test_does_not_fire_when_not_running_or_disabled_or_disconnected);
  RUN_TEST(test_due_waits_for_connection);
  RUN_TEST(test_clock_wraparound);
  RUN_TEST(test_run_now_queued_in_any_state_and_errors);
  RUN_TEST(test_pending_run_dropped_if_mouse_disconnects);
  RUN_TEST(test_deleted_script_skipped_and_rescheduled);
  RUN_TEST(test_replace_persists_and_invalid_changes_nothing);
  RUN_TEST(test_corrupt_blob_falls_back_to_defaults);
  RUN_TEST(test_automation_routes);
  return UNITY_END();
}
