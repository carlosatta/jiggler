#include <unity.h>

#include "led_logic.h"

using namespace jig;

void setUp() {}
void tearDown() {}

void test_led_blinks_when_disconnected() {
  TEST_ASSERT_TRUE(ledLevel(false, 0, 0, false));
  TEST_ASSERT_TRUE(ledLevel(false, 499, 0, false));
  TEST_ASSERT_FALSE(ledLevel(false, 500, 0, false));
  TEST_ASSERT_FALSE(ledLevel(false, 999, 0, false));
  TEST_ASSERT_TRUE(ledLevel(false, 1000, 0, false));
  TEST_ASSERT_TRUE(ledLevel(false, 123456000u, 0, false));
  TEST_ASSERT_FALSE(ledLevel(false, 123456500u, 0, false));
}

void test_led_solid_when_connected() {
  for (uint32_t now = 0; now <= 2000; now += 10) TEST_ASSERT_TRUE(ledLevel(true, now, 0, false));
}

void test_led_flashes_off_after_move() {
  TEST_ASSERT_FALSE(ledLevel(true, 5000, 5000, true));
  TEST_ASSERT_FALSE(ledLevel(true, 5079, 5000, true));
  TEST_ASSERT_TRUE(ledLevel(true, 5080, 5000, true));
  TEST_ASSERT_TRUE(ledLevel(true, 4999, 5000, true));
}

void test_led_flash_survives_clock_wraparound() {
  TEST_ASSERT_FALSE(ledLevel(true, 0x20u, 0xFFFFFFF0u, true));
  TEST_ASSERT_TRUE(ledLevel(true, 0x70u, 0xFFFFFFF0u, true));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_led_blinks_when_disconnected);
  RUN_TEST(test_led_solid_when_connected);
  RUN_TEST(test_led_flashes_off_after_move);
  RUN_TEST(test_led_flash_survives_clock_wraparound);
  return UNITY_END();
}
