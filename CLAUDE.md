# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository state

Pre-implementation. The only file is [funzionalita.md](funzionalita.md), the functional spec (in Italian). There is no source code, build system, firmware framework choice, or git repository yet, so there are no build/lint/test commands to document. When scaffolding, pick the toolchain (e.g. ESP-IDF vs. Arduino/PlatformIO) deliberately and add its commands here. The spec does not prescribe one.

Treat `funzionalita.md` as the source of truth for requirements. Keep documentation and UI strings in Italian unless told otherwise.

## What the project is

An ESP32 device that keeps a computer awake by acting as a real Bluetooth mouse, with control and status exposed over WiFi (web dashboard, REST API, MCP server).

## Architecture constraints from the spec

These span multiple subsystems, so they matter when designing any one of them:

- **Bluetooth HID and WiFi are independent.** The mouse must keep jiggling if WiFi drops. WiFi/network code must never block or gate the movement loop.
- **Three control surfaces, one state.** Dashboard, REST API and MCP server expose the same capabilities: read full status, start/stop/pause, trigger an immediate movement, read/modify config (min/max interval, movement amplitude, behavior). Implement the control logic once and have all three call into it.
- **Movement engine** (not a fixed-cadence jiggle):
  - random intervals between configurable min and max
  - curved, human-like trajectories with gradual acceleration/deceleration and micro-oscillation (no constant-speed straight lines)
  - cursor returns near its starting point so it does not drift over hours
  - occasional random clicks/scrolls
- **Device identity.** Presents as a Logitech MX Master (name, manufacturer, device IDs, Logitech BT address prefix). This is cosmetic only: HID++ is not implemented, so Logi Options+ will not recognize it as a real MX Master. Standard BT HID mouse only, with no host-side software or drivers.
- **WiFi provisioning.** On connection failure, start a temporary access point to enter credentials (never hardcoded in source); persist credentials across reboots.
- **Dashboard** shows live state (mouse connected, WiFi connected, time to next movement, movement count, last movement time) and offers start/pause, interval/amplitude editing, and a "test movement" button.
- **Hardware:** status LED (BT connection state, blinks on each movement); USB powered.
