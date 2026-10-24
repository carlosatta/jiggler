# Appearance

*Appearance* is how the device introduces itself to computers over Bluetooth: the name you see in the Bluetooth list, and the vendor and product identifiers behind it. There is **one device**, a mouse, so there is one setting to look at, not a list.

<p align="center">
  <img src="images/device-appearance.png" alt="Appearance in Device" width="260">
</p>

## The default: Custom

Out of the box the device is **Custom**: it shows up as **Bluetooth Mouse** (manufacturer "Generic"), with the Bluetooth address of the chip itself. It does not pretend to be anyone's product.

## Changing it

In **Device → Appearance**, **Shown as** lists **Custom** first, then the mouse models below. Under it are the fields that make up the identity:

| Field | What it is | Empty means |
|---|---|---|
| **Name** | The name in the computer's Bluetooth list (1 to 29 characters) | The model's name (Custom: "Bluetooth Mouse") |
| **Manufacturer** | The maker the computer may display (1 to 32 characters) | The model's maker (Custom: "Generic") |
| **Vendor ID (VID)** and **Product ID (PID)** | The numbers a computer uses to recognise a product, hexadecimal like `0x046D` or decimal | The model's numbers (Custom: `0x1209` / `0x0001`) |
| **Bluetooth address** | Like `AA:BB:CC:DD:EE:FF`, first byte even | The model's vendor prefix plus the chip's own last three bytes (Custom: the chip's own address) |

You can pick a model and then change any field: the computer sees your values. The grey text in each field is what the selected model would use. Then **Apply and restart** (it asks for a second tap).

**What happens next, in plain terms:** the device restarts and now has a different identity, so computers that were paired with the old one do not recognise it. On each of them, remove the old device in the Bluetooth settings, then pair again (**Computers → Open pairing**). That is the only reset involved, and nothing else is lost: your token, WiFi and schedules stay.

## Models

Only identities whose vendor/product identifiers and address prefix were checked against public sources are offered (the Linux kernel's HID driver tables and the IEEE address-prefix registry).

| Shown as | Vendor : product | Address prefix |
|---|---|---|
| **Custom** (default) | `1209:0001`, manufacturer "Generic" | the chip's own |
| Logitech MX Master 3S | `046D:B034` | `00:1F:20` |
| Logitech MX Master 3 | `046D:B023` | `00:1F:20` |
| Logitech MX Master 4 | `046D:B042` | `00:1F:20` |
| Logitech MX Master 2S | `046D:B019` | `00:1F:20` |
| Logitech MX Anywhere 3S | `046D:B037` | `00:1F:20` |
| Logitech MX Anywhere 3 | `046D:B025` | `00:1F:20` |
| Logitech MX Vertical | `046D:B020` | `00:1F:20` |
| Logitech M720 Triathlon | `046D:B015` | `00:1F:20` |
| Razer Orochi V2 | `1532:0095` | `44:5E:CD` |
| Apple Magic Mouse | `004C:030D` | `58:AD:12` |
| Apple Magic Mouse 2 | `004C:0269` | `58:AD:12` |
| Apple Magic Mouse (USB-C) | `004C:0323` | `58:AD:12` |

A model's address prefix becomes the first three bytes of the Bluetooth address (the rest comes from the chip), so the address looks like it belongs to that vendor rather than to Espressif. To add a model, a vendor/product pair and an address prefix confirmed in a public source are required.

## Good to know

- **It is cosmetic.** The device does not speak a vendor's proprietary protocol (for example Logitech's HID++). Vendor software such as Logi Options+ or Razer Synapse will not treat it as the real product.
- **One identity.** A classic ESP32 has a single Bluetooth address, so the device is one mouse. Several computers can be paired with it, one connected at a time.
- **Keyboards** are not offered for now; the jiggler is a mouse.
- **The same fields over the API:** `PUT /api/appearance`, see [the API](api.md#appearance). Setting a Bluetooth address also changes the board's WiFi address.
