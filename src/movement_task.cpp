#include "movement_task.h"

#include "movement.h"

namespace {

struct Context {
  jig::Controller* controller;
  BleMouse* mouse;
  ArduinoPlatform* platform;
};

Context ctx;

void sleepMs(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

void movementTask(void*) {
  for (;;) {
    if (auto plan = ctx.controller->tick()) {
#ifdef JIG_DEBUG
      const uint32_t started = millis();
      const size_t done = jig::runPlan(*plan, *ctx.mouse, ctx.mouse, *ctx.platform, sleepMs);
      Serial.printf("plan: %u/%u steps in %lu ms, stack high-water %u bytes\n", (unsigned)done,
                    (unsigned)plan->steps.size(), (unsigned long)(millis() - started),
                    (unsigned)uxTaskGetStackHighWaterMark(nullptr));
#else
      jig::runPlan(*plan, *ctx.mouse, ctx.mouse, *ctx.platform, sleepMs);
#endif
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

}  // namespace

void startMovementTask(jig::Controller& controller, BleMouse& mouse, ArduinoPlatform& platform) {
  ctx = {&controller, &mouse, &platform};
  xTaskCreatePinnedToCore(movementTask, "movement", 4096, nullptr, 2, nullptr, 1);
}
