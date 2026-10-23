#include <unity.h>

#include <map>
#include <string>
#include <vector>

#include "fakes.h"
#include "hid_report_map.h"
#include "keymap.h"
#include "movement.h"

using namespace jig;

void setUp() {}
void tearDown() {}

namespace {

// The mouse-only descriptor exactly as shipped in src/ble_mouse.cpp before the composite change.
const uint8_t kLegacyMouseMap[] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x01, 0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03,
    0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05, 0x81, 0x03, 0x05, 0x01,
    0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08,
    0x95, 0x03, 0x81, 0x06, 0xC0, 0xC0,
};

struct Walk {
  int collections = 0;
  int endCollections = 0;
  int depth = 0;
  bool balanced = true;
  std::map<int, int> inputBitsPerReportId;  // report ID -> input bits
};

// Minimal HID report-descriptor item walker (short items only).
Walk walk(const std::vector<uint8_t>& d) {
  Walk w;
  int reportId = 0, size = 0, count = 0;
  for (size_t i = 0; i < d.size();) {
    const uint8_t prefix = d[i];
    TEST_ASSERT_TRUE_MESSAGE(prefix != 0xFE, "long items not expected");
    size_t dataLen = prefix & 3;
    if (dataLen == 3) dataLen = 4;
    const int type = (prefix >> 2) & 3, tag = (prefix >> 4) & 0xF;
    TEST_ASSERT_TRUE_MESSAGE(i + 1 + dataLen <= d.size(), "truncated item");
    int value = 0;
    for (size_t b = 0; b < dataLen; b++) value |= d[i + 1 + b] << (8 * b);
    if (type == 0) {  // main
      if (tag == 0xA) { w.collections++; w.depth++; }
      if (tag == 0xC) { w.endCollections++; if (--w.depth < 0) w.balanced = false; }
      if (tag == 0x8) w.inputBitsPerReportId[reportId] += size * count;
    } else if (type == 1) {  // global
      if (tag == 0x8) reportId = value;
      if (tag == 0x7) size = value;
      if (tag == 0x9) count = value;
    }
    i += 1 + dataLen;
  }
  return w;
}

}  // namespace

void test_key_names() {
  TEST_ASSERT_EQUAL_HEX8(4, keyFromName("a")->usage);
  TEST_ASSERT_EQUAL_HEX8(29, keyFromName("Z")->usage);
  TEST_ASSERT_EQUAL_HEX8(30, keyFromName("1")->usage);
  TEST_ASSERT_EQUAL_HEX8(39, keyFromName("0")->usage);
  TEST_ASSERT_EQUAL_HEX8(0x3A, keyFromName("F1")->usage);
  TEST_ASSERT_EQUAL_HEX8(0x45, keyFromName("f12")->usage);
  TEST_ASSERT_EQUAL_HEX8(0x6A, keyFromName("F15")->usage);
  TEST_ASSERT_EQUAL_HEX8(0x73, keyFromName("f24")->usage);
  TEST_ASSERT_EQUAL_HEX8(0x28, keyFromName("Enter")->usage);
  TEST_ASSERT_EQUAL_HEX8(0x29, keyFromName("esc")->usage);
  TEST_ASSERT_EQUAL_HEX8(0x2B, keyFromName("Tab")->usage);
  TEST_ASSERT_EQUAL_HEX8(0x2C, keyFromName("Space")->usage);
  TEST_ASSERT_EQUAL_HEX8(0x2A, keyFromName("Backspace")->usage);
  TEST_ASSERT_EQUAL_HEX8(0x4F, keyFromName("Right")->usage);
  TEST_ASSERT_EQUAL_HEX8(0x52, keyFromName("Up")->usage);
  TEST_ASSERT_EQUAL_HEX8(0x4E, keyFromName("PageDown")->usage);
  auto cad = keyFromName("Ctrl+Alt+Delete");
  TEST_ASSERT_TRUE(cad.has_value());
  TEST_ASSERT_EQUAL_HEX8(0x05, cad->mods);
  TEST_ASSERT_EQUAL_HEX8(0x4C, cad->usage);
  auto sa = keyFromName("shift+a");
  TEST_ASSERT_EQUAL_HEX8(0x02, sa->mods);
  TEST_ASSERT_EQUAL_HEX8(0x04, sa->usage);
  TEST_ASSERT_EQUAL_HEX8(0x08, keyFromName("Gui+r")->mods);
  TEST_ASSERT_EQUAL_HEX8(0x00, keyFromName("a")->mods);
  for (const char* bad : {"", "F25", "F0", "F01", "Ctrl+", "+a", "a++b", "Hyper+a", "Ctrl+Hyper", "😀", "ab", "Ctrl+Ctrl"})
    if (std::string(bad) == "Ctrl+Ctrl") TEST_ASSERT_FALSE_MESSAGE(keyFromName(bad).has_value(), bad);
    else TEST_ASSERT_FALSE_MESSAGE(keyFromName(bad).has_value(), bad);
}

void test_chars_to_chords() {
  auto hi = charsToChords("Hi!");
  TEST_ASSERT_TRUE(hi.has_value());
  TEST_ASSERT_EQUAL_UINT32(3, hi->size());
  TEST_ASSERT_EQUAL_HEX8(0x02, (*hi)[0].mods);
  TEST_ASSERT_EQUAL_HEX8(0x0B, (*hi)[0].usage);  // h
  TEST_ASSERT_EQUAL_HEX8(0x00, (*hi)[1].mods);
  TEST_ASSERT_EQUAL_HEX8(0x0C, (*hi)[1].usage);  // i
  TEST_ASSERT_EQUAL_HEX8(0x02, (*hi)[2].mods);
  TEST_ASSERT_EQUAL_HEX8(0x1E, (*hi)[2].usage);  // ! = shift+1
  auto tab = charsToChords("a\tb\n");
  TEST_ASSERT_EQUAL_UINT32(4, tab->size());
  TEST_ASSERT_EQUAL_HEX8(0x2B, (*tab)[1].usage);
  TEST_ASSERT_EQUAL_HEX8(0x28, (*tab)[3].usage);
  auto sym = charsToChords("-_ /?");
  TEST_ASSERT_EQUAL_HEX8(0x2D, (*sym)[0].usage);
  TEST_ASSERT_EQUAL_HEX8(0x02, (*sym)[1].mods);
  TEST_ASSERT_EQUAL_HEX8(0x2C, (*sym)[2].usage);
  TEST_ASSERT_EQUAL_HEX8(0x38, (*sym)[4].usage);
  auto empty = charsToChords("");
  TEST_ASSERT_TRUE(empty.has_value());
  TEST_ASSERT_EQUAL_UINT32(0, empty->size());
}

void test_chars_to_chords_covers_all_printable_ascii() {
  std::string all;
  for (int c = 0x20; c <= 0x7E; c++) all += static_cast<char>(c);
  auto chords = charsToChords(all);
  TEST_ASSERT_TRUE(chords.has_value());
  TEST_ASSERT_EQUAL_UINT32(95, chords->size());
  for (const KeyChord& k : *chords) TEST_ASSERT_TRUE(k.usage >= 0x04 && k.usage <= 0x38);
}

void test_chars_to_chords_rejects_non_ascii_and_control() {  // RF3
  for (const char* bad : {"é", "😀", "\x01", "\x7f", "ok\x1b", "tab\ttext\x80"})
    TEST_ASSERT_FALSE_MESSAGE(charsToChords(bad).has_value(), bad);
}

void test_report_map_mouse_only_matches_legacy() {  // RF4
  auto map = buildReportMap(true, false);
  TEST_ASSERT_EQUAL_UINT32(sizeof(kLegacyMouseMap), map.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kLegacyMouseMap, map.data(), sizeof(kLegacyMouseMap));
}

void test_report_map_structure() {
  TEST_ASSERT_TRUE(buildReportMap(false, false).empty());

  Walk mouse = walk(buildReportMap(true, false));
  TEST_ASSERT_TRUE(mouse.balanced);
  TEST_ASSERT_EQUAL_INT(mouse.collections, mouse.endCollections);
  TEST_ASSERT_EQUAL_UINT32(1, mouse.inputBitsPerReportId.size());
  TEST_ASSERT_EQUAL_INT(32, mouse.inputBitsPerReportId[1]);  // 4 bytes

  Walk kb = walk(buildReportMap(false, true));
  TEST_ASSERT_TRUE(kb.balanced);
  TEST_ASSERT_EQUAL_INT(kb.collections, kb.endCollections);
  TEST_ASSERT_EQUAL_UINT32(1, kb.inputBitsPerReportId.size());
  TEST_ASSERT_EQUAL_INT(64, kb.inputBitsPerReportId[2]);  // 8 bytes

  auto both = buildReportMap(true, true);
  Walk w = walk(both);
  TEST_ASSERT_TRUE(w.balanced);
  TEST_ASSERT_EQUAL_INT(w.collections, w.endCollections);
  TEST_ASSERT_EQUAL_UINT32(2, w.inputBitsPerReportId.size());
  TEST_ASSERT_EQUAL_INT(32, w.inputBitsPerReportId[1]);
  TEST_ASSERT_EQUAL_INT(64, w.inputBitsPerReportId[2]);
  // the mouse collection is unchanged inside the composite
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kLegacyMouseMap, both.data(), sizeof(kLegacyMouseMap));
}

void test_keyboard_map_covers_f24() {
  // Logical maximum of the key array must reach F24 (0x73) so F13-F24 keep-alive keys are legal.
  auto kb = buildReportMap(false, true);
  bool found = false;
  for (size_t i = 0; i + 1 < kb.size(); i++)
    if (kb[i] == 0x25 && kb[i + 1] >= 0x73) found = true;
  TEST_ASSERT_TRUE(found);
}

void test_run_plan_dispatches_keys() {
  FakeClock clock;
  FakeMouse mouse;
  FakeKeyboard kb;
  Plan plan;
  plan.steps = {
      {Step::Kind::KeyDown, 0, 0, 0, 40, 0x6A, 0x00},
      {Step::Kind::KeyUp, 0, 0, 0, 100},
      {Step::Kind::Move, 1, 2, 0, 20},
      {Step::Kind::KeyDown, 0, 0, 0, 30, 0x04, 0x02},
      {Step::Kind::KeyUp, 0, 0, 0, 10},
  };
  std::vector<uint32_t> sleeps;
  size_t done = runPlan(plan, mouse, &kb, clock, [&](uint32_t ms) { sleeps.push_back(ms); });
  TEST_ASSERT_EQUAL_UINT32(5, done);
  TEST_ASSERT_EQUAL_UINT32(4, kb.calls.size());
  TEST_ASSERT_EQUAL_STRING("down 0,106", kb.calls[0].c_str());  // mods 0, usage 0x6A (F15)
  TEST_ASSERT_EQUAL_STRING("up", kb.calls[1].c_str());
  TEST_ASSERT_EQUAL_STRING("down 2,4", kb.calls[2].c_str());    // mods 2 (shift), usage 4
  TEST_ASSERT_EQUAL_UINT32(1, mouse.calls.size());
  const uint32_t expected[] = {40, 100, 20, 30, 10};
  TEST_ASSERT_EQUAL_UINT32(5, sleeps.size());
  for (size_t i = 0; i < 5; i++) TEST_ASSERT_EQUAL_UINT32(expected[i], sleeps[i]);
}

void test_run_plan_skips_keys_without_keyboard() {  // RF4
  FakeClock clock;
  FakeMouse mouse;
  Plan plan;
  plan.steps = {
      {Step::Kind::KeyDown, 0, 0, 0, 40, 0x6A, 0x00},
      {Step::Kind::KeyUp, 0, 0, 0, 100},
      {Step::Kind::Move, 3, 4, 0, 20},
  };
  std::vector<uint32_t> sleeps;
  size_t done = runPlan(plan, mouse, nullptr, clock, [&](uint32_t ms) { sleeps.push_back(ms); });
  TEST_ASSERT_EQUAL_UINT32(3, done);
  TEST_ASSERT_EQUAL_UINT32(1, mouse.calls.size());
  TEST_ASSERT_EQUAL_STRING("move 3,4", mouse.calls[0].c_str());
  TEST_ASSERT_EQUAL_UINT32(3, sleeps.size());  // waits are still honored
  // the 4-argument form keeps working and also skips key steps
  FakeMouse mouse2;
  TEST_ASSERT_EQUAL_UINT32(3, runPlan(plan, mouse2, clock, [](uint32_t) {}));
  TEST_ASSERT_EQUAL_UINT32(1, mouse2.calls.size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_key_names);
  RUN_TEST(test_chars_to_chords);
  RUN_TEST(test_chars_to_chords_covers_all_printable_ascii);
  RUN_TEST(test_chars_to_chords_rejects_non_ascii_and_control);
  RUN_TEST(test_report_map_mouse_only_matches_legacy);
  RUN_TEST(test_report_map_structure);
  RUN_TEST(test_keyboard_map_covers_f24);
  RUN_TEST(test_run_plan_dispatches_keys);
  RUN_TEST(test_run_plan_skips_keys_without_keyboard);
  return UNITY_END();
}
