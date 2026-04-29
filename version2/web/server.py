#!/usr/bin/env python3
"""
Stage 2 backend — serves the stage 1 form, accepts profile submissions,
serves the collective wall, and pushes new submissions over SSE.

One file, ~150 lines, no DB. Profiles live in users.json next to this
script. Designed for a single laptop hosting a classroom / pop-up demo
where 6–30 phones scan a QR and submit once each.

Run:
    pip install -r requirements.txt
    python3 server.py

The script prints a LAN URL + an ASCII QR code so phones on the same
WiFi can join in two seconds.
"""

import json
import os
import socket
import threading
import time
import uuid
from datetime import datetime, timezone
from pathlib import Path
from queue import Queue, Empty

from flask import Flask, request, jsonify, send_from_directory, Response

ROOT       = Path(__file__).resolve().parent
USERS_FILE = ROOT / "users.json"
PORT       = 8000

app = Flask(__name__, static_folder=str(ROOT), static_url_path="")

# ---------------------------------------------------------------------------
# users.json — single mutable file. A threading.Lock keeps concurrent
# /submit calls from clobbering each other; with 6 phones this is overkill
# but free correctness.
# ---------------------------------------------------------------------------
_lock = threading.Lock()

def load_users() -> list:
    if not USERS_FILE.exists():
        return []
    try:
        with open(USERS_FILE, encoding="utf-8") as f:
            data = json.load(f)
        return data if isinstance(data, list) else []
    except (json.JSONDecodeError, OSError):
        return []

def save_users(users: list) -> None:
    tmp = USERS_FILE.with_suffix(".json.tmp")
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(users, f, ensure_ascii=False, indent=2)
    os.replace(tmp, USERS_FILE)   # atomic on POSIX → no half-written file


# ---------------------------------------------------------------------------
# SSE — one in-process pub/sub. Each connected /events client gets its own
# queue; /submit drops the new entry into every queue.
# ---------------------------------------------------------------------------
_subscribers: list[Queue] = []
_subs_lock = threading.Lock()

def broadcast(event: str, payload: dict) -> None:
    msg = f"event: {event}\ndata: {json.dumps(payload, ensure_ascii=False)}\n\n"
    with _subs_lock:
        for q in list(_subscribers):
            try: q.put_nowait(msg)
            except Exception: pass


# ---------------------------------------------------------------------------
# Stage 3 — NFC tap → match flow.
#
# Architecture (intentionally mirrors the existing SSE pattern, no WebSocket):
#   - 5 demo users are hardcoded by NFC sticker UID (STAGE3_USERS).
#   - Browser representing "me" picks a my_user_id (later: read from device).
#   - When NFC tap fires, browser POSTs /api/tap with peer_uid.
#   - Server looks up both users, computes a fluffy match score + picks a
#     hint based on common interests, broadcasts a "match" event to all
#     /events_stage3 SSE subscribers (later: only push to the matched pair).
#
# Score is intentionally NOT serious — it's animation candy. Common-count /3
# gives 4 buckets (0/33/66/99%) which are visually distinct and easy to
# explain. We don't use Jaccard because (a) the user said "don't be too
# serious", (b) discrete buckets are cleaner for a number-rolling animation.
# ---------------------------------------------------------------------------
STAGE3_USERS = {
    # UIDs match physical sticker labels 1-5 from the bring-up session.
    # interests chosen to span the score buckets w.r.t. user_1:
    #   user_1 ↔ user_2: 66% (music+film common)
    #   user_1 ↔ user_3: 33% (film common)
    #   user_1 ↔ user_4:  0% (no overlap)
    #   user_1 ↔ user_5: 66% (music+art common)
    "75:91:49:A7": {
        "user_id":   "u1",
        "label":     "sticker 1",
        "avatar":    {"sex": "female", "color": 2, "num": 1, "smile": True,
                      "mood": 1, "bg_level": 4},
        "interests": ["music", "film", "art"],
        "interest_details": {"music": ["indie"], "film": ["sci-fi"], "art": ["painting"]},
        "quote":     "open to chat",
    },
    "65:BC:56:A7": {
        "user_id":   "u2",
        "label":     "sticker 2",
        "avatar":    {"sex": "male", "color": 1, "num": 0, "smile": False,
                      "mood": 1, "bg_level": 3},
        "interests": ["music", "film", "tech"],
        "interest_details": {"music": ["indie"], "film": ["sci-fi"], "tech": ["gaming-tech"]},
        "quote":     "say hi",
    },
    "65:D5:7F:A7": {
        "user_id":   "u3",
        "label":     "sticker 3",
        "avatar":    {"sex": "female", "color": 0, "num": 2, "smile": False,
                      "mood": 0, "bg_level": 2},
        "interests": ["film", "books", "photo"],
        "interest_details": {"film": ["docu"], "books": ["fiction"], "photo": ["portrait"]},
        "quote":     "daydreaming",
    },
    "65:E9:22:A7": {
        "user_id":   "u4",
        "label":     "sticker 4",
        "avatar":    {"sex": "male", "color": 3, "num": 1, "smile": True,
                      "mood": 2, "bg_level": 5},
        "interests": ["sport", "food", "gaming"],
        "interest_details": {"sport": ["cycling"], "food": ["coffee"], "gaming": ["console"]},
        "quote":     "need coffee",
    },
    "75:2A:E7:A7": {
        "user_id":   "u5",
        "label":     "sticker 5",
        "avatar":    {"sex": "female", "color": 4, "num": 0, "smile": True,
                      "mood": 4, "bg_level": 5},
        "interests": ["music", "art", "dance"],
        "interest_details": {"music": ["pop"], "art": ["painting"], "dance": ["contemporary"]},
        "quote":     "same vibe?",
    },
}

# One open-ended question per primary interest. Picked when two users share
# that interest. For 0% match (no common), we fall back to FALLBACK_HINT.
INTEREST_HINTS = {
    "music":  "what's been on repeat for you this week?",
    "film":   "a film you'd hand to a stranger right now?",
    "sport":  "what move did your body need today?",
    "art":    "last thing you saw that stopped you mid-step?",
    "tech":   "the side project you keep almost starting?",
    "food":   "something you cooked or ate that surprised you?",
    "gaming": "a game world you wish you lived in for a week?",
    "travel": "a place you'd go back to without hesitating?",
    "books":  "a sentence from a book that's still with you?",
    "photo":  "the shot you almost took but didn't?",
    "dance":  "a song that makes you move without thinking?",
    "pets":   "an animal that recently made your day?",
}
FALLBACK_HINT = "you two picked totally different things — what's the most surprising thing you've done lately?"

# Stickers are dealt to classmates by physical label (1-5). The web form
# collects "I am sticker N", and the server maps that to the underlying UID
# so submitted DIY data overrides the hardcoded fixture above.
STICKER_NUM_TO_UID = {
    1: "75:91:49:A7",
    2: "65:BC:56:A7",
    3: "65:D5:7F:A7",
    4: "65:E9:22:A7",
    5: "75:2A:E7:A7",
}

# Stage 3 has its own SSE subscriber list, separate from collective wall.
_stage3_subscribers: list[Queue] = []
_stage3_subs_lock   = threading.Lock()

def stage3_broadcast(event: str, payload: dict) -> None:
    msg = f"event: {event}\ndata: {json.dumps(payload, ensure_ascii=False)}\n\n"
    with _stage3_subs_lock:
        for q in list(_stage3_subscribers):
            try: q.put_nowait(msg)
            except Exception: pass

def compute_match(me_uid: str, peer_uid: str) -> dict | None:
    """Return a match payload for the (me, peer) UID pair, or None if either
    UID is unknown. Score is common_count / 3 → 0/33/66/99 — score is
    deliberately fluffy, just feeds the number-rolling animation."""
    me   = STAGE3_USERS.get(me_uid)
    peer = STAGE3_USERS.get(peer_uid)
    if not me or not peer or me_uid == peer_uid:
        return None
    common = sorted(set(me["interests"]) & set(peer["interests"]))
    score  = round(len(common) * 33) if common else 0
    if score == 99: score = 100      # cosmetic: 3/3 reads as 100%, not 99%
    hint = INTEREST_HINTS[common[0]] if common else FALLBACK_HINT

    # Flatten common interests + common sub-tags into a single comma-list
    # for the device's "you both like __" screen. For each shared primary,
    # if both sides picked at least one matching sub-tag, list those sub-tags
    # (skipping the primary name); otherwise list the bare primary. This
    # gives e.g. ["indie", "sci-fi"] when two users overlap precisely, vs
    # ["film"] when they share the category but not the flavor.
    me_details   = me.get("interest_details",   {})
    peer_details = peer.get("interest_details", {})
    common_tags: list[str] = []
    for primary in common:
        sub_overlap = sorted(
            set(me_details.get(primary, [])) &
            set(peer_details.get(primary, []))
        )
        if sub_overlap:
            common_tags.extend(sub_overlap)
        else:
            common_tags.append(primary)

    return {
        "ts":           datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "me_uid":       me_uid,
        "peer_uid":     peer_uid,
        "me_user_id":   me["user_id"],
        "peer_user_id": peer["user_id"],
        "score":        score,
        "common":       common,
        "common_tags":  common_tags,
        "hint":         hint,
        "peer_avatar":  peer["avatar"],
        "peer_quote":   peer["quote"],
        "peer_label":   peer["label"],
    }


# ---------------------------------------------------------------------------
# Routes
# ---------------------------------------------------------------------------
@app.route("/")
def index():
    return send_from_directory(str(ROOT), "index.html")

@app.route("/collective")
def collective():
    return send_from_directory(str(ROOT), "collective.html")

@app.route("/stage3")
def stage3():
    return send_from_directory(str(ROOT), "stage3.html")

@app.route("/api/users")
def api_users():
    return jsonify(load_users())

@app.route("/api/qr")
def api_qr():
    """PNG QR code pointing at the LAN URL of stage 1 — embedded by the
    collective wall so the people standing in front of the screen can
    scan and join without anyone needing to read the terminal."""
    import io, qrcode
    img = qrcode.make(f"http://{lan_ip()}:{PORT}/", border=2, box_size=10)
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    return Response(buf.getvalue(), mimetype="image/png", headers={
        "Cache-Control": "no-cache",
    })

@app.route("/submit", methods=["POST"])
def submit():
    profile = request.get_json(silent=True)
    if not isinstance(profile, dict):
        return jsonify({"error": "invalid payload"}), 400

    # Optional: classmate self-reports their sticker number (1-5). When
    # present, also overlay this submission onto STAGE3_USERS so my ESP32
    # tap reads their DIY data instead of the hardcoded fixture. The sticker
    # field is stripped before saving to users.json — it's a routing hint,
    # not part of the avatar profile.
    sticker_num = profile.pop("sticker", None)
    if sticker_num is not None:
        try:
            uid = STICKER_NUM_TO_UID[int(sticker_num)]
        except (KeyError, TypeError, ValueError):
            return jsonify({"error": f"invalid sticker number: {sticker_num}"}), 400
        STAGE3_USERS[uid] = {
            "user_id":   f"u{int(sticker_num)}",
            "label":     f"sticker {int(sticker_num)}",
            "avatar": {
                "sex":       profile.get("sex", "male"),
                "color":     profile.get("color", 0),
                "num":       profile.get("num", 0),
                "smile":     profile.get("smile", False),
                "mood":      profile.get("mood", 0),
                "bg_level":  profile.get("bg_level", 2),
            },
            "interests":        profile.get("interests", []),
            "interest_details": profile.get("interest_details", {}),
            "quote":            profile.get("quote", ""),
        }

    entry = {
        "id":      f"u_{uuid.uuid4().hex[:8]}",
        "ts":      datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "profile": profile,
    }
    # If this submission claims a sticker, treat sticker number as a stable
    # identity key: any prior users.json entries that selected the same
    # sticker get removed (and their tiles dropped from the wall via the
    # `remove` SSE event). Submissions without a sticker keep the original
    # append-only anonymous-wall behavior.
    removed_ids: list[str] = []
    with _lock:
        users = load_users()
        if sticker_num is not None:
            keep = []
            for u in users:
                if u.get("profile", {}).get("sticker") == int(sticker_num):
                    removed_ids.append(u["id"])
                else:
                    keep.append(u)
            users = keep
        # Persist the sticker number on the entry so we can match against
        # it on the next submission. (We popped it from `profile` earlier
        # for STAGE3_USERS overlay; put it back on the saved profile so
        # users.json carries the routing hint forward across restarts.)
        if sticker_num is not None:
            entry["profile"]["sticker"] = int(sticker_num)
        users.append(entry)
        save_users(users)
    for rid in removed_ids:
        broadcast("remove", {"id": rid})
    broadcast("user", entry)
    return jsonify({"ok": True, "id": entry["id"], "removed": removed_ids})

@app.route("/events")
def events():
    """SSE stream — emits 'user' events as they're submitted."""
    q: Queue = Queue(maxsize=64)
    with _subs_lock:
        _subscribers.append(q)

    def gen():
        try:
            yield ": connected\n\n"   # initial comment to flush headers
            while True:
                try:
                    yield q.get(timeout=15)
                except Empty:
                    yield ": ping\n\n"   # keepalive every 15 s
        finally:
            with _subs_lock:
                if q in _subscribers: _subscribers.remove(q)

    return Response(gen(), mimetype="text/event-stream", headers={
        "Cache-Control": "no-cache",
        "X-Accel-Buffering": "no",
    })

@app.route("/api/stage3/users")
def api_stage3_users():
    """List the hardcoded fixture so stage3.html can render the picker."""
    return jsonify([
        {"uid": uid, **{k: v for k, v in u.items() if k != "avatar"},
         "avatar": u["avatar"]}
        for uid, u in STAGE3_USERS.items()
    ])

@app.route("/api/tap", methods=["POST"])
def api_tap():
    """Browser → server: 'I (me_uid) just tapped peer_uid.' Server computes
    match and broadcasts to all stage3 SSE listeners. In MVP the broadcast
    is fan-out (all listeners see all matches); later we'll filter by user."""
    body = request.get_json(silent=True) or {}
    me_uid   = body.get("me_uid")
    peer_uid = body.get("peer_uid")
    if not me_uid or not peer_uid:
        return jsonify({"error": "me_uid and peer_uid required"}), 400
    match = compute_match(me_uid, peer_uid)
    if not match:
        return jsonify({"error": "unknown uid or self-tap",
                        "me_uid": me_uid, "peer_uid": peer_uid}), 404
    stage3_broadcast("match", match)
    return jsonify({"ok": True, "match": match})

@app.route("/events_stage3")
def events_stage3():
    """SSE stream for Stage 3 match events."""
    q: Queue = Queue(maxsize=64)
    with _stage3_subs_lock:
        _stage3_subscribers.append(q)

    def gen():
        try:
            yield ": connected\n\n"
            while True:
                try:
                    yield q.get(timeout=15)
                except Empty:
                    yield ": ping\n\n"
        finally:
            with _stage3_subs_lock:
                if q in _stage3_subscribers: _stage3_subscribers.remove(q)

    return Response(gen(), mimetype="text/event-stream", headers={
        "Cache-Control": "no-cache",
        "X-Accel-Buffering": "no",
    })


@app.route("/admin/clear", methods=["POST"])
def admin_clear():
    """JSON wipe endpoint for the wall page's clear button. localhost
    only — same guard as /admin — so phones on the LAN can't reset
    while the demo is mid-session."""
    if request.remote_addr not in ("127.0.0.1", "::1"):
        return jsonify({"error": "forbidden"}), 403
    with _lock:
        save_users([])
    broadcast("clear", {})
    return jsonify({"ok": True})


@app.route("/admin", methods=["GET", "POST"])
def admin():
    """Tiny self-contained admin page — list count + clear button.
    Localhost-only by IP check; nobody on the LAN can wipe the dataset."""
    if request.remote_addr not in ("127.0.0.1", "::1"):
        return "admin: localhost only", 403
    if request.method == "POST" and request.form.get("action") == "clear":
        with _lock:
            save_users([])
        broadcast("clear", {})
        return ('<meta http-equiv="refresh" content="0; url=/admin">', 200)
    n = len(load_users())
    return f"""
        <!doctype html><meta charset="utf-8">
        <title>admin</title>
        <style>body{{font-family:system-ui;padding:24px;background:#1a1a1e;color:#e8e8ec}}
        button{{padding:10px 16px;background:#e87b7b;color:#fff;border:0;border-radius:6px;cursor:pointer}}</style>
        <h2>collective admin</h2>
        <p>{n} profile(s) in users.json</p>
        <form method="post"><input type="hidden" name="action" value="clear">
        <button type="submit" onclick="return confirm('clear all {n} profiles?')">clear all</button>
        </form>"""


# ---------------------------------------------------------------------------
# Boot — print LAN URL + QR for phones to scan
# ---------------------------------------------------------------------------
def lan_ip() -> str:
    """Best-effort LAN IP. Connects a UDP socket to a public-ish address
    (no packet sent) so the OS picks the outbound interface IP."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()

def print_banner():
    ip = lan_ip()
    url = f"http://{ip}:{PORT}/"
    print()
    print(f"[server]  stage 1 (phone): {url}")
    print(f"[server]  collective wall: http://localhost:{PORT}/collective")
    print(f"[server]  stage 3 (tap)  : http://localhost:{PORT}/stage3")
    print(f"[server]  admin (clear)  : http://localhost:{PORT}/admin")
    print()
    try:
        import qrcode
        qr = qrcode.QRCode(border=1)
        qr.add_data(url)
        qr.make(fit=True)
        qr.print_ascii(invert=True)
    except ImportError:
        print("[server]  (install qrcode[pil] to print scan code)")
    print()


if __name__ == "__main__":
    print_banner()
    # threaded=True lets SSE stream in parallel with regular requests
    app.run(host="0.0.0.0", port=PORT, debug=False, threaded=True)
