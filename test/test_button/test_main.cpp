#include <unity.h>

#include "button_gesture.h"

using namespace jig;

void setUp() {}
void tearDown() {}

void test_a_tap_fires_on_release() {
  ButtonGesture b;
  TEST_ASSERT_TRUE(b.update(true, 1000) == Gesture::None);
  TEST_ASSERT_TRUE(b.update(true, 1100) == Gesture::None);
  TEST_ASSERT_TRUE(b.update(false, 1200) == Gesture::Tap);
  TEST_ASSERT_TRUE(b.update(false, 1300) == Gesture::None);
}

void test_contact_bounce_is_ignored() {
  ButtonGesture b;
  b.update(true, 1000);
  TEST_ASSERT_TRUE(b.update(false, 1010) == Gesture::None);  // 10 ms: bounce
}

void test_a_tap_must_be_quick() {
  ButtonGesture b;
  b.update(true, 0);
  TEST_ASSERT_TRUE(b.update(false, 800) == Gesture::Tap);
  b.update(true, 0);
  TEST_ASSERT_TRUE(b.update(false, 801) == Gesture::None);
}

void test_between_a_tap_and_pairing_nothing_happens() {
  ButtonGesture b;
  b.update(true, 0);
  b.update(true, 2000);
  TEST_ASSERT_TRUE(b.update(false, 4999) == Gesture::None);
}

void test_releasing_between_five_and_ten_seconds_opens_pairing() {
  ButtonGesture b;
  b.update(true, 0);
  TEST_ASSERT_TRUE(b.update(true, 5000) == Gesture::None);   // nothing yet: it fires on release
  TEST_ASSERT_TRUE(b.update(false, 5000) == Gesture::Pairing);
  b.update(true, 0);
  TEST_ASSERT_TRUE(b.update(false, 10000) == Gesture::Pairing);
  b.update(true, 0);
  TEST_ASSERT_TRUE(b.update(false, 10001) == Gesture::None);   // held on too long: cancelled
  b.update(true, 0);
  b.update(true, 20000);
  TEST_ASSERT_TRUE(b.update(false, 29999) == Gesture::None);
}

void test_thirty_seconds_fires_once_while_held() {
  ButtonGesture b;
  b.update(true, 0);
  TEST_ASSERT_TRUE(b.update(true, 29999) == Gesture::None);
  TEST_ASSERT_TRUE(b.update(true, 30000) == Gesture::Network);
  TEST_ASSERT_TRUE(b.update(true, 31000) == Gesture::None);
  TEST_ASSERT_TRUE(b.update(false, 32000) == Gesture::None);  // letting go afterwards does nothing more
}

void test_the_phase_tells_what_releasing_would_do() {
  ButtonGesture b;
  TEST_ASSERT_TRUE(b.phase(0) == HoldPhase::None);
  b.update(true, 1000);
  TEST_ASSERT_TRUE(b.phase(1500) == HoldPhase::None);
  TEST_ASSERT_TRUE(b.phase(5999) == HoldPhase::None);
  TEST_ASSERT_TRUE(b.phase(6000) == HoldPhase::Pairing);
  TEST_ASSERT_TRUE(b.phase(11000) == HoldPhase::Pairing);
  TEST_ASSERT_TRUE(b.phase(11001) == HoldPhase::Cancel);
  TEST_ASSERT_TRUE(b.phase(30999) == HoldPhase::Cancel);
  TEST_ASSERT_TRUE(b.phase(31000) == HoldPhase::Network);
  b.update(false, 31500);
  TEST_ASSERT_TRUE(b.phase(31600) == HoldPhase::None);
}

void test_rearms_after_release() {
  ButtonGesture b;
  b.update(true, 0);
  TEST_ASSERT_TRUE(b.update(true, 30000) == Gesture::Network);
  b.update(false, 30100);
  b.update(true, 40000);
  TEST_ASSERT_TRUE(b.update(false, 40200) == Gesture::Tap);
  b.update(true, 50000);
  TEST_ASSERT_TRUE(b.update(false, 56000) == Gesture::Pairing);
}

void test_wraparound() {
  ButtonGesture b;
  const uint32_t t0 = 0xFFFFFF00u;
  b.update(true, t0);
  TEST_ASSERT_TRUE(b.update(false, t0 + 200) == Gesture::Tap);
  b.update(true, t0);
  TEST_ASSERT_TRUE(b.update(true, t0 + 30000) == Gesture::Network);
  b.update(false, t0 + 30100);
  b.update(true, t0);
  TEST_ASSERT_TRUE(b.update(false, t0 + 6000) == Gesture::Pairing);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_tap_fires_on_release);
  RUN_TEST(test_contact_bounce_is_ignored);
  RUN_TEST(test_a_tap_must_be_quick);
  RUN_TEST(test_between_a_tap_and_pairing_nothing_happens);
  RUN_TEST(test_releasing_between_five_and_ten_seconds_opens_pairing);
  RUN_TEST(test_thirty_seconds_fires_once_while_held);
  RUN_TEST(test_the_phase_tells_what_releasing_would_do);
  RUN_TEST(test_rearms_after_release);
  RUN_TEST(test_wraparound);
  return UNITY_END();
}
