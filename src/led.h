#pragma once

#include <cstdint>

#include "controller.h"

class Led {
 public:
  void begin();
  void update(const jig::Status& status, uint32_t nowMs);
};
