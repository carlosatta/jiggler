# Architecture

All logic lives in `lib/core/src` with no Arduino headers, so it runs under `pio test -e native` on a laptop. `src/` holds thin hardware adapters that are verified on the board.

```
 Dashboard (HTML)   REST (/api/*)    MCP (/mcp)
        \               |               /
         \              v              /
          +----->  handleApiEx / handleMcpEx  <-----+   (json_codec shared by REST and MCP)
                        |
        +---------------+----------------+-------------+
        v               v                v             v
   Controller     DeviceRegistry    ScriptLibrary  AutomationEngine
   (run state,    (devices,         (JSON scripts, (random schedule,
    config, tick)  identity)         NVS)           run now)
        |                                              |
        +---------------> Plan / Step <----------------+
                              |
                     runPlan -> MouseSink (BleMouse: mouse + keyboard over NimBLE)
```

## Core pieces

- **`Controller`** is the single control surface: status, start/pause/stop, `moveNow`, `updateConfig`, `tick()`. Thread-safe (one mutex). The dashboard, REST and MCP all go through it, so they cannot disagree.
- **`planActivity`** turns a `Config` into a `Plan`: Bézier strokes with minimum-jerk easing, dwell, optional click and net-zero scroll, then a return stroke. The net `Move` displacement of a plan is exactly (0,0). `runPlan` executes it and aborts if the mouse disconnects.
- **`compileScript`** compiles user JSON into the same `Plan`/`Step` model (extra step kinds `KeyDown`, `KeyUp`, `Wait`). All limits are enforced at compile time.
- **`AutomationEngine`** schedules scripts at random intervals (only while the run state is `running`) and runs them on demand. The movement task calls `Controller::tick()` first, then the engine.
- **`DeviceRegistry`** holds the device list; `buildReportMap` builds the composite HID report map (mouse report ID 1, keyboard report ID 2).
- **Router entry points.** `handleApiEx(ApiContext&)` and `handleMcpEx(ApiContext&)` add the newer routes/tools and fall back to the original `handleApi` and the original six MCP tools. Services in `ApiContext` are optional: a missing one means 404 / tool not listed.

## Seams

`Platform`, `Rng`, `MouseSink`, `ConfigStore`, `BlobStore` and `NetworkStore` separate core from hardware. Firmware implements them in `arduino_platform.h`, `ble_mouse.*`, `nvs_config_store.*`, `nvs_blob_store.*` and `nvs_network_store.*`. Test fakes live in `test/support/fakes.h`.

`NvsBlobStore` hashes keys to short NVS names and keeps an index blob, so blob keys can be long (`script/<name>`).

## Tasks and startup

- The movement task (`movement_task.cpp`, core 1) calls `Controller::tick()` every 50 ms and runs plans. It is independent of WiFi, and `setup()` starts it **before** `wifi.begin()` because WiFi connection can block for seconds.
- `WifiSetup` (tzapu WiFiManager, non-blocking) also collects the API token. `HttpServer` starts only once WiFi is connected and the portal is closed, because both want port 80.
- `WifiSetup` and `HttpServer` hide their library headers behind a pimpl: WiFiManager and ESPAsyncWebServer must not share a translation unit.

## Gotchas

- Do not read NVS (`Preferences`) from global constructors: NVS is not initialised during static init. Read it in `setup()` or later.
- Time comparisons use `(int32_t)(now - due)` because `millis()` wraps at ~49.7 days.
- Partition table is `huge_app.csv` (3 MB app, no OTA); the default 1.25 MB partition was 95% full.
- HTTP body bytes from ESPAsyncWebServer can exceed the declared Content-Length; always copy through `copyBodyChunk`.
- Exact return-to-start is guaranteed in HID counts only. OS pointer acceleration can leave visible drift; this has not been measured on a real host.

## Further reading

Design history lives in [superpowers/](superpowers/): the [firmware plan](superpowers/plans/2026-10-23-jiggler-firmware.md), the [multi-device design](superpowers/specs/2026-10-23-multi-device-design.md) and its [plan](superpowers/plans/2026-10-23-multi-device.md), and the [multi-identity spike](superpowers/specs/2026-10-23-multi-identity-spike.md). Those files are in Italian.
