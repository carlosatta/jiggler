#include <unity.h>

#include "controller.h"
#include "fakes.h"

using namespace jig;

void setUp() {}
void tearDown() {}

namespace {

struct Fixture {
  FakeClock clock;
  XorShiftRng rng{1};
  MemStore store;
  Controller c{clock, rng, store};

  Fixture(uint32_t minSec, uint32_t maxSec) {
    store.stored.minIntervalSec = minSec;
    store.stored.maxIntervalSec = maxSec;
    c.begin();
  }
};

}  // namespace

void test_begin_is_running_with_interval_in_bounds() {
  Fixture f(10, 10);
  Status s = f.c.status();
  TEST_ASSERT_TRUE(s.runState == RunState::Running);
  TEST_ASSERT_TRUE(s.msToNextMove.has_value());
  TEST_ASSERT_EQUAL_UINT32(10000, *s.msToNextMove);
  TEST_ASSERT_EQUAL_UINT32(0, s.moveCount);
  TEST_ASSERT_FALSE(s.lastMoveAgoMs.has_value());

  Fixture g(30, 120);
  auto ms = g.c.status().msToNextMove;
  TEST_ASSERT_TRUE(ms.has_value() && *ms >= 30000 && *ms <= 120000);
}

void test_begin_falls_back_to_defaults_on_invalid_stored_config() {
  Fixture f(1000, 10);
  TEST_ASSERT_EQUAL_UINT32(Config{}.minIntervalSec, f.c.status().config.minIntervalSec);
  TEST_ASSERT_EQUAL_UINT32(Config{}.maxIntervalSec, f.c.status().config.maxIntervalSec);
}

void test_tick_fires_only_when_due_and_reschedules() {
  Fixture f(10, 10);
  f.clock.now = 9999;
  TEST_ASSERT_FALSE(f.c.tick().has_value());
  f.clock.now = 10000;
  auto plan = f.c.tick();
  TEST_ASSERT_TRUE(plan.has_value());
  TEST_ASSERT_TRUE(!plan->steps.empty());
  Status s = f.c.status();
  TEST_ASSERT_EQUAL_UINT32(1, s.moveCount);
  TEST_ASSERT_TRUE(s.lastMoveAgoMs.has_value());
  TEST_ASSERT_EQUAL_UINT32(0, *s.lastMoveAgoMs);
  TEST_ASSERT_EQUAL_UINT32(10000, *s.msToNextMove);
  TEST_ASSERT_FALSE(f.c.tick().has_value());
}

void test_pause_freezes_and_resume_restores_remaining() {
  Fixture f(10, 10);
  f.clock.now = 4000;
  f.c.pause();
  TEST_ASSERT_TRUE(f.c.status().runState == RunState::Paused);
  TEST_ASSERT_FALSE(f.c.status().msToNextMove.has_value());
  f.clock.now = 64000;
  TEST_ASSERT_FALSE(f.c.tick().has_value());
  f.c.start();
  TEST_ASSERT_TRUE(f.c.status().runState == RunState::Running);
  TEST_ASSERT_EQUAL_UINT32(6000, *f.c.status().msToNextMove);
  f.clock.now = 64000 + 5999;
  TEST_ASSERT_FALSE(f.c.tick().has_value());
  f.clock.now = 64000 + 6000;
  TEST_ASSERT_TRUE(f.c.tick().has_value());
}

void test_stop_clears_schedule() {
  Fixture f(10, 10);
  f.c.stop();
  Status s = f.c.status();
  TEST_ASSERT_TRUE(s.runState == RunState::Stopped);
  TEST_ASSERT_FALSE(s.msToNextMove.has_value());
  f.clock.now = 100000;
  TEST_ASSERT_FALSE(f.c.tick().has_value());
  f.c.start();
  TEST_ASSERT_EQUAL_UINT32(10000, *f.c.status().msToNextMove);
}

void test_commands_are_idempotent() {
  Fixture f(10, 10);
  f.clock.now = 3000;
  f.c.start();
  TEST_ASSERT_EQUAL_UINT32(7000, *f.c.status().msToNextMove);
  f.c.pause();
  f.clock.now = 5000;
  f.c.pause();
  f.c.start();
  TEST_ASSERT_EQUAL_UINT32(7000, *f.c.status().msToNextMove);
  f.c.stop();
  f.c.stop();
  f.c.pause();
  TEST_ASSERT_TRUE(f.c.status().runState == RunState::Stopped);
}

void test_due_move_waits_for_connection() {
  Fixture f(10, 10);
  f.clock.now = 10000;
  f.clock.mouse = false;
  TEST_ASSERT_FALSE(f.c.tick().has_value());
  TEST_ASSERT_EQUAL_UINT32(0, *f.c.status().msToNextMove);
  f.clock.mouse = true;
  TEST_ASSERT_TRUE(f.c.tick().has_value());
}

void test_move_now_disconnected_rejected() {
  Fixture f(10, 10);
  f.clock.mouse = false;
  CmdResult r = f.c.moveNow();
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL_STRING("mouse_not_connected", r.error.c_str());
  f.clock.mouse = true;
  TEST_ASSERT_FALSE(f.c.tick().has_value());
}

void test_move_now_served_even_when_stopped() {
  Fixture f(10, 10);
  f.c.stop();
  TEST_ASSERT_TRUE(f.c.moveNow().ok);
  TEST_ASSERT_TRUE(f.c.tick().has_value());
  Status s = f.c.status();
  TEST_ASSERT_EQUAL_UINT32(1, s.moveCount);
  TEST_ASSERT_TRUE(s.runState == RunState::Stopped);
  TEST_ASSERT_FALSE(s.msToNextMove.has_value());
  TEST_ASSERT_FALSE(f.c.tick().has_value());
}

void test_move_now_while_running_reschedules() {
  Fixture f(10, 10);
  f.clock.now = 3000;
  TEST_ASSERT_TRUE(f.c.moveNow().ok);
  TEST_ASSERT_TRUE(f.c.tick().has_value());
  TEST_ASSERT_EQUAL_UINT32(10000, *f.c.status().msToNextMove);
}

void test_pending_move_now_dropped_if_mouse_disconnects() {
  Fixture f(10, 10);
  TEST_ASSERT_TRUE(f.c.moveNow().ok);
  f.clock.mouse = false;
  TEST_ASSERT_FALSE(f.c.tick().has_value());
  f.clock.mouse = true;
  TEST_ASSERT_FALSE(f.c.tick().has_value());
}

void test_clock_wraparound() {  // RF1
  FakeClock clock;
  clock.now = 0xFFFFFF00u;
  XorShiftRng rng(1);
  MemStore store;
  store.stored.minIntervalSec = 5;
  store.stored.maxIntervalSec = 5;
  Controller c(clock, rng, store);
  c.begin();
  const uint32_t t0 = clock.now;
  clock.now = t0 + 4999;
  TEST_ASSERT_FALSE(c.tick().has_value());
  clock.now = t0 + 5000;  // wrapped past 2^32
  TEST_ASSERT_TRUE(c.tick().has_value());
  clock.now = t0 + 5000 + 4999;
  TEST_ASSERT_FALSE(c.tick().has_value());
  clock.now = t0 + 5000 + 5000;
  TEST_ASSERT_TRUE(c.tick().has_value());
  TEST_ASSERT_EQUAL_UINT32(2, c.status().moveCount);
}

void test_update_config_invalid_changes_nothing() {
  Fixture f(10, 10);
  ConfigPatch p;
  p.minIntervalSec = 100;
  CmdResult r = f.c.updateConfig(p);
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL_STRING("invalid_config", r.error.c_str());
  TEST_ASSERT_EQUAL_STRING("minIntervalSec", r.field.c_str());
  TEST_ASSERT_EQUAL_INT(0, f.store.saves);
  TEST_ASSERT_EQUAL_UINT32(10, f.c.status().config.minIntervalSec);
}

void test_update_config_valid_saves_and_reschedules() {
  Fixture f(10, 10);
  f.clock.now = 2000;
  ConfigPatch p;
  p.minIntervalSec = 20;
  p.maxIntervalSec = 20;
  CmdResult r = f.c.updateConfig(p);
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_INT(1, f.store.saves);
  TEST_ASSERT_EQUAL_UINT32(20, f.store.stored.minIntervalSec);
  TEST_ASSERT_EQUAL_UINT32(20, f.c.status().config.maxIntervalSec);
  TEST_ASSERT_EQUAL_UINT32(20000, *f.c.status().msToNextMove);
}

void test_last_move_unix_null_when_clock_unsynced() {
  Fixture f(10, 10);
  f.clock.unix = 0;
  f.clock.now = 10000;
  TEST_ASSERT_TRUE(f.c.tick().has_value());
  f.clock.now = 12000;
  Status s = f.c.status();
  TEST_ASSERT_TRUE(s.lastMoveAgoMs.has_value());
  TEST_ASSERT_EQUAL_UINT32(2000, *s.lastMoveAgoMs);
  TEST_ASSERT_FALSE(s.lastMoveUnix.has_value());

  f.clock.unix = 1760000000;
  s = f.c.status();
  TEST_ASSERT_TRUE(s.lastMoveUnix.has_value());
  TEST_ASSERT_EQUAL_UINT32(1760000000 - 2, *s.lastMoveUnix);
}

void test_start_for_stops_by_itself() {
  Fixture f(10, 10);
  f.c.stop();
  TEST_ASSERT_TRUE(f.c.startFor(5).ok);
  Status s = f.c.status();
  TEST_ASSERT_TRUE(s.runState == RunState::Running);
  TEST_ASSERT_TRUE(s.msToAutoStop.has_value());
  TEST_ASSERT_EQUAL_UINT32(300000, *s.msToAutoStop);
  f.clock.now = 299999;
  f.c.tick();
  TEST_ASSERT_TRUE(f.c.status().runState == RunState::Running);
  f.clock.now = 300000;
  f.c.tick();
  s = f.c.status();
  TEST_ASSERT_TRUE(s.runState == RunState::Stopped);
  TEST_ASSERT_FALSE(s.msToAutoStop.has_value());
}

void test_start_for_replaces_a_running_deadline() {
  Fixture f(10, 10);
  f.c.startFor(5);
  f.clock.now = 120000;
  f.c.startFor(30);
  TEST_ASSERT_EQUAL_UINT32(30u * 60000u, *f.c.status().msToAutoStop);
}

void test_plain_start_stop_and_pause_clear_the_deadline() {
  Fixture f(10, 10);
  f.c.startFor(5);
  f.c.start();
  TEST_ASSERT_FALSE(f.c.status().msToAutoStop.has_value());  // "start" means: run until told otherwise
  f.c.startFor(5);
  f.c.stop();
  TEST_ASSERT_FALSE(f.c.status().msToAutoStop.has_value());
  f.c.startFor(5);
  f.c.pause();
  TEST_ASSERT_FALSE(f.c.status().msToAutoStop.has_value());
}

void test_start_for_rejects_out_of_range_minutes() {
  Fixture f(10, 10);
  f.c.stop();
  CmdResult zero = f.c.startFor(0);
  TEST_ASSERT_FALSE(zero.ok);
  TEST_ASSERT_EQUAL_STRING("invalid_minutes", zero.error.c_str());
  TEST_ASSERT_FALSE(f.c.startFor(1441).ok);
  TEST_ASSERT_TRUE(f.c.status().runState == RunState::Stopped);  // nothing changed
  TEST_ASSERT_TRUE(f.c.startFor(1440).ok);
}

void test_start_for_deadline_survives_the_millis_wraparound() {
  Fixture f(10, 10);
  f.clock.now = 0xFFFFFF00u;
  f.c.startFor(1);
  f.clock.now = 0xFFFFFF00u + 59999u;
  f.c.tick();
  TEST_ASSERT_TRUE(f.c.status().runState == RunState::Running);
  f.clock.now = 0xFFFFFF00u + 60000u;
  f.c.tick();
  TEST_ASSERT_TRUE(f.c.status().runState == RunState::Stopped);
}

void test_it_cannot_run_without_a_paired_computer() {
  FakeClock clock;
  clock.paired = false;
  XorShiftRng rng{1};
  MemStore store;
  Controller c{clock, rng, store};
  c.begin();
  TEST_ASSERT_TRUE(c.status().runState == RunState::Stopped);   // a board with nothing paired boots stopped
  CmdResult r = c.start();
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL_STRING("no_computer_paired", r.error.c_str());
  TEST_ASSERT_FALSE(c.startFor(10).ok);
  TEST_ASSERT_TRUE(c.status().runState == RunState::Stopped);
  clock.now = 1000000;
  TEST_ASSERT_FALSE(c.tick().has_value());                       // and never moves
  clock.paired = true;                                           // a computer pairs
  TEST_ASSERT_TRUE(c.start().ok);
  TEST_ASSERT_TRUE(c.status().runState == RunState::Running);
}

void test_forgetting_every_computer_stops_a_running_jiggler() {
  FakeClock clock;
  XorShiftRng rng{1};
  MemStore store;
  Controller c{clock, rng, store};
  c.begin();
  TEST_ASSERT_TRUE(c.status().runState == RunState::Running);
  clock.paired = false;
  clock.now = 100;
  c.tick();
  TEST_ASSERT_TRUE(c.status().runState == RunState::Stopped);
  clock.paired = true;                                           // pairing again does not restart it by itself
  c.tick();
  TEST_ASSERT_TRUE(c.status().runState == RunState::Stopped);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_begin_is_running_with_interval_in_bounds);
  RUN_TEST(test_begin_falls_back_to_defaults_on_invalid_stored_config);
  RUN_TEST(test_tick_fires_only_when_due_and_reschedules);
  RUN_TEST(test_pause_freezes_and_resume_restores_remaining);
  RUN_TEST(test_stop_clears_schedule);
  RUN_TEST(test_commands_are_idempotent);
  RUN_TEST(test_due_move_waits_for_connection);
  RUN_TEST(test_move_now_disconnected_rejected);
  RUN_TEST(test_move_now_served_even_when_stopped);
  RUN_TEST(test_move_now_while_running_reschedules);
  RUN_TEST(test_pending_move_now_dropped_if_mouse_disconnects);
  RUN_TEST(test_clock_wraparound);
  RUN_TEST(test_update_config_invalid_changes_nothing);
  RUN_TEST(test_update_config_valid_saves_and_reschedules);
  RUN_TEST(test_last_move_unix_null_when_clock_unsynced);
  RUN_TEST(test_start_for_stops_by_itself);
  RUN_TEST(test_start_for_replaces_a_running_deadline);
  RUN_TEST(test_plain_start_stop_and_pause_clear_the_deadline);
  RUN_TEST(test_start_for_rejects_out_of_range_minutes);
  RUN_TEST(test_start_for_deadline_survives_the_millis_wraparound);
  RUN_TEST(test_it_cannot_run_without_a_paired_computer);
  RUN_TEST(test_forgetting_every_computer_stops_a_running_jiggler);
  return UNITY_END();
}
