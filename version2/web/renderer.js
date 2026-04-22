// =============================================================
// pip v2 — renderer.js
// Pure functions: given a profile + sprites + colors data,
// compute every visual property of the avatar.
//
// Color math is ported verbatim from hello-stranger's
//   src/components/hellostranger/HelloStranger.person.svelte, lines 48-114
// (github.com/the-pudding/hello-stranger, MIT License, Copyright (c) 2022 The Pudding).
//
// spriteKey rule ported from the same file, lines 153 + 162.
// =============================================================


// -------------------------------------------------------------
// Color derivation (hello-stranger, ported as-is)
// -------------------------------------------------------------

export function darkenColor(hex, factor = 0.3) {
  hex = hex.replace('#', '');
  const r = parseInt(hex.substr(0, 2), 16);
  const g = parseInt(hex.substr(2, 2), 16);
  const b = parseInt(hex.substr(4, 2), 16);
  const max = Math.max(r, g, b);
  const boost = 0.2;
  const darkR = Math.round(r * (factor + (r === max ? boost : 0)));
  const darkG = Math.round(g * (factor + (g === max ? boost : 0)));
  const darkB = Math.round(b * (factor + (b === max ? boost : 0)));
  const toHex = (n) => n.toString(16).padStart(2, '0');
  return `#${toHex(darkR)}${toHex(darkG)}${toHex(darkB)}`;
}

export function darkestColor(hex, factor = 0.27) {
  // Same body as darkenColor, just with a lower factor default.
  // Hello-stranger duplicates it verbatim; we match that decision
  // so future upstream merges stay painless.
  hex = hex.replace('#', '');
  const r = parseInt(hex.substr(0, 2), 16);
  const g = parseInt(hex.substr(2, 2), 16);
  const b = parseInt(hex.substr(4, 2), 16);
  const max = Math.max(r, g, b);
  const boost = 0.2;
  const darkR = Math.round(r * (factor + (r === max ? boost : 0)));
  const darkG = Math.round(g * (factor + (g === max ? boost : 0)));
  const darkB = Math.round(b * (factor + (b === max ? boost : 0)));
  const toHex = (n) => n.toString(16).padStart(2, '0');
  return `#${toHex(darkR)}${toHex(darkG)}${toHex(darkB)}`;
}

export function lightenColor(hex, factor = 1.3) {
  hex = hex.replace('#', '');
  const r = parseInt(hex.substr(0, 2), 16);
  const g = parseInt(hex.substr(2, 2), 16);
  const b = parseInt(hex.substr(4, 2), 16);
  const lightR = Math.min(255, Math.round(r * factor));
  const lightG = Math.min(255, Math.round(g * factor));
  const lightB = Math.min(255, Math.round(b * factor));
  const toHex = (n) => n.toString(16).padStart(2, '0');
  return `#${toHex(lightR)}${toHex(lightG)}${toHex(lightB)}`;
}


// -------------------------------------------------------------
// spriteKey builder (hello-stranger person.svelte line 153 + 162)
// -------------------------------------------------------------

export function buildSpriteKey(profile) {
  const smile = profile.smile ? "smile" : "";
  return `${profile.sex}_${profile.color}_${profile.num}${smile}`;
}

/**
 * Resolve sprite frames from sprites.json using profile.
 * Falls back to non-smile variant when the smile sprite doesn't exist
 * (hello-stranger only has smile variants for a few keys — see
 *  design.md §4.4).
 */
export function resolveSprite(profile, sprites) {
  let key = buildSpriteKey(profile);
  let frames = sprites[key];
  let smileFellBack = false;
  if (!frames && profile.smile) {
    // smile variant missing, drop the suffix
    key = buildSpriteKey({ ...profile, smile: false });
    frames = sprites[key];
    smileFellBack = true;
  }
  if (!frames) {
    return { key, frames: null, smileFellBack };
  }
  return { key, frames, smileFellBack };
}


// -------------------------------------------------------------
// Full profile → visual state
// -------------------------------------------------------------

/**
 * Turn a profile + palette data into every value the DOM needs.
 * This is a PURE function: no DOM, no side effects.
 *
 * v5: two-layer sprite model restored from hello-stranger.
 *   OUTER key  = sex + color + num + smile → WHICH sprite (user-locked)
 *   INNER idx  = animFrame % frames.length → WHICH FRAME of that sprite
 * The inner frames within one sprite differ only in subtle details
 * (blink, mouth, chest "breathing" symbol), so ticking animFrame gives
 * a subtle micro-animation without ever changing the figure's identity.
 *
 * @param {Object} profile    - { sex, color, num, smile, bg_level, quote }
 * @param {Object} sprites    - parsed sprites.json
 * @param {Object} colors     - parsed colors.json (with .energy used here)
 * @param {number} animFrame  - monotonically increasing tick from app.js
 */
export function renderProfile(profile, sprites, colors, animFrame = 0) {
  // 1. pick the sprite (outer key). Never changes unless the user changes
  //    sex/color/num/smile.
  const { key: spriteKey, frames, smileFellBack } = resolveSprite(profile, sprites);
  // 2. pick the frame inside that sprite (inner pointer). This is what
  //    produces the subtle animation — same sprite, different detail chars.
  const asciiArt = frames && frames.length
    ? frames[animFrame % frames.length]
    : null;

  // 2. v5: color source = palette[mood][bg_level]. mood picks a hue family
  //    (pink for playful, grey for tired, teal→indigo for chill, etc.),
  //    energy picks the brightness/saturation step inside that family.
  //    Falls back to the legacy 1D energy scale if a palette cell is missing.
  const moodKey = String(profile.mood);
  const energyKey = String(profile.bg_level);
  const paletteRow = colors.palette && colors.palette[moodKey];
  const paletteCell = paletteRow && paletteRow[energyKey];
  const legacyCell = colors.energy && colors.energy[energyKey];
  const bgHex = (paletteCell && paletteCell.hex)
    || (legacyCell && legacyCell.hex);
  if (!bgHex) {
    throw new Error(
      `colors.json missing palette[${moodKey}][${energyKey}] and no legacy energy fallback`
    );
  }

  // 3. derive three colors from the one hex (hello-stranger algorithm, unchanged).
  const personColor = darkenColor(bgHex);
  const bgColorSoft = lightenColor(bgHex);
  const darkerColor = darkestColor(bgHex);

  return {
    spriteKey,
    asciiArt,
    personColor,
    backgroundColor: bgHex,
    bgColorSoft,
    darkerColor,
    quote: (profile.quote || "").slice(0, 20),
    smileFellBack,
  };
}


// -------------------------------------------------------------
// DOM application (tiny convenience — optional to use)
// -------------------------------------------------------------

/**
 * Apply a rendered visual state to the DOM.
 * Only touches known element IDs defined in index.html.
 */
export function applyToDOM(vstate) {
  // css custom properties on :root
  const root = document.documentElement;
  root.style.setProperty("--person-color", vstate.personColor);
  root.style.setProperty("--background-color", vstate.backgroundColor);
  root.style.setProperty("--bg-color-soft", vstate.bgColorSoft);
  root.style.setProperty("--darker-color", vstate.darkerColor);

  // sprite text
  const pre = document.getElementById("avatar-pre");
  if (pre) {
    pre.textContent = vstate.asciiArt ?? "(sprite missing)";
  }

  // spriteKey label
  const label = document.getElementById("sprite-key");
  if (label) {
    label.textContent = vstate.spriteKey;
  }

  // quote bubble
  const bubble = document.getElementById("quote-bubble");
  const quoteText = document.getElementById("quote-text");
  if (bubble && quoteText) {
    if (vstate.quote) {
      quoteText.textContent = vstate.quote;
      bubble.hidden = false;
    } else {
      bubble.hidden = true;
    }
  }
}
