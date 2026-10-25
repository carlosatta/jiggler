#include <unity.h>

#include <ArduinoJson.h>

#include "api_ex.h"
#include "button_settings.h"
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
  ButtonStore store{blobs};
  ApiContext ctx{controller};
  Env() {
    controller.begin();
    store.begin();
    ctx.button = &store;
  }
};

JsonDocument parse(const std::string& s) {
  JsonDocument doc;
  TEST_ASSERT_TRUE_MESSAGE(!deserializeJson(doc, s), s.c_str());
  return doc;
}
}  // namespace

void test_the_default_is_the_boot_button() {
  Env e;
  TEST_ASSERT_EQUAL_UINT8(0, e.store.gpio());
}

void test_only_safe_pins_are_accepted() {
  for (int ok : {0, 4, 5, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33}) TEST_ASSERT_TRUE_MESSAGE(validButtonGpio(ok), std::to_string(ok).c_str());
  for (int bad : {-1, 1, 2, 3, 6, 7, 8, 9, 10, 11, 12, 20, 24, 28, 34, 35, 36, 39, 40, 255, 256, 1000})
    TEST_ASSERT_FALSE_MESSAGE(validButtonGpio(bad), std::to_string(bad).c_str());
}

void test_the_choice_persists() {
  Env e;
  TEST_ASSERT_TRUE(e.store.setGpio(14).ok);
  TEST_ASSERT_EQUAL_UINT8(14, e.store.gpio());
  ButtonStore again(e.blobs);
  again.begin();
  TEST_ASSERT_EQUAL_UINT8(14, again.gpio());
}

void test_a_bad_pin_or_a_full_storage_changes_nothing() {
  Env e;
  e.store.setGpio(14);
  CmdResult r = e.store.setGpio(2);
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL_STRING("invalid_gpio", r.error.c_str());
  e.blobs.failPuts = true;
  TEST_ASSERT_EQUAL_STRING("storage_error", e.store.setGpio(15).error.c_str());
  TEST_ASSERT_EQUAL_UINT8(14, e.store.gpio());
}

void test_a_damaged_stored_value_falls_back_to_the_default() {
  FlakyBlobStore blobs;
  blobs.put("button", "{\"gpio\":2}");
  ButtonStore store(blobs);
  store.begin();
  TEST_ASSERT_EQUAL_UINT8(0, store.gpio());
}

void test_button_routes() {
  Env e;
  HttpResponse g = handleApiEx(e.ctx, "GET", "/api/button", "");
  TEST_ASSERT_EQUAL_INT(200, g.status);
  JsonDocument d = parse(g.body);
  TEST_ASSERT_EQUAL_UINT32(0, d["gpio"].as<uint32_t>());
  TEST_ASSERT_EQUAL_UINT32(0, d["default"].as<uint32_t>());
  TEST_ASSERT_TRUE(d["gpios"].size() > 10);
  HttpResponse put = handleApiEx(e.ctx, "PUT", "/api/button", "{\"gpio\":27}");
  TEST_ASSERT_EQUAL_INT(200, put.status);
  TEST_ASSERT_EQUAL_UINT32(27, parse(put.body)["gpio"].as<uint32_t>());
  TEST_ASSERT_EQUAL_INT(200, handleApiEx(e.ctx, "PUT", "/api/button", g.body.empty() ? "" : "{\"gpio\":0,\"default\":0,\"gpios\":[0]}").status);  // a GET body can be sent back
  TEST_ASSERT_EQUAL_INT(400, handleApiEx(e.ctx, "PUT", "/api/button", "{\"gpio\":2}").status);
  TEST_ASSERT_EQUAL_INT(400, handleApiEx(e.ctx, "PUT", "/api/button", "{\"gpio\":\"x\"}").status);
  TEST_ASSERT_EQUAL_INT(400, handleApiEx(e.ctx, "PUT", "/api/button", "{}").status);
  TEST_ASSERT_EQUAL_INT(400, handleApiEx(e.ctx, "PUT", "/api/button", "{\"nope\":1}").status);
  TEST_ASSERT_EQUAL_INT(405, handleApiEx(e.ctx, "POST", "/api/button", "").status);
  TEST_ASSERT_EQUAL_INT(413, handleApiEx(e.ctx, "PUT", "/api/button", std::string(kMaxSmallBodyBytes + 1, ' ')).status);
  e.blobs.failPuts = true;
  TEST_ASSERT_EQUAL_INT(507, handleApiEx(e.ctx, "PUT", "/api/button", "{\"gpio\":4}").status);
  ApiContext none{e.controller};
  TEST_ASSERT_EQUAL_INT(404, handleApiEx(none, "GET", "/api/button", "").status);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_the_default_is_the_boot_button);
  RUN_TEST(test_only_safe_pins_are_accepted);
  RUN_TEST(test_the_choice_persists);
  RUN_TEST(test_a_bad_pin_or_a_full_storage_changes_nothing);
  RUN_TEST(test_a_damaged_stored_value_falls_back_to_the_default);
  RUN_TEST(test_button_routes);
  return UNITY_END();
}
