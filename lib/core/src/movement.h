#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "config.h"
#include "platform.h"

namespace jig {

struct Step {
  enum class Kind : uint8_t { Move, Click, Scroll, KeyDown, KeyUp, Wait };
  Kind kind;
  int8_t dx;
  int8_t dy;
  int8_t notches;
  uint16_t waitMs;  // pause after the step
  uint8_t key = 0;   // KeyDown: HID usage
  uint8_t mods = 0;  // KeyDown: modifier bitmask
};

struct Plan {
  std::vector<Step> steps;
};

// Net Move displacement of the returned plan is exactly (0,0).
Plan planActivity(const Config& config, Rng& rng);

// Appends a human-like stroke from (0,0) to exactly (dx,dy), |dx|,|dy| <= 2000; nothing for (0,0).
void appendHumanMove(std::vector<Step>& steps, int dx, int dy, Rng& rng);

// Stops as soon as the mouse is disconnected; returns the number of steps processed.
// Key steps are skipped (their wait is still honored) when `keyboard` is null.
size_t runPlan(const Plan& plan, MouseSink& mouse, KeyboardSink* keyboard, const Platform& platform,
               const std::function<void(uint32_t)>& sleepMs);
size_t runPlan(const Plan& plan, MouseSink& mouse, const Platform& platform,
               const std::function<void(uint32_t)>& sleepMs);

}  // namespace jig
