#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace jig {

constexpr uint8_t kModCtrl = 0x01;
constexpr uint8_t kModShift = 0x02;
constexpr uint8_t kModAlt = 0x04;
constexpr uint8_t kModGui = 0x08;

struct KeyChord {
  uint8_t mods;
  uint8_t usage;  // HID usage page 0x07
};

// "a", "F15", "Enter", "Ctrl+Alt+Delete"... (case-insensitive, US layout). Modifier prefixes: Ctrl, Shift, Alt, Gui.
std::optional<KeyChord> keyFromName(const std::string& name);

// Printable ASCII plus '\n' and '\t', US layout. Any other byte makes the whole text invalid.
std::optional<std::vector<KeyChord>> charsToChords(const std::string& text);

}  // namespace jig
