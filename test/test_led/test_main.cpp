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

void test_pairing_blinks_fast() {
  LedContext c{false, true, false, 0, 0, false};
  TEST_ASSERT_TRUE(ledLevel(c));
  c.nowMs = 124;
  TEST_ASSERT_TRUE(ledLevel(c));
  c.nowMs = 125;
  TEST_ASSERT_FALSE(ledLevel(c));
  c.nowMs = 250;
  TEST_ASSERT_TRUE(ledLevel(c));
  c.connected = true;  // still pairing: the fast blink wins so the owner knows the window is open
  c.nowMs = 130;
  TEST_ASSERT_FALSE(ledLevel(c));
}

void test_held_flashes_once_every_three_seconds() {
  LedContext c{false, false, true, 0, 0, false};
  TEST_ASSERT_TRUE(ledLevel(c));
  c.nowMs = 99;
  TEST_ASSERT_TRUE(ledLevel(c));
  c.nowMs = 100;
  TEST_ASSERT_FALSE(ledLevel(c));
  c.nowMs = 2999;
  TEST_ASSERT_FALSE(ledLevel(c));
  c.nowMs = 3000;
  TEST_ASSERT_TRUE(ledLevel(c));
}

void test_pairing_has_priority_over_held() {
  LedContext c{false, true, true, 130, 0, false};
  TEST_ASSERT_FALSE(ledLevel(c));  // fast-blink phase "off", not the held pattern
}

void test_context_falls_back_to_the_connection_pattern() {
  LedContext c{true, false, false, 5000, 5000, true};
  TEST_ASSERT_FALSE(ledLevel(c));  // flash right after a movement
  c.nowMs = 5080;
  TEST_ASSERT_TRUE(ledLevel(c));
  LedContext off{false, false, false, 500, 0, false};
  TEST_ASSERT_FALSE(ledLevel(off));  // disconnected blink, off half
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_led_blinks_when_disconnected);
  RUN_TEST(test_led_solid_when_connected);
  RUN_TEST(test_led_flashes_off_after_move);
  RUN_TEST(test_led_flash_survives_clock_wraparound);
  RUN_TEST(test_pairing_blinks_fast);
  RUN_TEST(test_held_flashes_once_every_three_seconds);
  RUN_TEST(test_pairing_has_priority_over_held);
  RUN_TEST(test_context_falls_back_to_the_connection_pattern);
  return UNITY_END();
}
