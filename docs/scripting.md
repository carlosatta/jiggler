# Scripts and automations

A **script** is a JSON program of mouse and keyboard actions. It is compiled once into the same plan model the random mouse movement uses, and executed by the same runner (which aborts if the computer disconnects). An **automation** runs a stored script at random intervals.

```json
{
  "steps": [
    { "op": "move", "dx": 120, "dy": -40 },
    { "op": "wait", "minMs": 300, "maxMs": 900 },
    { "op": "click" },
    { "op": "key", "key": "Ctrl+Shift+T" },
    { "op": "type", "text": "hello\n" }
  ]
}
```

## Operations

| `op` | Fields | Notes |
|---|---|---|
| `move` | `dx`, `dy` (−2000…2000) | Human-like curved stroke (same easing as the random mouse) |
| `click` | – | Left click |
| `scroll` | `notches` (−100…100, not 0) | Wheel notches; sign sets direction |
| `key` | `key` | Press and release a key or chord |
| `type` | `text` (1–200 chars) | Types printable ASCII plus `\n` and `\t`, with randomised timing. US layout |
| `wait` | `ms`, or `minMs` and `maxMs` (0…600000) | Fixed or random pause |
| `repeat` | `times` (1…1000), `steps` | Repeats a block; up to 3 levels deep |

Unknown fields are rejected, and error messages carry a JSON path such as `/steps/2/dx`.

## Key names

Case-insensitive. A chord joins modifiers and one key with `+`:

- Letters `a`–`z`, digits `0`–`9`
- `F1`–`F24` (`F15` is a good "keep awake" key: it is not on any real keyboard layout and does nothing by default)
- `Enter`, `Esc`, `Backspace`, `Tab`, `Space`, `Delete`, `Home`, `End`, `PageUp`, `PageDown`, `Up`, `Down`, `Left`, `Right`
- Modifiers: `Ctrl`, `Shift`, `Alt`, `Gui` (Cmd/Windows key)

Examples: `F15`, `Ctrl+C`, `Ctrl+Alt+Delete`.

## Limits

All limits are enforced when a script is compiled, so a script that compiles once cannot exceed them at run time.

| Limit | Value |
|---|---|
| Script size | 3000 bytes |
| Compiled steps | 2000 |
| Total waiting time (random waits counted at their maximum) | 600 s |
| `repeat` nesting | 3 |
| User scripts stored | 8 |
| Automations | 4 |
| Queued runs | 4 |

## Library

`keepalive-key` is built in and read-only: a single F15 tap. User scripts are stored in flash (NVS) under `script/<name>`. Manage them from the dashboard, [REST](api.md#scripts) or [MCP](mcp.md).

## Automations

```json
{ "id": "keepalive-key", "script": "keepalive-key",
  "minIntervalSec": 60, "maxIntervalSec": 180, "enabled": false }
```

- Each automation picks a random delay in `[minIntervalSec, maxIntervalSec]` (5–86400 s) and re-draws after every run.
- Scheduled runs only happen while the global state is `running`, the automation is `enabled`, and the computer is connected.
- **Run now** (dashboard, `POST /api/scripts/<name>/run`, `run_script`, `press_key`, `type_text`) works regardless of the run state; it only needs the computer connected.
- The default `keepalive-key` automation ships disabled.

The keyboard is only usable if a keyboard device is enabled (see [devices.md](devices.md)).
