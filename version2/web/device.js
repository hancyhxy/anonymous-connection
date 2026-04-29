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
// Gate writes until firmware finishes setup() — its gfx->begin + renderWaiting
// can block ~hundreds of ms, during which the USB-CDC RX FIFO (~256 B) drops
// the head of any packet larger than that. We wait for the firmware's
// "ready — N sprites loaded" line before letting sendState push to the wire.
let deviceReady = false;
let readyTimeoutId = null;

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

  const body = JSON.stringify(payload) + "\n";
  const packet = new TextEncoder().encode(body);
  console.log("[device] sendState called. ready=" + deviceReady + " packet=" + packet.length + "B preview=" + body.slice(0, 60));

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

    // Flush any residual half-frame from a prior session. Re-opening the
    // serial port does NOT reset the ESP32-C6 USB-CDC stack here, so the
    // firmware's `lineBuf` may still hold leftover bytes from before we
    // disconnected. Sending a lone '\n' terminates whatever's there — the
    // firmware will handleLine the garbage (likely a JSON parse err that
    // we ignore) and reset lineBuf to empty before our real first frame.
    // Without this, the very first tap after every page refresh tends to
    // fail with InvalidInput because host bytes get appended to a polluted
    // prefix from the previous session.
    try { await writer.write(new TextEncoder().encode("\n")); } catch (_) {}

    // Reset the ready flag — opening the port resets the ESP32 via DTR/RTS,
    // so the device is booting again from scratch.
    deviceReady = false;

    // Fire-and-forget read loop — echoes device logs to the JS console
    // and watches for the firmware's "ready" line to flip deviceReady.
    readerLoopAbort = new AbortController();
    startReadLoop(readerLoopAbort.signal);

    $btn.textContent = "Disconnect";
    setStatus("waiting for device...", "");

    // Fallback: if the firmware never emits "ready" (already-running device,
    // or USB-CDC port-open didn't reset the board), release the gate after 1 s.
    // Firmware setup() measures ~300–500 ms, so 1 s covers a real cold boot
    // while keeping the connect-to-usable latency tight when the device was
    // already in loop().
    readyTimeoutId = setTimeout(() => {
      if (!deviceReady) {
        console.warn("[device] no 'ready' line within 1s — releasing gate anyway");
        markDeviceReady();
      }
    }, 1000);
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

// Flip the gate, fire the connected event, clear the timeout.
// Called either by the read loop on seeing "ready —" or by the timeout fallback.
function markDeviceReady() {
  if (deviceReady) return;
  deviceReady = true;
  if (readyTimeoutId) {
    clearTimeout(readyTimeoutId);
    readyTimeoutId = null;
  }
  setStatus("connected", "ok");
  window.dispatchEvent(new CustomEvent("device:connected"));
  // Drain anything that piled up while we were waiting.
  flushPending().catch(() => {});
}

async function cleanup() {
  pendingPacket = null;
  sendBusy = false;
  deviceReady = false;
  if (readyTimeoutId) {
    clearTimeout(readyTimeoutId);
    readyTimeoutId = null;
  }
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
        // Firmware finished setup() and entered loop() — safe to write now.
        // Match on the prefix so the sprite-count suffix doesn't matter.
        if (!deviceReady && line.includes("[serial_avatar] ready")) {
          markDeviceReady();
        }
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
    // The read loop is fire-and-forget — if it throws (NetworkError when
    // the device is physically unplugged, pipe close, etc.) nothing else
    // would otherwise free the port. Without cleanup the next Connect
    // click hits "port is already open" because the stale port object is
    // still alive in the browser's view.
    console.warn("[device] read loop error:", err);
    setStatus("device disconnected — please reconnect", "err");
    cleanup().catch(() => {});
  } finally {
    try { reader.releaseLock(); } catch (_) {}
    await readableClosed;
  }
}

// Minimum gap between consecutive serial writes. The firmware's renderFull()
// blocks loop() for ~tens-to-hundreds of ms; during that window the ESP32-C6
// USB-CDC RX FIFO (~256 B) can fill up and silently drop bytes — including the
// '\n' separator, which makes the next two frames glue together (observed:
// host sent 354 B + 398 B, firmware received one 941 B "frame" → JSON parse
// err). 200 ms gives the device time to drain + render before we push again.
// Combined with newer-wins (intermediate pendingPacket overwrites), users can
// still tap as fast as they want — only the latest state ends up on the wire.
const MIN_TX_GAP_MS = 200;
let lastTxAt = 0;

async function flushPending() {
  // Hold writes until firmware finished setup. The packet stays in
  // pendingPacket; markDeviceReady() will call us again to drain it.
  if (!deviceReady) return;
  if (sendBusy || !writer || !pendingPacket) return;
  sendBusy = true;

  try {
    while (writer && pendingPacket) {
      const wait = MIN_TX_GAP_MS - (performance.now() - lastTxAt);
      if (wait > 0) await new Promise((r) => setTimeout(r, wait));
      const packet = pendingPacket;
      pendingPacket = null;
      console.log("[device] writer.write start, " + packet.length + "B");
      await writer.write(packet);
      console.log("[device] writer.write resolved");
      lastTxAt = performance.now();
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
