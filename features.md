# Jiggler ESP32 — Expected features

An ESP32-based device that keeps a computer awake by simulating a real Bluetooth mouse. Besides the base behaviour, it exposes control and status over WiFi through a dashboard, a REST API and an MCP server.

This is the baseline functional spec. Later additions (multiple devices, scripts, automations, AP mode) are described in [docs/devices.md](docs/devices.md), [docs/scripting.md](docs/scripting.md) and [docs/network.md](docs/network.md).

## 1. Bluetooth mouse

- Presents itself to the computer as a Logitech MX Master mouse (name, manufacturer and device identifiers consistent with a real model).
- Bluetooth address with a Logitech prefix, so that a check does not find an Espressif device.
- To the operating system it is a normal Bluetooth mouse: no software or driver to install on the computer being kept awake.

## 2. Movement

- Moves the cursor at random intervals, not on a fixed cadence, to avoid recognisable patterns.
- "Human-like" movement: curved trajectories, gradual acceleration and deceleration, micro-oscillations. No straight-line jumps at constant speed.
- The cursor returns close to its starting point, so it does not "drift" across the screen over the hours.
- Occasional random clicks or scrolls, to complete the activity profile.
- Configurable minimum and maximum interval.

## 3. WiFi connection

- The ESP32 joins the home WiFi network so that it is reachable from the dashboard, the API and MCP.
- If it cannot connect, it starts its own temporary network (access point) where the WiFi credentials can be entered the first time, without writing them in the code.
- Credentials are kept for subsequent restarts.
- The Bluetooth mouse keeps working independently of WiFi: if the network drops, the jiggler keeps working.

## 4. Web dashboard

A page reachable from a browser (on the local network) to control the device without technical tools:

- Real-time status: mouse connected or not, WiFi connected or not, time to the next movement.
- Start and pause the jiggler.
- Edit the intervals (minimum/maximum) and the movement amplitude.
- A button to force a test movement right away.
- Counter of movements performed and time of the last one.

## 5. REST API

An interface to control the device from scripts, automations or other programs:

- Read the full status.
- Start / stop / pause.
- Immediate movement on request.
- Read and change the configuration (intervals, amplitude, behaviour).

## 6. MCP server

The device exposes an MCP server, so it can be commanded directly by an assistant such as Claude:

- Ask for the status ("is the jiggler active? how long until the next movement?").
- Start, pause or stop.
- Change the configuration by voice.
- Make the mouse move on request.

## 7. Physical indications

- Status LED: shows whether the mouse is connected and flashes on every movement.
- USB power (wall adapter or monitor USB port).

## Notes

- The emulation is skin-deep: name and identifiers match an MX Master, but the device does not speak Logitech's proprietary protocol (HID++). To the computer it is a fully fledged Bluetooth mouse; software such as Logi Options+ would not recognise it as a real MX Master.
- On company-managed computers there may be tools that detect new devices or activity patterns: worth keeping in mind.
