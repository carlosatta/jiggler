# Bluetooth: pairing, computers and disconnecting

The board is a Bluetooth peripheral with **one** identity (see [devices.md](devices.md)). One computer is connected at a time, but several can be paired; the board reconnects to whichever one is in range.

## Pairing policy

| Situation | Pairing |
|---|---|
| A brand-new board that has never been paired | Open: the first computer that asks can pair. |
| Every bond lost later (the stack dropped a stale one, or you pressed "Forget all") | **Closed.** The board remembers it has been paired; only you can open a window. |
| At least one computer paired | **Closed.** A new computer that tries to pair is refused and its bond is removed. |
| Window opened by the owner | Open for **3 minutes** (the API also accepts 30–600 s), then closes by itself. It closes earlier if the owner stops it, or **as soon as a new computer pairs**. |

Open a window from the dashboard (**Computers → Pairing**), with `POST /api/bluetooth/pairing`, the `start_pairing` MCP tool, or a **short press** of the BOOT button (the LED blinks fast while it is open). The dashboard shows a countdown. Stop it early with `DELETE /api/bluetooth/pairing`.

Any new bond closes the window, so whoever pairs first in range uses it up: do it yourself, quickly, and check Computers afterwards. The refusal is enforced after the pairing handshake: as soon as the board learns the newcomer's identity its link is dropped and its bond, if it made one, is deleted. This also applies to a computer that pairs without asking to bond. Already-paired computers are never affected.

**At most 3 computers** can be paired. A fourth is refused even while a window is open: forget one first. (The stack keeps 5 bond slots and evicts the oldest when full; the spare slots keep a refused newcomer from ever pushing a real bond out.)

## When the computer disconnects

If you press *Disconnect* in the computer's own Bluetooth menu, the board sees the computer end the connection and treats it like the dashboard's Disconnect: the device is hidden for 3 minutes, so the computer does not simply reconnect at once. (Without this, macOS reconnects immediately because the device keeps advertising.) A connection lost for other reasons (out of range, computer asleep or off) is not affected, and neither is a drop during pairing.

## Which computer is connected?

Bluetooth does not give the peripheral the computer's name, only its address. The board numbers computers ("Computer 1", "Computer 2", …) and stores any name you give them (`PUT /api/bluetooth/peers/<id>`, 1–32 characters). To tell them apart: disconnect, see which computer loses the mouse, rename the one marked *Connected now*.

## Disconnect (without powering off)

`POST /api/bluetooth/disconnect` drops the link, closes any open pairing window and **hides the device for 3 minutes**: advertising stops, so the computer cannot simply reconnect. Because a bonded computer reconnects within seconds on its own, hiding is what makes "disconnect" stick. After the 3 minutes the device becomes visible again and the computer can reconnect, by itself or from its Bluetooth menu. `POST /api/bluetooth/reconnect` (or opening a pairing window) ends the hold early. The hold is not persisted: a restart brings the device back. The API reports the countdown as `heldRemainingMs`.

There is no way for a peripheral to tell a deliberate "connect" click on the computer from its automatic reconnection, so during the 3 minutes nothing can connect. If you want the computer to stay disconnected until *you* reconnect it, disconnect from the computer's own Bluetooth menu instead: computers do not reconnect by themselves after that, and you can reconnect from the same menu at any time.

## Forgetting

`DELETE /api/bluetooth/peers/<id>` removes one bond on the board and drops that computer's link if it is connected. `DELETE /api/bluetooth/peers` removes all of them; pairing stays closed until you open a window. **Also remove the device in the computer's Bluetooth settings**, otherwise that computer keeps a stale key and may fail to reconnect until you do.

## Limits

- The board has no display or keypad, so pairing is "Just Works": anyone in radio range while a window is open can pair. Keep windows short.
- A refused intruder briefly holds an encrypted link before it is dropped.
- A link that connects and never starts pairing is not refused by the policy; it cannot receive input reports without encryption. Hide the device with Disconnect if you suspect one.
- Names are stored on the board, never read from the computer.
