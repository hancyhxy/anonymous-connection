# User Flow — Version 3 Prototype (Wireless Wi-Fi)

> Audience-facing storyboard for the V3 wireless build. Use this when
> explaining the project to teammates / tutor / classmates.
>
> This is **not** a setup guide — see `Launch Runbook.md` for that.

---

## Version positioning

| Version | What it was | Why we moved on |
|---|---|---|
| V1 | Single sticker, USB tethered to a laptop, peers mocked in JSON | No real interaction between people — felt like a single-player demo |
| V2 | Multiple stickers over USB, real form on phone, real wall | Cables on the table broke the "wearable" feeling; only one demoer could hold a sticker at a time |
| **V3 (this build)** | **Battery-powered, Wi-Fi wireless, 5 free-standing stickers** | **Two strangers can each hold a sticker and walk around the room** |

---

## Five stages, one wall

The audience moves through five stages — each is a distinct shift in
what they're doing.

```
   ┌─────────────┐    ┌─────────────┐    ┌─────────────┐    ┌─────────────┐    ┌─────────────┐
   │  [1] SCAN   │ →  │ [2] UPLOAD  │ →  │ [3] BROWSE  │ →  │  [4] MATCH  │ →  │[5] INTERACT │
   │             │    │             │    │             │    │             │    │             │
   │  scan QR    │    │  fill in    │    │  see your   │    │  find       │    │  tap        │
   │  on the     │    │  interests  │    │  tile +     │    │  someone    │    │  stickers   │
   │  sticker's  │    │  & avatar   │    │  others on  │    │  to tap     │    │  → animation│
   │  LCD        │    │  → submit   │    │  the wall   │    │  with       │    │  on both    │
   └─────────────┘    └─────────────┘    └─────────────┘    └─────────────┘    └─────────────┘
     ~5 sec              ~30 sec            ~10 sec            ~30 sec            ~30 sec
     phone in hand       phone form         look at wall       social             both LCDs
```

---

### [1] SCAN — "this one's mine"

The audience walks up to a table of **5 free-standing stickers** (each
battery-powered, no cables). Each sticker's LCD shows a **QR code**
unique to that sticker number.

They pick one up, **scan the QR with their phone camera**. The phone
opens the upload form, already bound to that sticker number.

> _Why each sticker has its own QR_: scanning encodes the sticker
> number into the URL (`?sticker=3`). The user never has to "choose
> which sticker am I" — picking up the sticker IS the choice.

---

### [2] UPLOAD — "make yourself"

A two-step wizard on the phone, ~30 seconds total.

- **Step 1 — Interests.** Pick 1–3 things you care about right now
  (`music film sport art tech food gaming travel books photo dance pets`),
  optionally pick sub-tags under each.
- **Step 2 — Avatar.** Choose a body, a hand-drawn ASCII character,
  a background colour, optionally a quote and a nickname.

Hit submit. The form vanishes.

> _Design split_: interests drive **who you match with**; avatar
> drives **how you appear**. The two layers are intentionally
> separate — interests are private, avatar is public.

---

### [3] BROWSE — "where do I sit on the wall?"

Within ~2 seconds, **the sticker's LCD switches** from the QR code
to the avatar they just built, and **a new tile slides onto the wall**.

They look up. Their tile drifts across the wall and finds its place
near others with shared interests. A small line at the bottom-right
reads:

> _the closer they are, the more they have in common_

They can stand there and watch who clusters near them — already
discovering candidates for the next stage before any words have
been exchanged.

---

### [4] MATCH — "let me find someone"

Holding their sticker, they walk over to someone else who's also
holding one. Maybe they spotted that person's tile near theirs on
the wall, or maybe they just liked the vibe of the other avatar.

This stage is **all social, no UI** — it's the conversation
choreography of approaching another person with a small object in
hand. The system doesn't push them, doesn't suggest — the wall has
done its hint.

---

### [5] INTERACT — "let's find out"

They press the two stickers together (NFC tags on the back). Both
LCDs flip to a 3-screen animation:

```
   ① percentage    →    ② shared topics    →    ③ icebreaker prompt
   e.g. 66%             "you both like           "a film you'd
                         film, sci-fi"           hand to a stranger
                                                 right now?"
```

Between screens, **each person presses their sticker's button** to
advance. After the third press, both LCDs return to their avatars.
The two strangers now have something specific to talk about.

> _Why three screens, three button presses_: with two people each
> holding a sticker, a timed auto-play would force them to read at
> the same speed. Manual advance lets each side acknowledge before
> turning the page — the device sets the rhythm of the conversation.

---

## What's anonymous about this

Three deliberate choices keep it "anonymous":

1. **No name, no photo.** The only optional identifier is a 12-char nickname.
2. **Interests are invisible.** The matching field never appears on
   the sticker or on the wall. You can't profile someone by reading
   their LCD — you have to actually tap to find out.
3. **No accounts, no history.** A submission is one snapshot of how
   someone wants to be seen today.

The only persistent question:
> _can two people who might never have introduced themselves still
> find one good thing to talk about?_

---

## What's new in V3 vs V2

| V2 | V3 |
|---|---|
| Stickers tethered by USB to a laptop | **Battery-powered, fully wireless (Wi-Fi)** |
| Single-sticker-at-a-time demo | **Up to 5 physical stickers simultaneously** |
| One "join" QR on the wall | **One QR per sticker — picking up = choosing** |
| Form asked for mood / emotion by name | **Mood anonymised — pick a colour, not a feeling** |
| Interest tags rendered on sticker | **Interest tags invisible — matching layer only** |
| Wall scrolled through profiles | **Force-directed clustering — similar people gather** |

---

## Implementation status (demo day)

For transparency on what's truly running vs what's the V3 target state:

| Stage | V3 target | Demo-day implementation |
|---|---|---|
| [1] SCAN | QR rendered on sticker LCD | **Paper QR placed beside each sticker** (LCD QR rendering not yet in firmware) |
| [2] UPLOAD | Phone form bound to sticker N | Working |
| [3] BROWSE | Tile appears on wall, clusters by similarity | Working |
| [4] MATCH | Free walking, no system prompt | Working (social, no system involvement) |
| [5] INTERACT | NFC tap → match animation on both LCDs | Working (boards that are powered and online) |
