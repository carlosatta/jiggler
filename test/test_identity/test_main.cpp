#include <unity.h>

#include <cstring>

#include "identity.h"

using namespace jig;

void setUp() {}
void tearDown() {}

void test_identity_constants() {
  TEST_ASSERT_EQUAL_STRING("MX Master 3S", kBleName);
  TEST_ASSERT_EQUAL_STRING("Logitech", kManufacturer);
  TEST_ASSERT_EQUAL_HEX16(0x046D, kVid);
  TEST_ASSERT_EQUAL_HEX16(0xB034, kPid);
}

void test_mac_prefix_is_logitech() {
  const uint8_t efuse[6] = {0x24, 0x6F, 0x28, 0x11, 0x22, 0x33};
  auto mac = deriveLogitechMac(efuse);
  TEST_ASSERT_EQUAL_HEX8(0x00, mac[0]);
  TEST_ASSERT_EQUAL_HEX8(0x1F, mac[1]);
  TEST_ASSERT_EQUAL_HEX8(0x20, mac[2]);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kLogitechOui, mac.data(), 3);
}

void test_mac_tail_comes_from_efuse() {
  const uint8_t efuse[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
  auto mac = deriveLogitechMac(efuse);
  TEST_ASSERT_EQUAL_HEX8(0xDD, mac[3]);
  TEST_ASSERT_EQUAL_HEX8(0xEE, mac[4]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, mac[5]);
}

void test_mac_is_unicast_and_stable() {
  const uint8_t a[6] = {0xFF, 0xFF, 0xFF, 0x01, 0x02, 0x03};
  const uint8_t b[6] = {0x01, 0x00, 0x00, 0x01, 0x02, 0x04};
  TEST_ASSERT_EQUAL_UINT8(0, deriveLogitechMac(a)[0] & 0x01);
  TEST_ASSERT_EQUAL_UINT8(0, deriveLogitechMac(b)[0] & 0x01);
  TEST_ASSERT_TRUE(deriveLogitechMac(a) == deriveLogitechMac(a));
  TEST_ASSERT_TRUE(deriveLogitechMac(a) != deriveLogitechMac(b));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_identity_constants);
  RUN_TEST(test_mac_prefix_is_logitech);
  RUN_TEST(test_mac_tail_comes_from_efuse);
  RUN_TEST(test_mac_is_unicast_and_stable);
  return UNITY_END();
}
