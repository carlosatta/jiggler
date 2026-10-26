# Dashboard tour

The dashboard is a single page served by the board at `http://jiggler.local/`. It works on phones (bottom bar) and desktops (side bar), follows your light or dark theme, and signs in with your [token](network.md#the-token). Five sections: Status, Computers, Device, Schedule, System.

<p align="center">
  <img src="images/desktop-status-light.png" alt="Dashboard on a desktop, light theme" width="760">
</p>

## Status

The first thing you see: **which computer is connected and since when**, and what the jiggler is doing.

<p align="center">
  <img src="images/status-running.png" alt="Status while running for 30 minutes" width="260">
  <img src="images/status-paused.png" alt="Status while paused: it resumes in 15 minutes" width="260">
  <img src="images/status-bluetooth-off.png" alt="Status with Bluetooth switched off" width="260">
</p>

- **Connection.** One of four states: *Bluetooth is off*, *Waiting for a computer*, *Discovery is open* (with a countdown) or *Connected to Computer 1, for 12 minutes*, with the buttons that make sense for it: **Start Discovery** / **Stop Discovery**, **Turn Bluetooth off** (disconnects and silences the radio; the computer's own Disconnect does the same by default) and **Turn Bluetooth on**. See [Bluetooth](bluetooth.md).
- **Movement.** *Running*, *Paused* or *Stopped*, the time to the next movement, and, for a timed run, when it will stop.
- **Run for** (shown while stopped): *until I stop it*, 5, 10 or 30 minutes, 1 hour, *until a time of day*, or a custom number of minutes (1 to 1440). **Start** applies it; **Move now** makes one movement immediately.
- **Pause** (shown while running): *15 minutes, 30 minutes, 1 hour, 2 hours*, or *until I resume*. While paused the cursor does not move and the card says when it resumes by itself; **Resume** ends it at once and **Stop** stops everything. A time limit set with **Run for** keeps counting during a pause, and a schedule keeps its own timetable: a pause lasts until it ends or until the next window opens or closes. Use it when you sit down to work, or step away, without losing your schedule.
- **Facts.** Last movement, movements made, the device clock (where it comes from and its timezone) and WiFi. If a schedule window is open but no computer is paired, a note says why the jiggler has not started. When a newer firmware version exists (and updates are not ignored), a note says so and links to System.

On a new board, Status shows the guided **Set up your Jiggler** card until a computer is paired ([Getting started](getting-started.md)). The jiggler cannot start before that.

## Computers

<p align="center">
  <img src="images/computers.png" alt="Pairing and the list of paired computers" width="260">
</p>

- **Bluetooth** shows the state and **Turn Bluetooth off / on**.
- **Discovery** is closed by default. **Start Discovery** lets a new computer pair (180 seconds by default, set it here from 30 to 600); it closes by itself when the time is up or a computer pairs, and **Stop Discovery** closes it earlier. A checkbox decides whether the computer's own Disconnect switches Bluetooth off.
- **Paired computers.** Bluetooth does not tell the device a computer's name, so they are numbered. **Rename** gives one a name you will recognise; **Forget** removes it (confirm with a second tap); **Forget all** removes every one. Up to 3 can be paired.

Details and the reasoning are in [Bluetooth](bluetooth.md).

## Device

<p align="center">
  <img src="images/device.png" alt="Movement settings" width="260">
  <img src="images/device-appearance.png" alt="Appearance, with the model list and the spoofing fields" width="260">
</p>

- **Movement.** How a movement looks and how often it happens: the minimum and maximum interval between movements, the movement size, and the chance of a random click (always a left click) or scroll. Defaults are conservative: 30 to 120 seconds, 150 px, no clicks, a few scrolls.
- **Appearance.** How the device introduces itself over Bluetooth. **Shown as** starts with **Custom** (a neutral "Bluetooth Mouse") followed by real mouse models; under it you can change the **name, manufacturer, vendor and product ID, and Bluetooth address** (the spoofing fields). A field left empty uses the model's value, shown in grey. Applying restarts the device and each paired computer must pair again. See [Appearance](appearance.md).

## Schedule

<p align="center">
  <img src="images/schedule.png" alt="Weekly schedules" width="260">
</p>

Weekly entries, in the device's timezone (set in System → Time). A **window** runs the jiggler between a start and an end time (an end earlier than the start means the next day); an **action** does one thing at a time: start, stop, pause, resume, Bluetooth on or off, open Discovery. Each can have a **random shift** of up to 120 minutes either way, chosen afresh each day, and repeats **forever** or a **limited** number of times. A window that is open when you save it starts the jiggler. Details in [Schedules](schedules.md).

## System

<p align="center">
  <img src="images/system.png" alt="Security and network" width="260">
</p>

- **Security.** Change the token (you need the current one).
- **Time.** Timezone, NTP server, and **Set the time from this browser** for a board with no internet.
- **Button.** Which GPIO the physical button uses (default BOOT, GPIO 0) and what each press does.
- **Network.** *Auto* (your home WiFi, with the setup network as a fallback) or *Own network* (the board creates a protected WiFi network). The own-network password is optional: leave it empty and one is generated the first time. See [Network](network.md).
- **Appearance.** The name the device shows over Bluetooth: a neutral "Bluetooth Mouse" by default, a name of your own, or a real mouse model. Changing it restarts the device and needs each computer to pair again. See [Appearance](appearance.md).
- **Firmware updates.** The running version, whether a newer release exists, **Check for updates** and **Install**; and the choice of what happens when a release appears (**tell me**, **install automatically**, **ignore**) and which repository to look at. **Install from a file** uploads a `.bin` instead. See [Getting started](getting-started.md#updating-the-firmware-later-no-cable).
- **About this device.** Firmware version, Bluetooth name and address, a **Source code** link to the repository page, and **Restart**.

## When something goes wrong

The page says so plainly: a banner appears if the device cannot be reached and it retries by itself; after a restart it reconnects on its own and tells you when the device is back.
