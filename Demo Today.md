# Demo Today — single sheet for demo day

> The one document to read on demo day. Holds:
> - Context I share with the AI assistant (so we don't re-explain)
> - Two demo branches (mock-only vs real)
> - Hard rules (what NOT to do)
> - What to say to trigger each branch

---

## 1. Context the AI already knows about this demo

Captured here so you don't have to re-brief mid-demo:

- The project is `anonymous-connection`, located at
  `/Users/qiansui/Desktop/xinyihan/anonymous-connection/`.
- There is **exactly one Flask server** — `version2/web/server.py`. It serves
  three surfaces from one process on **port 8000**:
  - `/` — stage 1 form on phones (`?sticker=N` binds to physical board N)
  - `/collective` — wall view on the projector
  - `/api/device/*` — Wi-Fi polling endpoint for ESP32 boards
- There are 5 physical boards (sticker 1..5). Each is battery-powered,
  joins iPhone hotspot `Hannn`, and polls the server every 500 ms over
  Wi-Fi for new profile / match events. **Board A is the only one
  confirmed working** as of demo morning.
- `mock_seed.py` can inject 25 synthetic profiles into the wall.
  Profiles 1–5 carry sticker numbers and overlay `STAGE3_USERS[1..5]`;
  profiles 6–25 are wall-only decoration with no sticker binding.
- Yesterday we replaced the wall's mood-legend with the similarity
  hint pill `"the closer they are, the more they have in common"`
  and removed the old centre-screen QR overlay.
- **New today**: mock-mode wall has a small QR card in the top-right
  corner. Shown only when wall is loaded with `?mock=1`. QR encodes the
  bare stage-1 URL (no `?sticker=N`), so scanning it adds a wall tile
  WITHOUT touching sticker 1..5 slots.

---

## 2. Two demo branches

You'll pick one of these two at demo time. They share the same server;
only what's seeded and what's shown differs.

### Branch A — Real demo (audience tries the actual hardware)

```
1. Start the server (clean).
2. Have all the boards you can power online (A for sure; B–E if burned).
3. Paper QR codes labelled "1".."5" sit beside each physical board.
4. Audience picks up a sticker → scans its paper QR → form → submit
   → that sticker's LCD updates + a tile appears on the wall.
5. Two people tap stickers → match animation on both LCDs.
6. Empty wall is fine — tiles arrive as people submit.
```

**Wall URL**: `http://localhost:8000/collective` (no `?mock=1`, no QR card).

### Branch B — Mock demo (tutor sees a full wall + tries scanning)

```
1. Start the server (clean).
2. Seed 25 mock profiles. Profiles 1–5 visually occupy sticker slots
   but the tutor will NOT scan into those slots.
3. Physical boards stay OFF (or, with the boards online, sticker-paper
   QRs are physically removed/hidden so no one accidentally scans into
   sticker 1..5).
4. Wall loads with ?mock=1 → top-right QR card visible.
5. Tutor scans the QR card → fills the form → submit → a new
   wall-only tile appears beside the 25 mocks. Sticker 1..5 slots
   are untouched.
6. No tap interaction in this branch (no boards online or none used).
```

**Wall URL**: `http://localhost:8000/collective?mock=1` (top-right QR card visible).

### Hybrid (rare): real boards online + mock wall

Possible if you want the tutor to scan the wall QR AND the audience
to also tap physical sticker A. Keep paper QRs hidden so audience
can't scan into sticker 1..5 slots; let physical interaction happen
only through NFC tap.

---

## 3. Hard rule — sticker 1..5 URLs are private

`?sticker=1` through `?sticker=5` are **physical-board-only entry points**.
The only two legal ways to land on those URLs:

| Legal usage | Who | Why |
|---|---|---|
| **(a)** Scan the paper QR taped next to a physical board | Audience holding the sticker | The QR encodes the sticker number, picking up = choosing |
| **(b)** You or your teammate type the URL into a browser by hand | You / teammate | Pre-demo dry-run only |

**Everyone else** — tutor, classmates, random observers — should
scan only the wall's top-right QR card (when mock mode is active),
which is a bare URL with no sticker binding.

Why this matters:
- Each `?sticker=N` submission **overwrites** the `STAGE3_USERS[N]`
  overlay on the server, replacing the mock at that slot with the
  fresh form data.
- If the tutor accidentally lands on `?sticker=3` and submits, the
  mocked profile at sticker 3 is gone, and any physical board #3
  (if online) flips to the tutor's avatar.
- The hard rule is enforced by **physical access**, not by code.
  The 5 sticker URLs are unprotected — anyone who knows them can hit
  them. We rely on:
  - Paper QRs being physically beside (or removed from) the actual sticker
  - NOT writing `?sticker=N` URLs in slides, on whiteboards, or anywhere visible
  - This document being on your machine only, not shared with the tutor

---

## 4. How to trigger each branch — what to say to me

Pick one of these one-liners and the assistant runs the full sequence:

| You say | I do |
|---|---|
| `帮我起 demo (mock 模式)` or `start mock demo` | Start server → clear users.json → seed 25 mocks → tell you wall URL with `?mock=1` |
| `帮我起 demo (真实模式)` or `start real demo` | Start server → clear users.json → tell you wall URL (no `?mock=1`) |
| `先起 server 不投 mock` | Start server only → clear users.json → wait for you |
| `重新投一批 mock` | Skip restart, clear + reseed 25 |
| `server 起来了吗` | Curl `/api/users` to verify |
| `关 server` | Stop the background Flask process |

I cannot do these — you do them physically:

- Turn on iPhone hotspot `Hannn`; connect Mac to it
- Verify Mac got IP `172.20.10.12` (`ifconfig en0 | grep "inet "`)
- Power the physical boards
- Plug Mac into projector, open the wall URL fullscreen
- Place / hide paper QRs depending on which branch
- Scan QRs with the tutor / audience phones

---

## 5. Verification before going live

Three end-to-end checks. Don't skip them.

### Check 1 — server reachable on LAN IP

```bash
curl -s http://172.20.10.12:8000/api/users | head -c 80
```

If this fails with timeout: your Mac is on a different IP than the
boards expect. See `Launch Runbook.md` → Troubleshooting → wrong IP.

### Check 2 — wall renders the seeded tiles

Open `http://localhost:8000/collective?mock=1` (or no `?mock=1` for real
branch). Should see:
- 25 tiles (mock branch) or 0+ tiles (real branch, depending on
  whether you cleared)
- Status pill bottom-right: `N profiles · the closer they are, the
  more they have in common`
- Top-right white QR card visible only if `?mock=1`

### Check 3 — at least one physical board flips an avatar

(Skip in pure mock branch.) On your phone:

1. Open `http://172.20.10.12:8000/?sticker=1`
2. Fill the form (nickname `test`, pick any interest, pick any avatar)
3. Hit submit

Expected:
- Board #1's LCD switches within ~2 seconds to the avatar you built
- A tile labelled `test` (or whatever you nicknamed) appears on the wall

If the wall updates but the board doesn't → board isn't online; check
power and Wi-Fi.
If neither updates → server didn't get the submission; check the
Terminal where server.py is running.

After this check, clear and re-seed so the test profile doesn't
linger:
```
帮我重新投一批 mock
```

---

## 6. Live URLs cheat-sheet

| What | URL |
|---|---|
| Real-mode wall | `http://localhost:8000/collective` |
| Mock-mode wall | `http://localhost:8000/collective?mock=1` |
| Stage-1 (test from your phone) | `http://172.20.10.12:8000/?sticker=1` |
| Sticker-N form for board N | `http://172.20.10.12:8000/?sticker=N` (PRIVATE — see Section 3) |
| Wall join QR (mock mode only) | `http://172.20.10.12:8000/` (no sticker param — what the QR encodes) |

---

## 7. After demo — wind-down

```
关 server.
```

That stops the Flask process. The boards will keep polling and time out
their requests harmlessly. Unplug them when convenient.

If you want to preserve the audience submissions:
```bash
cd /Users/qiansui/Desktop/xinyihan/anonymous-connection/version2/web
cp users.json users-demo-$(date +%Y%m%d).json
```
