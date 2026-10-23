#pragma once

#include <array>
#include <cstdint>

namespace jig {

constexpr const char* kBleName = "MX Master 3S";
constexpr const char* kManufacturer = "Logitech";
constexpr uint16_t kVid = 0x046D;
constexpr uint16_t kPid = 0xB034;
constexpr uint8_t kLogitechOui[3] = {0x00, 0x1F, 0x20};

// Logitech OUI + the last 3 bytes of the chip's eFuse MAC: stable across reboots.
std::array<uint8_t, 6> deriveLogitechMac(const uint8_t efuseMac[6]);

}  // namespace jig
