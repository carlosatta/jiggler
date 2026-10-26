#include <unity.h>

#include <ArduinoJson.h>

#include "api_ex.h"
#include "event_log.h"
#include "fakes.h"

using namespace jig;

void setUp() {}
void tearDown() {}

void test_events_come_back_oldest_first_with_their_age() {
  EventLog log;
  log.add(1000, "BLE link up");
  log.add(4000, "BLE link down");
  JsonDocument d;
  TEST_ASSERT_TRUE(!deserializeJson(d, log.toJson(10000)));
  TEST_ASSERT_EQUAL_UINT32(10000, d["uptimeMs"].as<uint32_t>());
  TEST_ASSERT_EQUAL_UINT32(2, d["events"].size());
  TEST_ASSERT_EQUAL_STRING("BLE link up", d["events"][0]["message"].as<const char*>());
  TEST_ASSERT_EQUAL_UINT32(9000, d["events"][0]["agoMs"].as<uint32_t>());
  TEST_ASSERT_EQUAL_UINT32(6000, d["events"][1]["agoMs"].as<uint32_t>());
}

void test_only_the_latest_events_are_kept_and_long_ones_are_cut() {
  EventLog log;
  for (size_t i = 0; i < kEventLogCapacity + 10; ++i) log.add(static_cast<uint32_t>(i), "event " + std::to_string(i));
  log.add(500, std::string(400, 'x'));
  JsonDocument d;
  deserializeJson(d, log.toJson(1000));
  TEST_ASSERT_EQUAL_UINT32(kEventLogCapacity, d["events"].size());
  TEST_ASSERT_EQUAL_STRING("event 11", d["events"][0]["message"].as<const char*>());
  TEST_ASSERT_EQUAL_UINT32(160, std::string(d["events"][kEventLogCapacity - 1]["message"].as<const char*>()).size());
}

void test_the_log_route() {
  FakeClock clock;
  XorShiftRng rng{1};
  MemStore config;
  Controller controller{clock, rng, config};
  EventLog log;
  ApiContext ctx{controller};
  TEST_ASSERT_EQUAL_INT(404, handleApiEx(ctx, "GET", "/api/log", "").status);  // no log wired
  ctx.log = &log;
  ctx.nowMs = [] { return 5000u; };
  log.add(2000, "hello");
  HttpResponse r = handleApiEx(ctx, "GET", "/api/log", "");
  TEST_ASSERT_EQUAL_INT(200, r.status);
  JsonDocument d;
  deserializeJson(d, r.body);
  TEST_ASSERT_EQUAL_UINT32(3000, d["events"][0]["agoMs"].as<uint32_t>());
  TEST_ASSERT_EQUAL_INT(405, handleApiEx(ctx, "POST", "/api/log", "").status);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_events_come_back_oldest_first_with_their_age);
  RUN_TEST(test_only_the_latest_events_are_kept_and_long_ones_are_cut);
  RUN_TEST(test_the_log_route);
  return UNITY_END();
}
