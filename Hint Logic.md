# Hint Logic — full prompt table & selection rules

> Source: `version2/web/server.py` (`INTEREST_HINTS` dict + `compute_match()`).
> Last synced: 2026-05-12.

This document is the single source of truth for **every prompt the board's
Screen 3 can show** + **how the server picks which one to send**. Hand
this directly to whoever's putting the prompts in a slide.

---

## 1. Full prompt table

13 prompts total — one per primary interest tag (12), plus one fallback
for the 0% case.

| # | Trigger | Trigger value | Prompt text |
|---|---|---|---|
| 1 | `INTEREST_HINTS["music"]`  | shared primary = **music**  | what's been on repeat for you this week? |
| 2 | `INTEREST_HINTS["film"]`   | shared primary = **film**   | a film you'd hand to a stranger right now? |
| 3 | `INTEREST_HINTS["sport"]`  | shared primary = **sport**  | what move did your body need today? |
| 4 | `INTEREST_HINTS["art"]`    | shared primary = **art**    | last thing you saw that stopped you mid-step? |
| 5 | `INTEREST_HINTS["tech"]`   | shared primary = **tech**   | the side project you keep almost starting? |
| 6 | `INTEREST_HINTS["food"]`   | shared primary = **food**   | something you cooked or ate that surprised you? |
| 7 | `INTEREST_HINTS["gaming"]` | shared primary = **gaming** | a game world you wish you lived in for a week? |
| 8 | `INTEREST_HINTS["travel"]` | shared primary = **travel** | a place you'd go back to without hesitating? |
| 9 | `INTEREST_HINTS["books"]`  | shared primary = **books**  | a sentence from a book that's still with you? |
| 10 | `INTEREST_HINTS["photo"]` | shared primary = **photo**  | the shot you almost took but didn't? |
| 11 | `INTEREST_HINTS["dance"]` | shared primary = **dance**  | a song that makes you move without thinking? |
| 12 | `INTEREST_HINTS["pets"]`  | shared primary = **pets**   | an animal that recently made your day? |
| 13 | `FALLBACK_HINT`           | **no shared primary** (0% match) | you two picked totally different things — what's the most surprising thing you've done lately? |

---

## 2. Selection rules — which prompt plays

Two rules only. They run inside `compute_match()` after the score is computed.

### Rule A — No shared primary → `FALLBACK_HINT`

If the two users have **zero overlapping primary tags**:

```
score = 0
hint  = FALLBACK_HINT
```

This is the only branch that uses the fallback. Any non-zero score
takes Rule B.

### Rule B — At least one shared primary → `INTEREST_HINTS[common[0]]`

If the two users share **one or more primary tags**, the prompt is
selected by:

```python
common = sorted(set(me.interests) & set(peer.interests))
hint   = INTEREST_HINTS[common[0]]
```

Key detail: `common[0]` is the **alphabetically first** shared primary,
not "the most common" or "the rarest". So when several primaries
overlap, the alphabetical order below dictates which prompt wins:

```
art < books < dance < film < food < gaming < music < pets < photo < sport < tech < travel
```

Multiple shared primaries do **not** produce multiple prompts. Only one
prompt is shown on Screen 3, regardless of how many primaries overlap.
The extra-overlap information surfaces on **Screen 2** instead, via the
`common_tags` list.

---

## 3. Worked examples

| User A primaries | User B primaries | Shared (sorted) | Score | Prompt source | Prompt text |
|---|---|---|---|---|---|
| `sport, tech` | `music, books` | _(none)_ | **0%** | `FALLBACK_HINT` | you two picked totally different things — what's the most surprising thing you've done lately? |
| `music, film, travel` | `music, books, photo` | `[music]` | **33%** | `INTEREST_HINTS["music"]` | what's been on repeat for you this week? |
| `music, film, art` | `music, film, tech` | `[film, music]` → first = `film` | **66%** | `INTEREST_HINTS["film"]` | a film you'd hand to a stranger right now? |
| `music, film, art` | `music, film, art` | `[art, film, music]` → first = `art` | **100%** | `INTEREST_HINTS["art"]` | last thing you saw that stopped you mid-step? |
| `travel, books, pets` | `pets, books` | `[books, pets]` → first = `books` | **66%** | `INTEREST_HINTS["books"]` | a sentence from a book that's still with you? |
| `dance, photo` | `dance` | `[dance]` | **33%** | `INTEREST_HINTS["dance"]` | a song that makes you move without thinking? |

---

## 4. Score → percentage mapping (for context)

| Shared primary count | Displayed score |
|---|---|
| 0 | **0%** |
| 1 | **33%** |
| 2 | **66%** |
| 3 | **100%** (raw 99 is cosmetically bumped to 100) |

Notes:
- Sub-tags **do not affect score or hint selection**. They only affect
  the comma-joined tag list shown on Screen 2 (`common_tags`).
- The wall's force-directed layout uses a *different* similarity
  formula (`primary × 1.0 + sub × 3.0`) — this document covers only
  the on-board match animation prompts.

---

## 5. If you want to change the prompts

Edit `version2/web/server.py` lines 152-166 — `INTEREST_HINTS` dict and
`FALLBACK_HINT` string. Constraints to respect:

1. **Length**: each prompt is wrapped at 15 chars per line on the LCD
   (240 px / 16 px cell), max 6 lines. Practical upper bound ≈ 80
   chars total before truncation.
2. **ASCII only**: no curly quotes, no emoji, no accented characters.
   The LCD's Unifont glyph table only covers basic Latin + a handful
   of symbols. An em-dash (`—`) is in scope; smart quotes (`'` `"`)
   are not.
3. **Keep the question mark at the end**: it visually signals to the
   reader that the prompt is conversational, not a statement.
