# Changelog

## 0.2.0

- **Bluetooth control.** Pairing is closed once a computer is paired; open a timed window from the dashboard, the API, MCP or a short press of BOOT. New dashboard view shows which computer is connected and since when, with disconnect (drop and hide), reconnect, rename and forget. Unknown computers that try to pair while the window is closed are refused.
- **Dashboard redesign.** Four sections (Stato, Computer, Movimento, Sistema), connection-first home, inline confirmations, offline and restart recovery, light/dark, phone bottom bar and desktop side rail. Split into `web/` modules with JS tests.
- **API token change** (`PUT /api/token`, Sistema → Sicurezza). Version and BLE identity in `GET /api/status`.
- **Installable page.** Web manifest and icons served by the board.
- LED: fast blink while pairing is open, short flash every 3 s while hidden. BOOT: tap = pairing, hold 3 s = network mode.
- Fixes: the page is streamed from flash (the heap copy broke once the page grew).

## 0.1.0

Firmware, dashboard, REST and MCP; devices and profiles; scripts and automations; AP mode with captive portal.
