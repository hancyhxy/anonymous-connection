// device.js — Web Serial bridge to the ESP32-C6 board.
//
// Chrome/Edge only (Web Serial API). Safari/Firefox → button is a no-op,
// status text tells the user why.
//
// Protocol:
//   - One JSON line per packet, '\n' terminated:
//       {"mood":2,"energy":4,"sex":"male","color":0,"num":1,"smile":false,"quote":"..."}
//   - Device renders the sprite itself using its on-board font + sprites.h
//   - sendPreviewBitmap() is kept as a dead code path in case we want to
//     revisit the bitmap-mirror approach; currently unused.

const BAUD = 115200;

let port = null;
let writer = null;
let readerLoopAbort = null;
let sendBusy = false;
let pendingPacket = null;

// UI hooks, set in connectUI().
let $btn  = null;
let $text = null;


// ------------------------------------------------------------------
// Public API
// ------------------------------------------------------------------

/**
 * Wire up the Connect button and status text. Call once on page load.
 */
export function connectUI() {
  $btn  = document.getElementById("btn-connect");
  $text = document.getElementById("status-text");

  if (!("serial" in navigator)) {
    setStatus("Web Serial unsupported (use Chrome/Edge)", "err");
    $btn.disabled = true;
    return;
  }

  $btn.addEventListener("click", () => {
    if (port) {
      disconnect();
    } else {
      connect();
    }
  });
}

/**
 * Push the current avatar state to the device as a single-line JSON packet.
 * Newer state always wins: if the user changes options repeatedly while a
 * packet is still being transmitted, intermediate packets are dropped and
 * only the newest one is flushed next.
 */
export function sendState(payload) {
  if (!writer || !payload) return;

  const packet = new TextEncoder().encode(JSON.stringify(payload) + "\n");

  pendingPacket = packet;
  flushPending().catch((err) => {
    console.warn("[device] state write failed:", err);
    setStatus(err.message || "device write failed", "err");
  });
}

// Kept for reference — was used by the bitmap-mirror experiment. No caller
// in the current flow; device-side firmware no longer parses !BMPHEX.
export function sendPreviewBitmap(frame) {
  if (!writer || !frame) return;

  const hexBody = Array.from(frame.bitmap, (b) => b.toString(16).padStart(2, "0")).join("");
  const packet = new TextEncoder().encode(
    `!BMPHEX ${frame.fg565} ${frame.bg565} ${frame.bitmap.length}\n${hexBody}\n`
  );

  pendingPacket = packet;
  flushPending().catch((err) => {
    console.warn("[device] bitmap write failed:", err);
    setStatus(err.message || "device write failed", "err");
  });
}


// ------------------------------------------------------------------
// Connection lifecycle
// ------------------------------------------------------------------

async function connect() {
  try {
    setStatus("requesting port...", "");
    port = await navigator.serial.requestPort();
    await port.open({ baudRate: BAUD });

    writer = port.writable.getWriter();

    // Fire-and-forget read loop — echoes device logs to the JS console
    // for debugging. Not used for any control flow.
    readerLoopAbort = new AbortController();
    startReadLoop(readerLoopAbort.signal);

    $btn.textContent = "Disconnect";
    setStatus("connected", "ok");

    // Tell the app to flush current state on fresh connection.
    window.dispatchEvent(new CustomEvent("device:connected"));
  } catch (err) {
    // User cancelled the port picker, or open() failed.
    console.warn("[device] connect failed:", err);
    setStatus(err.message || "connect failed", "err");
    await cleanup();
  }
}

async function disconnect() {
  setStatus("disconnecting...", "");
  await cleanup();
  $btn.textContent = "Connect Device";
  setStatus("device not connected", "");
}

async function cleanup() {
  pendingPacket = null;
  sendBusy = false;
  if (readerLoopAbort) {
    readerLoopAbort.abort();
    readerLoopAbort = null;
  }
  if (writer) {
    // releaseLock first, then close — releaseLock is synchronous and
    // always works even if the underlying stream is in a bad state.
    // close() can hang if the device was physically disconnected.
    try { writer.releaseLock(); } catch (_) {}
    try { await writer.close(); } catch (_) {}
    writer = null;
  }
  if (port) {
    try { await port.close(); } catch (_) {}
    port = null;
  }
}


// ------------------------------------------------------------------
// Read loop — logs every line, and forwards JSON-shaped lines as
// `device:message` CustomEvents so app.js can react to telemetry
// (heart rate, etc.) without polling.
// ------------------------------------------------------------------
async function startReadLoop(signal) {
  const decoder = new TextDecoderStream();
  const readableClosed = port.readable.pipeTo(decoder.writable).catch(() => {});
  const reader = decoder.readable.getReader();

  let buf = "";
  try {
    while (!signal.aborted) {
      const { value, done } = await reader.read();
      if (done) break;
      buf += value;
      // Flush on newline
      let idx;
      while ((idx = buf.indexOf("\n")) >= 0) {
        const line = buf.slice(0, idx).replace(/\r$/, "");
        buf = buf.slice(idx + 1);
        if (line.length === 0) continue;
        console.log("[device]", line);
        // Forward JSON lines (start with `{`) as events. Tolerant: a
        // malformed JSON just gets logged, no exception.
        if (line[0] === "{") {
          try {
            const msg = JSON.parse(line);
            window.dispatchEvent(new CustomEvent("device:message", { detail: msg }));
          } catch (_) { /* not JSON, ignore */ }
        }
      }
    }
  } catch (err) {
    console.warn("[device] read loop error:", err);
  } finally {
    try { reader.releaseLock(); } catch (_) {}
    await readableClosed;
  }
}

async function flushPending() {
  if (sendBusy || !writer || !pendingPacket) return;
  sendBusy = true;

  try {
    while (writer && pendingPacket) {
      const packet = pendingPacket;
      pendingPacket = null;
      await writer.write(packet);
    }
  } finally {
    sendBusy = false;
  }
}


// ------------------------------------------------------------------
// Status helpers
// ------------------------------------------------------------------
function setStatus(msg, cls) {
  if (!$text) return;
  $text.textContent = msg;
  $text.classList.remove("ok", "err");
  if (cls) $text.classList.add(cls);
}
