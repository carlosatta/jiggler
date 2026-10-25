#pragma once

#include "led_logic.h"

class Led {
 public:
  void begin();
  void update(const jig::LedContext& context);
};
