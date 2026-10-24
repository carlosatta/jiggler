# Changelog

## 0.5.0

- **Pause.** Besides Start and Stop, **Pause** holds the jiggler for 15 minutes, 30 minutes, 1 or 2 hours, or until you press **Resume**. While paused nothing moves; a "run for" deadline keeps counting and schedules keep their timetable. `POST /api/pause {"minutes": n}`, `POST /api/resume`, `resumesInMs` in the status, MCP `pause` (with `minutes`) and `resume`.
- **Device section.** "Movement" is now **Device** and holds the movement settings and **Appearance**, which has its spoofing fields back: name, manufacturer, vendor and product ID, Bluetooth address, each with the model's value shown in grey. **Custom** is the first choice.
- **More known mice** in the catalog (verified against the Linux kernel's tables): Logitech MX Master 3, 4 and 2S, MX Anywhere 3 and 3S, MX Vertical, M720 Triathlon; Apple Magic Mouse and Magic Mouse (USB-C).
- **Schedules are easier to use.** Set a start and an **end time** instead of a number of minutes, and choose how often it runs: **forever** or a **limited number of times** (it turns itself off after the last run; a run is counted when its window opens, and the count survives a restart). API: `times` and `remaining` on each schedule.
- **Firmware updates over WiFi (OTA).** System → Firmware update takes a `.bin` built with `pio run -e esp32`; `POST /api/update` (multipart, Bearer token) does the same from a script. **The first time needs a USB flash**, because the partition table changes to two application slots (`partitions.csv`); the token, WiFi, pairings and schedules are kept. After that no cable is needed.
- **One device, configured as a single "Appearance"**, not a list. The default is **Custom**: a neutral "Bluetooth Mouse" with the chip's own address, not a Logitech. Pick a mouse model or your own name. API: `GET/PUT /api/appearance` replaces `/api/devices` and `/api/profiles`; MCP: `get_appearance` / `set_appearance`. **Upgrading a board that used the old default changes its Bluetooth name and address, so computers must pair again.**
- **Bluetooth stays visible.** A watchdog restarts advertising if it ever stops while nothing is connected (the board could stay online but invisible to the computer after a series of drops).
- Removed the fixed connection parameters added in 0.4.0: they made the periodic `0x28` drops far more frequent (every 10 to 40 s instead of every 7 to 13 min).

## 0.4.0

- **Mouse only.** The keyboard mode is gone from the dashboard, the API and the device list; keyboards are rejected. **Scripts and automations** are removed from the product (dashboard, REST, MCP, firmware wiring). The code stays in the repository, parked, and in the tag `v0.3.1-with-scripts-keyboard`.
- **Schedule has its own section** with the timezone, separate from Movement (five sections: Status, Computers, Movement, Schedule, System).
- **Appearance** replaces "Bluetooth identity": pick a mouse model or a custom name, with one plain sentence about what changing it means (restart, pair again). Advanced overrides remain in the API only. (Superseded by 0.5.0: one device, default Custom.)
- The own-network password field is empty unless one was set, with an explanation instead of placeholder text.
- First-setup fixes: the board restarts once after the WiFi portal (the dashboard could not bind port 80), and the API token is no longer written to the serial log.
- Documentation rewritten with real screenshots (neutral data): [README](README.md), [getting started](docs/getting-started.md), [dashboard tour](docs/dashboard.md).

## 0.3.1

- **The jiggler cannot run with no computer paired.** A board with nothing paired boots stopped; `start` answers `409 no_computer_paired`; forgetting every computer stops a running jiggler; a schedule window that opens before the first pairing starts the jiggler once a computer pairs.
- **First-time setup** on the Status page while nothing is paired: connect, open pairing, choose the device on the computer; the page switches to the normal view by itself when it connects.
- **Pairing is never open by default.** The old "no computer paired yet: pairing stays open" first-run state is gone, so a brand-new board, a board whose bond list emptied (a stale bond dropped by the stack) and a board after "Forget all" all refuse pairing until you open a window (dashboard or BOOT tap). The `firstRun` field is removed from the API.
- Pressing Disconnect on the computer now hides the device for 3 minutes instead of being undone by an immediate reconnect.
- Fixes from review: remaining-time counters can no longer underflow, forgetting a computer during an open window, schedule saved before the clock exists, DST-safe "until a time", locale-independent mock.
- Serial log lines for pairing decisions and disconnect reasons.

## 0.3.0

- **Timed runs.** Start for 5, 10 or 30 minutes, an hour, until a time of day or a custom number of minutes (`POST /api/start {"minutes": n}`, `stopsInMs` in the status, `minutes` on the MCP `start` tool).
- **Weekly schedules.** Up to 8 windows (days, start, duration, may cross midnight) with a timezone; they start and stop the jiggler by the clock. `GET/PUT /api/schedules`, MCP `get_schedules` / `set_schedules`, `schedule` block in the status. Needs a synced clock (home WiFi with internet).
- **Pairing is always 3 minutes**, with a countdown and a stop button, and it closes by itself as soon as a computer pairs.
- **Disconnect hides the device for 3 minutes**, not indefinitely, so a computer can reconnect afterwards (`heldRemainingMs`).
- The dashboard is now entirely in English, including the WiFi setup portal field. Left-click only is pinned by a test: nothing can right-click.

## 0.2.0

- **Bluetooth control.** Pairing is closed once a computer is paired; open a timed window from the dashboard, the API, MCP or a short press of BOOT. New dashboard view shows which computer is connected and since when, with disconnect (drop and hide), reconnect, rename and forget. Unknown computers that try to pair while the window is closed are refused.
- **Dashboard redesign.** Four sections (Stato, Computer, Movimento, Sistema), connection-first home, inline confirmations, offline and restart recovery, light/dark, phone bottom bar and desktop side rail. Split into `web/` modules with JS tests.
- **API token change** (`PUT /api/token`, Sistema → Sicurezza). Version and BLE identity in `GET /api/status`.
- **Installable page.** Web manifest and icons served by the board.
- Pairing is refused for unknown computers while closed (also when they do not ask to bond) and capped at 3 computers; disconnect closes an open window; forgetting a connected computer drops its link; default names never repeat.
- LED: fast blink while pairing is open, short flash every 3 s while hidden. BOOT: tap = pairing, hold 3 s = network mode.
- Fixes: the page is streamed from flash (the heap copy broke once the page grew).

## 0.1.0

Firmware, dashboard, REST and MCP; devices and profiles; scripts and automations; AP mode with captive portal.
