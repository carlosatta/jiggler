#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "config.h"
#include "platform.h"

namespace jig {

struct Step {
  enum class Kind : uint8_t { Move, Click, Scroll };
  Kind kind;
  int8_t dx;
  int8_t dy;
  int8_t notches;
  uint16_t waitMs;  // pause after the step
};

struct Plan {
  std::vector<Step> steps;
};

// Net Move displacement of the returned plan is exactly (0,0).
Plan planActivity(const Config& config, Rng& rng);

// Stops as soon as the mouse is disconnected; returns the number of steps executed.
size_t runPlan(const Plan& plan, MouseSink& mouse, const Platform& platform,
               const std::function<void(uint32_t)>& sleepMs);

}  // namespace jig
