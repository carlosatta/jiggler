#include "profiles.h"

namespace jig {

namespace {

constexpr uint16_t kAppearanceKeyboard = 0x03C1;
constexpr uint16_t kAppearanceMouse = 0x03C2;

Identity identity(const char* name, const char* manufacturer, uint16_t vid, uint16_t pid, uint8_t vidSource,
                  std::array<uint8_t, 3> oui, uint16_t appearance) {
  Identity i;
  i.name = name;
  i.manufacturer = manufacturer;
  i.vid = vid;
  i.pid = pid;
  i.vidSource = vidSource;
  i.oui = oui;
  i.appearance = appearance;
  return i;
}

// Sources: IEEE OUI registry (00:1F:20 Logitech Europe SA, 44:5E:CD Razer Inc, 58:AD:12 Apple, Inc.);
// Linux HID ids 0005:046D:B034 (MX Master 3S), 0005:1532:0095 (Razer Orochi V2), OpenLogi listing 046d:b35b
// (MX Keys), Apple Bluetooth vendor 004C with product 0269 (Magic Mouse 2) / 0267 (Magic Keyboard).
std::vector<Profile> buildCatalog() {
  return {
      {"logitech-mx-master-3s", "Logitech MX Master 3S", DeviceKind::Mouse,
       identity("MX Master 3S", "Logitech", 0x046D, 0xB034, 2, {0x00, 0x1F, 0x20}, kAppearanceMouse)},
      {"razer-orochi-v2", "Razer Orochi V2", DeviceKind::Mouse,
       identity("Razer Orochi V2", "Razer", 0x1532, 0x0095, 2, {0x44, 0x5E, 0xCD}, kAppearanceMouse)},
      {"apple-magic-mouse-2", "Apple Magic Mouse 2", DeviceKind::Mouse,
       identity("Magic Mouse 2", "Apple Inc.", 0x004C, 0x0269, 1, {0x58, 0xAD, 0x12}, kAppearanceMouse)},
      {"logitech-mx-keys", "Logitech MX Keys", DeviceKind::Keyboard,
       identity("MX Keys", "Logitech", 0x046D, 0xB35B, 2, {0x00, 0x1F, 0x20}, kAppearanceKeyboard)},
      {"apple-magic-keyboard", "Apple Magic Keyboard", DeviceKind::Keyboard,
       identity("Magic Keyboard", "Apple Inc.", 0x004C, 0x0267, 1, {0x58, 0xAD, 0x12}, kAppearanceKeyboard)},
  };
}

}  // namespace

const std::vector<Profile>& catalog() {
  static const std::vector<Profile> kCatalog = buildCatalog();
  return kCatalog;
}

const Profile* findProfile(const std::string& id) {
  for (const Profile& p : catalog())
    if (p.id == id) return &p;
  return nullptr;
}

Identity genericIdentity(DeviceKind kind) {
  Identity i;
  const bool mouse = kind == DeviceKind::Mouse;
  i.name = mouse ? "Bluetooth Mouse" : "Bluetooth Keyboard";
  i.manufacturer = "Generic";
  i.vid = 0x1209;  // pid.codes open-hardware vendor id
  i.pid = 0x0001;
  i.appearance = mouse ? kAppearanceMouse : kAppearanceKeyboard;
  return i;
}

}  // namespace jig
