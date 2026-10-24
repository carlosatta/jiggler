# Development

## Setup

Install PlatformIO (`pipx install platformio`; `pio` lives in `~/.local/bin`).

Hardware: a classic `esp32dev` board (ESP32-WROOM), USB cable. Nothing else is needed; the status LED is the on-board one (GPIO 2) and the BOOT button is GPIO 0.

## Commands

| Task | Command |
|---|---|
| Host unit tests (no board) | `pio test -e native` |
| One suite | `pio test -e native -f test_controller` |
| Build firmware | `pio run -e esp32` |
| Flash over USB | `pio run -e esp32 -t upload --upload-port /dev/cu.usbserial-0001` |
| Flash over WiFi (OTA) | `curl -H "Authorization: Bearer $TOKEN" -F firmware=@.pio/build/esp32/firmware.bin http://jiggler.local/api/update` |
| Serial monitor (115200 baud) | `pio device monitor` |

Suites: `test_config`, `test_movement`, `test_controller`, `test_api`, `test_mcp`, `test_identity`, `test_led`, `test_dashboard`, `test_network`, `test_button`, `test_bluetooth`, `test_schedule`, `test_appearance`, `test_mcp_ex`, plus `test_keyboard`, `test_script` and `test_automation` for the parked modules (see below).

Adjust the upload port to your system (`pio device list`).

## Debug build flags

Pass them via `PLATFORMIO_BUILD_FLAGS="..."`. Never commit them, and reflash a normal build afterwards.

| Flag | Effect |
|---|---|
| `-DJIG_FAKE_CONNECTED` | Fakes "mouse connected" |
| `-DJIG_DEBUG` | Logs each plan with the task's stack high-water mark |
| `-DJIG_FORCE_AP` | Boots in AP mode without saving it |

Example: `PLATFORMIO_BUILD_FLAGS="-DJIG_DEBUG" pio run -e esp32 -t upload`.

## Layout

```
lib/core/src/   hardware-free logic (controller, movement, schedules, Bluetooth policy, devices, routers)
src/            Arduino adapters: BLE, NVS, WiFi, HTTP server, LED, generated dashboard and web assets
test/           host unit tests (Unity) and fakes in test/support/
spike/          throwaway experiments (not part of the build)
docs/           documentation; docs/superpowers has design history (Italian)
```

## Parked modules

Scripts, automations and the keyboard mode were built and then taken out of the product to keep it focused. Their code and tests remain (`script.*`, `automation.*`, `keymap.*`, the keyboard report map, `test_script`, `test_automation`, `test_keyboard`) but nothing wires them into the firmware, the REST API, MCP or the dashboard. The last tree that had them wired is the git tag `v0.3.1-with-scripts-keyboard`.

## Screenshots

The images in `docs/images/` are real screenshots of the dashboard page, served by `tools/mock_device.py` with neutral data ("Computer 1", made-up addresses) so they contain nothing personal. Start the mock (`python3 tools/mock_device.py --port 8765`, token `mocktoken1`), force a state with `curl -X POST localhost:8765/__scenario/<firstrun|connected|idle|held|pairing|many>` (and `/__pair` to simulate a computer pairing while a window is open), and capture the pages with a browser at 390x844 (phone) and 1280x800 (desktop) at 2x scale.

## Conventions

- New logic goes in `lib/core/src` with a test first; keep Arduino headers out of it.
- The dashboard sources are in `web/` (`index.html`, `app.css`, `lib.js` for pure helpers, `core.js`, `view_*.js`, `main.js`, `manifest.webmanifest`, `icons/`). `tools/mock_device.py` serves the page against an in-memory imitation of the API, with forced scenarios, to develop and screenshot the UI without a board. `tools/build_web.py` inlines them into the generated `src/dashboard_html.h`; PlatformIO runs it before every build and test (`tools/pio_web.py`), so never edit the header by hand. JS helpers have node tests: `node --test web/test/*.test.js`. `test_dashboard` checks that the page only calls routes `handleApiEx` serves; when the page adds a call, update `kPageRoutes` in `test/test_dashboard` (the test also checks every route is referenced).
- Route bodies are limited and validated at the boundary; see [api.md](api.md).
- Add a mouse profile only with VID/PID/OUI verified against public sources.
