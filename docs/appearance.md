# Appearance

*Appearance* is how the device introduces itself to computers over Bluetooth: the name you see in the Bluetooth list, and the vendor and product identifiers behind it. By default it presents itself as a **Logitech MX Master 3S**, so that nothing about it looks out of place.

<p align="center">
  <img src="images/system-appearance.png" alt="Appearance in System" width="260">
</p>

## Changing it

In **System → Appearance**, pick a model from the list, or choose **Custom name…** and type your own (1 to 29 characters). Then **Apply and restart** (it asks for a second tap).

**What happens next, in plain terms:** the device restarts and now has a different identity, so computers that were paired with the old one do not recognise it. On each of them, remove the old device in the Bluetooth settings, then pair again (**Computers → Open pairing**). That is the only reset involved, and nothing else is lost: your token, WiFi and schedules stay.

## Models

Only identities whose vendor/product identifiers and address prefix were checked against public sources are included.

| Shown as | Vendor : product | Address prefix |
|---|---|---|
| Logitech MX Master 3S (default) | `046D:B034` | `00:1F:20` |
| Razer Orochi V2 | `1532:0095` | `44:5E:CD` |
| Apple Magic Mouse 2 | `004C:0269` | `58:AD:12` |
| Custom name | `1209:0001`, manufacturer "Generic" | the chip's own |

The model's address prefix becomes the first three bytes of the Bluetooth address (the rest comes from the chip), so the address looks like it belongs to that vendor rather than to Espressif.

## Good to know

- **It is cosmetic.** The device does not speak the vendor's proprietary protocol (for example Logitech's HID++). Vendor software such as Logi Options+ or Razer Synapse will not treat it as the real product.
- **One identity.** A classic ESP32 has a single Bluetooth address, so the device is one mouse. Several computers can be paired with it, one connected at a time.
- **Keyboards** are not offered for now; the jiggler is a mouse.
- **Advanced (API only).** `PUT /api/devices` also accepts `manufacturer`, `vid`, `pid` and `mac` overrides for the mouse; see [the API](api.md#appearance). Setting a Bluetooth address also changes the board's WiFi address.
