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
// Linux HID ids 0005:046D:B034 (MX Master 3S), 0005:1532:0095 (Razer Orochi V2); the other Logitech mice are the
// "... mouse over Bluetooth" entries of drivers/hid/hid-logitech-hidpp.c (MX Master B012/B019/B023/B042, MX Anywhere
// B025/B037, MX Vertical B020, M720 B015); Apple Bluetooth vendor 004C (drivers/hid/hid-ids.h BT_VENDOR_ID_APPLE)
// with products 030D (Magic Mouse), 0269 (Magic Mouse 2), 0323 (Magic Mouse 2 USB-C).
std::vector<Profile> buildCatalog() {
  return {
      {"logitech-mx-master-3s", "Logitech MX Master 3S", DeviceKind::Mouse,
       identity("MX Master 3S", "Logitech", 0x046D, 0xB034, 2, {0x00, 0x1F, 0x20}, kAppearanceMouse)},
      {"logitech-mx-master-3", "Logitech MX Master 3", DeviceKind::Mouse,
       identity("MX Master 3", "Logitech", 0x046D, 0xB023, 2, {0x00, 0x1F, 0x20}, kAppearanceMouse)},
      {"logitech-mx-master-4", "Logitech MX Master 4", DeviceKind::Mouse,
       identity("MX Master 4", "Logitech", 0x046D, 0xB042, 2, {0x00, 0x1F, 0x20}, kAppearanceMouse)},
      {"logitech-mx-master-2s", "Logitech MX Master 2S", DeviceKind::Mouse,
       identity("MX Master 2S", "Logitech", 0x046D, 0xB019, 2, {0x00, 0x1F, 0x20}, kAppearanceMouse)},
      {"logitech-mx-anywhere-3s", "Logitech MX Anywhere 3S", DeviceKind::Mouse,
       identity("MX Anywhere 3S", "Logitech", 0x046D, 0xB037, 2, {0x00, 0x1F, 0x20}, kAppearanceMouse)},
      {"logitech-mx-anywhere-3", "Logitech MX Anywhere 3", DeviceKind::Mouse,
       identity("MX Anywhere 3", "Logitech", 0x046D, 0xB025, 2, {0x00, 0x1F, 0x20}, kAppearanceMouse)},
      {"logitech-mx-vertical", "Logitech MX Vertical", DeviceKind::Mouse,
       identity("MX Vertical", "Logitech", 0x046D, 0xB020, 2, {0x00, 0x1F, 0x20}, kAppearanceMouse)},
      {"logitech-m720-triathlon", "Logitech M720 Triathlon", DeviceKind::Mouse,
       identity("M720 Triathlon", "Logitech", 0x046D, 0xB015, 2, {0x00, 0x1F, 0x20}, kAppearanceMouse)},
      {"razer-orochi-v2", "Razer Orochi V2", DeviceKind::Mouse,
       identity("Razer Orochi V2", "Razer", 0x1532, 0x0095, 2, {0x44, 0x5E, 0xCD}, kAppearanceMouse)},
      {"apple-magic-mouse", "Apple Magic Mouse", DeviceKind::Mouse,
       identity("Magic Mouse", "Apple Inc.", 0x004C, 0x030D, 1, {0x58, 0xAD, 0x12}, kAppearanceMouse)},
      {"apple-magic-mouse-2", "Apple Magic Mouse 2", DeviceKind::Mouse,
       identity("Magic Mouse 2", "Apple Inc.", 0x004C, 0x0269, 1, {0x58, 0xAD, 0x12}, kAppearanceMouse)},
      {"apple-magic-mouse-2-usbc", "Apple Magic Mouse (USB-C)", DeviceKind::Mouse,
       identity("Magic Mouse", "Apple Inc.", 0x004C, 0x0323, 1, {0x58, 0xAD, 0x12}, kAppearanceMouse)},
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
