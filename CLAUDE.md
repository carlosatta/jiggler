# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What the project is

An ESP32 (classic `esp32dev`) that keeps a computer awake by acting as a BLE mouse presenting as a Logitech MX Master 3S, with control and status over WiFi: web dashboard, REST API and MCP server. [funzionalita.md](funzionalita.md) is the functional spec (Italian) and the source of truth for requirements; the implementation plan is in `docs/superpowers/plans/`. Keep documentation and UI strings in Italian.

## Commands

PlatformIO (`pipx install platformio`; `pio` lives in `~/.local/bin`).

- Host unit tests (no board needed): `pio test -e native`
- One suite: `pio test -e native -f test_controller` (suites: `test_config`, `test_movement`, `test_controller`, `test_api`, `test_mcp`, `test_identity`, `test_led`, `test_dashboard`)
- Build firmware: `pio run -e esp32`
- Flash: `pio run -e esp32 -t upload --upload-port /dev/cu.usbserial-0001`
- Serial monitor: `pio device monitor` (115200 baud)
- Debug build without a paired host: `PLATFORMIO_BUILD_FLAGS="-DJIG_FAKE_CONNECTED -DJIG_DEBUG" pio run -e esp32 -t upload`. This fakes "mouse connected" and logs each plan with its stack high-water mark. Reflash a normal build afterwards.

## Architecture

All logic lives in `lib/core/src` with no Arduino headers, so it runs under `pio test -e native`. `src/` holds thin hardware adapters verified on the board. Fakes for tests are in `test/support/fakes.h`.

- `Controller` is the single control surface (status, start/pause/stop, moveNow, updateConfig, `tick()`). The dashboard, REST (`handleApi`) and MCP (`handleMcp`) all call it, and REST and MCP share `json_codec`, so they cannot disagree. It is thread-safe (one mutex).
- `planActivity` turns a `Config` into a `Plan` (Bézier strokes with minimum-jerk easing, dwell, optional click and net-zero scroll, return stroke). The net Move displacement of a plan is exactly (0,0). `runPlan` executes it and aborts if the mouse disconnects.
- `Platform`, `Rng`, `MouseSink` and `ConfigStore` are the seams between core and hardware. Firmware implements them in `arduino_platform.h`, `ble_mouse.*` and `nvs_config_store.*`.
- The movement task (`movement_task.cpp`, core 1) calls `Controller::tick()` every 50 ms and runs plans, and is independent of WiFi. `setup()` starts it before `wifi.begin()` on purpose, because WiFi connection can block for seconds.
- WiFi: `WifiSetup` (tzapu WiFiManager, non-blocking, AP `Jiggler-Setup`) also collects the API token. With no token stored, the portal is forced even if WiFi credentials exist. `HttpServer` starts only once WiFi is connected and the portal is closed (both want port 80).
- Auth: every `/api/*` and `/mcp` request needs `Authorization: Bearer <token>`. `GET /` (the dashboard, `src/dashboard_html.h`, one self-contained page) is public.
- `WifiSetup` and `HttpServer` hide their library headers behind a pimpl, because WiFiManager and ESPAsyncWebServer must not share a translation unit.

## Gotchas

- Do not read NVS (`Preferences`) from global constructors: NVS is not initialized during static init. Read it in `setup()` or later.
- Time comparisons use `(int32_t)(now - due)` because `millis()` wraps at ~49.7 days. Keep that pattern.
- Partition table is `huge_app.csv` (3 MB app, no OTA). The default 1.25 MB partition was 95% full.
- A mouse identity change touches `identity.h` (`kBleName`, `kVid`, `kPid`, OUI `00:1F:20`). The base MAC is set with `esp_base_mac_addr_set` before BLE init, which also changes the WiFi MAC.
- The dashboard test checks that the page only calls routes `handleApi` serves. Update `kPageRoutes` in `test/test_dashboard` when the page adds a call.
- REST and MCP bodies must be sent with `Content-Type: application/json`. `curl -d` alone sends form-encoded, which the HTTP library parses as params, so the device answers 415 `unsupported_media_type`. Use `curl -H 'Content-Type: application/json' -d ...`.
- HTTP body bytes from ESPAsyncWebServer can exceed the declared Content-Length; always copy through `copyBodyChunk`, never `memcpy` directly.
- Exact return-to-start is guaranteed in HID counts only. OS pointer acceleration can leave visible drift; this is unmeasured on a real host.
