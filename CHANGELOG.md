# Changelog

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
