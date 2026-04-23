# Anonymous Connection

> A tiny avatar-of-the-moment maker + an ESP32 screen that mirrors it.

Made for UTS **Physical Interaction of Prototyping** (2026). Two surfaces
share one avatar: a **web page** where you answer a handful of questions
about who you are right now, and a small **device** on your desk that
quietly shows the same picture to whoever walks past.

The whole point is that your avatar is "who you are **today**," not a
fixed profile. Come back tomorrow, pick different answers, get a
different avatar.

## Try it

→ **[Live page](https://hancyhxy.github.io/anonymous-connection/)** —
no install, no account.

Or run it locally:

```bash
cd version2/web
python3 -m http.server 8000
# open http://localhost:8000
```

### Stage 2 — Collective wall (multi-phone, with backend)

The collective wall is a second screen showing every avatar that's been
submitted, arranged so similar interests cluster together. Phones scan
a QR code printed in the terminal to join. Designed for in-person tests
where the host laptop runs the wall and 6+ people use phones to join.

```bash
cd version2/web
./start.sh                  # one-shot: creates .venv, installs deps, runs server
```

First run creates a Python virtualenv (~30 s, one-time). Subsequent runs
launch instantly. The terminal prints a QR code — phones on the same
WiFi can scan it to land on the form. On the host laptop, open
`http://localhost:8000/collective` to see the wall.

If the launcher complains about Python: `brew install python3` then
re-run `./start.sh`.

---

## How the avatar is generated

The avatar is not assembled from parts. Each one is a **single
hand-drawn 18×18 ASCII sprite** — adapted from The Pudding's
[*Hello, Stranger*](https://pudding.cool/2025/06/hello-stranger/)
project (MIT, 2022). Your answers pick **which** sprite to show and
**what colors** it renders in.

### The six questions

| Question | What it controls |
|---|---|
| **Body** — `male` / `female` | Which of two base bodies |
| **Avatar** — one of 15 tiles | The exact hand-drawn character (5 looks × 3 poses per body = 15) |
| **Smile?** — neutral / smile | Switches to a smile-variant sprite if one exists for the chosen tile |
| **Mood** — chill / curious / playful / tired / glowing | Picks a hue family for the color palette |
| **Energy** — empty → electric (6 steps) | Picks the brightness step inside that hue family |
| **Interests** — up to 3 tags | Shown as badges under the avatar (music, film, sport, art, tech, food, gaming, travel, books, photo, dance, pets) |
| **Quote** — free text, 28 chars | A short one-liner shown in a speech bubble above the avatar |

### From answers to pixels

1. **Sprite lookup.** `body + avatar tile + smile` form a key like `male_2_1smile`. The key selects a 3-frame ASCII sprite from `sprites.json`. The renderer cycles through the frames for a subtle breathing animation.
2. **Color derivation.** `mood × energy` picks a single hex color from a 5×6 palette (30 colors total). That one hex is then passed through three functions — `darkenColor`, `lightenColor`, `darkestColor` — to derive *character color*, *background color*, and *border color*. The three-tone algorithm is ported line-for-line from *Hello, Stranger* (see `version2/web/renderer.js`).
3. **Quote and interests** are written in directly — no mapping, no processing. They're just shown.

### What we borrowed vs what we built

| Layer | Source |
|---|---|
| Sprite art (36 hand-drawn ASCII characters) | *Hello, Stranger*, MIT, unchanged |
| Color derivation algorithm (darken/lighten/darkest) | *Hello, Stranger*, MIT, unchanged |
| 30-color mood × energy palette | Ours — color *values* inherited from *Hello, Stranger*, but re-organised into a 2D grid around our own semantics |
| Question copy (body / avatar / smile / mood / energy / interests / quote) | Ours — replaces the original's race/age/politics/edu demographics |
| Interest tag badges | Ours — drawn on the preview card, inline under the avatar |
| ESP32-C6 TFT rendering | Ours — the web uses browser fonts, the device uses a bitmap glyph lookup table (fonts won't fit in MCU flash) |

For the full design rationale, cross-surface sync model, and a dimension
registry for future extensions, see the Chinese design docs in
[`version2/design.md`](./version2/design.md) and
[`version2/design/`](./version2/design) (internal working docs).

---

## The device side

`version2/firmware/serial_avatar/` is an Arduino sketch for the
**Waveshare ESP32-C6-LCD-1.3**. It reads a small JSON packet over USB
serial from the live page (Chrome/Edge only — [Web Serial API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Serial_API)) and renders the same avatar on its TFT screen. The
"Connect Device" button at the bottom of the page is the entry point;
if you don't have the hardware, the button stays disabled and the web
toy still works on its own.

Wiring, parts list, and pin definitions live in
[`version2/hardware.md`](./version2/hardware.md).

---

## Repo map

- `version2/` — the current iteration, what's deployed above
  - `web/` — the browser surface (plain HTML/CSS/JS, no build step)
  - `firmware/serial_avatar/` — the ESP32-C6 Arduino sketch
  - `design/`, `concept.md`, `design.md`, `hardware.md`, `spec.md` — internal design docs
- `version1/` — earlier iteration, archival

---

## Credits

Sprite art and the three-tone color-derivation algorithm are adapted
from [The Pudding — *Hello, Stranger*](https://pudding.cool/2025/06/hello-stranger/)
by Alvin Chang (MIT License, Copyright (c) 2022 The Pudding).
Everything else is original coursework.

## License

This project is licensed under the MIT License (see [LICENSE](./LICENSE)).
Third-party notices for assets borrowed from *Hello, Stranger* and the
Adafruit GFX library are included in the same file.
