# Changelog

## 0.7.3

- **Installing from the repository now completes.** In 0.7.2 the download stopped half way (or crashed near the end) because the device did not have enough free memory for the secure connections. Three changes: the Bluetooth stack is shut down for the download (it holds most of the spare memory) and the device restarts afterwards, whether the install worked or not (if it failed, the reason is kept and shown on the dashboard after the restart); the release's SHA-256 is taken from the release information (GitHub reports each file's `digest`), so the separate `.sha256` file is only fetched when that is missing, which cuts four secure connections to two; and the install runs on a temporary task with a larger stack. Checked on a board: 0.7.0 → 0.7.2 from this repository's release in 42 seconds.
- While it installs, the jiggler stops and Bluetooth is off (the Mac sees a disconnect); both come back after the restart.

## 0.7.2

- **The update download uses one secure connection at a time.** GitHub serves a release file through a redirect to another host; the first connection is now closed before the second opens, so the download needs less memory, and every step (which host, how many bytes, how long, why it stopped) is written to the event log (`GET /api/log`), including the reason when an install fails. A failure is also reported on the dashboard.
- The repository moved to a new GitHub repository with the same address (`carlosatta/jiggler`) and a clean history; releases start again from v0.7.2.

## 0.7.1

- **The daily look has a time.** In Firmware updates, **Look for updates at** (default 03:00, on the device's clock and timezone) says when the device checks for a new release and, in automatic mode, installs it, so it can be a quiet hour. `checkTime` in `GET/PUT /api/updates` and in `set_updates`. A board that was off at that time looks about ten minutes after it is back; a failed look is retried every hour.
- The first release that devices can install from the repository: v0.7.0 was only a build for testing the updater on a board.

## 0.7.0

- **Firmware updates from GitHub releases.** System → **Firmware updates** looks at the latest release of a repository (default `https://github.com/carlosatta/jiggler`) about once a day. You choose what happens when a new version appears: **tell me and I install it** (default), **install it automatically**, or **ignore it**; you can also press **Check for updates** whenever you like. The repository is a setting (use your own fork, or **Use the default**). Status shows a note when a version is available. `GET/PUT /api/updates`, `POST /api/updates/check|install`, MCP `get_updates` / `set_updates` / `check_updates` / `install_update`, and `update` in the status.
  - The download is over HTTPS, verified against the standard public certificate authorities (a 55 KB list embedded in the firmware, `tools/make_cert_bundle.py`), and checked against the release's `.sha256`. It needs the clock to be set. A version that was installed but did not take effect is not retried automatically.
  - Releases are built by `.github/workflows/release.yml`, which attaches `jiggler-esp32.bin` and its checksum when you publish a release whose tag matches `JIG_VERSION`. The firmware file upload remains, as **Install from a file**.
  - **Only follow a repository you trust**: whoever can publish a release there controls what runs on the device.
- **About** now has a **Source code** link to the repository page.
- The firmware grew by about 230 KB (HTTPS) and now uses about 90% of an update slot.
- `GET /api/log` also records why the device last restarted (power, crash, brownout...) and the update steps.
- Fixed the dashboard test that forbade any `https://` text: links to GitHub are allowed, outside resources are still not.
## 0.6.0

- **Four clear Bluetooth states.** **Off** (the radio is silent: invisible, nothing can connect), **Waiting** (on; only computers that are already paired can find and reach the device), **Discovery** (a window is open so a new computer can pair) and **Connected**. Status, Computers, the LED, the API (`state`) and MCP all use them. "Disconnect" and "Reconnect" are gone: **Turn Bluetooth off / on** (`POST /api/bluetooth/disable|enable`, MCP `disable_bluetooth` / `enable_bluetooth`; the choice is stored). The computer's own Disconnect also switches Bluetooth off by default (`offWhenComputerDisconnects`, untick it on Computers); a computer that disconnects when it goes to sleep does the same, so untick it if your computer does not reconnect after waking. `held` and `heldRemainingMs` are removed from the API.
- **Strangers no longer see the device.** In Waiting the device advertises as non-discoverable and without its name, so it should not show up in other computers' "nearby devices" lists (it used to appear there even while pairing was closed, although pairing was refused). Only Discovery makes it discoverable and named.
- **Discovery length is configurable** (30 to 600 s, default 180) on Computers, `PUT /api/bluetooth/settings`. "Pairing" is called Discovery in the dashboard.
- **A button on the board.** Tap = start or stop the jiggler (three LED blinks). Hold 5 to 10 s and let go = open Discovery. Hold 30 s = switch the network mode (it used to be 3 s, and a tap used to open pairing). The pin is configurable (default the BOOT button, GPIO 0) in **System → Button**, `GET/PUT /api/button`, MCP `get_button` / `set_button`. The LED shows what letting go would do while you hold.
- **LED patterns** per state: off = one short flash every 3 s, waiting = slow blink, Discovery = fast blink, connected and running = solid, connected but stopped or paused = two short flashes every 3 s.
- **Time settings.** New **System → Time**: timezone (moved here from Schedule), NTP server (default `pool.ntp.org`, empty to stop using NTP) and **Set the time from this browser** for a board with no internet (lost at every restart). `GET/PUT /api/time`, `POST /api/time/set`, MCP `get_time` / `set_time`; `time` in the status. The timezone is no longer part of `/api/schedules`; a timezone saved by the old Schedule page is kept.
- **Schedules redesigned.** Two kinds: a **window** (start to end, runs the jiggler) and an **action** at a time (start, stop, pause, resume, Bluetooth on or off, open Discovery). Every entry can have a **random shift** of up to 120 minutes either way, drawn afresh each day. API: `kind`, `action`, `jitterMin`; `actions` in the GET.
- **Fix: a schedule saved while its window is open never started.** Saving used to mark an open window as "already open", so the jiggler did not start and the Start button stayed as it was. A window you add or change that is open at that moment now starts the jiggler (an untouched window you stopped by hand stays stopped). When an open window cannot start because no computer is paired, Status says so (`schedule.blocked`).
- Schedule body limit raised to 4096 bytes.
- **Event log over WiFi.** `GET /api/log` returns the last 80 things the device did (Bluetooth links, pairing decisions, advertising, the button, a heartbeat every 5 minutes with the free memory), so a connection problem can be diagnosed without a USB cable.

## 0.5.0

- **Pause.** Besides Start and Stop, **Pause** holds the jiggler for 15 minutes, 30 minutes, 1 or 2 hours, or until you press **Resume**. While paused nothing moves; a "run for" deadline keeps counting and schedules keep their timetable. `POST /api/pause {"minutes": n}`, `POST /api/resume`, `resumesInMs` in the status, MCP `pause` (with `minutes`) and `resume`.
- **Device section.** "Movement" is now **Device** and holds the movement settings and **Appearance**, which has its spoofing fields back: name, manufacturer, vendor and product ID, Bluetooth address, each with the model's value shown in grey. **Custom** is the first choice.
- **More known mice** in the catalog (verified against the Linux kernel's tables): Logitech MX Master 3, 4 and 2S, MX Anywhere 3 and 3S, MX Vertical, M720 Triathlon; Apple Magic Mouse and Magic Mouse (USB-C).
- **Schedules are easier to use.** Set a start and an **end time** instead of a number of minutes, and choose how often it runs: **forever** or a **limited number of times** (it turns itself off after the last run; a run is counted when its window opens, and the count survives a restart). API: `times` and `remaining` on each schedule.
- **Firmware updates over WiFi (OTA).** System → Firmware update takes a `.bin` built with `pio run -e esp32`; `POST /api/update` (multipart, Bearer token) does the same from a script. **The first time needs a USB flash**, because the partition table changes to two application slots (`partitions.csv`); the token, WiFi, pairings and schedules are kept. After that no cable is needed.
- **One device, configured as a single "Appearance"**, not a list. The default is **Custom**: a neutral "Bluetooth Mouse" with the chip's own address, not a Logitech. Pick a mouse model or your own name. API: `GET/PUT /api/appearance` replaces `/api/devices` and `/api/profiles`; MCP: `get_appearance` / `set_appearance`. **Upgrading a board that used the old default changes its Bluetooth name and address, so computers must pair again.**
- **Bluetooth stays visible.** A watchdog restarts advertising if it ever stops while nothing is connected (the board could stay online but invisible to the computer after a series of drops).
- **Disconnect is permanent, and consistent.** Disconnect from the dashboard (`POST /api/bluetooth/disconnect`, MCP `disconnect_bluetooth`) and Disconnect from the computer's own Bluetooth menu now do the same thing: the device stays visible in the computer's list but is **not connectable** (non-connectable advertising) until you press **Reconnect**, open a pairing window, or restart the board. There is no 3-minute timer any more, and `heldRemainingMs` is gone from the API. Checked on a board with a BLE client: while disconnected it is seen but a connection attempt times out. Not checked: how macOS draws a not-connectable device in its list.
- **OTA is sturdier.** Bluetooth goes quiet during an upload (no advertising, no new links) because WiFi and Bluetooth share one antenna; an interrupted upload is aborted so the next one can start; the dashboard retries a dropped upload up to 3 times by itself. Before this, some uploads were cut off mid-transfer; after it, 8 consecutive uploads on a real board all completed. The cause of the original drops was not pinned down.
- **WiFi setup recovers by itself.** If the saved network was briefly unavailable at boot, the setup portal now retries it every 30 seconds and closes itself when it connects, instead of staying in the portal. WiFi events (with reason codes) are written to the serial log.
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
