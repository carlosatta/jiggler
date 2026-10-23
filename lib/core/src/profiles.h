#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace jig {

enum class DeviceKind : uint8_t { Mouse, Keyboard };

struct Identity {
  std::string name;          // BLE name, <= 29 chars
  std::string manufacturer;  // <= 32 chars
  uint16_t vid = 0;
  uint16_t pid = 0;
  uint8_t vidSource = 2;     // 1 = Bluetooth SIG, 2 = USB-IF (PnP ID vendor source)
  uint16_t version = 0x0100;
  std::optional<std::array<uint8_t, 3>> oui;  // vendor prefix used to build the Bluetooth address
  uint16_t appearance = 0;   // GAP appearance: 0x03C1 keyboard, 0x03C2 mouse
};

struct Profile {
  std::string id;
  std::string label;
  DeviceKind kind;
  Identity identity;
};

// Only entries whose VID/PID/OUI were verified against public sources.
const std::vector<Profile>& catalog();
const Profile* findProfile(const std::string& id);

// Neutral identity used by profileId "custom".
Identity genericIdentity(DeviceKind kind);

}  // namespace jig
