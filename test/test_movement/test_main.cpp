#include <unity.h>

#include <cmath>
#include <functional>
#include <vector>

#include "config.h"
#include "fakes.h"
#include "movement.h"

using namespace jig;

void setUp() {}
void tearDown() {}

namespace {

struct Pos {
  double x, y;
};

Config configWith(uint16_t amplitude, float click, float scroll) {
  Config c;
  c.amplitudePx = amplitude;
  c.clickProbability = click;
  c.scrollProbability = scroll;
  return c;
}

Plan planFor(uint16_t amplitude, uint32_t seed, float click = 0.0f, float scroll = 0.1f) {
  XorShiftRng rng(seed * 7919u + amplitude);
  return planActivity(configWith(amplitude, click, scroll), rng);
}

void forEachCase(const std::vector<uint16_t>& amplitudes, int seeds,
                 const std::function<void(const Plan&, uint16_t, int)>& body) {
  for (uint16_t amp : amplitudes)
    for (int seed = 1; seed <= seeds; seed++) body(planFor(amp, seed), amp, seed);
}

// Cumulative cursor position after each Move step.
std::vector<Pos> positions(const Plan& plan) {
  std::vector<Pos> out;
  double x = 0, y = 0;
  for (const Step& s : plan.steps) {
    if (s.kind != Step::Kind::Move) continue;
    x += s.dx;
    y += s.dy;
    out.push_back({x, y});
  }
  return out;
}

// Index (in plan.steps) of the dwell step: the only Move whose waitMs != 20.
size_t dwellIndex(const Plan& plan) {
  for (size_t i = 0; i < plan.steps.size(); i++)
    if (plan.steps[i].kind == Step::Kind::Move && plan.steps[i].waitMs != 20) return i;
  return plan.steps.size();
}

double stepLen(const Step& s) { return std::hypot(double(s.dx), double(s.dy)); }

}  // namespace

void test_net_displacement_is_zero() {
  forEachCase({5, 150, 600}, 200, [](const Plan& p, uint16_t, int) {
    int sx = 0, sy = 0;
    for (const Step& s : p.steps)
      if (s.kind == Step::Kind::Move) {
        sx += s.dx;
        sy += s.dy;
      }
    TEST_ASSERT_EQUAL_INT(0, sx);
    TEST_ASSERT_EQUAL_INT(0, sy);
  });
}

void test_reach_matches_amplitude() {
  forEachCase({5, 150, 600}, 200, [](const Plan& p, uint16_t amp, int) {
    double maxDist = 0;
    for (const Pos& q : positions(p)) maxDist = std::fmax(maxDist, std::hypot(q.x, q.y));
    TEST_ASSERT_TRUE(maxDist <= amp + 2);
    if (amp >= 50) TEST_ASSERT_TRUE(maxDist >= 0.3 * amp - 2);
  });
}

void test_path_is_curved() {
  int curved = 0;
  const int seeds = 50;
  for (int seed = 1; seed <= seeds; seed++) {
    Plan p = planFor(150, seed);
    size_t d = dwellIndex(p);
    TEST_ASSERT_TRUE(d < p.steps.size());
    double x = 0, y = 0, maxDev = 0;
    std::vector<Pos> out;
    for (size_t i = 0; i <= d; i++) {
      x += p.steps[i].dx;
      y += p.steps[i].dy;
      out.push_back({x, y});
    }
    const Pos peak = out.back();
    const double chord = std::hypot(peak.x, peak.y);
    for (const Pos& q : out) maxDev = std::fmax(maxDev, std::fabs(q.x * peak.y - q.y * peak.x) / chord);
    if (maxDev > 2.0) curved++;
  }
  TEST_ASSERT_TRUE(curved >= seeds * 8 / 10);
}

void test_ease_in_out() {
  // Amplitude 5 is excluded: at 1-5 px, integer rounding makes speed profiles meaningless.
  forEachCase({150, 600}, 100, [](const Plan& p, uint16_t, int) {
    size_t d = dwellIndex(p);
    TEST_ASSERT_TRUE(d + 1 >= 12);
    double head = 0, tail = 0, maxMid = 0;
    for (size_t i = 0; i < 3; i++) head += stepLen(p.steps[i]);
    for (size_t i = d - 2; i <= d; i++) tail += stepLen(p.steps[i]);
    for (size_t i = 3; i + 3 <= d; i++) maxMid = std::fmax(maxMid, stepLen(p.steps[i]));
    TEST_ASSERT_TRUE(head / 3 < 0.5 * maxMid);
    TEST_ASSERT_TRUE(tail / 3 < 0.5 * maxMid);
  });
}

void test_steps_are_valid() {
  for (float prob : {0.0f, 1.0f}) {
    for (uint16_t amp : {5, 150, 600}) {
      for (int seed = 1; seed <= 100; seed++) {
        Plan p = planFor(amp, seed, prob, prob);
        int dwells = 0;
        for (const Step& s : p.steps) {
          TEST_ASSERT_TRUE(s.waitMs > 0);
          if (s.kind != Step::Kind::Move) continue;
          if (s.waitMs == 20) continue;
          dwells++;
          TEST_ASSERT_TRUE(s.waitMs >= 200 && s.waitMs <= 1500);
        }
        TEST_ASSERT_EQUAL_INT(1, dwells);
      }
    }
  }
}

void test_intervals_not_constant() {
  uint32_t first = 0;
  bool differs = false;
  for (int seed = 1; seed <= 50; seed++) {
    Plan p = planFor(150, seed);
    uint32_t total = 0;
    for (const Step& s : p.steps) total += s.waitMs;
    if (seed == 1) first = total;
    else if (total != first) differs = true;
  }
  TEST_ASSERT_TRUE(differs);
}

void test_click_scroll_probabilities() {
  for (int seed = 1; seed <= 100; seed++) {
    Plan none = planFor(150, seed, 0.0f, 0.0f);
    for (const Step& s : none.steps) TEST_ASSERT_TRUE(s.kind == Step::Kind::Move);

    Plan all = planFor(150, seed, 1.0f, 1.0f);
    int clicks = 0, scrolls = 0, notchSum = 0;
    for (const Step& s : all.steps) {
      if (s.kind == Step::Kind::Click) clicks++;
      if (s.kind == Step::Kind::Scroll) {
        scrolls++;
        notchSum += s.notches;
        TEST_ASSERT_TRUE(std::abs(s.notches) >= 1 && std::abs(s.notches) <= 3);
      }
    }
    TEST_ASSERT_EQUAL_INT(1, clicks);
    TEST_ASSERT_EQUAL_INT(2, scrolls);
    TEST_ASSERT_EQUAL_INT(0, notchSum);
  }
}

static Plan manualPlan() {
  Plan p;
  p.steps = {
      {Step::Kind::Move, 1, 2, 0, 20},
      {Step::Kind::Click, 0, 0, 0, 100},
      {Step::Kind::Scroll, 0, 0, -2, 300},
      {Step::Kind::Move, 3, 4, 0, 20},
      {Step::Kind::Move, 5, 6, 0, 20},
  };
  return p;
}

void test_run_plan_dispatches_kinds_and_sleeps_wait_ms() {
  FakeClock clock;
  FakeMouse mouse;
  std::vector<uint32_t> sleeps;
  size_t done = runPlan(manualPlan(), mouse, clock, [&](uint32_t ms) { sleeps.push_back(ms); });
  TEST_ASSERT_EQUAL_UINT32(5, done);
  TEST_ASSERT_EQUAL_UINT32(5, mouse.calls.size());
  TEST_ASSERT_EQUAL_STRING("move 1,2", mouse.calls[0].c_str());
  TEST_ASSERT_EQUAL_STRING("click", mouse.calls[1].c_str());
  TEST_ASSERT_EQUAL_STRING("scroll -2", mouse.calls[2].c_str());
  const uint32_t expected[] = {20, 100, 300, 20, 20};
  TEST_ASSERT_EQUAL_UINT32(5, sleeps.size());
  for (size_t i = 0; i < 5; i++) TEST_ASSERT_EQUAL_UINT32(expected[i], sleeps[i]);
}

void test_run_plan_stops_on_disconnect() {
  FakeClock clock;
  FakeMouse mouse;
  int sleepCalls = 0;
  size_t done = runPlan(manualPlan(), mouse, clock, [&](uint32_t) {
    if (++sleepCalls == 3) clock.mouse = false;
  });
  TEST_ASSERT_EQUAL_UINT32(3, done);
  TEST_ASSERT_EQUAL_UINT32(3, mouse.calls.size());
}

void test_run_plan_does_nothing_when_disconnected() {
  FakeClock clock;
  clock.mouse = false;
  FakeMouse mouse;
  size_t done = runPlan(manualPlan(), mouse, clock, [](uint32_t) {});
  TEST_ASSERT_EQUAL_UINT32(0, done);
  TEST_ASSERT_EQUAL_UINT32(0, mouse.calls.size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_net_displacement_is_zero);
  RUN_TEST(test_reach_matches_amplitude);
  RUN_TEST(test_path_is_curved);
  RUN_TEST(test_ease_in_out);
  RUN_TEST(test_steps_are_valid);
  RUN_TEST(test_intervals_not_constant);
  RUN_TEST(test_click_scroll_probabilities);
  RUN_TEST(test_run_plan_dispatches_kinds_and_sleeps_wait_ms);
  RUN_TEST(test_run_plan_stops_on_disconnect);
  RUN_TEST(test_run_plan_does_nothing_when_disconnected);
  return UNITY_END();
}
