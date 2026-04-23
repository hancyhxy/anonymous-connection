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
# Routes
# ---------------------------------------------------------------------------
@app.route("/")
def index():
    return send_from_directory(str(ROOT), "index.html")

@app.route("/collective")
def collective():
    return send_from_directory(str(ROOT), "collective.html")

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
    entry = {
        "id":      f"u_{uuid.uuid4().hex[:8]}",
        "ts":      datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "profile": profile,
    }
    with _lock:
        users = load_users()
        users.append(entry)
        save_users(users)
    broadcast("user", entry)
    return jsonify({"ok": True, "id": entry["id"]})

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
