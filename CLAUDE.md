# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What the project is

An ESP32 (classic `esp32dev`) that keeps a computer awake by acting as a BLE mouse presenting as a Logitech MX Master 3S, with control and status over WiFi: web dashboard, REST API and MCP server. [features.md](features.md) is the functional spec and the source of truth for requirements; the implementation plan is in `docs/superpowers/plans/`. Documentation is in English; dashboard UI strings are in Italian (the design history in `docs/superpowers/` is also Italian).

## Commands

PlatformIO (`pipx install platformio`; `pio` lives in `~/.local/bin`).

- Host unit tests (no board needed): `pio test -e native`
- One suite: `pio test -e native -f test_controller` (suites: `test_config`, `test_movement`, `test_controller`, `test_api`, `test_mcp`, `test_identity`, `test_led`, `test_dashboard`, `test_network`, `test_longpress`, `test_devices`, `test_keyboard`, `test_script`, `test_automation`, `test_mcp_ex`)
- Build firmware: `pio run -e esp32`
- Flash: `pio run -e esp32 -t upload --upload-port /dev/cu.usbserial-0001`
- Serial monitor: `pio device monitor` (115200 baud)
- Debug builds (pass via `PLATFORMIO_BUILD_FLAGS="..."`, never commit them; reflash a normal build afterwards): `-DJIG_FAKE_CONNECTED` fakes "mouse connected"; `-DJIG_DEBUG` logs each plan with the task's stack high-water mark; `-DJIG_FORCE_KEYBOARD` adds the keyboard collection in memory; `-DJIG_DEBUG_AUTOMATION` enables the keep-alive automation at 5 s; `-DJIG_FORCE_AP` boots in AP mode without saving it.

## Architecture

All logic lives in `lib/core/src` with no Arduino headers, so it runs under `pio test -e native`. `src/` holds thin hardware adapters verified on the board. Fakes for tests are in `test/support/fakes.h`.

- `Controller` is the single control surface (status, start/pause/stop, moveNow, updateConfig, `tick()`). The dashboard, REST (`handleApi`) and MCP (`handleMcp`) all call it, and REST and MCP share `json_codec`, so they cannot disagree. It is thread-safe (one mutex).
- `planActivity` turns a `Config` into a `Plan` (Bézier strokes with minimum-jerk easing, dwell, optional click and net-zero scroll, return stroke). The net Move displacement of a plan is exactly (0,0). `runPlan` executes it and aborts if the mouse disconnects.
- `Platform`, `Rng`, `MouseSink` and `ConfigStore` are the seams between core and hardware. Firmware implements them in `arduino_platform.h`, `ble_mouse.*` and `nvs_config_store.*`.
- The movement task (`movement_task.cpp`, core 1) calls `Controller::tick()` every 50 ms and runs plans, and is independent of WiFi. `setup()` starts it before `wifi.begin()` on purpose, because WiFi connection can block for seconds.
- WiFi: `WifiSetup` (tzapu WiFiManager, non-blocking, AP `Jiggler-Setup`) also collects the API token. With no token stored, the portal is forced even if WiFi credentials exist. `HttpServer` starts only once WiFi is connected and the portal is closed (both want port 80).
- Auth: every `/api/*` and `/mcp` request needs `Authorization: Bearer <token>`. `GET /` (the dashboard, `src/dashboard_html.h`, one self-contained page) is public.
- `WifiSetup` and `HttpServer` hide their library headers behind a pimpl, because WiFiManager and ESPAsyncWebServer must not share a translation unit.

### Devices, scripts, automations, network

- **One BLE identity.** `DeviceRegistry` (`devices.*`, blob key `devices`) holds a list of devices (mouse/keyboard) chosen from `catalog()` (`profiles.*`, only verified VID/PID/OUI) or `custom`, with optional name/manufacturer/VID/PID/MAC overrides. The identity (name, vendor, PnP ID, Bluetooth address) is the *first enabled device's*; every enabled device adds an HID collection (`buildReportMap`: mouse report ID 1, keyboard report ID 2) to the same BLE device (`BleMouse`, which is mouse + keyboard sink). The classic ESP32 has one Bluetooth address, so truly separate simultaneous identities are not possible here (see the spike notes in `docs/superpowers/specs/`). Identity changes apply after a restart and need re-pairing.
- **Scripts.** JSON `{"steps":[...]}` (ops `move`, `click`, `scroll`, `key`, `type`, `wait`, `repeat`) compiled by `compileScript` into the same `Plan`/`Step` model the mouse uses (extra step kinds `KeyDown`, `KeyUp`, `Wait`). All limits (steps, worst-case wait, nesting, coordinates, text) are enforced at compile time. `ScriptLibrary` stores up to 8 user scripts plus the read-only `keepalive-key` (F15 tap). `AutomationEngine` runs scripts on a random schedule (only while the global run state is Running) and on demand (`runNow`, `runInline`); the movement task runs `Controller::tick()` first, then the engine.
- **Router entry points.** `handleApiEx(ApiContext&)` / `handleMcpEx(ApiContext&)` add routes/tools (`/api/profiles|devices|scripts|automations|network|restart`, MCP tools for the same) and fall back to the original `handleApi` / the original six tools; services in `ApiContext` are optional (missing = 404 / tool not listed). Body caps: `kMaxBodyBytes` 4096 transport (scripts, MCP), `kMaxSmallBodyBytes` 1024 (config, network), `kMaxMediumBodyBytes` 2048 (devices, automations).
- **Network modes.** `auto` = home WiFi with the `Jiggler-Setup` portal fallback; `ap` = own WPA2 network (default SSID `Jiggler`, generated password printed on serial) with a DNS captive portal redirecting to the dashboard. In AP mode the token defaults to the AP password. Hold BOOT (GPIO0) 3 s to toggle the mode and restart (`LongPress`).
- `NvsBlobStore` hashes keys to short NVS names and keeps an index blob, so blob keys can be long (`script/<name>`).

## Gotchas

- Do not read NVS (`Preferences`) from global constructors: NVS is not initialized during static init. Read it in `setup()` or later.
- Time comparisons use `(int32_t)(now - due)` because `millis()` wraps at ~49.7 days. Keep that pattern.
- Partition table is `huge_app.csv` (3 MB app, no OTA). The default 1.25 MB partition was 95% full.
- Identities live in `profiles.cpp` (catalog) and `devices.cpp` (overrides). Add a profile only with VID/PID/OUI verified against public sources (`test_devices` pins the current ones). The base MAC is set with `esp_base_mac_addr_set` before BLE init, which also changes the WiFi MAC. `identity.h` is only the legacy Logitech constants used by `test_identity`.
- The dashboard test checks that the page only calls routes `handleApiEx` serves. Update `kPageRoutes` in `test/test_dashboard` (stateful order: a script is created before it is read/run/deleted) when the page adds a call.
- REST and MCP bodies must be sent with `Content-Type: application/json`. `curl -d` alone sends form-encoded, which the HTTP library parses as params, so the device answers 415 `unsupported_media_type`. Use `curl -H 'Content-Type: application/json' -d ...`.
- HTTP body bytes from ESPAsyncWebServer can exceed the declared Content-Length; always copy through `copyBodyChunk`, never `memcpy` directly.
- Exact return-to-start is guaranteed in HID counts only. OS pointer acceleration can leave visible drift; this is unmeasured on a real host.
