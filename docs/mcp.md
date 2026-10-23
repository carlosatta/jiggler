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
| `start` | – | Start or resume |
| `pause` | – | Pause, keeping the remaining time |
| `stop` | – | Stop |
| `move_now` | – | Queue a test movement; fails if the mouse is not connected |
| `set_config` | `minIntervalSec`, `maxIntervalSec`, `amplitudePx`, `clickProbability`, `scrollProbability` (all optional) | Change configuration |

## Extended tools

| Tool | Arguments | What it does |
|---|---|---|
| `list_profiles` | – | Profiles you can assign to a device, plus `custom` defaults |
| `get_devices` | – | Configured devices |
| `set_devices` | `devices[]` | Replace the device list. Needs restart and re-pairing |
| `list_scripts` | – | Stored scripts |
| `save_script` | `name`, `script` (`{"steps":[...]}`) | Create or overwrite a script |
| `delete_script` | `name` | Delete a user script |
| `run_script` | `name` | Run a stored script now |
| `get_automations` | – | Scheduled automations |
| `set_automations` | `automations[]` | Replace the automations (max 4) |
| `press_key` | `key`, `times` (1–20, optional) | Press a key or combination, e.g. `Ctrl+C` |
| `type_text` | `text` (up to 200 chars) | Type text (printable ASCII, US layout) |
| `get_network` | – | Network mode and AP settings |
| `set_network` | `mode`, `apSsid`, `apPassword` (optional) | Change network settings. Needs restart |
| `restart` | – | Restart the device |
| `get_bluetooth` | – | Connection, pairing window and paired computers |
| `start_pairing` | `seconds` (30–600, optional) | Open a pairing window |
| `disconnect_bluetooth` | – | Drop the link and hide the device until `reconnect_bluetooth` |
| `reconnect_bluetooth` | – | Make the device visible again |
| `forget_computer` | `id` | Forget one paired computer |

A tool is only listed when its service is available on the device.

## Example prompts

- "Is the jiggler active? How long until the next movement?" → `get_status`
- "Move every 20 to 45 seconds with small movements." → `set_config`
- "Pause it for now." → `pause`
- "Press F15 on the computer." → `press_key`
