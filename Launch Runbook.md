# Launch Runbook — UTS demo day

> Read this top-to-bottom on demo morning. Each step has a verify clause —
> if verify fails, stop and fix before moving on, don't push through.

---

## What gets shown to the audience

Three surfaces, running in parallel:

1. **The wall** — projector/big screen running `collective.html` in
   full-screen Chrome. ~25 tiles arranged by similarity.
2. **The boards** — physical ESP32+LCD stickers (1–5) on a table.
   Audience can tap two stickers together to trigger the match animation
   on both screens.
3. **The phone form** — audience members scan their sticker's QR code
   and fill in their own avatar. Their submission overrides that
   sticker's slot AND adds a tile to the wall.

All three share one Flask server running on your Mac.

---

## Network topology (the one thing that breaks everything if wrong)

```
   iPhone hotspot "Hannn"  (subnet 172.20.10.x)
            │
            ├── your Mac        — gets 172.20.10.12, runs Flask :8000
            ├── ESP32 sticker A — hard-coded SERVER_HOST=172.20.10.12
            ├── ESP32 sticker B — (only if burned)
            └── audience phones — scan QR → load Stage 1 form
```

**Single point of failure**: if your Mac doesn't land on `172.20.10.12`
when it joins the hotspot, the boards can't reach the server.
`secrets_shared.h` line 14 hard-codes that IP. iOS hotspot DHCP usually
hands out the same IP twice in a row, but not always — verify before
relying on it.

---

## Pre-demo prep (do at home tonight, NOT on demo morning)

### Checklist

- [ ] iPhone hotspot SSID is still `Hannn` and password is still `hancyhxy`
      (matches `secrets_shared.h` line 11–12). If you renamed them — re-burn
      every board with updated secrets.
- [ ] All five stickers (1–5) are physically labelled and you can tell them
      apart at a glance.
- [ ] Board A boots cleanly. (Open Serial Monitor @ 115200 → see
      `device hello OK` within ~5s of power-on.)
- [ ] Your Mac has Chrome installed and signed-in (so you don't get a
      "first-run" wizard mid-demo).
- [ ] Stage 1 form on phone tested end-to-end:
      `http://172.20.10.12:8000/?sticker=1` → fill → submit → board A
      LCD updates within 2s + tile appears on wall.

### Cable + power inventory

| Item | Count | Purpose |
|---|---|---|
| ESP32 stickers | 5 (or however many burned) | Boards |
| USB-C → USB-A or C cables | 1 per board | Power each board |
| USB power bank or wall adapter | 1–2 multi-port | Power source |
| HDMI / USB-C → projector adapter | 1 | Wall display |
| iPhone | 1 | Hotspot host |
| iPhone charger | 1 | Hotspot host won't die mid-demo |
| Your Mac + charger | 1 | Flask server + Chrome wall |

---

## Demo morning — launch sequence (do in this order)

### Step 1 — Network up

1. iPhone → Settings → Personal Hotspot → **Allow Others to Join**: ON
2. iPhone hotspot stays open. **Do not lock the iPhone screen during the
   demo** — iOS sometimes drops the hotspot when the screen sleeps.
3. Mac → click WiFi menu → connect to `Hannn`.
4. **Verify**: open Terminal, run `ifconfig en0 | grep "inet "` →
   expect `inet 172.20.10.12`. If you got a different IP (172.20.10.3,
   172.20.10.5, etc.), see **Troubleshooting → wrong IP** below.

### Step 2 — Server up

```bash
cd /Users/qiansui/Desktop/xinyihan/anonymous-connection/version2/web
./start.sh
```

What you'll see in Terminal:
```
[start] checking dependencies
[start] launching server — Ctrl+C to stop
[server]  stage 1 (phone): http://172.20.10.12:8000/
[server]  collective wall: http://localhost:8000/collective
[server]  stage 3 (tap)  : http://localhost:8000/stage3
```

**Verify**: in another Terminal tab, `curl -s http://127.0.0.1:8000/api/users`
returns JSON (an array, possibly with leftover entries from yesterday).

### Step 3 — Wipe yesterday's data

```bash
curl -X POST http://127.0.0.1:8000/admin/clear
```

OR open `http://localhost:8000/collective` → click "clear all profiles"
in the right-side panel.

**Verify**: `curl -s http://127.0.0.1:8000/api/users` now returns `[]`.

### Step 4 — Seed mock tiles (so the wall isn't empty when audience arrives)

Same folder, another tab:

```bash
.venv/bin/python3 mock_seed.py 25
```

**Verify**: Terminal prints 25 lines `[N/25] ... → sex mood=... bg=...
nick='...'` ending with `seeded 25/25 profiles`.

> The first 5 mocks carry sticker numbers (1–5) and overlay
> `STAGE3_USERS[N]`. The remaining 20 are wall-only decoration.
> If any audience member submits their own form with `?sticker=N`,
> their data overrides the mock for sticker N — so the mocks only
> "hold the slot" until a real person claims it.

### Step 5 — Wall on the projector

1. Plug your Mac into the projector.
2. Chrome → new window → `http://localhost:8000/collective`
3. Press `Ctrl+Cmd+F` (or click maximize → green-button-hold → "Enter
   Full Screen") → fullscreen mode.
4. **Optional**: collapse the debug panel (top-right ▾) so audience
   doesn't see the sliders. The status pill stays:
   `25 profiles · the closer they are, the more they have in common`.

**Verify**: 25 tiles fade in, similar pairs cluster. Bottom-right
shows the count + hint.

### Step 6 — Power the boards

1. Plug USB into board A. It boots → Serial Monitor (if you're watching)
   prints `device hello OK`.
2. LCD on board A should show the sticker's current avatar within ~2s.
   (Initially it'll show one of your mock seed profiles — whichever
   sticker number this board is configured as.)
3. If you have B/C/D/E burned, plug them in. Each will hello independently.

**Verify**:
- Each board's LCD is showing an avatar (sprite + quote bubble + nickname).
- Tap two boards' NFC stickers against each other. Both LCDs flip to
  the match animation: percentage → button → common topics →
  button → hint → button → back to avatar.
- If only one board lit up, the OTHER board never sent `hello` — check
  its Wi-Fi / power.

### Step 7 — You're live

Audience flow:
- They walk up, see the wall + sticker board.
- They pick up a sticker, scan its QR with their phone.
- Phone loads `http://172.20.10.12:8000/?sticker=N`.
- They fill the two-step form → submit.
- Their tile slides into the wall + that sticker's LCD updates.
- They tap their sticker against someone else's → match animation
  plays on both boards.

---

## Troubleshooting (in order of likelihood)

### Wall page won't load

Check the URL bar:
- `127.0.0.1:5500/...` → **Live Server**, won't work. Switch to `localhost:8000/collective`.
- `localhost:8000/collective` but page blank → server died. Check the
  Terminal where you ran `./start.sh` for stack trace.

### Phone scans QR but page won't load

- Phone connected to a different Wi-Fi (not the hotspot)? Phone Settings → Wi-Fi → join `Hannn`.
- Phone disconnected because iPhone locked. Wake iPhone, reconnect phone.
- Your Mac's IP ≠ `172.20.10.12` → see "wrong IP" below.

### Board's LCD shows the loading screen forever

The board hasn't successfully helloed the server. Three causes:
- **Wi-Fi**: SSID or password mismatch. Check `secrets_shared.h`.
- **IP**: Mac landed on a different LAN IP. Re-burn board with new IP
  OR reset the iPhone hotspot until it hands out `172.20.10.12` again.
- **Power**: USB cable doesn't deliver enough current. Try a different
  cable + a wall adapter (not a low-power port on your Mac).

Diagnostic: open Arduino IDE → Tools → Serial Monitor @ 115200 → power-
cycle the board. You should see:
```
[wifi] connecting to Hannn ...
[wifi] connected, ip=172.20.10.X
[http] hello to 172.20.10.12:8000 ...
device hello OK
```
If it stops at `[wifi] connecting`, password is wrong.
If it stops at `[http] hello`, IP mismatch.

### Wrong IP — Mac got 172.20.10.5 instead of 172.20.10.12

Two paths:
1. **Quick fix** — turn iPhone hotspot OFF, wait 10s, turn ON. Mac
   re-DHCPs, often gets `.12` again.
2. **Real fix** — give up on the hard-coded IP. Edit `secrets_shared.h`
   line 14 → set `SERVER_HOST` to whatever IP your Mac actually got →
   re-burn all boards. **Don't do this on demo morning.** Do it tonight
   if you want belt-and-suspenders.

### Tap doesn't trigger match animation

- Both boards must be running. If only A is online, tapping A against
  B's sticker pad reads A's UID + B's UID, but B is offline so B never
  gets the push.
- Each tap has a ~3s cooldown (otherwise multi-second NFC reads spam
  the animation). Wait then retry.

### Match animation stuck on first screen

The board's button advances each screen. If the button isn't being
pressed, the screen stays. Hard-reset (power-cycle) the board to abort
the animation.

### Wall fills up with junk submissions

Open Chrome on the wall → expand the debug panel → "clear all profiles".
This wipes both `users.json` and `STAGE3_USERS` overlays, and re-broadcasts
the empty state to the wall. **Re-seed with mocks** if you want the
visual to recover:

```bash
.venv/bin/python3 mock_seed.py 25
```

---

## After the demo

1. Ctrl+C in the Terminal running `start.sh` → kills the server.
2. Unplug the boards.
3. iPhone Settings → turn hotspot OFF (saves your battery).
4. Optional: `git add users.json && git commit -m "demo run N"` if you
   want to archive the audience submissions for the writeup.

---

## Quick reference card (print this if you want)

| Action | Command / URL |
|---|---|
| Start server | `./start.sh` from `version2/web/` |
| Wipe wall | `curl -X POST http://127.0.0.1:8000/admin/clear` |
| Seed 25 mocks | `.venv/bin/python3 mock_seed.py 25` |
| Wall URL | `http://localhost:8000/collective` |
| Phone stage 1 | `http://172.20.10.12:8000/?sticker=N` |
| Stop server | Ctrl+C |
| Check Mac IP | `ifconfig en0 \| grep "inet "` |
| Check board log | Arduino IDE → Serial Monitor → 115200 baud |
