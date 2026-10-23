#pragma once

#include <ArduinoJson.h>

#include <array>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "config.h"
#include "controller.h"
#include "json_codec.h"
#include "platform.h"
#include "profiles.h"

namespace jig {

struct DeviceConfig {
  std::string id;
  DeviceKind kind = DeviceKind::Mouse;
  bool enabled = false;
  std::string profileId = "custom";
  std::optional<std::string> name, manufacturer, mac;
  std::optional<uint16_t> vid, pid;
};

struct ResolvedDevice {
  Identity identity;
  std::optional<std::array<uint8_t, 6>> mac;  // nullopt = keep the chip's own address
  bool macExplicit = false;                    // the user typed this address: it must be the final Bluetooth address
  bool mouse = false;
  bool keyboard = false;
};

// "AA:BB:CC:DD:EE:FF" (any hex case). Multicast addresses parse; validateDevices rejects them.
std::optional<std::array<uint8_t, 6>> parseMac(const std::string& text);

std::vector<DeviceConfig> defaultDevices();
std::optional<ValidationError> validateDevices(const std::vector<DeviceConfig>& devices);

std::string devicesToJson(const std::vector<DeviceConfig>& devices, std::optional<bool> restartRequired = std::nullopt);
std::string profilesToJson();
bool parseDevices(const std::string& body, std::vector<DeviceConfig>& out, ParseError& err);
bool parseDevices(JsonArrayConst array, std::vector<DeviceConfig>& out, ParseError& err);

Identity resolveIdentity(const DeviceConfig& device);
std::optional<std::array<uint8_t, 6>> resolveMac(const DeviceConfig& device, const Identity& identity,
                                                 const uint8_t efuseMac[6]);

// The BLE identity is the first enabled device's; every enabled device adds an HID collection.
class DeviceRegistry {
 public:
  explicit DeviceRegistry(BlobStore& blobs);
  void begin();
  std::vector<DeviceConfig> list() const;
  CmdResult replace(const std::vector<DeviceConfig>& devices);
  ResolvedDevice resolved(const uint8_t efuseMac[6]) const;

 private:
  BlobStore& blobs_;
  mutable std::mutex mutex_;
  std::vector<DeviceConfig> devices_;
};

}  // namespace jig
