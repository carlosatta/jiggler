#include <unity.h>

#include <ArduinoJson.h>

#include <cstring>
#include <string>

#include "api_router.h"
#include "controller.h"
#include "fakes.h"
#include "json_codec.h"

using namespace jig;

void setUp() {}
void tearDown() {}

namespace {

struct Api {
  FakeClock clock;
  XorShiftRng rng{1};
  MemStore store;
  Controller c{clock, rng, store};

  Api() { c.begin(); }

  HttpResponse call(const std::string& method, const std::string& path, const std::string& body = "") {
    return handleApi(c, method, path, body);
  }
};

JsonDocument parse(const std::string& s) {
  JsonDocument doc;
  TEST_ASSERT_TRUE_MESSAGE(!deserializeJson(doc, s), s.c_str());
  return doc;
}

bool contains(const std::string& haystack, const char* needle) {
  return haystack.find(needle) != std::string::npos;
}

}  // namespace

void test_status_shape() {
  Api a;
  HttpResponse r = a.call("GET", "/api/status");
  TEST_ASSERT_EQUAL_INT(200, r.status);
  JsonDocument d = parse(r.body);
  TEST_ASSERT_TRUE(d["state"].is<const char*>());
  TEST_ASSERT_EQUAL_STRING("running", d["state"].as<const char*>());
  TEST_ASSERT_TRUE(d["mouseConnected"].is<bool>());
  TEST_ASSERT_TRUE(d["wifiConnected"].is<bool>());
  TEST_ASSERT_TRUE(d["msToNextMove"].is<int64_t>());
  TEST_ASSERT_TRUE(d["moveCount"].is<int64_t>());
  TEST_ASSERT_TRUE(contains(r.body, "\"lastMoveAgoMs\":null"));
  TEST_ASSERT_TRUE(contains(r.body, "\"lastMoveAt\":null"));
  TEST_ASSERT_TRUE(d["config"]["minIntervalSec"].is<int64_t>());
  TEST_ASSERT_TRUE(d["config"]["maxIntervalSec"].is<int64_t>());
  TEST_ASSERT_TRUE(d["config"]["amplitudePx"].is<int64_t>());
  TEST_ASSERT_TRUE(d["config"]["clickProbability"].is<double>());
  TEST_ASSERT_TRUE(d["config"]["scrollProbability"].is<double>());
}

void test_status_after_move_with_unsynced_clock() {
  Api a;
  a.clock.unix = 0;
  TEST_ASSERT_TRUE(a.c.moveNow().ok);
  TEST_ASSERT_TRUE(a.c.tick().has_value());
  a.clock.now = 1500;
  HttpResponse r = a.call("GET", "/api/status");
  JsonDocument d = parse(r.body);
  TEST_ASSERT_EQUAL_INT(1, d["moveCount"].as<int>());
  TEST_ASSERT_EQUAL_INT(1500, d["lastMoveAgoMs"].as<int>());
  TEST_ASSERT_TRUE(contains(r.body, "\"lastMoveAt\":null"));
}

void test_status_stopped_has_null_next_move() {
  Api a;
  a.call("POST", "/api/stop");
  HttpResponse r = a.call("GET", "/api/status");
  TEST_ASSERT_TRUE(contains(r.body, "\"msToNextMove\":null"));
  TEST_ASSERT_TRUE(contains(r.body, "\"state\":\"stopped\""));
}

void test_start_stop_pause_roundtrip() {
  Api a;
  const struct {
    const char* path;
    const char* state;
  } steps[] = {{"/api/stop", "stopped"}, {"/api/start", "running"}, {"/api/pause", "paused"}};
  for (const auto& s : steps) {
    HttpResponse r = a.call("POST", s.path);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    TEST_ASSERT_EQUAL_STRING(s.state, parse(r.body)["state"].as<const char*>());
  }
}

void test_move_409_when_disconnected() {
  Api a;
  a.clock.mouse = false;
  HttpResponse r = a.call("POST", "/api/move");
  TEST_ASSERT_EQUAL_INT(409, r.status);
  TEST_ASSERT_EQUAL_STRING("mouse_not_connected", parse(r.body)["error"].as<const char*>());
}

void test_move_202_when_connected() {
  Api a;
  HttpResponse r = a.call("POST", "/api/move");
  TEST_ASSERT_EQUAL_INT(202, r.status);
  TEST_ASSERT_TRUE(parse(r.body)["queued"].as<bool>());
  TEST_ASSERT_TRUE(a.c.tick().has_value());
}

void test_get_config() {
  Api a;
  HttpResponse r = a.call("GET", "/api/config");
  TEST_ASSERT_EQUAL_INT(200, r.status);
  JsonDocument d = parse(r.body);
  TEST_ASSERT_EQUAL_INT(30, d["minIntervalSec"].as<int>());
  TEST_ASSERT_EQUAL_INT(120, d["maxIntervalSec"].as<int>());
  TEST_ASSERT_EQUAL_INT(150, d["amplitudePx"].as<int>());
  TEST_ASSERT_TRUE(contains(r.body, "\"scrollProbability\":0.1"));
  TEST_ASSERT_EQUAL_UINT32(5, d.as<JsonObject>().size());
}

void test_put_config_partial_ok() {
  Api a;
  HttpResponse r = a.call("PUT", "/api/config", "{\"amplitudePx\":200}");
  TEST_ASSERT_EQUAL_INT(200, r.status);
  TEST_ASSERT_EQUAL_INT(200, parse(r.body)["amplitudePx"].as<int>());
  TEST_ASSERT_EQUAL_INT(1, a.store.saves);
  TEST_ASSERT_EQUAL_UINT16(200, a.store.stored.amplitudePx);
  TEST_ASSERT_EQUAL_UINT32(30, a.store.stored.minIntervalSec);
}

void test_put_config_inconsistent_rejected() {  // RF2
  Api a;
  HttpResponse r = a.call("PUT", "/api/config", "{\"maxIntervalSec\":10}");
  TEST_ASSERT_EQUAL_INT(400, r.status);
  JsonDocument d = parse(r.body);
  TEST_ASSERT_EQUAL_STRING("invalid_config", d["error"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("maxIntervalSec", d["field"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(0, a.store.saves);
  TEST_ASSERT_EQUAL_UINT32(120, a.c.status().config.maxIntervalSec);
}

void test_put_config_bad_input() {  // RF3
  const std::string big = std::string("{\"amplitudePx\":200,\"x\":\"") + std::string(1000, 'a') + "\"}";
  const struct {
    std::string body;
    int status;
    const char* error;
  } cases[] = {
      {"", 400, "bad_json"},
      {"{", 400, "bad_json"},
      {"[]", 400, "bad_json"},
      {"null", 400, "bad_json"},
      {"{\"minIntervalSec\":\"abc\"}", 400, "invalid_config"},
      {"{\"minIntervalSec\":30.5}", 400, "invalid_config"},
      {"{\"amplitudePx\":null}", 400, "invalid_config"},
      {"{\"amplitudePx\":70000}", 400, "invalid_config"},
      {"{\"amplitudePx\":true}", 400, "invalid_config"},
      {"{\"bogus\":1}", 400, "invalid_config"},
      {big, 413, "payload_too_large"},
  };
  for (const auto& tc : cases) {
    Api a;
    HttpResponse r = a.call("PUT", "/api/config", tc.body);
    TEST_ASSERT_EQUAL_INT_MESSAGE(tc.status, r.status, tc.body.substr(0, 40).c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE(tc.error, parse(r.body)["error"].as<const char*>(),
                                     tc.body.substr(0, 40).c_str());
    TEST_ASSERT_EQUAL_INT(0, a.store.saves);
    TEST_ASSERT_EQUAL_UINT16(150, a.c.status().config.amplitudePx);
  }
}

void test_put_config_empty_object_is_noop() {
  Api a;
  HttpResponse r = a.call("PUT", "/api/config", "{}");
  TEST_ASSERT_EQUAL_INT(200, r.status);
  TEST_ASSERT_EQUAL_INT(150, parse(r.body)["amplitudePx"].as<int>());
}

void test_routing_errors() {
  Api a;
  HttpResponse nf = a.call("GET", "/api/nope");
  TEST_ASSERT_EQUAL_INT(404, nf.status);
  TEST_ASSERT_EQUAL_STRING("not_found", parse(nf.body)["error"].as<const char*>());
  HttpResponse bad = a.call("GET", "/api/start");
  TEST_ASSERT_EQUAL_INT(405, bad.status);
  TEST_ASSERT_EQUAL_STRING("method_not_allowed", parse(bad.body)["error"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(405, a.call("POST", "/api/status").status);
  TEST_ASSERT_EQUAL_INT(405, a.call("DELETE", "/api/config").status);
}

void test_authorized() {
  TEST_ASSERT_TRUE(authorized("Bearer abc", "abc"));
  TEST_ASSERT_FALSE(authorized("Bearer abd", "abc"));
  TEST_ASSERT_FALSE(authorized("Bearer abcd", "abc"));
  TEST_ASSERT_FALSE(authorized("Bearer ab", "abc"));
  TEST_ASSERT_FALSE(authorized("", "abc"));
  TEST_ASSERT_FALSE(authorized("Basic abc", "abc"));
  TEST_ASSERT_FALSE(authorized("Bearer", "abc"));
  TEST_ASSERT_FALSE(authorized("bearer abc", "abc"));
  TEST_ASSERT_FALSE(authorized("Bearer ", ""));
  TEST_ASSERT_FALSE(authorized("", ""));
}

void test_copy_body_chunk_never_writes_past_total() {  // final review, Critical
  char buf[16];
  memset(buf, 0xAA, sizeof(buf));
  const uint8_t big[10] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};
  // Declared total 4, but the library hands over a 10-byte chunk.
  TEST_ASSERT_EQUAL_UINT32(4, copyBodyChunk(buf, 4, big, 10, 0));
  TEST_ASSERT_EQUAL_UINT8_ARRAY("0123", buf, 4);
  for (int i = 4; i < 16; i++) TEST_ASSERT_EQUAL_HEX8(0xAA, static_cast<uint8_t>(buf[i]));
  // Chunk starting at or past the end writes nothing.
  TEST_ASSERT_EQUAL_UINT32(0, copyBodyChunk(buf, 4, big, 10, 4));
  TEST_ASSERT_EQUAL_UINT32(0, copyBodyChunk(buf, 4, big, 10, 99));
  for (int i = 4; i < 16; i++) TEST_ASSERT_EQUAL_HEX8(0xAA, static_cast<uint8_t>(buf[i]));
}

void test_copy_body_chunk_assembles_chunks() {
  char buf[8] = {0};
  const uint8_t a[] = {'a', 'b', 'c'}, b[] = {'d', 'e', 'f', 'g', 'h', 'i'};
  TEST_ASSERT_EQUAL_UINT32(3, copyBodyChunk(buf, 6, a, 3, 0));
  TEST_ASSERT_EQUAL_UINT32(3, copyBodyChunk(buf, 6, b, 6, 3));  // clamped from 6 to 3
  TEST_ASSERT_EQUAL_STRING("abcdef", buf);
}

void test_reject_body() {  // final review, Important (curl -d sends form-encoded)
  TEST_ASSERT_FALSE(rejectBody(0, false).has_value());
  TEST_ASSERT_FALSE(rejectBody(10, true).has_value());
  TEST_ASSERT_FALSE(rejectBody(kMaxBodyBytes, true).has_value());
  auto big = rejectBody(kMaxBodyBytes + 1, true);
  TEST_ASSERT_TRUE(big.has_value());
  TEST_ASSERT_EQUAL_INT(413, big->status);
  TEST_ASSERT_EQUAL_STRING("payload_too_large", parse(big->body)["error"].as<const char*>());
  auto form = rejectBody(10, false);
  TEST_ASSERT_TRUE(form.has_value());
  TEST_ASSERT_EQUAL_INT(415, form->status);
  TEST_ASSERT_EQUAL_STRING("unsupported_media_type", parse(form->body)["error"].as<const char*>());
}

void test_start_with_minutes_sets_a_time_limit() {
  Api a;
  a.call("POST", "/api/stop");
  HttpResponse r = a.call("POST", "/api/start", "{\"minutes\":30}");
  TEST_ASSERT_EQUAL_INT(200, r.status);
  JsonDocument d = parse(r.body);
  TEST_ASSERT_EQUAL_STRING("running", d["state"].as<const char*>());
  TEST_ASSERT_EQUAL_UINT32(1800000, d["stopsInMs"].as<uint32_t>());
  // and it shows in status, as null once there is no limit
  TEST_ASSERT_EQUAL_UINT32(1800000, parse(a.call("GET", "/api/status").body)["stopsInMs"].as<uint32_t>());
  a.call("POST", "/api/start");
  TEST_ASSERT_TRUE(contains(a.call("GET", "/api/status").body, "\"stopsInMs\":null"));
}

void test_start_rejects_bad_minutes_and_bodies() {
  Api a;
  a.call("POST", "/api/stop");
  for (const char* body : {"{\"minutes\":0}", "{\"minutes\":1441}", "{\"minutes\":-5}"}) {
    HttpResponse r = a.call("POST", "/api/start", body);
    TEST_ASSERT_EQUAL_INT(400, r.status);
    TEST_ASSERT_TRUE(contains(r.body, "invalid_minutes"));
  }
  HttpResponse notInt = a.call("POST", "/api/start", "{\"minutes\":\"30\"}");
  TEST_ASSERT_EQUAL_INT(400, notInt.status);
  HttpResponse unknown = a.call("POST", "/api/start", "{\"hours\":1}");
  TEST_ASSERT_EQUAL_INT(400, unknown.status);
  HttpResponse notJson = a.call("POST", "/api/start", "nope");
  TEST_ASSERT_EQUAL_INT(400, notJson.status);
  TEST_ASSERT_TRUE(parse(a.call("GET", "/api/status").body)["state"].as<std::string>() == "stopped");  // nothing started
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_copy_body_chunk_never_writes_past_total);
  RUN_TEST(test_copy_body_chunk_assembles_chunks);
  RUN_TEST(test_reject_body);
  RUN_TEST(test_status_shape);
  RUN_TEST(test_status_after_move_with_unsynced_clock);
  RUN_TEST(test_status_stopped_has_null_next_move);
  RUN_TEST(test_start_stop_pause_roundtrip);
  RUN_TEST(test_move_409_when_disconnected);
  RUN_TEST(test_move_202_when_connected);
  RUN_TEST(test_get_config);
  RUN_TEST(test_put_config_partial_ok);
  RUN_TEST(test_put_config_inconsistent_rejected);
  RUN_TEST(test_put_config_bad_input);
  RUN_TEST(test_put_config_empty_object_is_noop);
  RUN_TEST(test_routing_errors);
  RUN_TEST(test_authorized);
  RUN_TEST(test_start_with_minutes_sets_a_time_limit);
  RUN_TEST(test_start_rejects_bad_minutes_and_bodies);
  return UNITY_END();
}
