#include <unity.h>

#include <ArduinoJson.h>

#include "api_ex.h"
#include "clock_settings.h"
#include "fakes.h"

using namespace jig;

void setUp() {}
void tearDown() {}

namespace {

struct Env {
  FakeClock clock;
  XorShiftRng rng{1};
  MemStore config;
  Controller controller{clock, rng, config};
  FlakyBlobStore blobs;
  ClockStore store{clock, blobs};
  ApiContext ctx{controller};
  Env() {
    controller.begin();
    store.begin();
    ctx.clock = &store;
  }
};

JsonDocument parse(const std::string& s) {
  JsonDocument doc;
  TEST_ASSERT_TRUE_MESSAGE(!deserializeJson(doc, s), s.c_str());
  return doc;
}

}  // namespace

void test_defaults_and_the_ntp_server_reaches_the_platform() {
  Env e;
  TEST_ASSERT_EQUAL_STRING("Europe/Rome", e.store.settings().timezone.c_str());
  TEST_ASSERT_EQUAL_STRING("pool.ntp.org", e.store.settings().ntpServer.c_str());
  TEST_ASSERT_EQUAL_STRING("pool.ntp.org", e.clock.ntp.c_str());
  ClockSettings c = e.store.settings();
  c.ntpServer = "time.google.com";
  c.timezone = "Asia/Tokyo";
  TEST_ASSERT_TRUE(e.store.replace(c).ok);
  TEST_ASSERT_EQUAL_STRING("time.google.com", e.clock.ntp.c_str());
  TEST_ASSERT_EQUAL_STRING("JST-9", e.store.posixTz().c_str());
  c.ntpServer = "";                                   // no NTP: the time is set by hand
  TEST_ASSERT_TRUE(e.store.replace(c).ok);
  TEST_ASSERT_EQUAL_STRING("", e.clock.ntp.c_str());
}

void test_settings_persist_and_a_bad_value_changes_nothing() {
  Env e;
  ClockSettings c;
  c.timezone = "America/Chicago";
  c.ntpServer = "ntp.example.org";
  TEST_ASSERT_TRUE(e.store.replace(c).ok);
  ClockStore again(e.clock, e.blobs);
  again.begin();
  TEST_ASSERT_EQUAL_STRING("America/Chicago", again.settings().timezone.c_str());
  TEST_ASSERT_EQUAL_STRING("ntp.example.org", again.settings().ntpServer.c_str());

  ClockSettings bad = c;
  bad.timezone = "Mars/Olympus";
  CmdResult r = e.store.replace(bad);
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL_STRING("timezone", r.field.c_str());
  bad = c;
  bad.ntpServer = "not a host!";
  TEST_ASSERT_EQUAL_STRING("ntpServer", e.store.replace(bad).field.c_str());
  TEST_ASSERT_EQUAL_STRING("America/Chicago", e.store.settings().timezone.c_str());

  e.blobs.failPuts = true;
  TEST_ASSERT_EQUAL_STRING("storage_error", e.store.replace(c).error.c_str());
}

void test_a_timezone_saved_by_the_old_schedule_page_is_kept() {
  FakeClock clock;
  FlakyBlobStore blobs;
  blobs.put("schedules", "{\"timezone\":\"Europe/London\",\"schedules\":[]}");
  ClockStore store(clock, blobs);
  store.begin();
  TEST_ASSERT_EQUAL_STRING("Europe/London", store.settings().timezone.c_str());
}

void test_ntp_server_validation() {
  for (const char* ok : {"", "pool.ntp.org", "192.168.1.1", "time-a.nist.gov", "ntp1"}) TEST_ASSERT_TRUE_MESSAGE(validNtpServer(ok), ok);
  for (const char* bad : {" ", "a b", ".x", "-x", "x-", "http://x", "a/b", "ünï"}) TEST_ASSERT_FALSE_MESSAGE(validNtpServer(bad), bad);
  TEST_ASSERT_FALSE(validNtpServer(std::string(64, 'a')));
}

void test_setting_the_time_by_hand() {
  Env e;
  e.clock.unixTime = 0;
  TEST_ASSERT_TRUE(e.store.setTime(1790000000).ok);
  TEST_ASSERT_EQUAL_UINT32(1790000000, e.clock.unixTime);
  TEST_ASSERT_EQUAL_STRING("manual", e.clock.source);
  for (uint32_t bad : {0u, 1000u, 1699999999u, 4102444801u}) {
    CmdResult r = e.store.setTime(bad);
    TEST_ASSERT_FALSE_MESSAGE(r.ok, std::to_string(bad).c_str());
    TEST_ASSERT_EQUAL_STRING("invalid_time", r.error.c_str());
  }
  e.clock.canSetTime = false;
  TEST_ASSERT_EQUAL_STRING("not_supported", e.store.setTime(1790000000).error.c_str());
}

void test_status_reports_the_source_and_the_local_time() {
  Env e;
  ClockStatus s = e.store.status();
  TEST_ASSERT_FALSE(s.synced);
  TEST_ASSERT_EQUAL_STRING("none", s.source.c_str());
  TEST_ASSERT_TRUE(parse(clockStatusToJson(s))["localTime"].isNull());
  e.clock.local = LocalTime{2, 9 * 60 + 5, 20005};
  s = e.store.status();
  TEST_ASSERT_TRUE(s.synced);
  TEST_ASSERT_EQUAL_STRING("ntp", s.source.c_str());
  TEST_ASSERT_EQUAL_STRING("09:05", s.localTime.c_str());
  TEST_ASSERT_EQUAL_STRING("wed", s.weekday.c_str());
  TEST_ASSERT_EQUAL_STRING("Europe/Rome", s.timezone.c_str());
}

void test_time_routes() {
  Env e;
  HttpResponse g = handleApiEx(e.ctx, "GET", "/api/time", "");
  TEST_ASSERT_EQUAL_INT(200, g.status);
  JsonDocument d = parse(g.body);
  TEST_ASSERT_EQUAL_STRING("Europe/Rome", d["timezone"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("pool.ntp.org", d["ntpServer"].as<const char*>());
  TEST_ASSERT_TRUE(d["timezones"].size() > 5);
  TEST_ASSERT_TRUE(d["status"]["synced"].isNull() == false);

  HttpResponse put = handleApiEx(e.ctx, "PUT", "/api/time", g.body);  // a GET body can be sent back unchanged
  TEST_ASSERT_EQUAL_INT(200, put.status);
  put = handleApiEx(e.ctx, "PUT", "/api/time", "{\"timezone\":\"Europe/London\",\"ntpServer\":\"time.cloudflare.com\"}");
  TEST_ASSERT_EQUAL_INT(200, put.status);
  TEST_ASSERT_EQUAL_STRING("time.cloudflare.com", parse(put.body)["ntpServer"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("time.cloudflare.com", e.clock.ntp.c_str());
  TEST_ASSERT_EQUAL_INT(400, handleApiEx(e.ctx, "PUT", "/api/time", "{\"timezone\":\"Mars/Olympus\"}").status);
  TEST_ASSERT_EQUAL_INT(400, handleApiEx(e.ctx, "PUT", "/api/time", "{\"nope\":1}").status);
  TEST_ASSERT_EQUAL_INT(400, handleApiEx(e.ctx, "PUT", "/api/time", "[]").status);

  HttpResponse set = handleApiEx(e.ctx, "POST", "/api/time/set", "{\"epoch\":1795000000}");
  TEST_ASSERT_EQUAL_INT(200, set.status);
  TEST_ASSERT_EQUAL_UINT32(1795000000, e.clock.unixTime);
  TEST_ASSERT_EQUAL_INT(400, handleApiEx(e.ctx, "POST", "/api/time/set", "{\"epoch\":5}").status);
  TEST_ASSERT_EQUAL_INT(400, handleApiEx(e.ctx, "POST", "/api/time/set", "{\"epoch\":\"x\"}").status);
  TEST_ASSERT_EQUAL_INT(400, handleApiEx(e.ctx, "POST", "/api/time/set", "{}").status);
  TEST_ASSERT_EQUAL_INT(405, handleApiEx(e.ctx, "GET", "/api/time/set", "").status);
  TEST_ASSERT_EQUAL_INT(405, handleApiEx(e.ctx, "POST", "/api/time", "").status);
  TEST_ASSERT_EQUAL_INT(413, handleApiEx(e.ctx, "PUT", "/api/time", std::string(kMaxSmallBodyBytes + 1, ' ')).status);

  e.blobs.failPuts = true;
  TEST_ASSERT_EQUAL_INT(507, handleApiEx(e.ctx, "PUT", "/api/time", "{\"timezone\":\"UTC\"}").status);

  ApiContext none{e.controller};
  TEST_ASSERT_EQUAL_INT(404, handleApiEx(none, "GET", "/api/time", "").status);
  TEST_ASSERT_TRUE(parse(handleApiEx(none, "GET", "/api/status", "").body)["time"].isNull());
  e.clock.local = LocalTime{0, 9 * 60, 20003};
  JsonDocument st = parse(handleApiEx(e.ctx, "GET", "/api/status", "").body);
  TEST_ASSERT_EQUAL_STRING("09:00", st["time"]["localTime"].as<const char*>());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_and_the_ntp_server_reaches_the_platform);
  RUN_TEST(test_settings_persist_and_a_bad_value_changes_nothing);
  RUN_TEST(test_a_timezone_saved_by_the_old_schedule_page_is_kept);
  RUN_TEST(test_ntp_server_validation);
  RUN_TEST(test_setting_the_time_by_hand);
  RUN_TEST(test_status_reports_the_source_and_the_local_time);
  RUN_TEST(test_time_routes);
  return UNITY_END();
}
