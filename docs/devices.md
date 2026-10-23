# Devices and Bluetooth identity

## Model

A *device* is a mouse or a keyboard. The configured list (1–4 entries, stored in flash) decides what the ESP32 presents to the computer.

- **One Bluetooth identity.** The name, vendor, product ID, PnP ID and Bluetooth address come from the **first enabled device**.
- **One HID function per enabled device.** Each enabled device adds a collection to the same BLE device: mouse (report ID 1) and keyboard (report ID 2). So the computer sees *one* composite device that is both a mouse and a keyboard.
- Default: mouse enabled as Logitech MX Master 3S, keyboard disabled.
- Changing the identity or enabling the keyboard applies **after a restart** and the computer must **pair again** (forget the old device first).

## Profiles

A profile is a ready-made identity from the catalog. Only entries whose VID/PID/OUI were checked against public sources are included.

| Profile id | Kind | Name | VID:PID | Address prefix (OUI) |
|---|---|---|---|---|
| `logitech-mx-master-3s` | mouse | MX Master 3S | `046D:B034` | `00:1F:20` |
| `razer-orochi-v2` | mouse | Razer Orochi V2 | `1532:0095` | `44:5E:CD` |
| `apple-magic-mouse-2` | mouse | Magic Mouse 2 | `004C:0269` (Bluetooth SIG source) | `58:AD:12` |
| `logitech-mx-keys` | keyboard | MX Keys | `046D:B35B` | `00:1F:20` |
| `apple-magic-keyboard` | keyboard | Magic Keyboard | `004C:0267` (Bluetooth SIG source) | `58:AD:12` |

`custom` uses a neutral identity (`Bluetooth Mouse` / `Bluetooth Keyboard`, manufacturer `Generic`, VID `1209`, PID `0001`) unless you override fields.

The profile's OUI becomes the first three bytes of the Bluetooth address (the rest comes from the chip), so the address looks like it belongs to that vendor rather than Espressif. An explicit `mac` override wins.

Add a profile only with VID/PID/OUI verified against public sources; `test_devices` pins the current ones.

## Overrides

Per device: `name` (1–29 characters), `manufacturer` (1–32), `vid`, `pid` and `mac` (unicast). Only the first enabled device's overrides matter for identity; the others contribute just their function.

See [api.md](api.md#devices-and-profiles) for the JSON.

## Limitations

- **No simultaneous separate identities.** The classic ESP32 has one Bluetooth address and one GATT database. A spike ([spike notes](superpowers/specs/2026-10-23-multi-identity-spike.md), code in [../spike/identities](../spike/identities)) showed two identities advertise fine and each connects alone, but a second connection was refused while the first was active. The cause (ESP32 controller or the Mac) was not isolated. A truly separate pair would need an ESP32-S3/C3 or more boards.
- **The identity is cosmetic.** The device does not implement Logitech's HID++ protocol. Vendor software (Logi Options+, Razer Synapse…) will not treat it as the real product.
- **Setting the base MAC changes the WiFi MAC too.** The base address is set before BLE init (`esp_base_mac_addr_set`).
- **Keyboard layout is US only.**
