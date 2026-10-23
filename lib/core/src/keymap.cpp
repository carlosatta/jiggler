#include "keymap.h"

#include <cctype>
#include <cstring>

namespace jig {

namespace {

struct NamedKey {
  const char* name;
  uint8_t usage;
};

const NamedKey kNamedKeys[] = {
    {"enter", 0x28}, {"esc", 0x29},   {"escape", 0x29}, {"backspace", 0x2A}, {"tab", 0x2B},
    {"space", 0x2C}, {"delete", 0x4C}, {"right", 0x4F},  {"left", 0x50},      {"down", 0x51},
    {"up", 0x52},    {"home", 0x4A},  {"end", 0x4D},    {"pageup", 0x4B},    {"pagedown", 0x4E},
};

std::string lower(const std::string& s) {
  std::string out = s;
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

std::optional<uint8_t> modifierBit(const std::string& part) {
  if (part == "ctrl") return kModCtrl;
  if (part == "shift") return kModShift;
  if (part == "alt") return kModAlt;
  if (part == "gui") return kModGui;
  return std::nullopt;
}

std::optional<uint8_t> usageForName(const std::string& part) {
  if (part.size() == 1) {
    const char c = part[0];
    if (c >= 'a' && c <= 'z') return static_cast<uint8_t>(0x04 + (c - 'a'));
    if (c >= '1' && c <= '9') return static_cast<uint8_t>(0x1E + (c - '1'));
    if (c == '0') return 0x27;
    return std::nullopt;
  }
  if (part[0] == 'f' && part.size() <= 3 && part[1] >= '1' && part[1] <= '9') {
    int n = 0;
    for (size_t i = 1; i < part.size(); i++) {
      if (part[i] < '0' || part[i] > '9') return std::nullopt;
      n = n * 10 + (part[i] - '0');
    }
    if (n >= 1 && n <= 12) return static_cast<uint8_t>(0x3A + (n - 1));
    if (n >= 13 && n <= 24) return static_cast<uint8_t>(0x68 + (n - 13));
    return std::nullopt;
  }
  for (const NamedKey& k : kNamedKeys)
    if (part == k.name) return k.usage;
  return std::nullopt;
}

// US-layout symbols: {unshifted char, shifted char, usage}
struct Symbol {
  char plain;
  char shifted;
  uint8_t usage;
};

const Symbol kSymbols[] = {
    {'-', '_', 0x2D}, {'=', '+', 0x2E}, {'[', '{', 0x2F}, {']', '}', 0x30}, {'\\', '|', 0x31},
    {';', ':', 0x33}, {'\'', '"', 0x34}, {'`', '~', 0x35}, {',', '<', 0x36}, {'.', '>', 0x37},
    {'/', '?', 0x38},
};

const char kShiftedDigits[] = ")!@#$%^&*(";  // index = digit

}  // namespace

std::optional<KeyChord> keyFromName(const std::string& name) {
  if (name.empty()) return std::nullopt;
  const std::string text = lower(name);
  std::vector<std::string> parts;
  size_t start = 0;
  for (;;) {
    const size_t plus = text.find('+', start);
    parts.push_back(text.substr(start, plus == std::string::npos ? std::string::npos : plus - start));
    if (plus == std::string::npos) break;
    start = plus + 1;
  }
  uint8_t mods = 0;
  for (size_t i = 0; i + 1 < parts.size(); i++) {
    auto bit = modifierBit(parts[i]);
    if (!bit) return std::nullopt;
    mods |= *bit;
  }
  if (parts.back().empty()) return std::nullopt;
  auto usage = usageForName(parts.back());
  if (!usage) return std::nullopt;
  return KeyChord{mods, *usage};
}

std::optional<std::vector<KeyChord>> charsToChords(const std::string& text) {
  std::vector<KeyChord> out;
  for (unsigned char c : text) {
    if (c == '\n') out.push_back({0, 0x28});
    else if (c == '\t') out.push_back({0, 0x2B});
    else if (c == ' ') out.push_back({0, 0x2C});
    else if (c >= 'a' && c <= 'z') out.push_back({0, static_cast<uint8_t>(0x04 + (c - 'a'))});
    else if (c >= 'A' && c <= 'Z') out.push_back({kModShift, static_cast<uint8_t>(0x04 + (c - 'A'))});
    else if (c >= '1' && c <= '9') out.push_back({0, static_cast<uint8_t>(0x1E + (c - '1'))});
    else if (c == '0') out.push_back({0, 0x27});
    else {
      bool found = false;
      for (int d = 0; d <= 9 && !found; d++)
        if (kShiftedDigits[d] == static_cast<char>(c)) {
          out.push_back({kModShift, static_cast<uint8_t>(d == 0 ? 0x27 : 0x1E + (d - 1))});
          found = true;
        }
      for (const Symbol& s : kSymbols) {
        if (found) break;
        if (s.plain == static_cast<char>(c)) { out.push_back({0, s.usage}); found = true; }
        else if (s.shifted == static_cast<char>(c)) { out.push_back({kModShift, s.usage}); found = true; }
      }
      if (!found) return std::nullopt;
    }
  }
  return out;
}

}  // namespace jig
