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

Body size caps: 4096 bytes for scripts and MCP, 2048 for devices and automations, 1024 for config and network. Larger bodies get `413 payload_too_large`.

## Errors

```json
{ "error": "invalid_config", "field": "minIntervalSec", "message": "must be between 5 and 3600" }
```

`field` and `message` are optional. Common codes:

| Status | `error` | Meaning |
|---|---|---|
| 400 | `bad_json`, `invalid_config`, `invalid_devices`, `invalid_automation`, `invalid_network`, `invalid_script` | Validation failed; nothing was changed |
| 401 | | Missing or wrong token |
| 403 | `read_only` | Trying to change or delete the built-in script |
| 404 | `not_found` | Unknown route, script, or service not available |
| 405 | `method_not_allowed` | Wrong HTTP method for the route |
| 409 | `mouse_not_connected`, `busy`, `limit_reached` | The computer is not connected, too many runs queued, or script/automation limit reached |
| 413 | `payload_too_large` | Body over the cap |
| 415 | `unsupported_media_type` | Body sent without `Content-Type: application/json` |

## Status and control

| Method | Path | Description |
|---|---|---|
| GET | `/api/status` | Full status |
| POST | `/api/start` | Start (or resume). Optional body `{"minutes": 1..1440}`: it then stops by itself after that long; without it, it runs until stopped |
| POST | `/api/pause` | Pause; resuming keeps the remaining time |
| POST | `/api/stop` | Stop completely |
| POST | `/api/move` | Queue one movement now. `202 {"queued":true}`, or `409 mouse_not_connected` |

`start`, `pause` and `stop` return the new status. A plain `start`, `pause` or `stop` clears any time limit. Bad `minutes` answer `400 invalid_minutes`.

```json
{
  "state": "running",
  "mouseConnected": true,
  "wifiConnected": true,
  "msToNextMove": 41250,
  "stopsInMs": null,
  "moveCount": 17,
  "lastMoveAgoMs": 12000,
  "lastMoveAt": 1792750000,
  "config": { "minIntervalSec": 30, "maxIntervalSec": 120, "amplitudePx": 150,
              "clickProbability": 0.0, "scrollProbability": 0.1 }
}
```

`state` is `running`, `paused` or `stopped`. `stopsInMs` is the time left before a timed run stops by itself (`null` when there is no limit). `msToNextMove`, `lastMoveAgoMs` and `lastMoveAt` (Unix time, needs NTP) are `null` when unknown.

## Bluetooth

See [bluetooth.md](bluetooth.md) for the pairing policy. `GET /api/status` also carries a compact `"bluetooth"` object (`connected`, `held`, `heldRemainingMs`, `pairing{open, remainingMs}`, `computer{label, connectedForMs}`, `peerCount`) and a `"device"` object (`version`, `bleName`, `bleAddress`).

| Method | Path | Description |
|---|---|---|
| GET | `/api/bluetooth` | Full state: connection, pairing window, `computer`, and `peers[]` (`id`, `address`, `label`, `connected`) |
| POST | `/api/bluetooth/pairing` | Open a pairing window. Optional body `{"seconds":180}` (30–600; default 180). It closes by itself when a computer pairs |
| DELETE | `/api/bluetooth/pairing` | Close the window |
| POST | `/api/bluetooth/disconnect` | Drop the link and hide the device for 3 minutes (or until reconnect) |
| POST | `/api/bluetooth/reconnect` | Make the device visible again |
| PUT | `/api/bluetooth/peers/<id>` | Rename a paired computer: `{"label":"MacBook"}` (1–32 chars) |
| DELETE | `/api/bluetooth/peers/<id>` | Forget one computer (`404 not_found` if unknown) |
| DELETE | `/api/bluetooth/peers` | Forget all computers and reopen pairing |

## Schedules

See [schedules.md](schedules.md). `GET /api/status` also carries `"schedule": {timeSynced, localTime, weekday, active, enabledCount, timezone}`.

| Method | Path | Description |
|---|---|---|
| GET | `/api/schedules` | `{"timezone", "schedules":[...], "timezones":[...]}` (the last is the list of accepted zones) |
| PUT | `/api/schedules` | Replace the schedules (max 8) and/or set the timezone; keys you omit keep their value. `400 invalid_schedule` with the offending `field` |

```json
{ "timezone": "Europe/Rome",
  "schedules": [ { "id": "workdays", "enabled": true, "days": ["mon","tue","wed","thu","fri"],
                   "start": "09:00", "durationMin": 480 } ] }
```

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

## Devices and profiles

See [devices.md](devices.md) for the model. Changes take effect **after a restart** and need re-pairing on the computer.

| Method | Path | Description |
|---|---|---|
| GET | `/api/profiles` | Catalog of verified profiles plus the `custom` defaults |
| GET | `/api/devices` | Configured devices |
| PUT | `/api/devices` | Replace the whole list (1–4 devices). Answers with the list plus `"restartRequired": true` |
| POST | `/api/restart` | Restart the device (`202 {"restarting":true}`) |

```json
{
  "devices": [
    { "id": "mouse", "kind": "mouse", "enabled": true, "profileId": "logitech-mx-master-3s" },
    { "id": "keyboard", "kind": "keyboard", "enabled": true, "profileId": "logitech-mx-keys",
      "name": "MX Keys", "mac": "00:1F:20:AA:BB:CC" }
  ]
}
```

Per device: `id` (1–24 chars of `a-z 0-9 _ -`, unique), `kind` (`mouse`/`keyboard`), `enabled`, `profileId` (a catalog id or `custom`, and its kind must match), and optional overrides `name` (1–29 chars), `manufacturer` (1–32), `vid`, `pid` (0–65535) and `mac` (`AA:BB:CC:DD:EE:FF`, unicast). At least one device must be enabled.

## Scripts

See [scripting.md](scripting.md) for the script format.

| Method | Path | Description |
|---|---|---|
| GET | `/api/scripts` | `{"scripts":[{"name":"keepalive-key","builtin":true}, ...]}` |
| GET | `/api/scripts/<name>` | `{"name":..., "builtin":..., "script":{"steps":[...]}}` |
| PUT | `/api/scripts/<name>` | Create or overwrite (body: `{"steps":[...]}`). Max 8 user scripts, else `409 limit_reached` |
| DELETE | `/api/scripts/<name>` | Delete a user script (`403 read_only` for the built-in) |
| POST | `/api/scripts/<name>/run` | Run now. `202 {"queued":true}`; `409` if the computer is not connected |

Script names are 1–24 characters of `a-z 0-9 _ -`.

## Automations

An automation runs a stored script at a random interval. They only fire while the global state is `running`.

| Method | Path | Description |
|---|---|---|
| GET | `/api/automations` | Current list |
| PUT | `/api/automations` | Replace the whole list (max 4) |

```json
{
  "automations": [
    { "id": "keepalive-key", "script": "keepalive-key",
      "minIntervalSec": 60, "maxIntervalSec": 180, "enabled": false }
  ]
}
```

`minIntervalSec` / `maxIntervalSec` range from 5 to 86400 and `max >= min`. The `script` must exist.

## Network

| Method | Path | Description |
|---|---|---|
| GET | `/api/network` | `{"mode":"auto","apSsid":"Jiggler","apPassword":"..."}` |
| PUT | `/api/network` | Partial update. Applies after a restart |

`mode` is `auto` or `ap`; `apSsid` is 1–32 characters; `apPassword` is empty (generated) or 8–63 printable ASCII characters. See [network.md](network.md).
