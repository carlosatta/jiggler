# Jiggler ESP32 Firmware Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** ESP32 firmware that acts as a BLE mouse presenting as a Logitech MX Master, jiggles the cursor with human-like random movement, and is controllable over WiFi via dashboard, REST API and MCP server.

**Architecture:** All logic (config, movement planning, run-state/scheduling, JSON, REST routing, MCP) lives in `lib/core`, free of Arduino headers and unit-tested on the host (`pio test -e native`). Hardware adapters in `src/` (BLE HID, NVS, LED, WiFi, HTTP) are thin and verified on a real board. One `Controller` is the single control surface; dashboard, REST and MCP all call it. Movement runs in its own FreeRTOS task, so WiFi problems never stall the mouse.

**Tech Stack:** PlatformIO, Arduino framework on ESP32 (`esp32dev`), C++17, NimBLE-Arduino, ArduinoJson 7, mathieucarbou/ESPAsyncWebServer, tzapu/WiFiManager, Unity (host tests).

**Spec:** [features.md](../../../features.md)

## Global Constraints

- BLE name `MX Master 3S`, manufacturer `Logitech`, PnP ID vendor source USB (0x02), VID `0x046D`, PID `0xB034`, appearance `0x03C2` (HID mouse). Standard HID mouse only; no HID++.
- BLE public address prefix `00:1F:20` (Logitech OUI; verify against the IEEE registry in Task 6); last 3 bytes derived from the chip's eFuse MAC so the address is stable across reboots.
- Mouse works with no host software; BLE operation never depends on WiFi state.
- Config defaults: `minIntervalSec=30`, `maxIntervalSec=120`, `amplitudePx=150`, `clickProbability=0.0` (clicks off by default; opt-in), `scrollProbability=0.10`. Limits: intervals 5..3600 s with `max >= min`; `amplitudePx` 5..600; probabilities 0..1.
- Movement: intervals uniformly random in `[min,max]`; net HID displacement of every plan is exactly (0,0); step period 20 ms; per-step deltas fit `int8`.
- Run state on boot is `Running`; config persists in NVS; run state and counters do not.
- WiFi provisioning AP name `Jiggler-Setup` (open); credentials never in source.
- LED on GPIO 2: 1 Hz blink when mouse disconnected, solid when connected, 80 ms off-flash per movement.
- Auth: every `/api/*` and `/mcp` request needs `Authorization: Bearer <token>`; missing/wrong → 401 `unauthorized`. The token is a custom field in the WiFi setup portal (pre-filled with a random 16-hex value, stored in NVS); if no token has been stored, all API/MCP requests are 401. `GET /` (dashboard HTML) is public and static.
- API errors are machine-readable codes in English (`{"error":"<code>"}`); dashboard text is Italian.
- Request bodies larger than 1024 bytes are rejected with 413 `payload_too_large`.
- Namespace `jig`; core files in `lib/core/src/`, tests in `test/test_<area>/test_main.cpp`.

## Review Focus

- [RF1] Clock wraparound: `millis()` wraps at 2^32 ms (~49.7 days); scheduling must keep firing across the wrap (Task 3).
- [RF2] Partial config update that is individually valid but inconsistent with stored values (lowering `maxIntervalSec` below the stored min) must be rejected and leave config untouched (Tasks 1, 4).
- [RF3] Malformed JSON, wrong types (`"abc"`, `30.5`, `null`), out-of-range numbers (`70000`) or oversized body on `PUT /api/config` and `/mcp` must produce 4xx/JSON-RPC errors, never a crash or partial apply (Tasks 4, 5).
- [RF4] Mouse disconnecting mid-plan must stop the plan immediately with no further HID calls (Task 2).
- [RF5] MCP: unknown method, unknown tool, missing `id` (notification) and batch-less garbage must yield correct JSON-RPC errors / empty 202 (Task 5).

---

### Task 1: Scaffold, core interfaces, config validation

**Files:**
- Create: `platformio.ini`, `.gitignore` (`.pio/`, `.vscode/`), `lib/core/src/platform.h`, `lib/core/src/config.h`, `lib/core/src/config.cpp`, `test/support/fakes.h`, `test/test_config/test_main.cpp`

**Interfaces:**
- Produces (`platform.h`): `struct Rng { virtual uint32_t next()=0; }`; `double rand01(Rng&)` ([0,1)); `int32_t randRange(Rng&, int32_t lo, int32_t hi)` (inclusive); `struct Platform { virtual uint32_t nowMs() const=0; virtual uint32_t unixSeconds() const=0 /*0 = clock not synced*/; virtual bool mouseConnected() const=0; virtual bool wifiConnected() const=0; }`; `struct MouseSink { virtual void move(int8_t dx,int8_t dy)=0; virtual void click()=0; virtual void scroll(int8_t notches)=0; }`; `struct ConfigStore { virtual Config load()=0; virtual void save(const Config&)=0; }`.
- Produces (`config.h`): `struct Config {uint32_t minIntervalSec; uint32_t maxIntervalSec; uint16_t amplitudePx; float clickProbability; float scrollProbability;}` with the defaults above; `struct ConfigPatch` with `std::optional<int64_t>` for the three integer fields and `std::optional<double>` for the two probabilities (wide types so out-of-range input is rejected, not truncated); `struct ValidationError {std::string field; std::string message;}`; `std::optional<ValidationError> applyPatch(const Config& base, const ConfigPatch& patch, Config& out)` — `nullopt` on success, `out` untouched on failure; checks the merged result against the limits.
- Produces (`fakes.h`): `XorShiftRng(seed)`, `FakeClock : Platform` (public settable `now`, `unix`, `mouse`, `wifi`), `FakeMouse : MouseSink` (records calls in a `std::vector<std::string>`), `MemStore : ConfigStore` (counts `saves`).

- [ ] **Step 1: Write `platformio.ini`.** Envs: `[env:esp32]` (`platform = espressif32@^6.9.0`, `board = esp32dev`, `framework = arduino`, `build_unflags = -std=gnu++11`, `build_flags = -std=gnu++17`, `lib_deps` = the four libraries in Tech Stack at `^` of current versions, `monitor_speed = 115200`) and `[env:native]` (`platform = native`, `build_flags = -std=gnu++17 -I test/support`, `lib_deps = bblanchon/ArduinoJson@^7`). `default_envs = esp32`.
- [ ] **Step 2: Write failing tests** in `test_config`: `test_defaults_match_spec`; `test_partial_patch_merges` (patch only `amplitudePx=200` keeps other fields); `test_bounds` (table: min 4 and 3601 rejected, 5 and 3600 accepted; amplitude 4/601 rejected, 5/600 accepted; probability -0.1/1.1 rejected); `test_max_below_stored_min_rejected_out_untouched` [RF2] (base min=30, patch max=10 → error field `maxIntervalSec`, `out` unchanged); `test_amplitude_70000_rejected` [RF3].
- [ ] **Step 3: Run** `pio test -e native -f test_config` → FAIL (does not compile).
- [ ] **Step 4: Implement** `platform.h`, `config.h/.cpp`, `fakes.h` per Interfaces.
- [ ] **Step 5: Run** the same command → PASS.
- [ ] **Step 6: Commit** (`git init` first; this repo has no git yet): `git add . && git commit -m "feat: scaffold PlatformIO project, core interfaces and config validation"`.

---

### Task 2: Movement planner and plan runner

**Files:**
- Create: `lib/core/src/movement.h`, `lib/core/src/movement.cpp`, `test/test_movement/test_main.cpp`

**Interfaces:**
- Consumes: `Config`, `Rng`/`rand01`/`randRange`, `MouseSink`, `Platform` (Task 1).
- Produces:
  - `struct Step { enum class Kind : uint8_t {Move, Click, Scroll} kind; int8_t dx; int8_t dy; int8_t notches; uint16_t waitMs; }` (`waitMs` = pause after the step).
  - `struct Plan { std::vector<Step> steps; }`
  - `Plan planActivity(const Config&, Rng&)`
  - `size_t runPlan(const Plan&, MouseSink&, const Platform&, const std::function<void(uint32_t)>& sleepMs)` — before each step checks `platform.mouseConnected()` and stops if false; returns steps executed; sleeps `waitMs` after each executed step.

- [ ] **Step 1: Write failing tests** (loop over 200 seeds and amplitudes {5,150,600} unless stated):
  - `test_net_displacement_is_zero`: sum of `dx` and of `dy` over Move steps == 0.
  - `test_reach_matches_amplitude`: max distance from origin of the cumulative position is ≤ amplitude+2, and ≥ 0.3*amplitude−2 when amplitude ≥ 50.
  - `test_path_is_curved`: amplitude 150, 50 seeds; in ≥ 80% of plans the out-stroke deviates > 2 px from its start→peak chord.
  - `test_ease_in_out`: out-stroke mean |step| over its first 3 and last 3 Move steps is < 50% of the max |step| in between.
  - `test_steps_are_valid`: all `waitMs > 0`, Move steps' `waitMs == 20` except the dwell step at the stroke end (200..1500).
  - `test_intervals_not_constant`: planned total durations over 50 seeds are not all equal.
  - `test_click_scroll_probabilities`: both 0 → no Click/Scroll; both 1 → exactly one Click and Scroll steps whose `notches` sum to 0 (scroll down then back).
  - `test_run_plan_stops_on_disconnect` [RF4]: `FakeMouse` + `FakeClock` whose `mouse` flips false after 3 executed steps → `runPlan` returns 3 and the mouse recorded exactly 3 calls.
  - `test_run_plan_sleeps_wait_ms`: sleep callback receives each executed step's `waitMs` in order.
- [ ] **Step 2: Run** `pio test -e native -f test_movement` → FAIL.
- [ ] **Step 3: Implement `planActivity`** in `movement.cpp`. Algorithm (not determined by the tests): pick angle uniform and distance uniform in `[0.3,1.0]*amplitudePx`; cubic Bézier from (0,0) to the target with two control points offset perpendicular to the chord by random ±(0.1..0.3)*distance; stroke duration `clamp(300 + 1.2*distance, 300, 1200)` ms, sampled every 20 ms with minimum-jerk easing `s(t)=10t³−15t⁴+6t⁵`; add jitter ≤ 1 px scaled by `sin(πt)`; emit `round(pos_i)−round(pos_{i−1})` so rounded positions telescope and the final position is exact. Sequence: out-stroke → dwell (200..1500 ms) → optional Click (probability) and optional scroll (n=1..3 notches, wait 400..1500 ms, then −n) → return stroke to (0,0) with the same construction. Implement `runPlan` per Interfaces.
- [ ] **Step 4: Run** the same command → PASS.
- [ ] **Step 5: Commit**: `git add lib/core test && git commit -m "feat: human-like movement planner and plan runner"`.

---

### Task 3: Controller (state machine, scheduling)

**Files:**
- Create: `lib/core/src/controller.h`, `lib/core/src/controller.cpp`, `test/test_controller/test_main.cpp`

**Interfaces:**
- Consumes: `Platform`, `Rng`, `ConfigStore`, `Config`, `ConfigPatch`, `applyPatch`, `Plan`, `planActivity` (Tasks 1–2).
- Produces:
  - `enum class RunState {Stopped, Running, Paused}`
  - `struct Status {RunState runState; bool mouseConnected; bool wifiConnected; std::optional<uint32_t> msToNextMove /*only Running*/; uint32_t moveCount; std::optional<uint32_t> lastMoveAgoMs; std::optional<uint32_t> lastMoveUnix /*only if unixSeconds()!=0*/; Config config;}`
  - `struct CmdResult {bool ok; std::string error; std::string field; std::string message;}` (`error` is a code: `mouse_not_connected`, `invalid_config`)
  - `class Controller { Controller(Platform&, Rng&, ConfigStore&); void begin(); Status status() const; void start(); void pause(); void stop(); CmdResult moveNow(); CmdResult updateConfig(const ConfigPatch&); std::optional<Plan> tick(); }` — thread-safe via an internal `std::mutex`; `tick()` is called every ~50 ms by the movement task and returns the plan to execute.

Semantics pinned: `begin()` loads config, state Running, schedules next move uniformly in `[min,max]` s. `start()` from Stopped schedules fresh; from Paused restores the remaining time; `pause()` stores remaining; `stop()` clears the schedule; all idempotent. `moveNow()` fails with `mouse_not_connected` if disconnected, otherwise sets a pending flag served by the next `tick()` in any run state. `tick()` serves the pending flag first, else fires when Running and due and connected (a due move stays due while disconnected and fires on connect); every fired plan increments `moveCount`, records the time and (when Running) reschedules. Due-check uses signed difference `(int32_t)(now − due) >= 0`. A valid `updateConfig` saves via the store and, while Running, reschedules using the new bounds; an invalid one saves nothing.

- [ ] **Step 1: Write failing tests** (`FakeClock`, `XorShiftRng`, `MemStore`):
  - `test_begin_is_running_with_interval_in_bounds` (min=max=10 → `msToNextMove == 10000`).
  - `test_tick_fires_only_when_due_and_reschedules` (nullopt at 9999 ms, a Plan at 10000, `moveCount==1`, `lastMoveAgoMs==0`, `msToNextMove` back to 10000).
  - `test_pause_freezes_and_resume_restores_remaining` (pause at 4000 ms elapsed, advance 60 s, no fire; resume → `msToNextMove==6000`).
  - `test_stop_clears_schedule` (`msToNextMove` nullopt; `tick()` never fires).
  - `test_due_move_waits_for_connection` (disconnected past due → nullopt; connect → fires on next tick).
  - `test_move_now_disconnected_rejected` / `test_move_now_served_even_when_stopped`.
  - `test_clock_wraparound` [RF1]: `now=0xFFFFFF00`, min=max=5 s; begin; advance across 2^32 → fires exactly when 5 s elapsed, then fires again 5 s later.
  - `test_update_config_invalid_changes_nothing` (store `saves==0`, config unchanged) and `test_update_config_valid_saves_and_reschedules`.
  - `test_last_move_unix_null_when_clock_unsynced` (`unix=0` → nullopt, not 0).
- [ ] **Step 2: Run** `pio test -e native -f test_controller` → FAIL.
- [ ] **Step 3: Implement** `controller.h/.cpp` per the semantics above.
- [ ] **Step 4: Run** the same command → PASS.
- [ ] **Step 5: Commit**: `git add lib/core test && git commit -m "feat: controller with run states and scheduling"`.

---

### Task 4: JSON codec and REST router

**Files:**
- Create: `lib/core/src/json_codec.h/.cpp`, `lib/core/src/api_router.h/.cpp`, `test/test_api/test_main.cpp`

**Interfaces:**
- Consumes: `Controller`, `Status`, `Config`, `ConfigPatch`, `CmdResult` (Tasks 1, 3).
- Produces:
  - `bool authorized(const std::string& authorizationHeader, const std::string& token)` — true only for `Bearer <token>` with a non-empty configured token, compared in constant time; an empty `token` never authorizes.
  - `std::string statusToJson(const Status&)` → `{"state":"running|paused|stopped","mouseConnected":bool,"wifiConnected":bool,"msToNextMove":n|null,"moveCount":n,"lastMoveAgoMs":n|null,"lastMoveAt":unix|null,"config":{...}}`
  - `std::string configToJson(const Config&)` → keys exactly the `Config` field names.
  - `bool parseConfigPatch(const std::string& body, ConfigPatch& out, std::string& err)` — rejects non-JSON, non-object, unknown types (string/null/bool; integer fields given a fractional number).
  - `struct HttpResponse {int status; std::string body;}`; `HttpResponse handleApi(Controller&, const std::string& method, const std::string& path, const std::string& body)` (body always `application/json`).
- Routes: `GET /api/status`, `POST /api/start|stop|pause` (200 + status JSON), `POST /api/move` (202 `{"queued":true}`; 409 `mouse_not_connected`), `GET /api/config`, `PUT /api/config` (partial update; 200 + config JSON; 400 `invalid_config` with `field`/`message`; 400 `bad_json`; 413 `payload_too_large` over 1024 bytes). Unknown path 404 `not_found`; known path wrong method 405 `method_not_allowed`.

- [ ] **Step 1: Write failing tests** with a real `Controller` over fakes:
  - `test_status_shape`: connected+running status contains every key above with correct types; `lastMoveAt` is `null` when unsynced.
  - `test_start_stop_pause_roundtrip`: `state` string after each call.
  - `test_move_409_when_disconnected` and `test_move_202_when_connected`.
  - `test_put_config_partial_ok`: `{"amplitudePx":200}` → 200 and persisted.
  - `test_put_config_inconsistent_rejected` [RF2]: stored min 30, body `{"maxIntervalSec":10}` → 400 `invalid_config`, field `maxIntervalSec`, config unchanged.
  - `test_put_config_bad_input` [RF3]: table of bodies → expected status and zero changes: `""`, `"{"`, `[]`, `"null"`, `{"minIntervalSec":"abc"}`, `{"minIntervalSec":30.5}`, `{"amplitudePx":null}`, `{"amplitudePx":70000}`, 1025-byte body (413).
  - `test_routing_errors`: `GET /api/nope` 404; `GET /api/start` 405.
  - `test_authorized`: `("Bearer abc","abc")` true; wrong token, missing header, `"Basic abc"`, `"Bearer"`, `"bearer abc"` false; empty configured token with `"Bearer "` false.
- [ ] **Step 2: Run** `pio test -e native -f test_api` → FAIL.
- [ ] **Step 3: Implement** with ArduinoJson 7; parse into `JsonDocument`, check `is<int64_t>()`/`is<double>()` per field before assigning.
- [ ] **Step 4: Run** the same command → PASS.
- [ ] **Step 5: Commit**: `git add lib/core test && git commit -m "feat: JSON codec and REST API router"`.

---

### Task 5: MCP server handler

**Files:**
- Create: `lib/core/src/mcp_server.h/.cpp`, `test/test_mcp/test_main.cpp`

**Interfaces:**
- Consumes: `Controller`, `statusToJson`, `configToJson`, `parseConfigPatch` (Tasks 3–4).
- Produces: `struct McpReply {int httpStatus; std::string body;}`; `McpReply handleMcp(Controller&, const std::string& body)`. Stateless Streamable-HTTP style: JSON-RPC 2.0 request in, `application/json` out.
- Methods: `initialize` (result `protocolVersion:"2025-03-26"`, `capabilities:{tools:{}}`, `serverInfo:{name:"jiggler",version:"1.0.0"}`); `ping` (`{}`); `tools/list`; `tools/call`; any message without `id` (e.g. `notifications/initialized`) → 202 with empty body.
- Tools (all with JSON-schema `inputSchema`; results are `{content:[{type:"text",text:<json string>}],isError:bool}`): `get_status` (status JSON), `start`, `pause`, `stop` (return status JSON), `move_now`, `set_config` (properties = the five config fields, all optional integers/numbers; invalid → `isError:true` with the error JSON as text).
- Errors: unparseable body → `-32700`; not an object/missing `method` → `-32600`; unknown method → `-32601`; `tools/call` with unknown tool name or non-object `arguments` → `-32602`. Error replies use HTTP 200 (HTTP 400 only for `-32700`).

- [ ] **Step 1: Write failing tests**: `test_initialize`; `test_tools_list_names` (exactly the six names); `test_call_get_status`; `test_call_set_config_valid_and_invalid` (invalid → `isError:true`, config unchanged) [RF2]; `test_call_move_now_disconnected_is_error_result`; `test_notification_returns_202_empty` [RF5]; `test_protocol_errors` [RF3, RF5]: `"not json"` → -32700, `"[]"` → -32600, unknown method → -32601, unknown tool → -32602, `arguments:"x"` → -32602, `set_config` with `{"amplitudePx":"big"}` → `isError:true`.
- [ ] **Step 2: Run** `pio test -e native -f test_mcp` → FAIL.
- [ ] **Step 3: Implement** `handleMcp` reusing the Task 4 codec for tool results so REST and MCP can never disagree.
- [ ] **Step 4: Run** the same command → PASS; also run `pio test -e native` (all suites green).
- [ ] **Step 5: Commit**: `git add lib/core test && git commit -m "feat: MCP server handler"`.

---

### Task 6: Logitech identity and BLE HID mouse

**Files:**
- Create: `lib/core/src/identity.h/.cpp`, `test/test_identity/test_main.cpp`, `src/ble_mouse.h`, `src/ble_mouse.cpp`
- Modify: `src/main.cpp` (temporary: pair + send a test move every 5 s for verification)

**Interfaces:**
- Produces (core): constants `kBleName="MX Master 3S"`, `kManufacturer="Logitech"`, `kVid=0x046D`, `kPid=0xB034`, `kLogitechOui[3]={0x00,0x1F,0x20}`; `std::array<uint8_t,6> deriveLogitechMac(const uint8_t efuseMac[6])` → OUI + last 3 bytes of `efuseMac`.
- Produces (firmware): `class BleMouse : public jig::MouseSink { void begin(); bool connected() const; void move(int8_t,int8_t) override; void click() override; void scroll(int8_t) override; }`.

- [ ] **Step 1: Write failing tests**: `test_mac_prefix_is_logitech`; `test_mac_tail_comes_from_efuse`; `test_mac_is_unicast_and_stable` (first byte bit0 == 0; same input → same output).
- [ ] **Step 2: Run** `pio test -e native -f test_identity` → FAIL. **Implement** `identity`, **run** → PASS.
- [ ] **Step 3: Verify the identity constants.** Check OUI `00:1F:20` in the IEEE OUI registry and PID `0xB034` against a real MX Master 3S (macOS: `system_profiler SPBluetoothDataType`); if either differs, correct `identity.cpp` and the Global Constraints, then rerun the test.
- [ ] **Step 4: Implement `BleMouse`** with NimBLE-Arduino: before `NimBLEDevice::init`, call `esp_base_mac_addr_set(deriveLogitechMac(efuse))` (read efuse via `esp_efuse_mac_get_default`; note this also sets the WiFi MAC) and use a public own-address; HID service with a standard report map (report 1, input: 3 buttons, `int8` X, `int8` Y, `int8` wheel — 4 bytes) and Device Information service (manufacturer, PnP ID from the constants); Just-Works bonding with bond persistence; advertise with appearance `0x03C2`; `click()` = press then release left button 20 ms apart.
- [ ] **Step 5: Verify on device** (`pio run -t upload`): the computer's Bluetooth list shows "MX Master 3S"; pairing needs no software; the temporary test move is seen on screen; after reboot the computer reconnects without re-pairing; a BLE scanner (nRF Connect) shows a `00:1F:20:…` address.
- [ ] **Step 6: Commit**: `git add lib src && git commit -m "feat: Logitech identity and BLE HID mouse"`.

---

### Task 7: Standalone jiggling (NVS, LED, movement task)

**Files:**
- Create: `lib/core/src/led_logic.h`, `test/test_led/test_main.cpp`, `src/nvs_config_store.h/.cpp`, `src/led.h/.cpp`, `src/movement_task.h/.cpp`, `src/arduino_platform.h`
- Modify: `src/main.cpp` (replace the temporary test move)

**Interfaces:**
- Consumes: `Controller`, `runPlan`, `BleMouse`, `Platform`, `ConfigStore` (Tasks 1–6).
- Produces: `bool ledLevel(bool connected, uint32_t nowMs, uint32_t lastMoveMs, bool hasMoved)` (core, pure: disconnected → on for the first 500 ms of each second; connected → on, except off during the 80 ms after `lastMoveMs`); `class NvsConfigStore : public jig::ConfigStore` (Preferences namespace `jiggler`, one key per field; missing keys → defaults); `class ArduinoPlatform : public jig::Platform` (`nowMs=millis()`, `unixSeconds()` returns 0 until `time(nullptr) >= 1700000000`, `mouseConnected` from `BleMouse`, `wifiConnected = WiFi.status()==WL_CONNECTED`); `void startMovementTask(Controller&, BleMouse&, ArduinoPlatform&)` — FreeRTOS task on core 1, priority 2, stack 4096 B, loop: `tick()` every 50 ms, and if it returns a Plan, `runPlan(..., vTaskDelay)`.

- [ ] **Step 1: Write failing tests** `test_led_blinks_when_disconnected`, `test_led_solid_when_connected`, `test_led_flashes_off_after_move` (off at `lastMoveMs+0..79`, on at +80).
- [ ] **Step 2: Run** `pio test -e native -f test_led` → FAIL; implement `led_logic.h`; run → PASS.
- [ ] **Step 3: Implement** `NvsConfigStore`, `Led` (GPIO 2, `pinMode` + `digitalWrite` from `ledLevel`, updated in `loop()`), `ArduinoPlatform`, `startMovementTask`; in `setup()`: construct everything, `controller.begin()`, `ble.begin()`, `startMovementTask(...)`. No WiFi code yet.
- [ ] **Step 4: Verify on device** (WiFi absent, serial monitor): pair; LED blinks while disconnected, solid when connected; with config min=max=5 (temporarily via `Config` defaults edit, then revert) the cursor moves every ~5 s along a curve and returns to its start; LED flashes off on each move; unpair/disconnect mid-move stops cleanly with no stuck button; after 1 hour of idle running, measure pointer drift (macOS/Windows pointer acceleration can break exact return — if drift is visible, note it in `CLAUDE.md` and open a follow-up; do not change the planner here).
- [ ] **Step 5: Commit**: `git add lib src test && git commit -m "feat: standalone jiggling with NVS config, LED and movement task"`.

---

### Task 8: WiFi provisioning, NTP, REST + MCP over HTTP

**Files:**
- Create: `src/wifi_setup.h/.cpp`, `src/http_server.h/.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `handleApi`, `handleMcp`, `Controller` (Tasks 4–5), `ArduinoPlatform` (Task 7).
- Produces: `class TokenStore { std::string load(); void save(const std::string&); }` (NVS key `apitoken`; empty if never saved); `class WifiSetup { void begin(); void loop(); }` — tzapu `WiFiManager` in non-blocking mode (`setConfigPortalBlocking(false)`, AP `Jiggler-Setup`) with a `WiFiManagerParameter` "Token API" pre-filled with a random 16-hex string (from `esp_random()`) or the stored token, saved via `TokenStore` in the save callback; `loop()` calls `process()` until connected; on first connection calls `configTime(0,0,"pool.ntp.org")` and signals the HTTP server to start (once). `class HttpServer { HttpServer(Controller&, TokenStore&); void begin(); }` — `AsyncWebServer` on port 80: for `/api/*` and `/mcp`, `authorized(header, tokenStore.load())` is checked first (401 `unauthorized` otherwise), then `/api/*` → `handleApi`, `POST /mcp` → `handleMcp`, `GET /mcp` → 405; request bodies accumulated in `onBody` and aborted with 413 beyond 1024 bytes; CORS not enabled. Dashboard route is added in Task 9. Only `WifiSetup::loop()` runs in Arduino `loop()`; the movement task is untouched.

- [ ] **Step 1: Implement** both classes and wire into `setup()`/`loop()`.
- [ ] **Step 2: Verify provisioning on device:** erase WiFi credentials, boot → AP `Jiggler-Setup` appears; submit home-network credentials from a phone; device joins; reboot → joins automatically without the portal; no credentials appear in the source tree.
- [ ] **Step 3: Verify API** (replace `$IP`, `$T` = the token entered in the portal): `curl $IP/api/status` → 401; every following call uses `-H "Authorization: Bearer $T"`; `curl` with a wrong token → 401; status returns JSON with `"wifiConnected":true` and a non-null `lastMoveAt` after a move (NTP synced); `curl -X POST $IP/api/pause` then `/api/status` shows `"state":"paused"`; `curl -X POST $IP/api/move` → 202 and the cursor moves; `curl -X PUT -d '{"maxIntervalSec":1}' $IP/api/config` → 400 and config unchanged after reboot; a 2 KB body → 413.
- [ ] **Step 4: Verify MCP:** `curl -X POST $IP/mcp -H 'content-type: application/json' -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}'` lists six tools; register `http://$IP/mcp` as an HTTP MCP server in Claude Code with header `Authorization: Bearer $T` and ask "il jiggler è attivo? quanto manca al prossimo movimento?" — it answers from `get_status`.
- [ ] **Step 5: Verify BT independent of WiFi:** switch the router off (or change WiFi password); the cursor keeps moving on schedule and the LED stays solid; restore the router → API reachable again without reboot.
- [ ] **Step 6: Commit**: `git add src && git commit -m "feat: WiFi provisioning, NTP and HTTP API/MCP"`.

---

### Task 9: Web dashboard

**Files:**
- Create: `src/dashboard_html.h` (single raw-string `kDashboardHtml`, inline CSS/JS, no external requests), `test/test_dashboard/test_main.cpp`
- Modify: `src/http_server.cpp` (add `GET /` → `kDashboardHtml`, `text/html`)

**Interfaces:**
- Consumes: the REST routes from Task 4. Italian UI.
- Produces: page that asks for the API token on first load (stored in `localStorage`, sent as `Authorization: Bearer` on every call; a 401 clears it and re-prompts) and polls `GET /api/status` every 1 s and shows: mouse connected yes/no, WiFi connected yes/no, run state, countdown to the next movement (client-side ticking between polls), movement count, last movement (clock time from `lastMoveAt`, else "n minuti fa" from `lastMoveAgoMs`); controls Avvia / Pausa / Ferma, "Muovi ora" (disabled when mouse disconnected; shows Italian text for error `mouse_not_connected`), and a config form (min, max, ampiezza, probabilità click/scroll) that `PUT`s only changed fields and shows `field`/`message` errors inline.

- [ ] **Step 1: Write failing test** `test_dashboard_only_references_real_routes`: extract every `/api/...` string from `kDashboardHtml` (include `src/dashboard_html.h` via `-I src` added to the `native` env) and assert `handleApi` returns neither 404 nor 405 for it with the method the page uses (list the `(method,path)` pairs in the test).
- [ ] **Step 2: Run** `pio test -e native -f test_dashboard` → FAIL; **write** `dashboard_html.h`; run → PASS.
- [ ] **Step 3: Verify in a browser on device** (`http://$IP/` from a phone and a laptop): every Global Constraint and spec §4 item works — live state, start/pause/stop, editing intervals and amplitude (invalid values show an inline error, valid ones persist across reboot), "Muovi ora" moves the cursor and increments the counter, "last movement" shows a clock time after NTP sync. Take a screenshot at phone width to confirm it is usable.
- [ ] **Step 4: Update `CLAUDE.md`** with the real commands (`pio run`, `pio run -t upload`, `pio test -e native`, single suite `pio test -e native -f test_<area>`, `pio device monitor`) and the lib/core vs src split; remove the "pre-implementation" section.
- [ ] **Step 5: Commit**: `git add src test CLAUDE.md && git commit -m "feat: web dashboard"`.

---

## Self-Review

- **Spec coverage:** §1 → Task 6; §2 → Task 2 (+ interval config Task 1, scheduling Task 3); §3 → Task 8 (BT independence enforced by Task 7 task split, verified Task 8 Step 5); §4 → Task 9; §5 → Task 4; §6 → Task 5; §7 → Task 7 (LED). The spec's "Note" (no HID++) is in Global Constraints.
- **Type consistency:** `Controller`, `Status`, `CmdResult`, `Plan`, `runPlan` names/signatures match across Tasks 2–5, 7–9.
- **Decisions (confirmed by user 2026-10-23):** board `esp32dev` (classic ESP32), clicks off by default, static bearer token on API/MCP. **Still assumed:** model "MX Master 3S", setup AP is open.
