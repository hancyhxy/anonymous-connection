// =============================================================
// pip v2 — app.js (v6)
//
// User-facing dimensions:
//   1. sex       — 2 (male/female)
//   2. avatar    — 15 per sex (Q_AVATAR gallery, v6)
//                  Internally encoded as (color, num) = (0..4, 0..2),
//                  but the user sees ONE 15-tile visual picker, not a
//                  5 × 3 composition. The 15 sprites are independent
//                  hand-drawn illustrations; there is no "same character
//                  in a different pose" — that was a v5 UI fiction.
//   3. smile     — 2 (neutral/smile — independent dim since v5)
//   4. mood      — 5 (vibe tag; no longer affects animation)
//   5. bg_level  — 6 (energy; SINGLE color source)
//   6. quote     — free text, 14 ASCII chars (single-line on device)
//
// Internal sprite matrix stays 2 × 5 × 3 × 2 = 60 keys
// (sex_color_num[smile]), unchanged — only the UI collapses.
// Missing smile variants fall back to neutral inside resolveSprite().
//
// Hidden parameters (code-level constants, NOT exposed in UI):
//   - ANIM_INTERVAL_MS: tempo of the subtle in-sprite micro-animation.
//     Matches hello-stranger's original 1000/6 ≈ 166ms cadence. Changing
//     it alters the breathing/blinking rhythm but never the identity.
//
// Animation model (ported back from hello-stranger):
//   - OUTER key (sex/color/num/smile) = WHICH sprite — set by user, stable.
//   - INNER pointer (animFrame)       = WHICH FRAME of that sprite — ticks
//     on a setInterval, cycling through frames[animFrame % frames.length].
//     This produces subtle detail changes (blink, mouth twitch, chest
//     "breathing" symbol) without ever swapping the whole figure.
// =============================================================

import { renderProfile, applyToDOM, buildSpriteKey } from "./renderer.js";
import { connectUI, sendState } from "./device.js";

// ---- Hidden animation parameter (not in UI) -----------------
// 1000/6 ≈ 166ms was hello-stranger's frameRate. Tune here if the
// breathing feels too fast/slow. Do NOT expose to end users —
// a stable tempo is part of the aesthetic.
const ANIM_INTERVAL_MS = 166;

const state = {
  sex: "male",
  color: 0,         // 0..4 — sprite row in the 5×3 Q_AVATAR gallery
  num: 0,           // 0..2 — sprite column. USER-LOCKED; animation never touches this.
                    // color + num together are set atomically by one tile click (v6).
  smile: false,     // v5: user-picked independent dim (was mood-derived in v4)
  mood: 0,          // vibe tag; no longer drives anything in v5+
  bg_level: 2,
  quote: "",
  interests: [],    // v7: Q_INTERESTS — up to 3 primary tag values.
                    // Sent to device (icon+label) for hardware tag row.
  interest_details: {},  // v8: Q_INTERESTS_DETAIL — { primary: [subtag, ...] }.
                         // Collected for the collective-view similarity layout
                         // ONLY. Never rendered on phone preview, hardware
                         // LCD, or wall. Excluded from syncDevice() payload.
};
const MAX_INTERESTS = 3;

// Internal animation pointer — NOT part of user state. Ticks on setInterval.
// Used by renderer.js to pick frames[animFrame % frames.length].
let animFrame = 0;

const ENERGY_LABELS = ["empty", "low", "medium", "steady", "high", "electric"];

// Heart-rate → energy mapping (firmware reports BPM via {"hr":N} on serial).
// Tuned for "person sitting calmly" rather than gym/cardio: the medium band
// (70-85 BPM) covers a typical resting adult, giving the avatar a stable
// "default" color when no excitement happens.
//
// 0 = no signal (finger lifted) — leaves bg_level at last user-set value.
function bpmToEnergy(bpm) {
  if (bpm <= 0)   return null;     // no signal → don't override
  if (bpm < 60)   return 0;        // empty    — deep relaxation
  if (bpm < 70)   return 1;        // low      — quiet
  if (bpm < 85)   return 2;        // medium   — typical resting
  if (bpm < 100)  return 3;        // steady   — mildly active
  if (bpm < 115)  return 4;        // high     — excited
  return 5;                        // electric — strong / running
}

// Most recent BPM reported by device. Null = no reading yet (or finger
// lifted). UI uses this to show the "♥ 78 bpm" readout.
let lastBpm = null;

let sprites = null;
let colors = null;
let questions = null;
let animationTimer = null;
let lastRendered = null;


// -------------------------------------------------------------
// Data loading
// -------------------------------------------------------------
async function loadJSON(path) {
  const res = await fetch(path);
  if (!res.ok) throw new Error(`${path} HTTP ${res.status}`);
  return res.json();
}

async function loadData() {
  [sprites, colors, questions] = await Promise.all([
    loadJSON("sprites.json"),
    loadJSON("colors.json"),
    loadJSON("questions.json"),
  ]);
}


// -------------------------------------------------------------
// DOM population (from questions.json)
// -------------------------------------------------------------
function renderRadioGroup(containerId, radioName, options, currentValue) {
  const container = document.getElementById(containerId);
  container.innerHTML = options.map(opt => `
    <label class="choice">
      <input type="radio" name="${radioName}" value="${opt.value}"
             ${String(opt.value) === String(currentValue) ? "checked" : ""}>
      <span>${opt.label}</span>
    </label>
  `).join("");
}

// Cheap HTML-escape for sprite frames. Current sprites.json uses only
// Unicode geometric / box-drawing chars, but guard against future edits
// that might include <, >, or &.
function escapeHtml(s) {
  return s.replace(/[&<>]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;" }[c]));
}

// Q_AVATAR (v6): 15-tile visual gallery per sex. Tiles are derived from
// sprite keys at runtime — questions.json Q_AVATAR has no options array.
// A tile carries data-color + data-num; click sets both state fields.
function populateAvatarGallery() {
  const container = document.getElementById("opts-avatar");
  const tiles = [];
  for (let color = 0; color < 5; color++) {
    for (let num = 0; num < 3; num++) {
      const key = buildSpriteKey({ sex: state.sex, color, num, smile: false });
      const frames = sprites && sprites[key];
      const firstFrame = frames && frames.length ? frames[0] : "(missing)";
      const isSelected = color === state.color && num === state.num;
      tiles.push(
        `<button type="button" class="avatar-tile${isSelected ? " selected" : ""}"`
        + ` role="radio" aria-checked="${isSelected}"`
        + ` data-color="${color}" data-num="${num}"`
        + ` aria-label="avatar ${color + 1} pose ${num + 1}">`
        + `<pre class="tile-ascii">${escapeHtml(firstFrame)}</pre>`
        + `</button>`
      );
    }
  }
  container.innerHTML = tiles.join("");
}

function populateSmile() {
  renderRadioGroup("opts-smile", "smile", questions.Q_SMILE.options, state.smile);
}

function populateMood() {
  renderRadioGroup("opts-mood", "mood", questions.Q_MOOD.options, state.mood);
}

function populateQuoteSuggestions() {
  const container = document.getElementById("quote-suggestions");
  container.innerHTML = questions.Q_QUOTE.suggestions.map(s => `
    <button type="button" class="suggestion-chip" data-suggest="${s}">${s}</button>
  `).join("");
}

// Q_INTERESTS (v7): multi-select tag picker, max 3.
// Each tag is a toggle button; we track selection in state.interests.
function populateInterests() {
  const container = document.getElementById("opts-interests");
  const opts = questions.Q_INTERESTS.options;
  container.innerHTML = opts.map(opt => {
    const isSelected = state.interests.includes(opt.value);
    return `<button type="button" class="interest-tag${isSelected ? " selected" : ""}"
              role="checkbox" aria-checked="${isSelected}"
              data-value="${opt.value}">
              <span class="interest-icon">${opt.icon}</span><span class="interest-label">${opt.label}</span>
            </button>`;
  }).join("");
}


// -------------------------------------------------------------
// Subtle in-sprite animation loop
// -------------------------------------------------------------
// Ticks animFrame on a fixed interval. state.num (pose) is never touched.
// renderer.js reads animFrame and picks frames[animFrame % frames.length],
// so each tick flips to the next micro-variant (blink / mouth / breathing)
// within the user-locked sprite.
function startAnimation() {
  if (animationTimer !== null) return;
  animationTimer = setInterval(() => {
    animFrame = (animFrame + 1) >>> 0;  // unsigned overflow-safe increment
    rerender();
  }, ANIM_INTERVAL_MS);
}


// -------------------------------------------------------------
// Render (single source of UI truth)
// -------------------------------------------------------------
function rerender() {
  try {
    lastRendered = renderProfile(state, sprites, colors, animFrame);
    applyToDOM(lastRendered);
  } catch (err) {
    console.error("render error:", err);
    document.getElementById("avatar-pre").textContent =
      `ERR: ${err.message}`;
    lastRendered = null;
  }
}

// Push current state to the device. Call after user-driven state changes
// only — NOT from the animation loop. The 166ms breathing is a browser-
// only visual detail; the device cycles its own animFrame locally.
//
// Wire-format field name is `energy`, not `bg_level` — a holdover from an
// earlier prototype. Translate here rather than renaming the UI state.
function syncDevice() {
  // Persist to sessionStorage on every user-driven state change, so an
  // accidental refresh restores the in-progress profile. Submit clears it.
  saveSessionState();
  sendState({
    sex:    state.sex,
    color:  state.color,
    num:    state.num,
    smile:  state.smile,
    mood:   state.mood,
    energy: state.bg_level,
    quote:  state.quote,
    interests: buildInterestsPayload(),
  });
}

// Resolve state.interests (array of value strings) into the {icon,label}
// objects the device needs. Sending the icon char directly keeps the
// firmware free of any value→icon mapping table — questions.json stays
// the single source of truth.
function buildInterestsPayload() {
  if (!state.interests.length || !questions) return [];
  const byValue = new Map(questions.Q_INTERESTS.options.map(o => [o.value, o]));
  return state.interests
    .map(v => byValue.get(v))
    .filter(Boolean)
    .map(o => ({ icon: o.icon, label: o.label }));
}


// -------------------------------------------------------------
// Event wiring
// -------------------------------------------------------------
function wireSex() {
  document.querySelectorAll('input[name="sex"]').forEach(r => {
    r.addEventListener("change", () => {
      if (r.checked) {
        state.sex = r.value;
        // Rebuild the 15 thumbnails with the new sex prefix. The selected
        // (color, num) indices are preserved — user's "that one" pick
        // lands on the same grid cell for the other sex.
        populateAvatarGallery();
        rerender();
        syncDevice();
      }
    });
  });
}

// Q_AVATAR (v6): click on a tile → set both state.color and state.num
// atomically. wireDelegated is designed for single-field radio groups;
// a compound-value gallery gets its own handler.
function wireAvatarGallery() {
  document.getElementById("opts-avatar").addEventListener("click", (e) => {
    const tile = e.target.closest(".avatar-tile");
    if (!tile) return;
    state.color = Number(tile.dataset.color);
    state.num = Number(tile.dataset.num);
    document.querySelectorAll("#opts-avatar .avatar-tile").forEach(t => {
      const isThis = t === tile;
      t.classList.toggle("selected", isThis);
      t.setAttribute("aria-checked", isThis ? "true" : "false");
    });
    rerender();
    syncDevice();
  });
}

function wireDelegated(containerId, radioName, field, coerce = Number, onAfter) {
  document.getElementById(containerId).addEventListener("change", (e) => {
    if (e.target.name === radioName) {
      state[field] = coerce(e.target.value);
      if (typeof onAfter === "function") onAfter();
      rerender();
      syncDevice();
    }
  });
}

// Smile radio values come in as strings "true"/"false"; coerce to bool.
const toBool = (v) => v === "true" || v === true;

function wireEnergy() {
  const slider = document.getElementById("slider-energy");
  const label = document.getElementById("slider-energy-label");
  const handler = () => {
    state.bg_level = Number(slider.value);
    label.textContent = ENERGY_LABELS[state.bg_level] ?? "—";
    rerender();
    syncDevice();
  };
  slider.addEventListener("input", handler);
  handler();
}

// Called every time firmware reports {"hr":N} (~1 Hz). Maps BPM to a
// 0-5 energy bucket and pushes it through the same path as a slider drag.
// When bpm=0 (no finger) we leave bg_level alone so the avatar doesn't
// flicker between "no signal" and "low".
function handleHeartRate(bpm) {
  lastBpm = bpm;
  updateHeartRateReadout();
  const energy = bpmToEnergy(bpm);
  if (energy === null) return;
  if (energy === state.bg_level) return;          // no-op, skip rerender
  state.bg_level = energy;
  const slider = document.getElementById("slider-energy");
  const label  = document.getElementById("slider-energy-label");
  if (slider) slider.value = String(energy);
  if (label)  label.textContent = ENERGY_LABELS[energy] ?? "—";
  rerender();
  syncDevice();
}

// Render the "♥ 78 bpm · medium" readout (or "no finger detected"), and
// flip the slider between editable (no signal) and read-only (sensor live).
// The slider stays a manual fallback so demos without hardware keep working.
function updateHeartRateReadout() {
  const el = document.getElementById("hr-readout");
  const slider = document.getElementById("slider-energy");
  const live = (lastBpm !== null && lastBpm > 0);
  if (el) {
    if (!live) {
      el.textContent = "♥ — no signal";
      el.classList.remove("active");
    } else {
      const lvl = ENERGY_LABELS[bpmToEnergy(lastBpm) ?? 2] ?? "—";
      el.textContent = `♥ ${lastBpm} bpm · ${lvl}`;
      el.classList.add("active");
    }
  }
  if (slider) slider.disabled = live;
}

// Rebuild the preview's interest row from state.interests.
// Called after any selection change in wireInterests, and once on boot.
// Format: "♪ music · ▶ film · ☕ food" — hidden when empty.
function renderPreviewInterests() {
  const row = document.getElementById("preview-interests");
  if (!row) return;
  if (!state.interests.length || !questions) {
    row.innerHTML = "";
    row.classList.remove("visible");
    return;
  }
  const byValue = new Map(questions.Q_INTERESTS.options.map(o => [o.value, o]));
  const parts = state.interests
    .map(v => byValue.get(v))
    .filter(Boolean)
    .map(o => `<span class="preview-interest"><span class="preview-interest-icon">${o.icon}</span>${o.label}</span>`);
  row.innerHTML = parts.join('<span class="preview-interest-sep">·</span>');
  row.classList.add("visible");
}

// Q_INTERESTS: click a tag to toggle; enforce MAX_INTERESTS.
// When selection is full, a 4th click doesn't add — it shows a hint
// asking the user to deselect one first.
function wireInterests() {
  const container = document.getElementById("opts-interests");
  const hint = document.getElementById("interests-hint");
  let hintTimer = null;

  const showHint = (msg) => {
    hint.textContent = msg;
    hint.classList.add("visible");
    clearTimeout(hintTimer);
    hintTimer = setTimeout(() => {
      hint.classList.remove("visible");
      hint.textContent = "";
    }, 2400);
  };

  container.addEventListener("click", (e) => {
    const tag = e.target.closest(".interest-tag");
    if (!tag) return;
    const value = tag.dataset.value;
    const idx = state.interests.indexOf(value);

    if (idx !== -1) {
      // Already selected → deselect.
      state.interests.splice(idx, 1);
      tag.classList.remove("selected");
      tag.setAttribute("aria-checked", "false");
      hint.classList.remove("visible");
      hint.textContent = "";
    } else {
      // Not yet selected.
      if (state.interests.length >= MAX_INTERESTS) {
        // Already at max — block and prompt.
        showHint(`max ${MAX_INTERESTS} — tap one to free a slot`);
        return;
      }
      state.interests.push(value);
      tag.classList.add("selected");
      tag.setAttribute("aria-checked", "true");
    }
    renderPreviewInterests();
    syncDevice();
    renderInterestSubtags();
  });
}

// Q_INTERESTS_DETAIL (v8): for each currently-selected primary interest,
// render a row of pill buttons with that primary's subtags. Toggling a
// pill updates state.interest_details[primary]. Subtag selection is
// optional (zero-or-more), so there's no max-cap or hint.
//
// The whole fieldset hides when no primary is selected. When a primary
// is deselected we also drop its detail entry to keep state.interest_details
// in sync with state.interests.
function renderInterestSubtags() {
  const fs = document.getElementById("qblock-subtags");
  const container = document.getElementById("opts-subtags");
  if (!fs || !container || !questions) return;

  // Drop entries for primaries that are no longer selected.
  for (const k of Object.keys(state.interest_details)) {
    if (!state.interests.includes(k)) delete state.interest_details[k];
  }

  if (state.interests.length === 0) {
    fs.hidden = true;
    container.innerHTML = "";
    return;
  }
  fs.hidden = false;

  const byValue = new Map(questions.Q_INTERESTS.options.map(o => [o.value, o]));
  const groups = state.interests.map(primary => {
    const opt = byValue.get(primary);
    if (!opt || !opt.subtags) return "";
    const picked = state.interest_details[primary] || [];
    const pills = opt.subtags.map(sub => {
      const sel = picked.includes(sub) ? " selected" : "";
      return `<button type="button" class="subtag-pill${sel}" `
           + `data-primary="${primary}" data-subtag="${sub}" `
           + `aria-pressed="${picked.includes(sub) ? "true" : "false"}">${sub}</button>`;
    }).join("");
    return `<div class="subtag-group">
              <div class="subtag-group-label"><span class="subtag-icon">${opt.icon}</span>${opt.label}</div>
              <div class="subtag-pills">${pills}</div>
            </div>`;
  }).join("");
  container.innerHTML = groups;
}

function wireInterestSubtags() {
  const container = document.getElementById("opts-subtags");
  if (!container) return;
  container.addEventListener("click", (e) => {
    const pill = e.target.closest(".subtag-pill");
    if (!pill) return;
    const primary = pill.dataset.primary;
    const subtag  = pill.dataset.subtag;
    const list = state.interest_details[primary] || (state.interest_details[primary] = []);
    const idx = list.indexOf(subtag);
    if (idx !== -1) {
      list.splice(idx, 1);
      pill.classList.remove("selected");
      pill.setAttribute("aria-pressed", "false");
    } else {
      list.push(subtag);
      pill.classList.add("selected");
      pill.setAttribute("aria-pressed", "true");
    }
    // Subtags are NOT sent to device or rendered in preview — collective
    // view consumes them on submit. No syncDevice / rerender needed.
    saveSessionState();
  });
}

function wireQuote() {
  const input = document.getElementById("input-quote");
  input.addEventListener("input", () => {
    state.quote = input.value;
    rerender();
    syncDevice();
  });

  document.getElementById("quote-suggestions")
    .addEventListener("click", (e) => {
      const chip = e.target.closest(".suggestion-chip");
      if (!chip) return;
      const s = chip.dataset.suggest || "";
      input.value = s;
      state.quote = s;
      rerender();
      syncDevice();
    });
}


// -------------------------------------------------------------
// Persistence — sessionStorage keeps the in-progress profile across
// accidental refreshes. Cleared once a successful /submit happens so
// the next visitor on the same phone gets a fresh form.
// -------------------------------------------------------------
const SESSION_KEY = "xy_profile_v8";

function saveSessionState() {
  try { sessionStorage.setItem(SESSION_KEY, JSON.stringify(state)); }
  catch (_) { /* quota/permission — ignore */ }
}

function loadSessionState() {
  try {
    const raw = sessionStorage.getItem(SESSION_KEY);
    if (!raw) return;
    const saved = JSON.parse(raw);
    // Whitelisted merge: only restore known fields, skip anything else
    // so a stale schema can't corrupt current state.
    for (const k of ["sex","color","num","smile","mood","bg_level","quote","interests","interest_details"]) {
      if (k in saved) state[k] = saved[k];
    }
  } catch (_) { /* malformed — ignore */ }
}

function clearSessionState() {
  try { sessionStorage.removeItem(SESSION_KEY); } catch (_) {}
}


// -------------------------------------------------------------
// Submit — POST current full state (incl. interest_details) to the
// Flask backend. Disable the button on success to block accidental
// double submits; sessionStorage is wiped so a refresh after submit
// doesn't reload the same profile.
// -------------------------------------------------------------
function wireSubmit() {
  const btn = document.getElementById("btn-submit");
  const status = document.getElementById("submit-status");
  if (!btn || !status) return;

  // Match what the backend stores. Mirrors syncDevice() field set
  // PLUS interest_details, which the device doesn't get. The optional
  // sticker number tells the server to also bind this submission to a
  // STAGE3_USERS slot so my ESP32 tap reads the DIY data; an empty value
  // keeps the legacy "wall only" behavior.
  const buildSubmitPayload = () => {
    const stickerEl = document.getElementById("input-sticker");
    const stickerVal = stickerEl ? stickerEl.value : "";
    const payload = {
      sex:               state.sex,
      color:             state.color,
      num:               state.num,
      smile:             state.smile,
      mood:              state.mood,
      bg_level:          state.bg_level,
      quote:             state.quote,
      interests:         state.interests,
      interest_details:  state.interest_details,
    };
    if (stickerVal) payload.sticker = Number(stickerVal);
    return payload;
  };

  btn.addEventListener("click", async () => {
    btn.disabled = true;
    status.className = "submit-status";
    status.textContent = "submitting…";

    try {
      const res = await fetch("/submit", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(buildSubmitPayload()),
      });
      if (!res.ok) throw new Error(`server replied ${res.status}`);
      status.className = "submit-status success";
      status.textContent = "submitted ✓ check the wall";
      clearSessionState();
      // Leave button disabled — they shouldn't submit twice this session.
    } catch (err) {
      console.error(err);
      btn.disabled = false;
      status.className = "submit-status error";
      status.textContent = `submit failed — ${err.message}`;
    }
  });
}


// -------------------------------------------------------------
// Main
// -------------------------------------------------------------
async function main() {
  try {
    await loadData();

    // Restore in-progress profile from sessionStorage BEFORE populating any
    // selected/highlighted UI so initial DOM reflects the restored state.
    loadSessionState();

    populateAvatarGallery();
    populateSmile();
    populateMood();
    populateInterests();
    populateQuoteSuggestions();

    // Restore quote input value if state has one (other UI repaints itself
    // via the next rerender + populate* paths).
    const quoteInput = document.getElementById("input-quote");
    if (quoteInput && state.quote) quoteInput.value = state.quote;

    wireSex();
    wireAvatarGallery();
    wireDelegated("opts-smile", "smile", "smile", toBool);
    wireDelegated("opts-mood",  "mood",  "mood");
    wireEnergy();
    wireInterests();
    wireInterestSubtags();
    renderInterestSubtags();   // populates from restored state.interests
    renderPreviewInterests();  // restored primaries reflect on preview row
    wireQuote();
    wireSubmit();

    // USB Serial connect button. Once connected, the device:connected
    // event flushes current state, so the board doesn't stay stuck on
    // its boot "waiting..." screen even if the user doesn't touch anything.
    connectUI();
    window.addEventListener("device:connected", syncDevice);

    // Heart rate from device → drives bg_level (energy). The slider UI
    // becomes a read-only readout when a BPM is being received.
    window.addEventListener("device:message", (e) => {
      const msg = e.detail;
      if (typeof msg.hr === "number") handleHeartRate(msg.hr);
    });

    startAnimation();
    rerender();
    syncDevice();
  } catch (err) {
    console.error(err);
    document.getElementById("avatar-pre").textContent =
      `ERR loading data: ${err.message}`;
  }
}

main();
