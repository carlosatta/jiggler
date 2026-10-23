#include <unity.h>

#include "config.h"
#include "fakes.h"
#include "platform.h"

using namespace jig;

void setUp() {}
void tearDown() {}

void test_defaults_match_spec() {
  Config c;
  TEST_ASSERT_EQUAL_UINT32(30, c.minIntervalSec);
  TEST_ASSERT_EQUAL_UINT32(120, c.maxIntervalSec);
  TEST_ASSERT_EQUAL_UINT16(150, c.amplitudePx);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, c.clickProbability);
  TEST_ASSERT_EQUAL_FLOAT(0.10f, c.scrollProbability);
}

void test_partial_patch_merges() {
  Config base, out;
  ConfigPatch p;
  p.amplitudePx = 200;
  auto err = applyPatch(base, p, out);
  TEST_ASSERT_FALSE(err.has_value());
  TEST_ASSERT_EQUAL_UINT16(200, out.amplitudePx);
  TEST_ASSERT_EQUAL_UINT32(base.minIntervalSec, out.minIntervalSec);
  TEST_ASSERT_EQUAL_UINT32(base.maxIntervalSec, out.maxIntervalSec);
  TEST_ASSERT_EQUAL_FLOAT(base.scrollProbability, out.scrollProbability);
}

static bool accepts(const ConfigPatch& p) {
  Config base, out;
  return !applyPatch(base, p, out).has_value();
}

void test_bounds() {
  ConfigPatch p;
  p = {}; p.minIntervalSec = 4;    TEST_ASSERT_FALSE(accepts(p));
  p = {}; p.minIntervalSec = 5;    TEST_ASSERT_TRUE(accepts(p));
  p = {}; p.maxIntervalSec = 3600; TEST_ASSERT_TRUE(accepts(p));
  p = {}; p.maxIntervalSec = 3601; TEST_ASSERT_FALSE(accepts(p));
  p = {}; p.amplitudePx = 4;       TEST_ASSERT_FALSE(accepts(p));
  p = {}; p.amplitudePx = 5;       TEST_ASSERT_TRUE(accepts(p));
  p = {}; p.amplitudePx = 600;     TEST_ASSERT_TRUE(accepts(p));
  p = {}; p.amplitudePx = 601;     TEST_ASSERT_FALSE(accepts(p));
  p = {}; p.clickProbability = -0.1;  TEST_ASSERT_FALSE(accepts(p));
  p = {}; p.clickProbability = 1.0;   TEST_ASSERT_TRUE(accepts(p));
  p = {}; p.scrollProbability = 1.1;  TEST_ASSERT_FALSE(accepts(p));
}

void test_max_below_stored_min_rejected_out_untouched() {
  Config base;
  base.minIntervalSec = 30;
  Config out;
  out.amplitudePx = 321;
  ConfigPatch p;
  p.maxIntervalSec = 10;
  auto err = applyPatch(base, p, out);
  TEST_ASSERT_TRUE(err.has_value());
  TEST_ASSERT_EQUAL_STRING("maxIntervalSec", err->field.c_str());
  TEST_ASSERT_EQUAL_UINT16(321, out.amplitudePx);
  TEST_ASSERT_EQUAL_UINT32(Config{}.maxIntervalSec, out.maxIntervalSec);
}

void test_amplitude_70000_rejected() {
  Config base, out;
  ConfigPatch p;
  p.amplitudePx = 70000;
  auto err = applyPatch(base, p, out);
  TEST_ASSERT_TRUE(err.has_value());
  TEST_ASSERT_EQUAL_STRING("amplitudePx", err->field.c_str());
}

void test_rand01_in_unit_interval() {
  XorShiftRng rng(1);
  for (int i = 0; i < 10000; i++) {
    double v = rand01(rng);
    TEST_ASSERT_TRUE(v >= 0.0 && v < 1.0);
  }
}

void test_rand_range_inclusive_bounds() {
  XorShiftRng rng(7);
  bool sawLo = false, sawHi = false;
  for (int i = 0; i < 10000; i++) {
    int32_t v = randRange(rng, -2, 2);
    TEST_ASSERT_TRUE(v >= -2 && v <= 2);
    sawLo |= (v == -2);
    sawHi |= (v == 2);
  }
  TEST_ASSERT_TRUE(sawLo);
  TEST_ASSERT_TRUE(sawHi);
  TEST_ASSERT_EQUAL_INT32(5, randRange(rng, 5, 5));
}

void test_fakes_behave() {
  FakeClock clock;
  clock.now = 42;
  TEST_ASSERT_EQUAL_UINT32(42, clock.nowMs());
  FakeMouse mouse;
  mouse.move(1, -2);
  mouse.click();
  mouse.scroll(3);
  TEST_ASSERT_EQUAL_UINT32(3, mouse.calls.size());
  MemStore store;
  store.save(Config{});
  TEST_ASSERT_EQUAL_INT(1, store.saves);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_match_spec);
  RUN_TEST(test_partial_patch_merges);
  RUN_TEST(test_bounds);
  RUN_TEST(test_max_below_stored_min_rejected_out_untouched);
  RUN_TEST(test_amplitude_70000_rejected);
  RUN_TEST(test_rand01_in_unit_interval);
  RUN_TEST(test_rand_range_inclusive_bounds);
  RUN_TEST(test_fakes_behave);
  return UNITY_END();
}
