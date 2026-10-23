# Network, setup and security

The Bluetooth side works without WiFi: the movement task runs on its own core and is started *before* WiFi, so a missing or dropped network never stops the jiggler.

## First setup (`auto` mode)

1. On first boot (or when no WiFi credentials/token are stored) the device opens an access point named **`Jiggler-Setup`**.
2. Join it from a phone or laptop; the captive portal opens (or browse to `192.168.4.1`).
3. Choose your WiFi network, enter its password, and set the **API token**. Without a stored token the portal is forced even if WiFi credentials exist. A wrong WiFi password keeps the portal open so you can retry.
4. When it connects, **the board restarts once** (the portal leaves port 80 busy for a while, so a clean start is the reliable way to bring up the dashboard). It then joins your WiFi and starts the web server. Reach it at **`http://jiggler.local/`** (mDNS) or by IP (shown on the serial monitor and your router).
5. Credentials and token persist across restarts. If the saved network cannot be joined at boot, the setup portal starts again.

Time (for `lastMoveAt`) comes from NTP (`pool.ntp.org`, `time.google.com`) once WiFi is up.

## AP mode (no home WiFi)

`ap` mode makes the device create its own WPA2 network, useful away from a known WiFi.

- SSID defaults to **`Jiggler`**. The password is optional: leave the field empty in **System → Network** and one is generated the first time the own network starts, **saved and printed on the serial monitor**.
- A DNS captive portal redirects every hostname to the dashboard at the AP address.
- If no token is stored, the AP password is also used as the API token.
- Switch mode via the dashboard (the "Network" card), `PUT /api/network`, the `set_network` MCP tool, or by **holding the BOOT button (GPIO 0) for 3 seconds**, which toggles `auto`↔`ap` and restarts. Changes made through the API apply after a restart.

## The token

The token is the password for the dashboard, the REST API and MCP. You choose it in the setup portal. Change it any time in **System → Security** (you need the current one). It is stored on the device only and never written to the serial log. In own-network mode with no token set, the network password doubles as the token.

## Authentication

| Route | Access |
|---|---|
| `GET /` (dashboard page) | Public: it is a static page with no data |
| `/api/*`, `/mcp` | `Authorization: Bearer <token>` required |

The dashboard asks for the token and keeps it in the browser. Token comparison is constant-time.

Treat the token and the AP password as secrets: `GET /api/network` returns the AP password. The server speaks plain HTTP, so use it only on networks you trust.

## LED

GPIO 2 (the on-board LED on most `esp32dev` boards):

- Blinking slowly (1 Hz): no computer connected.
- Solid: connected. It goes off briefly (80 ms) right after each movement.
- Blinking fast: **pairing is open**.
- A short flash every 3 seconds: the device is **hidden** after a Disconnect.
