# Timed runs and schedules

Two ways to have the jiggler stop by itself.

## Run for a while

Start it with a limit instead of "until I stop it": **5, 10, 30 minutes, 1 hour, until a time of day, or a custom number of minutes** (1 to 1440). The dashboard's *Run for* menu does this; over the API it is `POST /api/start` with `{"minutes": 30}`, and over MCP the `start` tool's `minutes` argument. "Until 15:40" is turned into minutes by the dashboard, using the browser's clock, so it works even when the device has no clock of its own. The remaining time is `stopsInMs` in the status.

A plain Start, Pause or Stop clears the limit.

## Weekly schedules

A schedule is a window: **on these days, from this time, for this long**. While a window is open the jiggler runs; when it ends the jiggler stops. Up to 8 schedules, each with an id (the dashboard calls it "Name": 1 to 24 characters of `a-z 0-9 _ -`), an on/off switch, days (Mon–Sun), a start time (`HH:MM`) and a duration of 1 to 1440 minutes. A window may run past midnight: Friday 22:00 for 8 hours ends Saturday 06:00 and belongs to Friday.

```json
{ "timezone": "Europe/Rome",
  "schedules": [
    { "id": "workdays", "enabled": true, "days": ["mon","tue","wed","thu","fri"], "start": "09:00", "durationMin": 540 },
    { "id": "night-job", "enabled": true, "days": ["fri"], "start": "22:00", "durationMin": 480 } ] }
```

Several schedules can overlap; the jiggler runs while any enabled window is open.

### How it behaves

- **Edges, not enforcement.** The schedule starts the jiggler when a window opens and stops it when the last open window closes. If you stop it by hand in the middle of a window it stays stopped until the next window opens; if you start it by hand outside a window it keeps running until you stop it (a window that opens later still works).
- **After a reboot** the schedule is in charge: once the device knows the time, the jiggler matches whether a window is open. With no enabled schedule the jiggler boots running, as before.
- **Saving a schedule never starts or stops anything** by itself, including when the device has no clock yet (once it has one, only a window opening or closing acts).
- **A window opening takes over a timed run.** If you started "run for 30 minutes" and a window opens meanwhile, the time limit is dropped and the jiggler runs until the window closes.
- **Daylight-saving changes.** Windows are matched on the local clock time, so a window that spans the hour that repeats (autumn) or is skipped (spring) can run twice or not at all that one night.
- **It needs the time.** The device gets it from the internet (SNTP), so it needs the home WiFi with internet access; in own-network (AP) mode there is no clock and schedules do nothing. The dashboard says "Not synced" until the clock is known, and the status reports `timeSynced`.
- **Timezone.** Pick one of the built-in zones (`GET /api/schedules` lists them, default `Europe/Rome`); daylight saving is handled.
- A schedule does not change what each movement looks like (see the configuration) and is separate from *automations*, which run scripts such as an F15 tap at random intervals while the jiggler runs.
