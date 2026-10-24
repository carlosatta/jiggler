# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What the project is

An ESP32 (classic `esp32dev`) that keeps a computer awake by acting as a BLE mouse (a Logitech MX Master 3S by default), with control and status over WiFi: web dashboard, REST API and MCP server. [features.md](features.md) is the functional spec and the source of truth for requirements; the implementation plan is in `docs/superpowers/plans/`. Documentation and the dashboard UI are in English (only the historic design notes in `docs/superpowers/` are in Italian). Screenshots in `docs/images/` come from `tools/mock_device.py` with neutral data.

## Commands

PlatformIO (`pipx install platformio`; `pio` lives in `~/.local/bin`).

- Host unit tests (no board needed): `pio test -e native`
- One suite: `pio test -e native -f test_controller` (suites: `test_config`, `test_movement`, `test_controller`, `test_api`, `test_mcp`, `test_identity`, `test_led`, `test_dashboard`, `test_network`, `test_button`, `test_bluetooth`, `test_schedule`, `test_appearance`, `test_keyboard`, `test_script`, `test_automation`, `test_mcp_ex`)
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
- Auth: every `/api/*` and `/mcp` request needs `Authorization: Bearer <token>`. `GET /` (the dashboard, one self-contained page generated into `src/dashboard_html.h` from the sources in `web/`) is public.
- `WifiSetup` and `HttpServer` hide their library headers behind a pimpl, because WiFiManager and ESPAsyncWebServer must not share a translation unit.

### Devices, schedules, network, Bluetooth

- **One BLE identity, one device (a mouse).** `AppearanceStore` (`appearance.*`, blob key `appearance`) holds a single `Appearance`: `profile` (`custom` by default, a neutral "Bluetooth Mouse" with the chip's own address, or a verified model from `catalog()` in `profiles.*`) plus optional name/manufacturer/VID/PID/MAC overrides. There is no device list and no keyboard. The dashboard's Appearance card sets the profile or a custom name; `GET/PUT /api/appearance` (MCP `get_appearance`/`set_appearance`) is a full replacement. Changes apply after a restart and need re-pairing.
- **Parked: scripts, automations, keyboard.** `script.*`, `automation.*`, `keymap.*`, the keyboard HID collection and their tests remain in `lib/core` but nothing wires them: `main.cpp` does not create them, so their REST routes answer 404 and their MCP tools are not listed. Last fully wired tree: git tag `v0.3.1-with-scripts-keyboard`.
- **Router entry points.** `handleApiEx(ApiContext&)` / `handleMcpEx(ApiContext&)` add routes/tools (`/api/bluetooth|schedules|token|network|appearance|restart`) and fall back to the original `handleApi` / the original six tools; services in `ApiContext` are optional (missing = 404 / tool not listed). Body caps: `kMaxBodyBytes` 4096 (MCP), `kMaxSmallBodyBytes` 1024 (config, network), `kMaxMediumBodyBytes` 2048 (schedules).
- **Timed runs and schedules.** `Controller::startFor(minutes)` stops by itself (deadline in `millis`, wrap-safe); a plain start or stop clears it. `pause()` / `pauseFor(minutes)` / `resume()` hold the jiggler; a pause **keeps** a time limit running (a deadline is a moment) and a timed pause resumes by itself (`resumeAtMs_`); `start()` while paused clears the limit, `resume()` does not. The controller **cannot run while `Platform::computerPaired()` is false** (boots stopped, `start` answers `no_computer_paired`, stops if every computer is forgotten). `ScheduleEngine` (`schedule.*`, blob key `schedules`) holds up to 8 weekly windows plus a timezone from a built-in table mapped to POSIX TZ strings; `tick()` (once a second from `loop()`) is edge-driven, except for the first look at the clock after boot, when the schedule is authoritative. Saving never starts or stops anything. The clock comes from `Platform::localTime(posixTz)` (`ArduinoPlatform`, SNTP-backed, serialised by a mutex; nullopt until synced).
- **Network modes.** `auto` = home WiFi with the `Jiggler-Setup` portal fallback; `ap` = own WPA2 network (default SSID `Jiggler`, password optional and generated on first start if empty) with a DNS captive portal redirecting to the dashboard. In AP mode the token defaults to the AP password. After the first portal setup the board restarts once (`WifiSetup::needsRestart`), because the portal leaves port 80 busy. BOOT (GPIO0): hold 3 s toggles the mode and restarts, a short tap opens a pairing window (`ButtonGesture`).
- **Bluetooth policy.** `BluetoothManager` (`bluetooth.*`, blob key `peers` for labels) owns pairing windows, "disconnect = drop link + hide for 3 min", forget and labels; it only talks to the stack through `BleLink`, implemented by `BleMouse`. **Pairing is always closed unless a window is open** (3 min, closes when a new bond appears); there is no first-run open state. `BleMouse` refuses in two places: IO capability is DisplayYesNo while closed and `onConfirmPIN` declines (stops even a known computer re-pairing), and `linkIdentified` drops unknown computers after the fact (`refuseNewPeer`); at most 3 bonds (`kMaxPairedComputers`) with 5 stack slots (`CONFIG_BT_NIMBLE_MAX_BONDS`). A gap listener sees the disconnect reason: the computer ending the link (0x213) hides the device like the dashboard's Disconnect. `PUT /api/token` goes through `TokenVault`.
- `NvsBlobStore` hashes keys to short NVS names and keeps an index blob, so blob keys can be long.

## Gotchas

- Do not read NVS (`Preferences`) from global constructors: NVS is not initialized during static init. Read it in `setup()` or later.
- Time comparisons use `(int32_t)(now - due)` because `millis()` wraps at ~49.7 days. Keep that pattern.
- Partition table is `partitions.csv`: two 1.875 MB app slots (OTA), NVS at the same 0x9000/20 KB as before so stored settings survive the switch. Changing the table needs a USB flash; OTA cannot change it. The firmware is ~1.47 MB (75% of a slot). `POST /api/update` (`HttpServer::Impl::handleUpload`, ESPAsyncWebServer multipart upload + `Update`) checks the token on the first chunk and writes nothing without it; there is no app rollback (no `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`), only the bootloader's fallback when an image is invalid.
- Identities live in `profiles.cpp` (catalog) and `appearance.cpp` (overrides). Add a profile only with VID/PID/OUI verified against public sources (`test_appearance` pins the current ones). The base MAC is set with `esp_base_mac_addr_set` before BLE init, which also changes the WiFi MAC. `identity.h` is only the legacy Logitech constants used by `test_identity`.
- The dashboard test checks that the page only calls routes `handleApiEx` serves (and that every route is referenced). Update `kPageRoutes` in `test/test_dashboard` when the page adds a call.
- REST and MCP bodies must be sent with `Content-Type: application/json`. `curl -d` alone sends form-encoded, which the HTTP library parses as params, so the device answers 415 `unsupported_media_type`. Use `curl -H 'Content-Type: application/json' -d ...`.
- HTTP body bytes from ESPAsyncWebServer can exceed the declared Content-Length; always copy through `copyBodyChunk`, never `memcpy` directly.
- Exact return-to-start is guaranteed in HID counts only. OS pointer acceleration can leave visible drift; this is unmeasured on a real host.

## Dashboard sources

Serve the page and PWA files from flash (`beginResponse` with a `const uint8_t*`), never as a `const char*`: that overload copies the whole 60+ KB page to the heap and the request dies with a connection reset. `web_assets.h` (manifest, icons) is generated too.


Edit `web/` (script order is `JS_ORDER` in `tools/build_web.py`) and run `python3 tools/build_web.py` (PlatformIO does it automatically before builds/tests); never edit `src/dashboard_html.h` by hand. Pure JS helpers live in `web/lib.js` with node tests: `node --test web/test/*.test.js`. UI work: `python3 tools/mock_device.py` (token `mocktoken1`, scenarios via `POST /__scenario/{firstrun|connected|idle|held|pairing|many}`).
