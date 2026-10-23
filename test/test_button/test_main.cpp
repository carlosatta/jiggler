#include <unity.h>

#include "button_gesture.h"

using namespace jig;

void setUp() {}
void tearDown() {}

void test_short_press_fires_on_release() {
  ButtonGesture b;
  TEST_ASSERT_TRUE(b.update(true, 1000) == Gesture::None);
  TEST_ASSERT_TRUE(b.update(true, 1100) == Gesture::None);
  TEST_ASSERT_TRUE(b.update(false, 1200) == Gesture::Short);
  TEST_ASSERT_TRUE(b.update(false, 1300) == Gesture::None);
}

void test_contact_bounce_is_ignored() {
  ButtonGesture b;
  b.update(true, 1000);
  TEST_ASSERT_TRUE(b.update(false, 1010) == Gesture::None);  // 10 ms: bounce
}

void test_medium_press_is_neither_short_nor_long() {
  ButtonGesture b;
  b.update(true, 0);
  b.update(true, 1500);
  TEST_ASSERT_TRUE(b.update(false, 1600) == Gesture::None);  // 1.6 s: ambiguous, do nothing
}

void test_long_press_fires_once_while_held() {
  ButtonGesture b;
  TEST_ASSERT_TRUE(b.update(true, 0) == Gesture::None);
  TEST_ASSERT_TRUE(b.update(true, 2999) == Gesture::None);
  TEST_ASSERT_TRUE(b.update(true, 3000) == Gesture::Long);
  TEST_ASSERT_TRUE(b.update(true, 4000) == Gesture::None);
  TEST_ASSERT_TRUE(b.update(false, 4100) == Gesture::None);  // releasing after a long press is not a short press
}

void test_rearms_after_release() {
  ButtonGesture b;
  b.update(true, 0);
  TEST_ASSERT_TRUE(b.update(true, 3000) == Gesture::Long);
  b.update(false, 3100);
  b.update(true, 5000);
  TEST_ASSERT_TRUE(b.update(false, 5200) == Gesture::Short);
  b.update(true, 6000);
  TEST_ASSERT_TRUE(b.update(true, 9000) == Gesture::Long);
}

void test_wraparound() {
  ButtonGesture b;
  const uint32_t t0 = 0xFFFFFF00u;
  b.update(true, t0);
  TEST_ASSERT_TRUE(b.update(false, t0 + 200) == Gesture::Short);
  b.update(true, t0);
  TEST_ASSERT_TRUE(b.update(true, t0 + 3000) == Gesture::Long);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_short_press_fires_on_release);
  RUN_TEST(test_contact_bounce_is_ignored);
  RUN_TEST(test_medium_press_is_neither_short_nor_long);
  RUN_TEST(test_long_press_fires_once_while_held);
  RUN_TEST(test_rearms_after_release);
  RUN_TEST(test_wraparound);
  return UNITY_END();
}
