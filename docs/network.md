# Network, setup and security

The Bluetooth side works without WiFi: the movement task runs on its own core and is started *before* WiFi, so a missing or dropped network never stops the jiggler.

## First setup (`auto` mode)

1. On first boot (or when no WiFi credentials/token are stored) the device opens an access point named **`Jiggler-Setup`**.
2. Join it from a phone or laptop; the captive portal opens (or browse to `192.168.4.1`).
3. Choose your WiFi network, enter its password, and set the **API token**. Without a stored token the portal is forced even if WiFi credentials exist.
4. The device joins the network and starts the web server. Reach it at **`http://jiggler.local/`** (mDNS) or by IP (shown on the serial monitor and your router).
5. Credentials and token persist across restarts. If the saved network cannot be joined at boot, the setup portal starts again.

Time (for `lastMoveAt`) comes from NTP (`pool.ntp.org`, `time.google.com`) once WiFi is up.

## AP mode (no home WiFi)

`ap` mode makes the device create its own WPA2 network, useful away from a known WiFi.

- SSID defaults to **`Jiggler`**. If no password is set, a random one is generated on first start, **saved and printed on the serial monitor**.
- A DNS captive portal redirects every hostname to the dashboard at the AP address.
- If no token is stored, the AP password is also used as the API token.
- Switch mode via the dashboard (the "Rete" card), `PUT /api/network`, the `set_network` MCP tool, or by **holding the BOOT button (GPIO 0) for 3 seconds**, which toggles `auto`↔`ap` and restarts. Changes made through the API apply after a restart.

## Authentication

| Route | Access |
|---|---|
| `GET /` (dashboard page) | Public: it is a static page with no data |
| `/api/*`, `/mcp` | `Authorization: Bearer <token>` required |

The dashboard asks for the token and keeps it in the browser. Token comparison is constant-time.

Treat the token and the AP password as secrets: `GET /api/network` returns the AP password. The server speaks plain HTTP, so use it only on networks you trust.

## LED

GPIO 2 (the on-board LED on most `esp32dev` boards):

- Blinking at 1 Hz: the computer is not connected.
- Solid: connected. It goes off briefly (80 ms) right after each movement.
