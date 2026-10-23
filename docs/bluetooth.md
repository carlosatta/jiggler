# Bluetooth: pairing, computers and disconnecting

The board is a Bluetooth peripheral with **one** identity (see [devices.md](devices.md)). One computer is connected at a time, but several can be paired; the board reconnects to whichever one is in range.

## Pairing policy

| Situation | Pairing |
|---|---|
| No computer paired yet (new board, or after "Dimentica tutti") | Open: the first computer that asks can pair. |
| At least one computer paired | **Closed.** A new computer that tries to pair is refused and its bond is removed. |
| Window opened by the owner | Open for 30–600 s (default 120), then closes by itself. |

Open a window from the dashboard (**Computer → Abbinamento**), with `POST /api/bluetooth/pairing`, the `start_pairing` MCP tool, or a **short press** of the BOOT button (the LED blinks fast while it is open). Close it early with `DELETE /api/bluetooth/pairing`.

The refusal is enforced after the pairing handshake: as soon as the board learns the newcomer's identity its link is dropped and its bond, if it made one, is deleted. This also applies to a computer that pairs without asking to bond. Already-paired computers are never affected.

**At most 3 computers** can be paired. A fourth is refused even while a window is open: forget one first. (The stack keeps 5 bond slots and evicts the oldest when full; the spare slots keep a refused newcomer from ever pushing a real bond out.)

## Which computer is connected?

Bluetooth does not give the peripheral the computer's name, only its address. The board numbers computers ("Computer 1", "Computer 2", …) and stores any name you give them (`PUT /api/bluetooth/peers/<id>`, 1–32 characters). To tell them apart: disconnect, see which computer loses the mouse, rename the one marked *Collegato adesso*.

## Disconnect (without powering off)

`POST /api/bluetooth/disconnect` drops the link, closes any open pairing window **and hides the device**: advertising stops, so the computer cannot simply reconnect. Because a bonded computer reconnects within seconds on its own, hiding is what makes "disconnect" stick. `POST /api/bluetooth/reconnect` (or opening a pairing window) makes the device visible again. The hidden state is not persisted: a restart brings the device back.

## Forgetting

`DELETE /api/bluetooth/peers/<id>` removes one bond on the board and drops that computer's link if it is connected. `DELETE /api/bluetooth/peers` removes all of them and reopens pairing. **Also remove the device in the computer's Bluetooth settings**, otherwise that computer keeps a stale key and may fail to reconnect until you do.

## Limits

- The board has no display or keypad, so pairing is "Just Works": anyone in radio range while a window is open can pair. Keep windows short.
- A refused intruder briefly holds an encrypted link before it is dropped.
- A link that connects and never starts pairing is not refused by the policy; it cannot receive input reports without encryption. Hide the device with Disconnect if you suspect one.
- Names are stored on the board, never read from the computer.
