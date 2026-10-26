# Jiggler: functional spec

An ESP32 that keeps a computer awake by acting as a real Bluetooth mouse, controlled over WiFi from a dashboard, a REST API and an MCP server. This is the source of truth for what the product does; the how is in [docs/](docs/README.md).

## 1. Bluetooth mouse

- Presents itself as a mouse. By default a neutral "Bluetooth Mouse" (custom, the chip's own address); the owner can give it a name of their own or pick a real mouse model (name, vendor, product id, address prefix). There is a single device ([Appearance](docs/appearance.md)).
- To the operating system it is an ordinary Bluetooth mouse: nothing to install on the computer.
- One Bluetooth identity; up to 3 computers can be paired, one connected at a time.

## 2. Pairing and safety

- Pairing is **always closed** by default, also on a brand-new board and after "forget all".
- The owner opens **Discovery** (dashboard, API, MCP, a schedule action, or holding the button for 5 to 10 seconds). It lasts 180 seconds by default (30 to 600, configurable), closes by itself or when a computer pairs, and can be stopped early.
- A computer that tries to pair while it is closed is refused.
- The dashboard shows which computer is connected and since when; computers can be renamed, forgotten, disconnected.
- Four Bluetooth states: **Off** (radio silent), **Waiting** (on; only paired computers can find and reach the device, it is not discoverable), **Discovery** (a new computer can pair) and **Connected**. Switching Bluetooth off (dashboard, API, MCP, a schedule action, or the computer's own Disconnect when the setting says so) is stored.
- Everything except the dashboard page itself requires the API token.

## 3. Movement

- Moves the cursor at random intervals with human-like curved paths, smooth acceleration and a return to the start (no drift in mouse counts).
- Optional random left clicks and scrolls; never a right click.
- Configurable minimum/maximum interval and size.
- **The jiggler cannot run while no computer is paired**: it boots stopped on a new board, refuses to start, and stops if every computer is forgotten.

## 4. Running it

- Start/stop/**pause** (for a set time or until resumed; it keeps a timed run's deadline and the schedule's timetable), a test movement, and **timed runs**: 5, 10 or 30 minutes, an hour, until a time of day, or a custom number of minutes.
- **Weekly schedules** (up to 8): **windows** (days, start and end time, may cross midnight) and **actions** (start, stop, pause, resume, Bluetooth on/off, open Discovery at a time), each with an optional random shift of up to 120 minutes, repeating forever or a set number of times. A window open when saved starts the jiggler. Needs the clock.
- **Clock**: timezone, NTP server, or set by hand from the browser (lost at restart).

## 5. Network

- Joins the home WiFi; if it cannot, a setup network (`Jiggler-Setup`) collects the WiFi credentials and the API token. The board restarts once after the first setup.
- Optional own-network mode (its own protected WiFi with a captive portal) for places without WiFi; the button (30 s) toggles it.
- The Bluetooth mouse works independently of WiFi.

## 6. Dashboard

- Five sections: Status, Computers, Device (movement settings and appearance), Schedule, System. A guided first-time setup while nothing is paired.
- Phones and desktops, light and dark, installable to the home screen, English UI.
- Recovers by itself from restarts and lost connections; every error says what is wrong and what to do.

## 7. REST API and MCP

- The same controller behind both, so they cannot disagree: status, start/stop (optionally for N minutes), a test movement, configuration, Bluetooth, schedules, network, appearance, token, restart. See [docs/api.md](docs/api.md) and [docs/mcp.md](docs/mcp.md).

## 8. Physical indications

- Status LED: off = one short flash every 3 s, waiting = slow blink, Discovery = fast blink, connected and running = solid (brief off-flash per movement), connected but stopped = two short flashes every 3 s; it also shows what releasing the button would do.
- Button (BOOT / GPIO 0 by default, configurable): tap = start or stop the jiggler, hold 5 to 10 s and release = open Discovery, hold 30 s = switch network mode.
- USB power. After the first USB flash, firmware updates go over WiFi (OTA): from a file, or from the project's GitHub releases (default repository `carlosatta/jiggler`, changeable), either on demand, automatically, or ignored. The dashboard's About card links to the repository.

## Not included for now

A keyboard mode, scripts and automations were built and parked (see [development](docs/development.md#parked-modules)).

## Notes

- The identity is skin-deep: the device does not speak the vendor's proprietary protocol.
- Company-managed computers may detect new peripherals or activity patterns; check your policy.
