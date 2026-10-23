# Jiggler

An ESP32 that keeps a computer awake by acting as a real Bluetooth mouse (and, optionally, keyboard). It moves the cursor the way a person would, and you control it over WiFi from a web dashboard, a REST API or an MCP server, so an assistant like Claude can drive it.

The computer sees an ordinary Bluetooth device: nothing to install on it. By default the device presents itself as a Logitech MX Master 3S.

## Features

- **Human-like movement.** Curved Bézier paths with smooth acceleration and deceleration, random intervals, occasional clicks and scrolls, and a return to the starting point so the cursor does not drift.
- **Configurable identity.** Pick a mouse or keyboard profile (Logitech, Razer, Apple) or define a custom one with name, manufacturer, VID/PID and Bluetooth address.
- **Scripts and automations.** Small JSON programs (`move`, `click`, `scroll`, `key`, `type`, `wait`, `repeat`) that you can run on demand or on a random schedule, e.g. tap F15 every few minutes.
- **Control surfaces.** Web dashboard, REST API and MCP server, all backed by the same controller.
- **Works without WiFi.** The Bluetooth side is independent of the network. If there is no home WiFi, the device can create its own access point with a captive portal.
- **Timed runs and schedules.** Run for 5, 10 or 30 minutes, until a time, or a custom length; or define weekly windows ("Mon–Fri 09:00 for 8 hours") that start and stop it for you. See [docs/schedules.md](docs/schedules.md).
- **Pairing under your control.** The dashboard shows which computer is connected and since when, lets you open a 3-minute pairing window, disconnect (without powering off; the device stays hidden for 3 minutes), rename and forget computers. See [docs/bluetooth.md](docs/bluetooth.md).
- **Status LED.** Slow blink while no computer is connected; solid when connected, with a brief flash on each movement; fast blink while pairing is open; short flash every 3 s while hidden.
- **Installable page.** On a phone, "Add to Home Screen" opens the dashboard full-screen (web manifest and icons are served by the board).

## Quick start

You need a classic ESP32 dev board (`esp32dev`) and [PlatformIO](https://platformio.org) (`pipx install platformio`).

```sh
pio test -e native                     # unit tests on your computer, no board needed
pio run -e esp32 -t upload             # build and flash (add --upload-port if needed)
pio device monitor                     # serial log at 115200 baud
```

Then:

1. **Join the setup network.** On first boot the board opens a WiFi access point called `Jiggler-Setup`. Connect to it, pick your WiFi network, and set an API token.
2. **Pair it.** On the computer, pair the Bluetooth device named *MX Master 3S*. Pairing is **always closed** until you open a window: tap BOOT once on the board, or press *Open pairing* in the dashboard (**Computers**). The window lasts 3 minutes and closes as soon as a computer pairs. Do this first, then choose the device on the computer.
3. **Open the dashboard** at `http://jiggler.local/` and enter the token. Press **Start**.

```sh
TOKEN=your-token
curl -H "Authorization: Bearer $TOKEN" http://jiggler.local/api/status
curl -H "Authorization: Bearer $TOKEN" -X POST http://jiggler.local/api/start
curl -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' \
     -X PUT -d '{"minIntervalSec":20,"maxIntervalSec":60}' http://jiggler.local/api/config
```

BOOT button: **tap** = open pairing for 3 minutes; **hold 3 seconds** = switch to AP mode.

No home WiFi? Hold the BOOT button for 3 seconds to switch to AP mode: the board creates a `Jiggler` network (password printed on the serial monitor). See [docs/network.md](docs/network.md).

## Using it from an assistant

```sh
claude mcp add --transport http jiggler http://jiggler.local/mcp \
  --header "Authorization: Bearer <token>"
```

Then ask: "is the jiggler running?", "pause it", "move every 20 to 45 seconds". See [docs/mcp.md](docs/mcp.md).

## Documentation

See the [docs index](docs/README.md): [features](features.md), [architecture](docs/architecture.md), [REST API](docs/api.md), [Bluetooth](docs/bluetooth.md), [schedules](docs/schedules.md), [MCP](docs/mcp.md), [scripting](docs/scripting.md), [devices](docs/devices.md), [network and security](docs/network.md), [development](docs/development.md).

## Limitations

- **One Bluetooth identity.** A classic ESP32 has a single Bluetooth address, so mouse and keyboard are functions of one composite device rather than two separate devices ([details](docs/devices.md#limitations)).
- **The identity is skin-deep.** The device does not implement Logitech's HID++ protocol; software like Logi Options+ will not see it as a real MX Master.
- **Plain HTTP.** Authentication is a Bearer token over unencrypted HTTP: use it on a network you trust. Browsers only offer the full "Install app" prompt on HTTPS, so on a LAN board you get "Add to Home Screen" (standalone window) rather than an offline-capable app.
- **Pairing is Just Works.** The board has no display, so anyone in range while a pairing window is open can pair; keep windows short.
- **Computers are numbered, not named.** Bluetooth does not reveal the computer's name to a peripheral; rename them in the dashboard.
- **No OTA updates.** Flash over USB.
- **Cursor drift is unmeasured.** The cursor returns exactly in HID counts, but OS pointer acceleration can leave visible drift on a real host.

## Responsible use

This is meant for keeping your own computer awake. Company-managed machines may run tools that detect new peripherals or activity patterns, and many workplaces have policies on this. Check yours before using it on a managed device.

## Project status

Firmware, dashboard, REST and MCP are implemented and covered by host unit tests; hardware behaviour is verified on a board by hand. The functional spec is [features.md](features.md); design history (in Italian) is in [docs/superpowers](docs/superpowers).
