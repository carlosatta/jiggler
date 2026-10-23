# Architecture

All logic lives in `lib/core/src` with no Arduino headers, so it runs under `pio test -e native` on a laptop. `src/` holds thin hardware adapters that are verified on the board.

```
 Dashboard (HTML)   REST (/api/*)    MCP (/mcp)
        \               |               /
         \              v              /
          +----->  handleApiEx / handleMcpEx  <-----+   (json_codec shared by REST and MCP)
                        |
     +---------+--------+---------+-----------+
     v         v                  v           v
 Controller  BluetoothManager  ScheduleEngine  AppearanceStore
 (run state, (pairing windows, (weekly windows, (how the device
  timed runs) disconnect hold,  timezone)        appears)
     |        peers)                |
     |            |                 +--> Controller (start / stop)
     v            v (BleLink)
 Plan / Step   BleMouse (NimBLE)
     |
 runPlan -> MouseSink (BleMouse)
```

## Core pieces

- **`Controller`** is the single control surface: status, start/pause/stop, `moveNow`, `updateConfig`, `tick()`. Thread-safe (one mutex). The dashboard, REST and MCP all go through it, so they cannot disagree.
- **`planActivity`** turns a `Config` into a `Plan`: Bézier strokes with minimum-jerk easing, dwell, optional click and net-zero scroll, then a return stroke. The net `Move` displacement of a plan is exactly (0,0). `runPlan` executes it and aborts if the mouse disconnects.
- **`Controller::startFor(minutes)`** runs for a while and stops by itself (a wrap-safe deadline in `millis`). It refuses to run, and stops, while no computer is paired.
- **`ScheduleEngine`** holds up to 8 weekly windows and a timezone (mapped to a POSIX TZ string). Once a second it compares the local time with the windows and starts or stops the controller on the edges; the first look after boot is authoritative, saving never starts or stops anything.
- **`BluetoothManager`** owns the pairing policy: pairing is always closed unless a 3-minute window is open, which closes when a computer pairs; "disconnect" hides the device for 3 minutes; forget, labels. It only talks to the stack through `BleLink`, which `BleMouse` implements.
- **`AppearanceStore`** holds the one device's appearance (the default custom identity, a model from the catalog, or overrides); `buildReportMap` builds the HID report map.
- **Router entry points.** `handleApiEx(ApiContext&)` and `handleMcpEx(ApiContext&)` add the newer routes/tools and fall back to the original `handleApi` and the original six MCP tools. Services in `ApiContext` are optional: a missing one means 404 / tool not listed.

## Seams

`Platform`, `Rng`, `MouseSink`, `ConfigStore`, `BlobStore` and `NetworkStore` separate core from hardware. Firmware implements them in `arduino_platform.h`, `ble_mouse.*`, `nvs_config_store.*`, `nvs_blob_store.*` and `nvs_network_store.*`. Test fakes live in `test/support/fakes.h`.

`NvsBlobStore` hashes keys to short NVS names and keeps an index blob, so blob keys can be long.

## Tasks and startup

- The movement task (`movement_task.cpp`, core 1) calls `Controller::tick()` every 50 ms and runs plans. It is independent of WiFi, and `setup()` starts it **before** `wifi.begin()` because WiFi connection can block for seconds.
- `WifiSetup` (tzapu WiFiManager, non-blocking) also collects the API token. `HttpServer` starts only once WiFi is connected and the portal is closed, because both want port 80. After the **first** setup the board restarts once, because the portal's web server leaves port 80 busy and the dashboard cannot bind it in the same boot.
- `WifiSetup` and `HttpServer` hide their library headers behind a pimpl: WiFiManager and ESPAsyncWebServer must not share a translation unit.

## Gotchas

- Do not read NVS (`Preferences`) from global constructors: NVS is not initialised during static init. Read it in `setup()` or later.
- Time comparisons use `(int32_t)(now - due)` because `millis()` wraps at ~49.7 days.
- Partition table is `huge_app.csv` (3 MB app, no OTA); the default 1.25 MB partition was 95% full.
- HTTP body bytes from ESPAsyncWebServer can exceed the declared Content-Length; always copy through `copyBodyChunk`.
- Exact return-to-start is guaranteed in HID counts only. OS pointer acceleration can leave visible drift; this has not been measured on a real host.

## Further reading

Design history lives in [superpowers/](superpowers/): the [firmware plan](superpowers/plans/2026-10-23-jiggler-firmware.md), the [multi-device design](superpowers/specs/2026-10-23-multi-device-design.md) and its [plan](superpowers/plans/2026-10-23-multi-device.md), and the [multi-identity spike](superpowers/specs/2026-10-23-multi-identity-spike.md). Those files are in Italian.
