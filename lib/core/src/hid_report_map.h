#pragma once

#include <cstdint>
#include <vector>

namespace jig {

// Composite HID report descriptor: mouse = report ID 1 (4-byte input), keyboard = report ID 2
// (8-byte input: modifiers, reserved, 6 keys). Empty when neither is requested.
std::vector<uint8_t> buildReportMap(bool mouse, bool keyboard);

}  // namespace jig
