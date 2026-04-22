# anonymous-connection

> A tiny avatar-of-the-moment maker + a small ESP32 screen that mirrors it.

Made for UTS *Physical Interaction of Prototyping* (2026). Two surfaces sharing one
avatar: a **web page** where you pick a body, a vibe, a one-liner; and a **device** on
your desk that quietly shows the same picture to whoever walks past.

## Play with it

→ [Live page](https://hancyhxy.github.io/anonymous-connection/) — pick a body, tap a
tile, drag the energy slider, type a thought. No install, no account.

Run locally:

```bash
cd version2/web
python3 -m http.server 8000
# open http://localhost:8000
```

## Repo map

- `version2/` — the current iteration (this is what gets deployed)
  - `web/` — the browser surface (plain HTML/CSS/JS, no build step)
  - `firmware/serial_avatar/` — ESP32-C6 Arduino sketch that mirrors the avatar over USB serial
  - `firmware/smoke_test/` — hardware smoke test
  - `design/` — question design, quote design, mechanism notes
  - `concept.md` — what the project is
  - `design.md` — how it works, end to end
  - `hardware.md` — parts list + wiring
  - `spec.md` — target specs
  - `process/` — process narrative (journey map, slides)
  - `scripts/` — sprite / glyph helper scripts (Python)
- `version1/` — earlier iteration, archival

If you're a classmate and want the short version, read `version2/concept.md` first.

## The device side

`version2/firmware/serial_avatar/` is the Arduino sketch. It reads a small JSON packet
over USB serial from the live page (Chrome/Edge only — uses the Web Serial API) and
renders the avatar on a small TFT screen. Browsers without Web Serial support still
work for the web toy; the "Connect Device" button just stays disabled. See
`version2/hardware.md` for wiring.

The ESP32-C6 development board is the Waveshare
[ESP32-C6-LCD-1.3](https://www.waveshare.com/wiki/ESP32-C6-LCD-1.3) — the vendor's
reference demos are not included in this repo; grab them from Waveshare's site if you
want to experiment.

## Credits

Sprite art and the three-tone color-derivation algorithm are adapted from
[The Pudding — *Hello, Stranger*](https://pudding.cool/2025/06/hello-stranger/) by
Alvin Chang (MIT License, 2022). Everything else is original coursework.
