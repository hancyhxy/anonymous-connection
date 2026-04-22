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
//   6. quote     — free text, 28 ASCII chars
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
  interests: [],    // v7: Q_INTERESTS — up to 3 tag values. Web-only for now,
                    // not included in the device sync payload.
};
const MAX_INTERESTS = 3;

// Internal animation pointer — NOT part of user state. Ticks on setInterval.
// Used by renderer.js to pick frames[animFrame % frames.length].
let animFrame = 0;

const ENERGY_LABELS = ["empty", "low", "medium", "steady", "high", "electric"];

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
  sendState({
    sex:    state.sex,
    color:  state.color,
    num:    state.num,
    smile:  state.smile,
    mood:   state.mood,
    energy: state.bg_level,
    quote:  state.quote,
  });
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
    // No syncDevice — interests are web-only; device sync payload unchanged.
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
// Main
// -------------------------------------------------------------
async function main() {
  try {
    await loadData();
    populateAvatarGallery();
    populateSmile();
    populateMood();
    populateInterests();
    populateQuoteSuggestions();

    wireSex();
    wireAvatarGallery();
    wireDelegated("opts-smile", "smile", "smile", toBool);
    wireDelegated("opts-mood",  "mood",  "mood");
    wireEnergy();
    wireInterests();
    wireQuote();

    // USB Serial connect button. Once connected, the device:connected
    // event flushes current state, so the board doesn't stay stuck on
    // its boot "waiting..." screen even if the user doesn't touch anything.
    connectUI();
    window.addEventListener("device:connected", syncDevice);

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
