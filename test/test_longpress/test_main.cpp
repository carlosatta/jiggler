#include <unity.h>

#include "long_press.h"

using namespace jig;

void setUp() {}
void tearDown() {}

void test_longpress_fires_once_after_hold() {
  LongPress p(3000);
  TEST_ASSERT_FALSE(p.update(true, 0));
  TEST_ASSERT_FALSE(p.update(true, 2999));
  TEST_ASSERT_TRUE(p.update(true, 3000));
  TEST_ASSERT_FALSE(p.update(true, 3500));
  TEST_ASSERT_FALSE(p.update(true, 9000));
}

void test_longpress_released_early_never_fires() {
  LongPress p(3000);
  TEST_ASSERT_FALSE(p.update(true, 0));
  TEST_ASSERT_FALSE(p.update(true, 1000));
  TEST_ASSERT_FALSE(p.update(false, 1100));
  TEST_ASSERT_FALSE(p.update(true, 1200));      // a new press starts counting from 1200
  TEST_ASSERT_FALSE(p.update(true, 1200 + 2999));
  TEST_ASSERT_TRUE(p.update(true, 1200 + 3000));
}

void test_longpress_rearms_after_release() {
  LongPress p(3000);
  p.update(true, 0);
  TEST_ASSERT_TRUE(p.update(true, 3000));
  TEST_ASSERT_FALSE(p.update(false, 3100));
  TEST_ASSERT_FALSE(p.update(true, 4000));
  TEST_ASSERT_TRUE(p.update(true, 7000));
}

void test_longpress_wraparound() {
  LongPress p(3000);
  const uint32_t t0 = 0xFFFFFF00u;
  TEST_ASSERT_FALSE(p.update(true, t0));
  TEST_ASSERT_FALSE(p.update(true, t0 + 2999));
  TEST_ASSERT_TRUE(p.update(true, t0 + 3000));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_longpress_fires_once_after_hold);
  RUN_TEST(test_longpress_released_early_never_fires);
  RUN_TEST(test_longpress_rearms_after_release);
  RUN_TEST(test_longpress_wraparound);
  return UNITY_END();
}
