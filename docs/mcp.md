# MCP server

The device speaks MCP (JSON-RPC 2.0 over HTTP, protocol `2025-03-26`) at `POST /mcp`, so an assistant can drive it directly. It uses the same controller and JSON codec as the REST API, so the two always agree. Authentication is the same Bearer token (see [api.md](api.md)).

## Connecting

Claude Code:

```sh
claude mcp add --transport http jiggler http://jiggler.local/mcp \
  --header "Authorization: Bearer <token>"
```

Any other MCP client that supports streamable HTTP with custom headers works the same way. Notifications (requests without an `id`) are answered with `202` and no body.

Raw check:

```sh
curl -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}' http://jiggler.local/mcp
```

Tool failures are returned as a normal result with `isError: true` and a JSON error object in the text content.

## Core tools

| Tool | Arguments | What it does |
|---|---|---|
| `get_status` | – | Run state, mouse/WiFi connection, time to next movement, move count, last movement, config |
| `start` | `minutes` (1–1440, optional) | Start or resume; with `minutes` it stops by itself after that long |
| `pause` | `minutes` (1–1440, optional) | Pause; with `minutes` it resumes by itself after that long |
| `resume` | – | End a pause now |
| `stop` | – | Stop |
| `move_now` | – | Queue a test movement; fails if the mouse is not connected |
| `set_config` | `minIntervalSec`, `maxIntervalSec`, `amplitudePx`, `clickProbability`, `scrollProbability` (all optional) | Change configuration |

## Extended tools

| Tool | Arguments | What it does |
|---|---|---|
| `get_appearance` | – | How the device appears over Bluetooth, and the models you can choose (see [appearance](appearance.md)) |
| `set_appearance` | `profile`, `name`, `manufacturer`, `vid`, `pid`, `mac` (all optional) | Change it (a full replacement). Needs a restart and re-pairing |
| `get_network` | – | Network mode and AP settings |
| `set_network` | `mode`, `apSsid`, `apPassword` (optional) | Change network settings. Needs restart |
| `restart` | – | Restart the device |
| `get_schedules` | – | Weekly schedules and the timezone |
| `set_schedules` | `timezone`, `schedules[]` (`id`, `enabled`, `days`, `start`, `durationMin`) | Replace the schedules (max 8) |
| `get_bluetooth` | – | Connection, pairing window and paired computers |
| `start_pairing` | `seconds` (30–600, default 180, optional) | Open a pairing window |
| `disconnect_bluetooth` | – | Drop the link and hide the device until `reconnect_bluetooth` |
| `reconnect_bluetooth` | – | Make the device visible again |
| `forget_computer` | `id` | Forget one paired computer |

A tool is only listed when its service is available on the device.

## Example prompts

- "Is the jiggler active? How long until the next movement?" → `get_status`
- "Move every 20 to 45 seconds with small movements." → `set_config`
- "Pause it for now." → `pause`
- "Keep it on for the next 30 minutes." → `start` with `minutes: 30`
- "Run it Monday to Friday, 9 to 6." → `set_schedules`
