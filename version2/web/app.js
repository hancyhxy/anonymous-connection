// =============================================================
// pip v2 — app.js (v10)
//
// User-facing structure (two parallel tabs, NOT a wizard):
//
//   Tab "avatar" — presentation layer.
//     1. sex       — 2 (male/female)
//     2. avatar    — 15 per sex (Q_AVATAR gallery)
//     3. mood + bg — Q_MOOD_BG: 5 hue swatches + brightness slider
//     4. quote     — free text, 14 ASCII chars
//     5. nickname  — optional, 12 ASCII chars
//
//   Tab "interest" — matching layer.
//     6. interests       — primary tags (max 3)
//     7. interest_details — per-primary subtags
//
// Smile was dropped in v10 — sprite falls back to neutral variant.
// state.smile is kept as `false` because renderer.js buildSpriteKey
// still consumes it.
//
// IMPORTANT: interest fields are NEVER rendered on any surface
// (phone preview, collective wall, hardware LCD). They only feed
// compute_match() on the server. The bottom row of the preview is
// the nickname slot, not an interest row.
//
// Animation model (ported back from hello-stranger):
//   - OUTER key (sex/color/num/smile) = WHICH sprite — stable.
//   - INNER pointer (animFrame) = WHICH FRAME of that sprite — ticks
//     on a setInterval. Subtle blink / mouth / breathing detail.
// =============================================================

import { renderProfile, applyToDOM, buildSpriteKey } from "./renderer.js";

// ---- Hidden animation parameter (not in UI) -----------------
const ANIM_INTERVAL_MS = 166;

// v12: true empty state — required fields start as null so the UI can
// render a placeholder instead of preselecting defaults. bg_level keeps
// a numeric default because the slider thumb has to physically rest
// somewhere; `touched.color` is the source of truth for "user decided
// on a color", not bg_level's value.
const state = {
  sex: null,        // null until user clicks female/male pill
  color: null,      // null until user clicks an avatar tile
  num: null,        // null until user clicks an avatar tile
  smile: false,     // dropped from UI since v10; buildSpriteKey still needs it
  mood: null,       // null until user clicks a mood swatch
  bg_level: 2,      // slider thumb must rest somewhere; touched.color is truth
  quote: "",
  nickname: "",
  interests: [],
  interest_details: {},
};

// v12: touched lifted from field-level to accordion-group-level. The UI
// shows two required accordions ("pick your look", "pick a background
// color"); each contains multiple fields. Touching ANY field inside a
// group marks the whole group touched. Mirrors what the user sees.
//
// v13: added an interest requirement (≥1 tag picked). interest lives
// on a separate tab, so it's checked via state.interests.length rather
// than a touched flag — having picked then unpicked still counts as "0".
const touched = {
  look:  false,     // sex pill OR avatar tile clicked
  color: false,     // mood swatch clicked OR brightness slider dragged
};

// v14: requirements grouped by wizard step. Step 1 gates the "next"
// button; step 2 gates the "submit" button. Order within a group
// determines which accordion gets highlighted on a failed gate.
const STEP1_REQS = [
  { key: "interest", test: () => state.interests.length > 0 },
];
const STEP2_REQS = [
  { key: "look",  test: () => touched.look,  acc: "form"  },
  { key: "color", test: () => touched.color, acc: "color" },
];

function firstUnmet(reqs) {
  for (const r of reqs) {
    if (!r.test()) return r;
  }
  return null;
}
const MAX_INTERESTS = 3;

// Internal animation pointer — NOT part of user state.
let animFrame = 0;

// URL-driven sticker binding. Captured once on boot from ?sticker=N
// in the URL. Used by buildSubmitPayload to attach sticker to /submit.
// Replaces the deleted Q_STICKER dropdown.
let _urlSticker = null;

// v14: replaced tabs with a 2-step wizard. currentStep is 1 (interest)
// or 2 (avatar). Persisted to sessionStorage so refresh lands on the
// step the user was last on.
let currentStep = 1;

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
function escapeHtml(s) {
  return s.replace(/[&<>]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;" }[c]));
}

// Q_AVATAR: 15-tile visual gallery per sex.
// v12: when sex is null (user hasn't picked yet) show a placeholder card
// instead of the grid — sprites can't be resolved without a sex prefix.
function populateAvatarGallery() {
  const container = document.getElementById("opts-avatar");
  if (state.sex === null) {
    container.innerHTML =
      `<div class="gallery-placeholder">pick female or male above to see avatars</div>`;
    return;
  }
  const tiles = [];
  for (let color = 0; color < 5; color++) {
    for (let num = 0; num < 3; num++) {
      const key = buildSpriteKey({ sex: state.sex, color, num, smile: false });
      const frames = sprites && sprites[key];
      const firstFrame = frames && frames.length ? frames[0] : "(missing)";
      // v12: null state.color/num → no tile is preselected.
      const isSelected = state.color !== null
                      && state.num !== null
                      && color === state.color
                      && num === state.num;
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

// Q_MOOD_BG (v10): 5 hue swatches, anonymized — no chill/curious/playful
// labels. The color itself is the choice. Anchor hex per hue comes from
// colors.palette[mood][3] (the "steady" cell — the most representative
// brightness for showing what a hue family looks like).
function populateMood() {
  const container = document.getElementById("opts-mood");
  if (!container || !colors) return;
  const opts = questions.Q_MOOD.options;
  container.innerHTML = opts.map(opt => {
    const anchor = colors.palette[String(opt.value)]?.["3"]?.hex || "#888";
    // v12: null state.mood → no swatch is preselected.
    const isSelected = state.mood !== null && Number(opt.value) === state.mood;
    return `<button type="button"
              class="mood-swatch${isSelected ? " selected" : ""}"
              role="radio" aria-checked="${isSelected}"
              data-mood="${opt.value}"
              style="background:${anchor}"
              aria-label="background hue ${opt.value}"></button>`;
  }).join("");
}

function populateQuoteSuggestions() {
  const container = document.getElementById("quote-suggestions");
  container.innerHTML = questions.Q_QUOTE.suggestions.map(s => `
    <button type="button" class="suggestion-chip" data-suggest="${s}">${s}</button>
  `).join("");
}

function populateInterests() {
  const container = document.getElementById("opts-interests");
  const opts = questions.Q_INTERESTS.options;
  container.innerHTML = opts.map(opt => {
    const isSelected = state.interests.includes(opt.value);
    return `<button type="button" class="interest-tag${isSelected ? " selected" : ""}"
              role="checkbox" aria-checked="${isSelected}"
              data-value="${opt.value}">
              <span class="interest-label">${opt.label}</span>
            </button>`;
  }).join("");
}


// -------------------------------------------------------------
// Subtle in-sprite animation loop
// -------------------------------------------------------------
function startAnimation() {
  if (animationTimer !== null) return;
  animationTimer = setInterval(() => {
    animFrame = (animFrame + 1) >>> 0;
    rerender();
  }, ANIM_INTERVAL_MS);
}


// -------------------------------------------------------------
// Render (single source of UI truth)
// -------------------------------------------------------------
// v12: three-stage preview reveal.
//   stage 1 — !touched.look  → grey block + guidance line, no avatar drawn
//   stage 2 — look touched, !color → ascii avatar drawn, grey-neutral palette
//   stage 3 — both touched   → ascii avatar drawn, full mood/bg palette
// The stages map directly to the two required accordions; each user
// decision visibly reveals more of the avatar.
function rerender() {
  const previewFrame = document.getElementById("preview-frame");
  const pre = document.getElementById("avatar-pre");
  const placeholder = document.getElementById("preview-placeholder");
  const bubble = document.getElementById("quote-bubble");

  // v13.8: placeholder when ANY of sex/color/num is null — not just when
  // touched.look is false. Previously a user could touch the look group
  // (e.g. click sex but not a tile) and leave state.color/num as null;
  // rerender would then call buildSpriteKey("female_null_null"), get a
  // sprites[key]=undefined miss, and applyToDOM would write "(sprite
  // missing)" into avatar-pre. Guarding on the actual data avoids that.
  const needsPlaceholder = state.sex === null
                        || state.color === null
                        || state.num === null;

  if (needsPlaceholder) {
    if (previewFrame) previewFrame.classList.add("placeholder-mode");
    if (pre) pre.style.display = "none";
    if (placeholder) {
      placeholder.style.display = "flex";
      placeholder.textContent = "pick a body to begin";
    }
    if (bubble) bubble.hidden = true;
    return;
  }

  // Stage 2 / 3: avatar drawn. Color path depends on touched.color.
  if (previewFrame) previewFrame.classList.remove("placeholder-mode");
  if (pre) pre.style.display = "";
  if (placeholder) placeholder.style.display = "none";

  try {
    const opts = { neutral: !touched.color };
    lastRendered = renderProfile(state, sprites, colors, animFrame, opts);
    applyToDOM(lastRendered);
    renderPreviewNickname();
  } catch (err) {
    console.error("render error:", err);
    if (pre) pre.textContent = `ERR: ${err.message}`;
    lastRendered = null;
  }
}

// Replaces the v9 renderPreviewInterests. The bottom slot of the
// preview frame now shows the user's nickname (if any) — interest
// is intentionally not rendered on any surface.
function renderPreviewNickname() {
  const row = document.getElementById("preview-nickname");
  if (!row) return;
  const nick = (state.nickname || "").trim();
  if (!nick) {
    row.textContent = "";
    row.classList.remove("visible");
    return;
  }
  row.textContent = nick;
  row.classList.add("visible");
}

// v10: no more browser→device push. Device pulls profile from server
// via HTTP poll. The only side effect on state change is persisting
// the in-progress draft to sessionStorage so an accidental refresh
// doesn't lose the user's choices.
function persistDraft() {
  saveSessionState();
  // v13/v14: every user action that's worth saving also triggers a
  // button-state refresh. Single chokepoint — every wire* gets it free.
  updateWizardButtons();
}

// Update the energy slider's CSS gradient track based on current mood.
// Called on boot and every time state.mood changes.
function updateEnergySliderGradient() {
  const slider = document.getElementById("slider-energy");
  if (!slider || !colors) return;
  const row = colors.palette[String(state.mood)];
  if (!row) return;
  const stops = ["0","1","2","3","4","5"].map(k => row[k].hex).join(", ");
  slider.style.background = `linear-gradient(to right, ${stops})`;
}


// -------------------------------------------------------------
// Event wiring
// -------------------------------------------------------------
// v11: sex is rendered as two capsule pills (female first, then male).
// Click toggles state.sex and re-renders the 15-tile gallery so users
// see only the body variant they picked.
function wireSexPills() {
  const container = document.getElementById("opts-sex");
  if (!container) return;
  container.addEventListener("click", (e) => {
    const pill = e.target.closest(".pill");
    if (!pill) return;
    state.sex = pill.dataset.sex;
    touched.look = true;
    container.querySelectorAll(".pill").forEach(p => {
      const isThis = p === pill;
      p.classList.toggle("active", isThis);
      p.setAttribute("aria-checked", isThis ? "true" : "false");
    });
    populateAvatarGallery();
    rerender();
    persistDraft();
  });
}

function wireAvatarGallery() {
  document.getElementById("opts-avatar").addEventListener("click", (e) => {
    const tile = e.target.closest(".avatar-tile");
    if (!tile) return;
    state.color = Number(tile.dataset.color);
    state.num = Number(tile.dataset.num);
    touched.look = true;
    document.querySelectorAll("#opts-avatar .avatar-tile").forEach(t => {
      const isThis = t === tile;
      t.classList.toggle("selected", isThis);
      t.setAttribute("aria-checked", isThis ? "true" : "false");
    });
    rerender();
    persistDraft();
  });
}

// Q_MOOD_BG swatches (v10): click a hue → set state.mood, rebuild the
// gradient on the brightness slider, repaint.
function wireMoodSwatches() {
  const container = document.getElementById("opts-mood");
  if (!container) return;
  container.addEventListener("click", (e) => {
    const sw = e.target.closest(".mood-swatch");
    if (!sw) return;
    state.mood = Number(sw.dataset.mood);
    touched.color = true;
    document.querySelectorAll("#opts-mood .mood-swatch").forEach(s => {
      const isThis = s === sw;
      s.classList.toggle("selected", isThis);
      s.setAttribute("aria-checked", isThis ? "true" : "false");
    });
    updateEnergySliderGradient();
    rerender();
    persistDraft();
  });
}

function wireEnergy() {
  const slider = document.getElementById("slider-energy");
  if (!slider) return;
  const handler = () => {
    state.bg_level = Number(slider.value);
    touched.color = true;
    rerender();
    persistDraft();
  };
  slider.addEventListener("input", handler);
}

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
      state.interests.splice(idx, 1);
      tag.classList.remove("selected");
      tag.setAttribute("aria-checked", "false");
      hint.classList.remove("visible");
      hint.textContent = "";
    } else {
      if (state.interests.length >= MAX_INTERESTS) {
        showHint(`max ${MAX_INTERESTS} — tap one to free a slot`);
        return;
      }
      state.interests.push(value);
      tag.classList.add("selected");
      tag.setAttribute("aria-checked", "true");
    }
    persistDraft();
    renderInterestSubtags();
    updateWizardButtons();
  });
}

function renderInterestSubtags() {
  const fs = document.getElementById("qblock-subtags");
  const container = document.getElementById("opts-subtags");
  if (!fs || !container || !questions) return;

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
              <div class="subtag-group-label">${opt.label}</div>
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
    saveSessionState();
  });
}

function wireQuote() {
  const input = document.getElementById("input-quote");
  if (!input) return;
  input.addEventListener("input", () => {
    state.quote = input.value;
    rerender();
    persistDraft();
  });

  document.getElementById("quote-suggestions")
    .addEventListener("click", (e) => {
      const chip = e.target.closest(".suggestion-chip");
      if (!chip) return;
      const s = chip.dataset.suggest || "";
      input.value = s;
      state.quote = s;
      rerender();
      persistDraft();
    });
}

function wireNickname() {
  const input = document.getElementById("input-nickname");
  if (!input) return;
  input.addEventListener("input", () => {
    state.nickname = input.value;
    renderPreviewNickname();
    persistDraft();
  });
}


// -------------------------------------------------------------
// Tab switching — two parallel panels, free to switch.
// -------------------------------------------------------------
// v14: switch to step n (1 or 2). Always scrolls to top so the user
// sees the new step's first field, not whatever the previous step's
// scroll position was.
function showStep(n) {
  if (n !== 1 && n !== 2) n = 1;
  currentStep = n;
  document.querySelectorAll(".step-panel").forEach(el => {
    el.hidden = String(n) !== el.dataset.step;
  });
  document.querySelectorAll(".step-dot").forEach(dot => {
    const dn = Number(dot.dataset.stepDot);
    dot.classList.toggle("active", dn === n);
    dot.classList.toggle("done",   dn < n);
  });
  window.scrollTo({ top: 0, behavior: "instant" });
  saveSessionState();
  updateWizardButtons();
}

function wireStepNav() {
  document.getElementById("btn-back")?.addEventListener("click", () => {
    showStep(currentStep - 1);
  });
  document.getElementById("btn-next")?.addEventListener("click", () => {
    const blocked = firstUnmet(STEP1_REQS);
    if (blocked) {
      // Step 1 incomplete — scroll to the interest grid so the user
      // sees what's missing. interests-hint already lights up if they
      // try to pick a 4th, but here they haven't picked any.
      const grid = document.getElementById("opts-interests");
      grid?.scrollIntoView({ behavior: "smooth", block: "start" });
      // Flash the fieldset with the invalid class for a moment.
      const fs = document.querySelector('fieldset[data-q="Q_INTERESTS"]');
      if (fs) {
        fs.classList.add("invalid");
        setTimeout(() => fs.classList.remove("invalid"), 1600);
      }
      return;
    }
    showStep(2);
  });
}


// -------------------------------------------------------------
// Persistence
// -------------------------------------------------------------
// v14: bumped from v12 because currentTab → currentStep (int) — old
// "avatar"/"interest" string would re-hydrate as undefined and silently
// reset the wizard to step 1, which is correct anyway but cleaner to
// skip outright.
const SESSION_KEY = "xy_profile_v14";

function saveSessionState() {
  try {
    sessionStorage.setItem(
      SESSION_KEY,
      JSON.stringify({ ...state, currentStep, touched })
    );
  } catch (_) { /* quota/permission — ignore */ }
}

function loadSessionState() {
  // v14: evict older schema leftovers so refresh lands cleanly.
  try { sessionStorage.removeItem("xy_profile_v11"); } catch (_) {}
  try { sessionStorage.removeItem("xy_profile_v12"); } catch (_) {}
  try {
    const raw = sessionStorage.getItem(SESSION_KEY);
    if (!raw) return;
    const saved = JSON.parse(raw);
    for (const k of ["sex","color","num","mood","bg_level","quote","nickname","interests","interest_details"]) {
      if (k in saved) state[k] = saved[k];
    }
    if (saved.currentStep === 1 || saved.currentStep === 2) {
      currentStep = saved.currentStep;
    }
    if (saved.touched && typeof saved.touched === "object") {
      // Object.keys(touched) is {look, color} in v12; any v11 keys
      // (sex/avatar/mood/bg_level) in saved.touched are ignored.
      for (const k of Object.keys(touched)) {
        if (typeof saved.touched[k] === "boolean") touched[k] = saved.touched[k];
      }
    }
  } catch (_) { /* malformed — ignore */ }
}

function clearSessionState() {
  try { sessionStorage.removeItem(SESSION_KEY); } catch (_) {}
}


// -------------------------------------------------------------
// Sticker binding from URL — replaces the deleted Q_STICKER dropdown.
// Each station's LCD shows its own QR code with ?sticker=N in the URL.
// Scanning that QR is what tells the server which device to push the
// profile to. There's no manual fallback anymore — if a user lands on
// /index.html directly, the submission only goes to the wall.
// -------------------------------------------------------------
function applyUrlSticker() {
  const params = new URLSearchParams(window.location.search);
  const raw = params.get("sticker");
  if (!raw) return;
  const n = Number(raw);
  if (!Number.isInteger(n) || n < 1 || n > 5) return;
  _urlSticker = n;
}


// -------------------------------------------------------------
// Submit
// -------------------------------------------------------------
// v14: wizard buttons. The dock shows different buttons per step:
//   step 1 → [next →]    (disabled until step 1 reqs met)
//   step 2 → [← back] [submit]   (submit disabled until step 2 reqs met)
// Back is always enabled. Disabled buttons stay clickable so we can
// scroll the user to whatever's missing (better than dead clicks).
function updateWizardButtons() {
  const back = document.getElementById("btn-back");
  const next = document.getElementById("btn-next");
  const submit = document.getElementById("btn-submit");
  if (!back || !next || !submit) return;

  if (currentStep === 1) {
    back.hidden = true;
    next.hidden = false;
    submit.hidden = true;
    const blocked = firstUnmet(STEP1_REQS);
    if (blocked) {
      next.dataset.unmet = blocked.key;
    } else {
      delete next.dataset.unmet;
    }
  } else {
    back.hidden = false;
    next.hidden = true;
    submit.hidden = false;
    const blocked = firstUnmet(STEP2_REQS);
    if (blocked) {
      submit.dataset.unmet = blocked.key;
    } else {
      delete submit.dataset.unmet;
    }
  }
}

// v11/v14: open the accordion identified by data-acc + scroll to it.
// Renamed from focusAccordionStep to free up "step" for wizard concepts.
function focusAccordion(name) {
  const acc = document.querySelector(`.acc[data-acc="${name}"]`);
  if (!acc) return;
  acc.open = true;
  acc.classList.add("invalid");
  setTimeout(() => acc.classList.remove("invalid"), 1600);
  acc.scrollIntoView({ behavior: "smooth", block: "start" });
}

function wireSubmit() {
  const btn = document.getElementById("btn-submit");
  const status = document.getElementById("submit-status");
  if (!btn || !status) return;

  // v12/v13: belt-and-suspenders fill. firstUnmetRequirement() blocks
  // submits where required groups are untouched, so by the time this
  // runs we know touched.look + touched.color are both true and
  // state.interests has at least one entry. Null-coalescing here is
  // defense against a hypothetical logic bug leaking null to the server.
  const buildSubmitPayload = () => {
    const payload = {
      sex:               state.sex      ?? "female",
      color:             state.color    ?? 0,
      num:               state.num      ?? 0,
      smile:             state.smile,        // always false since v10
      mood:              state.mood     ?? 0,
      bg_level:          state.bg_level,
      quote:             state.quote,
      nickname:          state.nickname,
      interests:         state.interests,
      interest_details:  state.interest_details,
    };
    if (_urlSticker !== null) payload.sticker = _urlSticker;
    return payload;
  };

  btn.addEventListener("click", async () => {
    // v14: submit only fires on step 2. Even if step 2 reqs aren't met,
    // the button is still clickable — we scroll to the missing accordion
    // instead of doing nothing.
    const blocked = firstUnmet(STEP2_REQS);
    if (blocked) {
      focusAccordion(blocked.acc);
      return;
    }

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
      status.textContent = "submitted ✓";
      clearSessionState();
      btn.disabled = false;
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
    applyUrlSticker();
    loadSessionState();

    populateAvatarGallery();
    populateMood();
    populateInterests();
    populateQuoteSuggestions();

    // v12: sync sex pill selection to restored state.sex. When sex is
    // null (fresh visit), neither pill is highlighted — both stay in
    // the default "unselected" appearance.
    document.querySelectorAll("#opts-sex .pill").forEach(p => {
      const isThis = state.sex !== null && p.dataset.sex === state.sex;
      p.classList.toggle("active", isThis);
      p.setAttribute("aria-checked", isThis ? "true" : "false");
    });

    // Restore text inputs from state
    const quoteInput = document.getElementById("input-quote");
    if (quoteInput && state.quote) quoteInput.value = state.quote;
    const nicknameInput = document.getElementById("input-nickname");
    if (nicknameInput && state.nickname) nicknameInput.value = state.nickname;

    // Restore energy slider position
    const slider = document.getElementById("slider-energy");
    if (slider) slider.value = String(state.bg_level);

    wireStepNav();            // v14: wizard back/next handlers
    wireSexPills();
    wireAvatarGallery();
    wireMoodSwatches();
    wireEnergy();
    wireInterests();
    wireInterestSubtags();
    renderInterestSubtags();
    wireQuote();
    wireNickname();
    wireSubmit();

    showStep(currentStep);    // v14: enter saved step (defaults to 1)
    updateEnergySliderGradient();
    updateWizardButtons();    // v14: prime button states from restored state
    startAnimation();
    rerender();
  } catch (err) {
    console.error(err);
    document.getElementById("avatar-pre").textContent =
      `ERR loading data: ${err.message}`;
  }
}

main();
