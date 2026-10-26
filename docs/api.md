# REST API

All routes live under `/api/` and return JSON. The dashboard (`GET /`) is the only public route.

## Authentication

Every `/api/*` and `/mcp` request needs the API token:

```
Authorization: Bearer <token>
```

The token is entered in the WiFi setup portal the first time (see [network.md](network.md)). In AP mode it defaults to the AP password. A missing or wrong token gets `401`.

## Request bodies

Send bodies with `Content-Type: application/json`. A bare `curl -d '{...}'` sends form-encoded data, which the HTTP library parses as parameters, so the device answers `415 unsupported_media_type`:

```sh
curl -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' \
     -X PUT -d '{"minIntervalSec":20}' http://jiggler.local/api/config
```

Body size caps: 4096 bytes for MCP, 2048 for devices and schedules, 1024 for config and network. Larger bodies get `413 payload_too_large`.

## Errors

```json
{ "error": "invalid_config", "field": "minIntervalSec", "message": "must be between 5 and 3600" }
```

`field` and `message` are optional. Common codes:

| Status | `error` | Meaning |
|---|---|---|
| 400 | `bad_json`, `invalid_config`, `invalid_appearance`, `invalid_network`, `invalid_schedule`, `invalid_minutes` | Validation failed; nothing was changed |
| 401 | | Missing or wrong token |
| 404 | `not_found` | Unknown route, computer, or service not available |
| 405 | `method_not_allowed` | Wrong HTTP method for the route |
| 409 | `mouse_not_connected`, `no_computer_paired` | No computer is connected, or none is paired yet |
| 413 | `payload_too_large` | Body over the cap |
| 415 | `unsupported_media_type` | Body sent without `Content-Type: application/json` |

## Status and control

| Method | Path | Description |
|---|---|---|
| GET | `/api/status` | Full status |
| POST | `/api/start` | Start (or resume). Optional body `{"minutes": 1..1440}`: it then stops by itself after that long; without it, it runs until stopped |
| POST | `/api/pause` | Pause. Optional body `{"minutes": 1..1440}`: it then resumes by itself after that long; without it, it stays paused until `resume`. Keeps any time limit running. `409 not_running` if nothing is running |
| POST | `/api/resume` | End a pause now (`409 not_paused` otherwise) |
| POST | `/api/stop` | Stop completely |
| POST | `/api/move` | Queue one movement now. `202 {"queued":true}`, or `409 mouse_not_connected` |

`start`, `pause` and `stop` return the new status. `start` (with or without `minutes`) answers `409 no_computer_paired` while no computer is paired: the jiggler cannot run then, a board with nothing paired boots stopped, and forgetting every computer stops it. A plain `start`, `pause` or `stop` clears any time limit. Bad `minutes` answer `400 invalid_minutes`.

```json
{
  "state": "running",
  "mouseConnected": true,
  "wifiConnected": true,
  "msToNextMove": 41250,
  "stopsInMs": null,
  "resumesInMs": null,
  "moveCount": 17,
  "lastMoveAgoMs": 12000,
  "lastMoveAt": 1792750000,
  "config": { "minIntervalSec": 30, "maxIntervalSec": 120, "amplitudePx": 150,
              "clickProbability": 0.0, "scrollProbability": 0.1 }
}
```

`state` is `running`, `paused` or `stopped`. `resumesInMs` is the time left in a timed pause (`null` otherwise). `stopsInMs` is the time left before a timed run stops by itself (`null` when there is no limit). `msToNextMove`, `lastMoveAgoMs` and `lastMoveAt` (Unix time, needs NTP) are `null` when unknown.

## Bluetooth

See [bluetooth.md](bluetooth.md) for the states and the pairing policy. `GET /api/status` also carries a compact `"bluetooth"` object (`state`, `enabled`, `discoverySeconds`, `offWhenComputerDisconnects`, `connected`, `pairing{open, remainingMs}`, `computer{label, connectedForMs}`, `peerCount`), a `"time"` object (see Time) and a `"device"` object (`version`, `bleName`, `bleAddress`).

| Method | Path | Description |
|---|---|---|
| GET | `/api/bluetooth` | Full state: `state` (`off`, `waiting`, `discovery`, `connected`), the settings, the Discovery window, `computer`, and `peers[]` (`id`, `address`, `label`, `connected`) |
| POST | `/api/bluetooth/pairing` | Open Discovery (switches Bluetooth on). Optional body `{"seconds":120}` (30–600; default: the configured length, 180). It closes by itself when a computer pairs |
| DELETE | `/api/bluetooth/pairing` | Close Discovery |
| POST | `/api/bluetooth/disable` | Switch Bluetooth off: disconnects the computer, closes Discovery, silences the radio. Stored |
| POST | `/api/bluetooth/enable` | Switch Bluetooth on |
| PUT | `/api/bluetooth/settings` | `{"discoverySeconds":300,"offWhenComputerDisconnects":false}` (30–600 s; both optional) |
| PUT | `/api/bluetooth/peers/<id>` | Rename a paired computer: `{"label":"Office desktop"}` (1–32 chars) |
| DELETE | `/api/bluetooth/peers/<id>` | Forget one computer (`404 not_found` if unknown) |
| DELETE | `/api/bluetooth/peers` | Forget all computers. Discovery stays closed until you open it |

## Schedules

See [schedules.md](schedules.md). `GET /api/status` also carries `"schedule": {active, enabledCount, blocked}`; `blocked` is `"no_computer_paired"` while an open window cannot start the jiggler yet, else `null`.

| Method | Path | Description |
|---|---|---|
| GET | `/api/schedules` | `{"schedules":[...], "actions":[...]}` (the last lists the accepted actions) |
| PUT | `/api/schedules` | Replace the schedules (max 8). Each is a window (`kind` `window`, `start`, `durationMin`) or an action (`kind` `action`, `action`, `start`), with optional `jitterMin` 0–120, `times` 1–999 and `remaining`. `400 invalid_schedule` with the offending `field` |

```json
{ "schedules": [
  { "id": "workdays", "enabled": true, "kind": "window", "days": ["mon","tue","wed","thu","fri"],
    "start": "09:00", "durationMin": 480, "jitterMin": 10 },
  { "id": "lunch", "enabled": true, "kind": "action", "action": "pause", "days": ["mon","tue","wed","thu","fri"],
    "start": "12:30" } ] }
```

## Time

`GET /api/status` carries `"time": {synced, source, timezone, localTime, weekday, epoch}` (`source` is `ntp`, `manual` or `none`; the rest is `null` until the clock is known).

| Method | Path | Description |
|---|---|---|
| GET | `/api/time` | `{"timezone", "ntpServer", "status":{...}, "timezones":[...]}` |
| PUT | `/api/time` | `{"timezone":"Europe/Rome","ntpServer":"pool.ntp.org"}` (an empty `ntpServer` stops using NTP). `400 invalid_clock` with the `field` |
| POST | `/api/time/set` | `{"epoch":1795000000}` sets the clock by hand (Unix seconds, 2023–2100). A board with no internet loses it at every restart |

## Button

| Method | Path | Description |
|---|---|---|
| GET | `/api/button` | `{"gpio":0,"default":0,"gpios":[...]}`: the pin of the physical button and the pins that can be used |
| PUT | `/api/button` | `{"gpio":14}`; applies at once. `400 invalid_gpio` for a pin that is not listed |

## Event log

| Method | Path | Description |
|---|---|---|
| GET | `/api/log` | `{"uptimeMs", "events":[{"agoMs","message"}]}`: the last 80 events, oldest first (Bluetooth links and pairing decisions, advertising, the button, a heartbeat every 5 minutes). Useful when no USB cable is attached |

## Firmware updates from releases

`GET /api/status` also carries `"update": {mode, available, version, state}`. The device looks at the latest release of a GitHub repository for an asset named `jiggler-esp32.bin` (and optionally `jiggler-esp32.bin.sha256`).

| Method | Path | Description |
|---|---|---|
| GET | `/api/updates` | `{"mode","checkTime","repoUrl","defaultRepoUrl","current","state","progress","checkedAgoMs","latest":{"version","tag","pageUrl","sizeBytes"},"available","blocked","error"}`; `state` is `idle`, `checking`, `installing` or `restarting` |
| PUT | `/api/updates` | `{"mode":"manual\|automatic\|ignore","repoUrl":"https://github.com/owner/repo","checkTime":"03:00"}` (all optional; an empty `repoUrl` means the default; `checkTime` is the daily look, and install in automatic mode, in the device's timezone). `400 invalid_mode` / `invalid_repo_url` / `invalid_time` |
| POST | `/api/updates/check` | Look for a newer release now (answers `202`; poll `GET /api/updates`). `409 busy` / `no_clock` |
| POST | `/api/updates/install` | Install the newer release found (`202`; the device downloads it and restarts). `409 no_update` / `busy` / `no_clock` |

`blocked` is true when that version was installed earlier but the device is still running something else; automatic mode then does not try again. This is separate from `POST /api/update` (singular) below, which uploads a file.

## Firmware update from a file

| Method | Path | Description |
|---|---|---|
| POST | `/api/update` | Upload a firmware `.bin` as a multipart form (`-F firmware=@firmware.bin`). Needs the Bearer token; nothing is written without it. `200 {"updated":true,"restarting":true}` and the device restarts; `400 update_failed` with a message if the file is refused |

See [Getting started](getting-started.md#updating-the-firmware-later-no-cable).

## API token

`PUT /api/token` with `{"current":"<old>","token":"<new>"}` (new token: 8–40 printable characters, no spaces). A wrong `current` answers `403 wrong_current`. The old token stops working immediately; update dashboards and MCP clients.

## Configuration

| Method | Path | Description |
|---|---|---|
| GET | `/api/config` | Current configuration |
| PUT | `/api/config` | Partial update; omitted fields keep their value |

| Field | Type | Range | Default |
|---|---|---|---|
| `minIntervalSec` | integer | 5 – 3600 | 30 |
| `maxIntervalSec` | integer | 5 – 3600, `>= minIntervalSec` | 120 |
| `amplitudePx` | integer | 5 – 600 | 150 |
| `clickProbability` | number | 0 – 1 | 0 |
| `scrollProbability` | number | 0 – 1 | 0.1 |

Unknown fields are rejected.

## Appearance

How the device presents itself over Bluetooth; see [appearance.md](appearance.md). There is one device, so this is a single object. Changes take effect **after a restart** and each paired computer must pair again.

| Method | Path | Description |
|---|---|---|
| GET | `/api/appearance` | The current setting, the name currently shown (`shownAs`), the mouse models you can choose (`profiles`) and the `custom` defaults |
| PUT | `/api/appearance` | Replace it (omitted fields go back to the default). Answers like GET plus `"restartRequired": true`. Errors: `400 invalid_appearance` with the `field` |
| POST | `/api/restart` | Restart the device (`202 {"restarting":true}`) |

```json
{ "profile": "custom", "name": "Desk Mouse" }
```

Fields: `profile` (`custom`, the default, or a model id from `profiles`), and optional overrides `name` (1–29 chars), `manufacturer` (1–32), `vid`, `pid` (0–65535) and `mac` (`AA:BB:CC:DD:EE:FF`, unicast; this also changes the WiFi address). A GET body can be sent back to PUT unchanged.

## Network

| Method | Path | Description |
|---|---|---|
| GET | `/api/network` | `{"mode":"auto","apSsid":"Jiggler","apPassword":""}` (the password is empty until you set one or the own network generates it) |
| PUT | `/api/network` | Partial update. Applies after a restart |

`mode` is `auto` or `ap`; `apSsid` is 1–32 characters; `apPassword` is empty (generated) or 8–63 printable ASCII characters. See [network.md](network.md).
