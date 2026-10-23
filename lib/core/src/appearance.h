#pragma once

#include <ArduinoJson.h>

#include <array>
#include <mutex>
#include <optional>
#include <string>

#include "config.h"
#include "controller.h"
#include "json_codec.h"
#include "platform.h"
#include "profiles.h"

namespace jig {

// How the device introduces itself to computers over Bluetooth. There is exactly one device (a mouse), so this is
// a single configuration, not a list. The default is a neutral "custom" identity; a profile borrows a real
// model's name, vendor and address prefix. Every field except `profile` is an optional override.
struct Appearance {
  std::string profile = "custom";  // "custom" or an id from catalog()
  std::optional<std::string> name, manufacturer, mac;
  std::optional<uint16_t> vid, pid;
};

struct ResolvedAppearance {
  Identity identity;
  std::optional<std::array<uint8_t, 6>> mac;  // nullopt = keep the chip's own address
  bool macExplicit = false;                    // the user typed this address: it must be the final Bluetooth address
};

// "AA:BB:CC:DD:EE:FF" (any hex case). Multicast addresses parse; validateAppearance rejects them.
std::optional<std::array<uint8_t, 6>> parseMac(const std::string& text);

std::optional<ValidationError> validateAppearance(const Appearance& appearance);

// The stored fields, plus `shownAs` (the name the computer sees) and, unless `withCatalog` is false, the models
// the dashboard offers (`profiles`) and the `custom` defaults.
std::string appearanceToJson(const Appearance& appearance, std::optional<bool> restartRequired = std::nullopt,
                             bool withCatalog = true);
// A full replacement: omitted fields mean "default". Read-only keys that GET adds are ignored, so a GET body can
// be sent back unchanged. Errors carry the offending field.
bool parseAppearance(const std::string& body, Appearance& out, ParseError& err);

Identity resolveIdentity(const Appearance& appearance);
std::optional<std::array<uint8_t, 6>> resolveMac(const Appearance& appearance, const Identity& identity,
                                                 const uint8_t efuseMac[6]);

class AppearanceStore {
 public:
  explicit AppearanceStore(BlobStore& blobs);
  void begin();
  Appearance get() const;
  CmdResult set(const Appearance& appearance);  // errors: invalid_appearance, storage_error
  ResolvedAppearance resolved(const uint8_t efuseMac[6]) const;

 private:
  BlobStore& blobs_;
  mutable std::mutex mutex_;
  Appearance appearance_;
};

}  // namespace jig
