# Multi-device, Profiles, Scripts and AP Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an AP/captive-portal mode that works without home WiFi, a profile catalog with custom devices (mouse + keyboard in one BLE composite), scriptable keyboard/mouse behavior, and a spike on separate simultaneous identities.

**Architecture:** Everything new is pure core logic in `lib/core/src` (host-tested, `pio test -e native`) behind new router/MCP entry points that wrap the existing `handleApi`/`handleMcp` (so existing tests and clients keep working); hardware adapters in `src/` stay thin. Scripts compile to the existing `Plan`/`Step` model and run through `runPlan`. One BLE identity (the first enabled device's) exposes one HID collection per enabled device.

**Tech Stack:** unchanged (PlatformIO, Arduino ESP32, NimBLE-Arduino 1.4, ArduinoJson 7, ESPAsyncWebServer, WiFiManager, Unity) plus `DNSServer` (Arduino core).

**Spec:** [docs/superpowers/specs/2026-10-23-multi-device-design.md](../specs/2026-10-23-multi-device-design.md)

## Global Constraints

- Existing behavior and tests unchanged: `Controller`, `handleApi`, `handleMcp`, `/api/status|config|start|stop|pause|move`, the six existing MCP tools, bearer-token auth on every `/api/*` and `/mcp`.
- Devices: max 4; ids unique `[a-z0-9_-]{1,24}`; `kind` ∈ `mouse|keyboard`; BLE name 1..29 chars; manufacturer 1..32; vid/pid 0..65535; `mac` override `AA:BB:CC:DD:EE:FF`, first octet's bit 0 must be 0 (unicast); at least one device enabled; profile kind must equal device kind; `profileId:"custom"` allowed.
- Defaults: devices = `[{id:"mouse",kind:"mouse",enabled:true,profileId:"logitech-mx-master-3s"},{id:"keyboard",kind:"keyboard",enabled:false,profileId:"custom"}]`; custom mouse identity "Bluetooth Mouse"/"Generic", custom keyboard "Bluetooth Keyboard"/"Generic", no MAC spoof unless set.
- HID: one composite; mouse report ID 1 (4 bytes, unchanged), keyboard report ID 2 (8 bytes: modifiers, reserved, 6 key slots). US layout.
- Scripts: JSON `{"steps":[...]}` ≤ 3000 bytes; ops `move{dx,dy}`, `click`, `scroll{notches}`, `key{key}`, `type{text}`, `wait{ms}` or `wait{minMs,maxMs}`, `repeat{times,steps}`; nesting depth ≤ 3; compiled plan ≤ 2000 steps and ≤ 600000 ms total wait; |dx|,|dy| ≤ 2000; `type` text ≤ 200 printable ASCII chars (plus `\n`, `\t`); max 8 user scripts, names `[a-z0-9_-]{1,24}`; built-in read-only script `keepalive-key` = one `key F15` step.
- Automations: max 4, ids `[a-z0-9_-]{1,24}`, intervals 5..86400 s with max ≥ min, script must exist; default `[{id:"keepalive-key",script:"keepalive-key",minIntervalSec:60,maxIntervalSec:180,enabled:false}]`; they run only while the global run state is Running and the HID is connected.
- Network: mode `auto|ap`; AP SSID 1..32 chars default `Jiggler`; AP password empty (= generated 10-hex on first AP start, printed on serial) or 8..63 chars; in AP mode with no stored API token the token becomes the AP password; DNS answers every name with the AP IP; BOOT (GPIO0) held 3000 ms toggles `auto`↔`ap` and restarts; `POST /api/restart` reboots after the response is sent.
- Error codes (English, machine-readable) as in the existing API: `invalid_config`-style `{"error":"invalid_devices|invalid_script|invalid_automation|invalid_network|not_found|mouse_not_connected|…","field":…,"message":…}`; bodies > 1024 bytes → 413 (script PUT: 4096).
- Dashboard text in Italian. Namespace `jig`. Persisted blobs in NVS namespace `jiggler`, one key per blob, value ≤ 3900 bytes.

## Review Focus

- [RF1] Device list validation: unknown profile, kind mismatch, duplicate ids, 5-octet / non-hex / multicast MAC, name too long, none enabled → 400, nothing persisted (Task 3).
- [RF2] Script bombs: `repeat` nested 4 deep, `repeat` of 1e9, total steps/waits over limit, `dx` 1e9, oversized body → rejected at save and at run, never unbounded memory/time (Task 6).
- [RF3] `type_text`/`type` with emoji, non-ASCII, control chars, empty string → error, never wrong keys (Tasks 4, 6, 8).
- [RF4] Key ops with keyboard disabled or no sink: skipped, plan continues, no crash; mouse-only report map byte-identical to today's (Tasks 4, 5).
- [RF5] Lock-out: AP mode with forgotten password/token → recoverable via BOOT long-press; invalid AP SSID/password rejected before saving (Tasks 1, 2).

---

### Task 1: Network settings, router extension, restart route

**Files:**
- Create: `lib/core/src/network_settings.h/.cpp`, `lib/core/src/api_ex.h/.cpp`, `test/test_network/test_main.cpp`

**Interfaces:**
- Consumes: `Controller`, `handleApi`, `HttpResponse`, `ValidationError`, `ParseError`, `errorJson`, `kMaxBodyBytes`.
- Produces:
  - `enum class NetworkMode {Auto, Ap}`; `struct NetworkSettings {NetworkMode mode=Auto; std::string apSsid="Jiggler"; std::string apPassword;}`; `struct NetworkStore {virtual NetworkSettings load()=0; virtual void save(const NetworkSettings&)=0;}`
  - `std::optional<ValidationError> validateNetwork(const NetworkSettings&)`; `std::string networkToJson(const NetworkSettings&)` (keys `mode` "auto"|"ap", `apSsid`, `apPassword`); `bool parseNetworkPatch(const std::string& body, NetworkSettings& base, ParseError& err)` (partial: updates `base` only on success).
  - `bool isCaptiveProbePath(const std::string& path)` — true for `/generate_204`, `/gen_204`, `/hotspot-detect.html`, `/library/test/success.html`, `/ncsi.txt`, `/connecttest.txt`, `/redirect`, `/canonical.html`, `/success.txt`.
  - `struct ApiContext {Controller& controller; NetworkStore* network=nullptr; std::function<void()> restart;}` (later tasks add more nullable members); `HttpResponse handleApiEx(ApiContext&, const std::string& method, const std::string& path, const std::string& body)` — handles `GET|PUT /api/network` (PUT partial, 200 + network JSON, 400 `invalid_network` with field), `POST /api/restart` (202 `{"restarting":true}`, calls `ctx.restart` once); every other path → `handleApi(ctx.controller, …)`.

- [ ] **Step 1: Write failing tests** (`test_network`, a `MemNetworkStore` in the test file): `test_defaults`; `test_validate` (ssid "" and 33 chars rejected; password 7 chars rejected, 8 and 63 accepted, empty accepted); `test_patch_partial_and_atomic` (bad password in a multi-field patch leaves base untouched); `test_bad_input_table` [RF5] (`""`, `"{"`, `[]`, `{"mode":"x"}`, `{"mode":5}`, `{"apSsid":null}`, `{"apPassword":"short"}`, `{"bogus":1}`, 1025-byte body → 400/413 and nothing saved); `test_get_put_network_routes`; `test_restart_route_calls_callback_once_and_returns_202`; `test_legacy_routes_pass_through` (`GET /api/status` works via `handleApiEx`, unknown path 404); `test_network_routes_404_when_service_missing` (`ctx.network == nullptr`); `test_captive_probe_paths`.
- [ ] **Step 2: Run** `pio test -e native -f test_network` → FAIL.
- [ ] **Step 3: Implement** the interfaces above with ArduinoJson like `parseConfigPatch`; unknown keys and wrong types → 400 `invalid_network` with the field.
- [ ] **Step 4: Run** the same command, then `pio test -e native` → PASS.
- [ ] **Step 5: Commit**: `git add lib test && git commit -m "feat: network settings and extended API router"`.

---

### Task 2: AP mode with captive portal (firmware)

**Files:**
- Create: `lib/core/src/long_press.h`, `test/test_longpress/test_main.cpp`, `src/nvs_network_store.h/.cpp`
- Modify: `src/wifi_setup.h/.cpp`, `src/http_server.h/.cpp`, `src/main.cpp`

**Interfaces:**
- Consumes: `NetworkStore`, `NetworkSettings`, `ApiContext`, `handleApiEx`, `isCaptiveProbePath`, `TokenStore`.
- Produces: `class LongPress {explicit LongPress(uint32_t holdMs); bool update(bool pressed, uint32_t nowMs);}` (core, header-only; returns true exactly once per press when held ≥ holdMs; resets on release; wrap-safe); `class NvsNetworkStore : public jig::NetworkStore` (keys `netMode`,`apSsid`,`apPass`); `WifiSetup(TokenStore&, NetworkStore&)`; `bool WifiSetup::apMode() const`; `HttpServer(ApiContext&, TokenStore&, bool captive)`.

- [ ] **Step 1: Write failing tests** `test_longpress_fires_once_after_hold` (3000 ms), `test_longpress_released_early_never_fires`, `test_longpress_rearms_after_release`, `test_longpress_wraparound`.
- [ ] **Step 2: Run** `pio test -e native -f test_longpress` → FAIL; implement `long_press.h`; run → PASS.
- [ ] **Step 3: Implement firmware.** `WifiSetup::begin()`: load settings; if `mode==Ap`: generate+save `apPassword` when empty (10 hex from `esp_random()`, print on serial), `WiFi.mode(WIFI_AP)`, `WiFi.softAP(ssid, password)`, start `DNSServer` (port 53, `"*"` → `WiFi.softAPIP()`), MDNS, and if no API token is stored save `apPassword` as the token; `loop()` returns true once the AP is up (and calls `dns.processNextRequest()`). `mode==Auto`: existing behavior. `HttpServer` with `captive`: unknown non-`/api`, non-`/mcp` paths and `isCaptiveProbePath` hits answer `302 Location: http://<softAPIP>/`; `GET /` serves the dashboard as today; all `/api/*` calls go through `handleApiEx` (restart callback sets `restartAt = millis()+500`, performed in `loop()` after the response is sent). `main.cpp`: `LongPress boot(3000)` on GPIO0 (`INPUT_PULLUP`, pressed = LOW) toggles the stored mode and restarts. Debug flag `-DJIG_FORCE_AP` forces `Ap` without saving.
- [ ] **Step 4: Verify** `pio run -e esp32` compiles. Flash with `PLATFORMIO_BUILD_FLAGS="-DJIG_FORCE_AP"`: serial shows the AP SSID, password and IP; then flash a normal build and confirm the previous (`auto`, portal) behavior is unchanged. Joining the AP from a phone and the captive popup are checked by the user.
- [ ] **Step 5: Commit**: `git add lib src test && git commit -m "feat: AP mode with captive portal and BOOT long-press toggle"`.

---

### Task 3: Profile catalog, devices, identity resolution

**Files:**
- Create: `lib/core/src/profiles.h/.cpp`, `lib/core/src/devices.h/.cpp`, `test/test_devices/test_main.cpp`
- Modify: `lib/core/src/platform.h` (add `BlobStore`), `lib/core/src/api_ex.h/.cpp`, `test/support/fakes.h` (add `MemBlobStore`)

**Interfaces:**
- Produces:
  - `struct BlobStore {virtual std::string get(const std::string& key)=0; virtual void put(const std::string& key, const std::string& value)=0; virtual void remove(const std::string& key)=0; virtual std::vector<std::string> keys(const std::string& prefix)=0;}` (`get` returns "" if absent).
  - `enum class DeviceKind {Mouse, Keyboard}`; `struct Identity {std::string name; std::string manufacturer; uint16_t vid; uint16_t pid; uint8_t vidSource /*1=Bluetooth SIG,2=USB*/; uint16_t version; std::optional<std::array<uint8_t,3>> oui; uint16_t appearance;}`; `struct Profile {std::string id; std::string label; DeviceKind kind; Identity identity;}`; `const std::vector<Profile>& catalog()`; `const Profile* findProfile(const std::string& id)`.
  - `struct DeviceConfig {std::string id; DeviceKind kind; bool enabled; std::string profileId; std::optional<std::string> name, manufacturer, mac; std::optional<uint16_t> vid, pid;}`; `std::vector<DeviceConfig> defaultDevices()`; `std::optional<ValidationError> validateDevices(const std::vector<DeviceConfig>&)`; `std::string devicesToJson(const std::vector<DeviceConfig>&)`; `bool parseDevices(const std::string&, std::vector<DeviceConfig>&, ParseError&)`; `std::string profilesToJson()`.
  - `struct ResolvedDevice {Identity identity; std::optional<std::array<uint8_t,6>> mac; bool mouse; bool keyboard;}`; `Identity resolveIdentity(const DeviceConfig&)` (profile identity, `custom` = generic defaults, then overrides); `std::optional<std::array<uint8_t,6>> resolveMac(const DeviceConfig&, const Identity&, const uint8_t efuse[6])` (explicit `mac` → that; else profile OUI + efuse[3..5]; else nullopt = keep chip MAC).
  - `class DeviceRegistry {explicit DeviceRegistry(BlobStore&); void begin(); std::vector<DeviceConfig> list() const; CmdResult replace(const std::vector<DeviceConfig>&); ResolvedDevice resolved(const uint8_t efuse[6]) const;}` — primary identity = first enabled device; `mouse`/`keyboard` flags = any enabled device of that kind; blob key `devices`; corrupt/invalid stored JSON → defaults.
  - Router: `ApiContext.devices` (`DeviceRegistry*`); `GET /api/profiles` (catalog JSON), `GET /api/devices`, `PUT /api/devices` (replaces the list; 200 `{"devices":[...],"restartRequired":true}`; 400 `invalid_devices`).

- [ ] **Step 1: Research the catalog (no code yet).** Download the IEEE OUI list (`curl https://standards-oui.ieee.org/oui/oui.txt`) and search the web for each entry's Bluetooth VID/PID. Include only entries whose VID, PID and OUI you could verify from a public source; record sources in the ledger. Target: Logitech MX Master 3S (`046D:B034`, OUI `00:1F:20`, verified), plus as many of Logitech MX Keys, Razer, Microsoft, Apple Magic Mouse 2 / Magic Keyboard as verify. Never invent values.
- [ ] **Step 2: Write failing tests** (`test_devices`): `test_catalog_has_verified_logitech_default` (`logitech-mx-master-3s`, kind Mouse, vid 0x046D, pid 0xB034, OUI `00:1F:20`, name "MX Master 3S") and `test_catalog_ids_unique_and_wellformed` (every entry: non-empty name ≤ 29, kind valid, id `[a-z0-9-]+`); `test_default_devices` (matches Global Constraints, `validateDevices` ok); `test_resolve_identity_overrides`; `test_resolve_mac` (profile OUI + efuse tail; explicit override wins; custom without mac → nullopt); `test_validate_devices_table` [RF1] (unknown profile, mouse device with keyboard profile, duplicate ids, 5-octet / non-hex / multicast MAC, name of 30 chars, vid 70000, none enabled, 5 devices); `test_json_roundtrip`; `test_parse_devices_bad_input` [RF1] (non-JSON, `{}`, wrong types); `test_registry_primary_and_flags` (keyboard first enabled → its identity primary; both enabled → mouse+keyboard flags); `test_registry_replace_persists_and_invalid_changes_nothing` [RF1]; `test_registry_corrupt_blob_falls_back_to_defaults`; `test_api_profiles_devices_routes` (GET/PUT, 400 on bad, `restartRequired`).
- [ ] **Step 3: Run** `pio test -e native -f test_devices` → FAIL. **Implement**, run → PASS; then `pio test -e native`.
- [ ] **Step 4: Commit**: `git add lib test && git commit -m "feat: profile catalog, device registry and identity resolution"`.

---

### Task 4: Keymap, keyboard steps, composite report map

**Files:**
- Create: `lib/core/src/keymap.h/.cpp`, `lib/core/src/hid_report_map.h/.cpp`, `test/test_keyboard/test_main.cpp`
- Modify: `lib/core/src/platform.h` (add `KeyboardSink`), `lib/core/src/movement.h/.cpp`, `test/support/fakes.h` (add `FakeKeyboard`), `test/test_movement/test_main.cpp` (unchanged tests must still pass)

**Interfaces:**
- Produces:
  - `struct KeyboardSink {virtual void keyDown(uint8_t modifiers, uint8_t usage)=0; virtual void keyUp()=0;}`
  - `struct KeyChord {uint8_t mods; uint8_t usage;}` (modifier bits: Ctrl 0x01, Shift 0x02, Alt 0x04, Gui 0x08); `std::optional<KeyChord> keyFromName(const std::string&)` (case-insensitive; `a`–`z`, `0`–`9`, `F1`–`F24`, `Enter`, `Esc`, `Tab`, `Space`, `Backspace`, `Delete`, `Up/Down/Left/Right`, `Home`, `End`, `PageUp`, `PageDown`; optional prefixes `Ctrl+`, `Shift+`, `Alt+`, `Gui+` in any combination); `std::optional<std::vector<KeyChord>> charsToChords(const std::string& text)` (US layout: printable ASCII, `\n`→Enter, `\t`→Tab, uppercase/symbols add Shift; any other byte → nullopt).
  - `std::vector<uint8_t> buildReportMap(bool mouse, bool keyboard)` — mouse part byte-identical to the current `kReportMap` in `src/ble_mouse.cpp` (report ID 1); keyboard part standard 8-byte keyboard (report ID 2, no LED output report); both in a single descriptor; empty if neither.
  - `Step` gains trailing `uint8_t key = 0; uint8_t mods = 0;` and `Kind::KeyDown`, `Kind::KeyUp` (existing aggregate initializers keep compiling); `size_t runPlan(const Plan&, MouseSink&, KeyboardSink*, const Platform&, const std::function<void(uint32_t)>&)` — the existing 4-argument overload forwards with `nullptr`; KeyDown/KeyUp with a null keyboard are skipped (their `waitMs` still slept).

- [ ] **Step 1: Write failing tests** (`test_keyboard`): `test_key_names` (`a`→usage 4, `Z`→29, `1`→30, `0`→39, `F15`→0x6A, `f24`→0x73, `Enter`→0x28, `Ctrl+Alt+Delete`→mods 0x05 usage 0x4C, `shift+a`→mods 0x02; `""`, `F25`, `Ctrl+`, `Hyper+a`, `😀` → nullopt); `test_chars_to_chords` (`"Hi!"` → Shift+h, i, Shift+1; `"a\tb\n"`; `""` → empty vector); `test_chars_to_chords_rejects_non_ascii_and_control` [RF3] (`"é"`, `"😀"`, `"\x01"`, `"\x7f"` → nullopt); `test_report_map_mouse_only_matches_legacy` [RF4] (compare with a literal copy of today's 54 bytes); `test_report_map_structure` (a small HID item parser walks the descriptor: Collection/End Collection balanced, report IDs present exactly `{1}`, `{2}`, `{1,2}` per flags, keyboard input report = 8 bytes, none → empty); `test_run_plan_dispatches_keys` (KeyDown/KeyUp reach `FakeKeyboard`, waits slept in order); `test_run_plan_skips_keys_without_keyboard` [RF4]; existing movement tests unchanged and green.
- [ ] **Step 2: Run** `pio test -e native -f test_keyboard` → FAIL. **Implement**; run → PASS; then `pio test -e native` (all suites).
- [ ] **Step 3: Commit**: `git add lib test && git commit -m "feat: keymap, keyboard steps and composite HID report map"`.

---

### Task 5: BLE composite device (firmware)

**Files:**
- Create: `src/nvs_blob_store.h/.cpp`
- Modify: `src/ble_mouse.h/.cpp`, `src/main.cpp`, `src/arduino_platform.h` (no change expected), `src/movement_task.cpp`

**Interfaces:**
- Consumes: `buildReportMap`, `DeviceRegistry`, `ResolvedDevice`, `KeyboardSink`, `runPlan` 5-arg overload.
- Produces: `class NvsBlobStore : public jig::BlobStore` (Preferences namespace `jiggler`; keys ≤ 15 chars, so `scr_<name>` names are truncated-safe because script names ≤ 24 → store them under hashed/prefixed short keys `s0..s7` with an index blob `scripts`; value ≤ 3900 bytes); `BleMouse::begin(const jig::ResolvedDevice&)` (name, manufacturer, PnP (`vidSource`, vid, pid, version), appearance, base MAC when `mac` set, report map from `buildReportMap(mouse, keyboard)`, input report 1 and, if keyboard, report 2); `BleMouse` also implements `jig::KeyboardSink` (8-byte report, ID 2). `main.cpp`: `DeviceRegistry devices(blobs)`; `devices.begin()` before `ble.begin(devices.resolved(efuse))`; movement task passes the keyboard sink to `runPlan`. Debug flag `-DJIG_FORCE_KEYBOARD` enables the keyboard device in memory.

- [ ] **Step 1: Implement** per Interfaces; keep `JIG_FAKE_CONNECTED`/`JIG_DEBUG` behavior.
- [ ] **Step 2: Verify** `pio test -e native` green and `pio run -e esp32` compiles. Flash a normal build: serial address/name match the default profile; the bleak scan (`scratchpad/scan.py`) still shows "MX Master 3S" with service 0x1812. Flash with `-DJIG_FORCE_KEYBOARD`: boot is clean (no crash) and the scan still sees the same identity.
- [ ] **Step 3: Commit**: `git add src && git commit -m "feat: composite BLE HID driven by the device registry"`.

---

### Task 6: Script compiler, library and REST

**Files:**
- Create: `lib/core/src/script.h/.cpp`, `test/test_script/test_main.cpp`
- Modify: `lib/core/src/api_ex.h/.cpp` (`ApiContext.scripts`), `lib/core/src/movement.h/.cpp` (expose `void appendHumanMove(std::vector<Step>&, int dx, int dy, Rng&)`)

**Interfaces:**
- Produces:
  - `struct CompileError {std::string path; std::string message;}`; `std::optional<CompileError> compileScript(const std::string& json, Rng&, Plan& out)` — `out` untouched on error; enforces every Global Constraints script limit; `move` → `appendHumanMove` (curved/eased, ends exactly at (dx,dy)); `key` → KeyDown+KeyUp 40 ms apart; `type` → per char KeyDown/KeyUp with 30..70 ms random gaps; `wait{minMs,maxMs}` → uniform random.
  - `class ScriptLibrary {explicit ScriptLibrary(BlobStore&); std::vector<std::string> names() const /*user + builtin*/; std::optional<std::string> get(const std::string& name) const; CmdResult put(const std::string& name, const std::string& json); CmdResult remove(const std::string& name);}` — `put` validates name, size ≤ 3000, `compileScript` with a throwaway rng; builtin `keepalive-key` cannot be overwritten/removed (`read_only`); max 8 user scripts (`limit_reached`).
  - Router: `GET /api/scripts` (`{"scripts":[{"name","builtin"}]}`), `GET /api/scripts/{name}`, `PUT /api/scripts/{name}` (body = script JSON; 200; 400 `invalid_script` with `path`/`message`; 413 over 4096), `DELETE /api/scripts/{name}` (200/404/403 `read_only`).

- [ ] **Step 1: Write failing tests** (`test_script`): `test_compile_ops_table` (each op yields the expected Step kinds, e.g. `key F15` → KeyDown(0,0x6A) then KeyUp; `scroll 3` → one Scroll step notches 3; `click` → Click); `test_move_ends_exactly` (sum of Move steps == (dx,dy) for 100 seeds, deltas fit int8); `test_type_emits_shift_for_uppercase`; `test_type_rejects_non_ascii` [RF3] (emoji, `é`, `\x01`, empty text → error path `/steps/0/text`); `test_repeat_unrolls`; `test_script_bombs_rejected` [RF2] (depth-4 nesting; `repeat` 1e9; 3 nested `repeat` of 20 each = 8000 steps; `wait` totals > 600000 ms; `dx` 1e9; `ms` negative / fractional; unknown op; unknown key name; non-object step; > 3000 bytes) each → error and `out` empty, completing instantly; `test_wait_random_in_range`; `test_library_put_get_remove`; `test_library_limits` (9th script → `limit_reached`; bad names `""`, `UPPER`, 25 chars, `../x`; builtin read-only); `test_library_invalid_script_not_stored` [RF2]; `test_script_routes` (CRUD codes, 413 on 4097 bytes, 404 unknown).
- [ ] **Step 2: Run** `pio test -e native -f test_script` → FAIL. **Implement**; run → PASS; then `pio test -e native`.
- [ ] **Step 3: Commit**: `git add lib test && git commit -m "feat: script compiler, library and REST"`.

---

### Task 7: Automations engine and script execution

**Files:**
- Create: `lib/core/src/automation.h/.cpp`, `test/test_automation/test_main.cpp`
- Modify: `lib/core/src/api_ex.h/.cpp` (`ApiContext.automations`), `src/movement_task.cpp`, `src/main.cpp`

**Interfaces:**
- Produces: `struct Automation {std::string id; std::string script; uint32_t minIntervalSec; uint32_t maxIntervalSec; bool enabled;}`; `class AutomationEngine {AutomationEngine(Platform&, Rng&, ScriptLibrary&, BlobStore&); void begin(); std::vector<Automation> list() const; CmdResult replace(const std::vector<Automation>&); CmdResult runNow(const std::string& script); std::optional<Plan> tick(bool running);}` — thread-safe (mutex); blob key `autos`; `runNow` fails with `not_found` (unknown script) or `mouse_not_connected`, otherwise queues and is served by the next `tick` in any run state; scheduled runs need `running && enabled && platform.mouseConnected()`; due check `(int32_t)(now - due) >= 0`; a script that no longer compiles is skipped and rescheduled.
- Router: `GET|PUT /api/automations` (400 `invalid_automation` with field), `POST /api/scripts/{name}/run` (202 `{"queued":true}`; 404; 409).
- Movement task: each 50 ms `controller.tick()` first, else `automations.tick(controller.status().runState == RunState::Running)`.

- [ ] **Step 1: Write failing tests** (`test_automation`, fakes): `test_default_automation_disabled`; `test_validate_table` (dup id, unknown script, min 4, max<min, 5 automations, bad id); `test_fires_when_due_running_connected_and_reschedules` (min=max=10 s); `test_does_not_fire_when_not_running_or_disabled_or_disconnected`; `test_due_waits_for_connection`; `test_clock_wraparound`; `test_run_now_queued_in_any_state_and_errors` (unknown script → `not_found`, disconnected → `mouse_not_connected`); `test_deleted_script_skipped_and_rescheduled`; `test_replace_invalid_changes_nothing_and_persists_valid`; `test_run_route_codes`.
- [ ] **Step 2: Run** `pio test -e native -f test_automation` → FAIL. **Implement**; run → PASS; `pio test -e native`.
- [ ] **Step 3: Verify on device** (`pio run`, flash a normal build, boot is clean). With `-DJIG_FAKE_CONNECTED -DJIG_DEBUG -DJIG_FORCE_KEYBOARD`, serial shows the `keepalive-key` automation firing once enabled via a temporary in-memory default (`-DJIG_DEBUG_AUTOMATION` sets it enabled with 5 s intervals) and a plan with KeyDown/KeyUp steps; reflash a normal build afterwards.
- [ ] **Step 4: Commit**: `git add lib src test && git commit -m "feat: automations engine and script execution"`.

---

### Task 8: MCP tools for the new features

**Files:**
- Create: `test/test_mcp_ex/test_main.cpp`
- Modify: `lib/core/src/mcp_server.h/.cpp` (add `McpReply handleMcpEx(ApiContext&, const std::string& body)`; `handleMcp` unchanged), `src/http_server.cpp` (use `handleMcpEx`)

**Interfaces:**
- Produces: `handleMcpEx` = existing six tools plus `list_profiles`, `get_devices`, `set_devices` (arg `devices` array; success text includes `restartRequired`), `list_scripts`, `save_script` (`name`, `script` object), `run_script` (`name`), `delete_script`, `get_automations`, `set_automations`, `press_key` (`key`, optional `times`), `type_text` (`text`), `get_network`, `set_network`, `restart`. `press_key`/`type_text` run an ad-hoc plan through `AutomationEngine::runNow`-style queueing (add `CmdResult AutomationEngine::runPlanNow(Plan)` with the same not-connected rule). Tool failures return `isError:true` with the REST error JSON as text.

- [ ] **Step 1: Write failing tests**: `test_tools_list_includes_new_tools` (old six first, in order, then the new ones); `test_legacy_mcp_unchanged` (`handleMcp` output for `tools/list` is byte-identical to before); `test_devices_tools`, `test_script_tools_roundtrip`, `test_run_script_errors` (unknown → isError; disconnected → isError), `test_type_text_rejects_non_ascii` [RF3], `test_press_key_unknown_key_error`, `test_network_tools_validate` [RF5], `test_restart_tool_calls_callback`.
- [ ] **Step 2: Run** `pio test -e native -f test_mcp_ex` → FAIL. **Implement** reusing the router handlers; run → PASS; `pio test -e native`.
- [ ] **Step 3: Commit**: `git add lib src test && git commit -m "feat: MCP tools for devices, scripts, automations and network"`.

---

### Task 9: Dashboard for the new features

**Files:**
- Modify: `src/dashboard_html.h`, `test/test_dashboard/test_main.cpp`, `CLAUDE.md`

**Interfaces:**
- Consumes: all new routes. Italian UI, same single self-contained page, `textContent` only, in-flight guard on every poll.
- Produces: tabs **Stato** (existing), **Dispositivi** (list; per device: abilitato, tipo, profilo from `/api/profiles` or "Personalizzato", then name/manufacturer/VID/PID/MAC fields prefilled from the profile as placeholders; "Salva e riavvia" with a warning that re-pairing is needed), **Script** (list, JSON editor textarea, Salva/Elimina/Esegui, errors with `path`/`message`), **Automazioni** (list: script select, min/max, enabled), **Rete** (mode Auto/AP, SSID, password, Riavvia).

- [ ] **Step 1: Update `test_dashboard`** (`kPageRoutes` lists every `(method,path)` the page calls, including path-parameter routes with a sample name) → run → FAIL; **implement** the page; run → PASS.
- [ ] **Step 2: Verify in a browser** (Playwright, 390 px, light/dark) against an extended local mock (`scratchpad/mock.py`) that serves the real `kDashboardHtml`: each tab loads, editing a device shows prefilled placeholders, an invalid MAC shows the server message inline, a script with an error shows `path`/`message`, run/delete/restart buttons call the right routes, no console errors.
- [ ] **Step 3: Update `CLAUDE.md`** (new modules, routes, debug flags, composite-identity limit, how scripts work) and the spec's status.
- [ ] **Step 4: Flash a normal build** and confirm a clean boot. Commit: `git add src test CLAUDE.md docs && git commit -m "feat: dashboard for devices, scripts, automations and network"`.

---

### Task 10: Spike — separate simultaneous identities on the classic ESP32

**Files:**
- Create: `spike/identities/` (throwaway sketch in its own PlatformIO env `[env:spike]` added to `platformio.ini`), `docs/superpowers/specs/2026-10-23-multi-identity-spike.md`

**Interfaces:** none; no production code changes.

- [ ] **Step 1: Question to answer:** can this chip (ESP32-D0WDQ6, NimBLE-Arduino 1.4, BLE 4.2) present two BLE identities with different addresses at the same time and accept one central on each? Try: (a) public address fixed + second identity with a random static address by re-advertising alternately (`NimBLEDevice::setOwnAddr` + `ble_hs_id_set_rnd`); (b) check whether a random address can carry a non-static OUI-like prefix (the host stack requires the two top bits = `11` for static random).
- [ ] **Step 2: Measure** with the Mac (`bleak` in `scratchpad/venv`): scan shows both names with different addresses; connect two clients (plain GATT service, not HID, since CoreBluetooth hides HID) and keep both connected for 60 s; record what works, what fails and why.
- [ ] **Step 3: Write the result** in the spike doc: feasible / feasible with limits (list them, including that only the public address can carry a vendor OUI) / not feasible, with a recommendation (stay composite, ESP32-S3, or several boards). Remove the `[env:spike]` env unless the user keeps it. Do not integrate anything from the spike.
- [ ] **Step 4: Commit**: `git add spike docs platformio.ini && git commit -m "docs: separate-identities spike result"`.

---

## Self-Review

- **Spec coverage:** AP/captive/BOOT toggle → Tasks 1–2; catalog + custom + defaults + MAC → Task 3 (+5 firmware); mouse+keyboard simultaneous → Tasks 4–5; scriptable behavior + scheduling → Tasks 6–7; MCP → 8; UI → 9; separate identities → 10 (spike, decision recorded).
- **Type consistency:** `ApiContext` gains nullable members in Tasks 3 (`devices`), 6 (`scripts`), 7 (`automations`); `handleApiEx`/`handleMcpEx` are the only new entry points; `runPlan` 5-arg overload keeps the 4-arg one; `BlobStore` defined in Task 3 and consumed by Tasks 5–7.
- **Open decisions made by this plan (flag to user):** one composite BLE identity (primary = first enabled device), catalog limited to verified entries, US keyboard layout, scripts as JSON, AP token defaults to the AP password.
