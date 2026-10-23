# Product

## What it is
Jiggler is a small ESP32 board that pairs with a computer over Bluetooth as a mouse (optionally a keyboard) and
makes tiny, human-looking movements so the computer never goes idle. It is configured from a web dashboard served
by the board itself, a REST API and an MCP server.

## Who uses it and where
One owner, on a phone or laptop on the same network (or on the board's own WiFi), glancing at it between other
things. Typical questions, in this order: *Is it connected, and to which computer? Is it running? How do I
disconnect it or pair another computer?* Everything else (scripts, automations, device identity, network) is
occasional and lives out of the way.

## Register
Operate: a tool, not a brochure. Scannability and honest state beat decoration. English UI, plain words, no
jargon on the first screen.

## Principles
- **Say what is true.** Bluetooth does not tell the board the computer's name, so computers are "Computer 1",
  "Computer 2" until the owner renames them. The UI never pretends to know more than the device does.
- **Every state has a next action.** Disconnected, hidden, pairing open, nothing paired: each says what is happening
  and offers the one thing to do about it.
- **Destructive actions are reversible in the moment.** Forgetting a computer or restarting asks for an inline
  confirmation, never a modal.
- **Works on the worst day.** No WiFi at home, no internet, a phone in dark mode, a flaky link: the page still
  tells the truth and recovers by itself.

## Anti-goals
- A generic admin template: identical cards, icon-title-text grids, a hero number.
- Hiding that the device is a Bluetooth peripheral with one identity (one computer at a time).
- Cloud, accounts, telemetry. The board is the whole product.

## Visual world
Calm instrument panel. Warm neutral paper (dark: warm charcoal), one deep-teal accent that means "alive and
connected", system fonts, tabular numerals for countdowns, one authored motion (the connection pulse).
