#include <unity.h>

#include "led_logic.h"

using namespace jig;

void setUp() {}
void tearDown() {}

namespace {
LedContext ctx(BluetoothState bt, bool running = false, uint32_t now = 0) {
  LedContext c;
  c.bluetooth = bt;
  c.running = running;
  c.nowMs = now;
  return c;
}
}  // namespace

void test_waiting_blinks_slowly() {
  LedContext c = ctx(BluetoothState::Waiting);
  TEST_ASSERT_TRUE(ledLevel(c));
  c.nowMs = 499;
  TEST_ASSERT_TRUE(ledLevel(c));
  c.nowMs = 500;
  TEST_ASSERT_FALSE(ledLevel(c));
  c.nowMs = 1000;
  TEST_ASSERT_TRUE(ledLevel(c));
  c.nowMs = 123456500u;
  TEST_ASSERT_FALSE(ledLevel(c));
}

void test_discovery_blinks_fast_even_when_connected() {
  LedContext c = ctx(BluetoothState::Discovery);
  TEST_ASSERT_TRUE(ledLevel(c));
  c.nowMs = 124;
  TEST_ASSERT_TRUE(ledLevel(c));
  c.nowMs = 125;
  TEST_ASSERT_FALSE(ledLevel(c));
  c.nowMs = 250;
  TEST_ASSERT_TRUE(ledLevel(c));
}

void test_off_flashes_once_every_three_seconds() {
  LedContext c = ctx(BluetoothState::Off);
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

void test_connected_and_running_is_solid() {
  for (uint32_t now = 0; now <= 6000; now += 10) TEST_ASSERT_TRUE(ledLevel(ctx(BluetoothState::Connected, true, now)));
}

void test_connected_and_running_flashes_off_after_a_move() {
  LedContext c = ctx(BluetoothState::Connected, true, 5000);
  c.hasMoved = true;
  c.lastMoveMs = 5000;
  TEST_ASSERT_FALSE(ledLevel(c));
  c.nowMs = 5079;
  TEST_ASSERT_FALSE(ledLevel(c));
  c.nowMs = 5080;
  TEST_ASSERT_TRUE(ledLevel(c));
  c.nowMs = 4999;
  TEST_ASSERT_TRUE(ledLevel(c));
}

void test_the_move_flash_survives_clock_wraparound() {
  LedContext c = ctx(BluetoothState::Connected, true, 0x20u);
  c.hasMoved = true;
  c.lastMoveMs = 0xFFFFFFF0u;
  TEST_ASSERT_FALSE(ledLevel(c));
  c.nowMs = 0x70u;
  TEST_ASSERT_TRUE(ledLevel(c));
}

void test_connected_but_not_running_flashes_twice_every_three_seconds() {
  LedContext c = ctx(BluetoothState::Connected, false);
  struct Case { uint32_t at; bool on; };
  const Case expected[] = {{0, true}, {99, true}, {100, false}, {249, false}, {250, true}, {349, true}, {350, false}, {2999, false}, {3000, true}};
  for (const Case& e : expected) {
    c.nowMs = e.at;
    TEST_ASSERT_EQUAL_MESSAGE(e.on, ledLevel(c), std::to_string(e.at).c_str());
  }
}

void test_holding_the_button_overrides_everything() {
  LedContext c = ctx(BluetoothState::Off, true, 130);  // off pattern would be dark at 130 ms
  c.hold = HoldPhase::Pairing;
  TEST_ASSERT_FALSE(ledLevel(c));                       // fast blink is dark at 130 ms
  c.nowMs = 10;
  TEST_ASSERT_TRUE(ledLevel(c));
  c.hold = HoldPhase::Cancel;
  c.nowMs = 10;
  TEST_ASSERT_FALSE(ledLevel(c));
  c.hold = HoldPhase::Network;
  c.nowMs = 130;
  TEST_ASSERT_TRUE(ledLevel(c));
}

void test_a_tap_is_confirmed_by_three_blinks_then_the_state_returns() {
  LedContext c = ctx(BluetoothState::Waiting, false, 10000 + 500);  // waiting is dark at 500 ms
  c.confirming = true;
  c.confirmStartMs = 10000;
  c.nowMs = 10000;
  TEST_ASSERT_TRUE(ledLevel(c));
  c.nowMs = 10100;
  TEST_ASSERT_FALSE(ledLevel(c));
  c.nowMs = 10200;
  TEST_ASSERT_TRUE(ledLevel(c));
  c.nowMs = 10400;
  TEST_ASSERT_TRUE(ledLevel(c));
  c.nowMs = 10599;
  TEST_ASSERT_FALSE(ledLevel(c));
  c.nowMs = 10600;                     // confirmation over: the waiting pattern again (10600 % 1000 = 600 -> dark)
  TEST_ASSERT_FALSE(ledLevel(c));
  c.nowMs = 11000;
  TEST_ASSERT_TRUE(ledLevel(c));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_waiting_blinks_slowly);
  RUN_TEST(test_discovery_blinks_fast_even_when_connected);
  RUN_TEST(test_off_flashes_once_every_three_seconds);
  RUN_TEST(test_connected_and_running_is_solid);
  RUN_TEST(test_connected_and_running_flashes_off_after_a_move);
  RUN_TEST(test_the_move_flash_survives_clock_wraparound);
  RUN_TEST(test_connected_but_not_running_flashes_twice_every_three_seconds);
  RUN_TEST(test_holding_the_button_overrides_everything);
  RUN_TEST(test_a_tap_is_confirmed_by_three_blinks_then_the_state_returns);
  return UNITY_END();
}
