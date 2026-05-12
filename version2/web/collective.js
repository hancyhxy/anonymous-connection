// =============================================================
// collective.js — Stage 2 wall
//
// Loads every submitted profile, renders one tile per profile, and
// arranges them so that visually-close pairs share more interests.
//
// Layout:
//   1. Compute weighted-Jaccard similarity score for every pair
//        score = #shared_primaries * 1.0  +  #shared_subtags * 3.0
//   2. Convert to a target spring "rest length" — high score = short.
//   3. Run a tiny force-directed simulation (springs + repulsion +
//      stage-center bias + collision floor) for ~120 ticks → settle.
//   4. Apply final positions via CSS transforms; CSS transitions make
//      new arrivals glide from the center.
//   5. A sway loop perturbs each tile by ±3 px on a 4–6 s cycle.
//
// SSE:
//   /events emits a `user` event when /submit completes — we add a
//   tile, re-score, re-layout. A `clear` event wipes everything.
//
// Render reuse:
//   renderProfile() from renderer.js is pure → returns asciiArt +
//   colors + quote. We DON'T reuse applyToDOM (it writes to :root +
//   #avatar-pre, which only suits a single avatar). Instead each
//   tile gets its own scoped CSS variables and a local <pre>.
// =============================================================

import { renderProfile } from "./renderer.js";

// ----- Data load ---------------------------------------------

let sprites   = null;
let colors    = null;
let questions = null;     // not strictly needed for rendering, but
                          // kept for parity / future debug overlay
let users     = [];       // array of { id, ts, profile }
let nodes     = [];       // layout nodes parallel to users
let animTick  = 0;        // sprite-frame ticker for breathing

const STAGE = document.getElementById("stage");
const COUNT_EL = document.getElementById("count");

// All runtime-tweakable layout knobs in one place. The debug panel binds
// its sliders to these keys; relayout() is called after every change so
// the wall reflows live. Defaults below are tuned for ~12-20 profiles
// on a desktop browser (~1500×800 stage).
const LAYOUT = {
  tileSize:     110,    // px, square tile edge — 1:1, mirrors stage 1 preview
  collisionPad:   6,    // px, extra clearance beyond edge so tiles don't touch
  restBase:     360,    // px, spring rest length for unrelated pairs (score=0)
  scoreK:       0.6,    // similarity → distance falloff. 0.5–0.8 is the sweet
                        // spot: best matches obviously cluster, while mid /
                        // weak matches keep visible gradient. <0.3 looks
                        // random; >1.0 collapses everyone into a few blobs.
};
const LAYOUT_DEFAULTS = { ...LAYOUT };

// Width/height kept as named consts because force step() reads them often.
// They re-derive from LAYOUT.tileSize whenever the panel changes it.
let TILE_W = LAYOUT.tileSize;
let TILE_H = LAYOUT.tileSize;

async function loadData() {
  const [s, c, q, u] = await Promise.all([
    fetch("./sprites.json").then(r => r.json()),
    fetch("./colors.json").then(r => r.json()),
    fetch("./questions.json").then(r => r.json()),
    fetch("./api/users").then(r => r.json()),
  ]);
  sprites = s; colors = c; questions = q;
  users = u;
}

// ----- Tile rendering ---------------------------------------

// Classify a user entry into one of three submission channels. Mirror of
// _entry_source() in server.py — kept in sync so the same logic decides
// both per-source clear (server-side) and per-source hide (client-side).
function classifyTile(user) {
  if (user.is_mock) return "C";                          // Mock
  if (user.profile?.sticker !== undefined && user.profile?.sticker !== null) return "A"; // Hardware
  return "B";                                            // Scanned via wall QR
}

function ensureTile(user) {
  let el = document.getElementById(`tile-${user.id}`);
  if (el) return el;
  el = document.createElement("div");
  el.id = `tile-${user.id}`;
  el.className = "avatar-tile";
  // Tag the tile with its channel so CSS can hide it per the panel's
  // show/hide checkboxes. A=hardware, B=scanned, C=mock.
  el.setAttribute("data-source", classifyTile(user));

  const bubble = document.createElement("div");
  bubble.className = "quote-bubble";
  // text + downward tail, mirrors stage 1's #quote-bubble structure.
  bubble.innerHTML = `<span class="quote-text"></span><span class="quote-tail">▼</span>`;
  el.appendChild(bubble);

  const pre = document.createElement("pre");
  el.appendChild(pre);

  // Nickname row (v10) — sits in the slot the old interest tag row used
  // to occupy. Class name kept as `.tile-interests` so existing CSS keeps
  // working without a stylesheet rename. Interest is matching-layer only
  // and never rendered on any surface (phone preview, wall, hardware).
  const tags = document.createElement("div");
  tags.className = "tile-interests";
  el.appendChild(tags);

  STAGE.appendChild(el);
  return el;
}

// Refresh the visual contents of a tile from its profile + current
// animation tick. Sets per-tile CSS vars so each tile has its own
// person/background colors without polluting :root.
function paintTile(user) {
  const el = ensureTile(user);
  const v  = renderProfile(user.profile, sprites, colors, animTick);
  el.style.setProperty("--person-color",     v.personColor);
  el.style.setProperty("--bg-color-soft",    v.bgColorSoft);
  el.style.setProperty("--darker-color",     v.darkerColor);
  // background is the soft tint; border the darker variant
  const pre = el.querySelector("pre");
  pre.textContent = v.asciiArt || "";
  const bubble = el.querySelector(".quote-bubble");
  const bubbleText = bubble.querySelector(".quote-text");
  if (v.quote && v.quote.trim()) {
    bubbleText.textContent = v.quote;
    bubble.removeAttribute("data-empty");
  } else {
    bubbleText.textContent = "";
    bubble.setAttribute("data-empty", "1");
  }
  // Bottom row of each tile (v10): nickname if set, else blank.
  // Replaces the old interest tag row — interest is matching-layer
  // only and is intentionally invisible on every surface.
  paintTileNickname(el, user);
}

// Render user.profile.nickname (if any) in the slot the old interest
// tag row used to occupy. Blank/missing nickname → empty + data-empty
// attribute (consumed by CSS to hide / collapse the row).
function paintTileNickname(el, user) {
  const tags = el.querySelector(".tile-interests");
  if (!tags) return;
  const nick = (user.profile?.nickname ?? "").trim();
  if (!nick) {
    tags.innerHTML = "";
    tags.setAttribute("data-empty", "1");
    return;
  }
  const safe = nick.replace(/[&<>]/g, c => ({ "&":"&amp;", "<":"&lt;", ">":"&gt;" }[c]));
  tags.innerHTML = `<span class="tile-nickname">${safe}</span>`;
  tags.removeAttribute("data-empty");
}

function paintAll() {
  for (const u of users) paintTile(u);
}

// ----- Similarity score ---------------------------------------

function similarity(a, b) {
  // Primary tags: shared count, ignores order.
  const aPrim = new Set(a.profile?.interests ?? []);
  const bPrim = new Set(b.profile?.interests ?? []);
  let primaryShared = 0;
  for (const v of aPrim) if (bPrim.has(v)) primaryShared++;

  // Subtags: count matches per-primary, then sum.
  const aDet = a.profile?.interest_details ?? {};
  const bDet = b.profile?.interest_details ?? {};
  let subShared = 0;
  for (const k of Object.keys(aDet)) {
    const aSet = new Set(aDet[k] ?? []);
    const bArr = bDet[k] ?? [];
    for (const sub of bArr) if (aSet.has(sub)) subShared++;
  }

  return primaryShared * 1.0 + subShared * 3.0;
}

// ----- Force-directed layout ---------------------------------

// Constants that aren't on the panel (tuned once, mostly fine).
const STAGE_PAD      = 80;
const SPRING_K       = 0.04;
const REPEL_K        = 26000;
const CENTER_PULL    = 0.012;
const ITER_PER_FRAME = 1;
const TOTAL_ITERS    = 220;

// Derived from LAYOUT.* — recomputed on every layout cycle so panel
// changes take effect on the next relayout() call.
function collisionR() { return TILE_W + LAYOUT.collisionPad; }
function restMin()    { return collisionR(); }
// Spring base auto-expands so unrelated pairs always have ~1.8 tiles of
// edge-to-edge breathing room, REGARDLESS of tile size. A flat "+200"
// cushion looked OK at tile=110 but vanishes at tile=200 (the cushion
// becomes smaller than a tile width), at which point the sim can't find
// a stable layout and slams tiles to the canvas edges. Scaling the
// cushion with TILE_W keeps the relative spacing invariant.
function restBase()   { return Math.max(restMin() + TILE_W * 1.8, LAYOUT.restBase); }

function ensureNodes() {
  // Add nodes for newly-arrived users; preserve positions for existing.
  const w = STAGE.clientWidth, h = STAGE.clientHeight;
  const cx = w / 2, cy = h / 2;
  const existing = new Map(nodes.map(n => [n.id, n]));
  nodes = users.map(u => {
    const prev = existing.get(u.id);
    if (prev) return prev;
    // New arrivals start at center → glide outward via CSS transition.
    return {
      id: u.id,
      x: cx + (Math.random() - 0.5) * 40,
      y: cy + (Math.random() - 0.5) * 40,
      vx: 0, vy: 0,
    };
  });
}

function step() {
  const w = STAGE.clientWidth, h = STAGE.clientHeight;
  const cx = w / 2, cy = h / 2;
  const n = nodes.length;

  // Snapshot derived layout values once per step (avoid recomputing inside
  // the n² inner loop).
  const cR    = collisionR();
  const rMin  = restMin();
  const rBase = restBase();
  const k     = LAYOUT.scoreK;

  // Pairwise springs (target length depends on similarity) + repulsion.
  for (let i = 0; i < n; i++) {
    const ni = nodes[i];
    for (let j = i + 1; j < n; j++) {
      const nj = nodes[j];
      const dx = nj.x - ni.x;
      const dy = nj.y - ni.y;
      let dist = Math.hypot(dx, dy);
      if (dist < 0.01) dist = 0.01;

      const score = similarity(users[i], users[j]);
      // Smooth exponential gradient: rest decays from rBase (score=0) toward
      // rMin (score→∞). Keeps the gradient visible across the whole range
      // instead of clamping every "kinda similar" pair to rMin.
      const rest = rMin + (rBase - rMin) * Math.exp(-score * k);
      const spring = SPRING_K * (dist - rest);
      const repel  = -REPEL_K / (dist * dist);

      // Hard collision floor — once two tiles cross cR, separate them
      // IMMEDIATELY by half the overlap each, before applying soft forces.
      // Guarantees no visual overlap regardless of computed attraction.
      if (dist < cR) {
        const push = (cR - dist) * 0.5;
        const ux = dx / dist, uy = dy / dist;
        ni.x -= ux * push; ni.y -= uy * push;
        nj.x += ux * push; nj.y += uy * push;
        ni.vx -= ni.vx * 0.4; ni.vy -= ni.vy * 0.4;
        nj.vx -= nj.vx * 0.4; nj.vy -= nj.vy * 0.4;
        continue;
      }

      const fx = (spring + repel) * (dx / dist);
      const fy = (spring + repel) * (dy / dist);
      ni.vx += fx; ni.vy += fy;
      nj.vx -= fx; nj.vy -= fy;
    }
  }

  // Center pull + integrate + clamp to stage bounds.
  for (const node of nodes) {
    node.vx += (cx - node.x) * CENTER_PULL;
    node.vy += (cy - node.y) * CENTER_PULL;
    node.vx *= 0.55;            // damping
    node.vy *= 0.55;
    node.x += node.vx;
    node.y += node.vy;
    // hard clamp so a tile never escapes the visible area
    node.x = Math.max(STAGE_PAD, Math.min(w - STAGE_PAD, node.x));
    node.y = Math.max(STAGE_PAD, Math.min(h - STAGE_PAD, node.y));
  }
}

function settle(iters = TOTAL_ITERS) {
  for (let i = 0; i < iters; i++) step();
}

// Centroid correction — translates the whole swarm so its centroid lands
// at the canvas center. Force-directed layouts settle to *some* stable
// state but it's rarely the centered one (random init + nonlinear forces).
// Decoupling "where each tile is relative to the others" (force solves
// that) from "where the group sits on the canvas" (we solve here) means
// the user can crank rest-base / score-weight to extremes and still see
// a balanced composition.
function recenterCluster() {
  if (nodes.length === 0) return;
  const w = STAGE.clientWidth, h = STAGE.clientHeight;
  let sx = 0, sy = 0;
  for (const n of nodes) { sx += n.x; sy += n.y; }
  const dx = w / 2 - sx / nodes.length;
  const dy = h / 2 - sy / nodes.length;
  for (const n of nodes) {
    n.x = Math.max(STAGE_PAD, Math.min(w - STAGE_PAD, n.x + dx));
    n.y = Math.max(STAGE_PAD, Math.min(h - STAGE_PAD, n.y + dy));
  }
}

function applyPositions() {
  for (let i = 0; i < users.length; i++) {
    const el = document.getElementById(`tile-${users[i].id}`);
    if (!el) continue;
    const n = nodes[i];
    // sway target written separately in swayLoop; here we set base.
    el._baseX = n.x - TILE_W / 2;
    el._baseY = n.y - TILE_H / 2;
    el.style.transform = `translate(${el._baseX}px, ${el._baseY}px)`;
  }
}

// Push the current LAYOUT.tileSize into the CSS variable (so all
// tile interior elements rescale via calc()) AND into the JS-side
// TILE_W/H (force step reads them every frame).
function applyTileSize() {
  TILE_W = LAYOUT.tileSize;
  TILE_H = LAYOUT.tileSize;
  document.documentElement.style.setProperty("--tile-size", `${LAYOUT.tileSize}px`);
}

// Re-do everything: paint, allocate nodes, settle, place. Called on
// initial load AND on each new arrival.
function relayout() {
  applyTileSize();
  ensureNodes();
  paintAll();
  settle();
  recenterCluster();
  applyPositions();
  COUNT_EL.textContent = String(users.length);
}

// ----- Debug panel — bind sliders to LAYOUT, relayout on input ----

const DEBUG_PANEL_KEY = "xy_debug_panel_v1";

// Panel state that doesn't belong to LAYOUT: mock count + 3 per-channel
// visibility flags. The QR modal is its own ephemeral state, not
// persisted (closing it should always be fresh next visit).
const MOCK = {
  count:        25,
  showHardware: true,
  showScanned:  true,
  showMock:     true,
};

function loadPanelState() {
  try {
    const raw = localStorage.getItem(DEBUG_PANEL_KEY);
    if (!raw) return;
    const saved = JSON.parse(raw);
    for (const k of Object.keys(LAYOUT_DEFAULTS)) {
      if (typeof saved[k] === "number") LAYOUT[k] = saved[k];
    }
    // Mock-section restore. Each key is optional so older saves still work.
    if (typeof saved._mockCount    === "number")  MOCK.count        = saved._mockCount;
    if (typeof saved._showHardware === "boolean") MOCK.showHardware = saved._showHardware;
    if (typeof saved._showScanned  === "boolean") MOCK.showScanned  = saved._showScanned;
    if (typeof saved._showMock     === "boolean") MOCK.showMock     = saved._showMock;
    // Optional collapsed flag
    if (saved._collapsed) {
      document.getElementById("debug-panel")?.classList.add("collapsed");
    }
  } catch (_) {}
}

function savePanelState() {
  try {
    const collapsed = document.getElementById("debug-panel")?.classList.contains("collapsed");
    localStorage.setItem(DEBUG_PANEL_KEY, JSON.stringify({
      ...LAYOUT,
      _collapsed:     !!collapsed,
      _mockCount:     MOCK.count,
      _showHardware:  MOCK.showHardware,
      _showScanned:   MOCK.showScanned,
      _showMock:      MOCK.showMock,
    }));
  } catch (_) {}
}

// Apply per-channel visibility to <body> via CSS classes. Single
// chokepoint so toggle handlers and the initial restore-from-localStorage
// path both route through one function.
function applyChannelFilters() {
  document.body.classList.toggle("hide-hardware", !MOCK.showHardware);
  document.body.classList.toggle("hide-scanned",  !MOCK.showScanned);
  document.body.classList.toggle("hide-mock",     !MOCK.showMock);
}

// Show/hide the join-QR modal. Pure DOM toggle — no persistence.
function setQROverlay(visible) {
  const overlay = document.getElementById("qr-overlay");
  if (overlay) overlay.toggleAttribute("hidden", !visible);
}

// ----- Mock-seed (in-page, no shell script needed) ---------------
//
// POSTs N synthetic profiles to /submit so they take the exact same
// path real submissions take (validation, SSE broadcast, etc.). Each
// is flagged is_mock=true so the wall can later filter them out.
// Sticker numbers are NEVER attached — synthetic data is wall-only
// and must not overlay STAGE3_USERS[1..5] (those slots belong to the
// physical boards).

const MOCK_PRIMARIES = [
  "music", "film", "sport", "art", "tech", "food",
  "gaming", "travel", "books", "photo", "dance", "pets",
];
const MOCK_SUBTAGS = {
  music:  ["jazz","classical","pop","electronic","rock","hip-hop","lo-fi","indie"],
  film:   ["sci-fi","drama","comedy","documentary","horror","animation","art-house","action"],
  sport:  ["running","yoga","cycling","swimming","climbing","team-sport","martial-arts","skating"],
  art:    ["painting","sculpture","digital","illustration","street-art","ceramics","design","collage"],
  tech:   ["coding","ai","hardware","gaming-tech","web","robotics","data","open-source"],
  food:   ["cooking","baking","asian","italian","vegan","street-food","coffee","desserts"],
  gaming: ["rpg","fps","indie-games","puzzle","strategy","co-op","retro","mobile"],
  travel: ["solo","city","nature","road-trip","backpacking","food-tour","festival","remote"],
  books:  ["fiction","nonfiction","poetry","sci-fi","mystery","philosophy","manga","essays"],
  photo:  ["portrait","street","landscape","film-photo","macro","night","phone","documentary"],
  dance:  ["ballet","hip-hop","contemporary","salsa","swing","k-pop","club","freestyle"],
  pets:   ["dogs","cats","birds","fish","reptiles","rabbits","exotic","farm-animals"],
};
const MOCK_QUOTES = [
  "need coffee","say hi","want quiet","open to chat",
  "new here","tired","daydreaming","same vibe?",
  "running late","warm hands","low battery","loud thoughts",
];
const MOCK_NICKS = [
  "ik","mei","ren","jo","kai","lin","ari","sam","tay","rio","ona","elf",
  "moss","wren","pip","halo","neon","echo","june","syd","noor","rae","bo","ivy","zev",
];

function pick(arr)       { return arr[Math.floor(Math.random() * arr.length)]; }
function pickN(arr, n)   {
  // sample n unique entries without replacement
  const copy = arr.slice();
  const out = [];
  for (let i = 0; i < n && copy.length; i++) {
    const idx = Math.floor(Math.random() * copy.length);
    out.push(copy.splice(idx, 1)[0]);
  }
  return out;
}

function randomMockProfile() {
  const nP = 1 + Math.floor(Math.random() * 3);   // 1..3 primaries
  const primaries = pickN(MOCK_PRIMARIES, nP);
  const interest_details = {};
  for (const p of primaries) {
    const nSub = Math.floor(Math.random() * 3);   // 0..2 sub-tags per primary
    if (nSub > 0) interest_details[p] = pickN(MOCK_SUBTAGS[p], nSub);
    else          interest_details[p] = [];
  }
  return {
    sex:              pick(["male", "female"]),
    color:            Math.floor(Math.random() * 5),    // 0..4
    num:              Math.floor(Math.random() * 3),    // 0..2
    smile:            false,
    mood:             Math.floor(Math.random() * 5),    // 0..4
    bg_level:         Math.floor(Math.random() * 6),    // 0..5
    quote:            pick(MOCK_QUOTES),
    nickname:         pick(MOCK_NICKS),
    interests:        primaries,
    interest_details,
    is_mock:          true,
  };
}

async function seedMockProfiles(n) {
  const count = Math.max(1, Math.min(100, n | 0));
  const button = document.getElementById("debug-seed-mock");
  const original = button ? button.textContent : null;
  if (button) { button.disabled = true; button.textContent = `seeding 0/${count}…`; }
  let ok = 0;
  for (let i = 1; i <= count; i++) {
    try {
      const res = await fetch("/submit", {
        method:  "POST",
        headers: { "Content-Type": "application/json" },
        body:    JSON.stringify(randomMockProfile()),
      });
      if (res.ok) ok++;
    } catch (_) { /* ignore individual failures */ }
    if (button) button.textContent = `seeding ${i}/${count}…`;
  }
  if (button) {
    button.disabled = false;
    button.textContent = original ?? "seed mock";
  }
  console.log(`[mock] seeded ${ok}/${count}`);
}

function refreshPanelDisplay() {
  for (const k of Object.keys(LAYOUT_DEFAULTS)) {
    const input = document.getElementById(`t-${k}`);
    const valEl = document.getElementById(`v-${k}`);
    if (input) input.value = String(LAYOUT[k]);
    if (valEl) valEl.textContent = (k === "scoreK")
      ? Number(LAYOUT[k]).toFixed(2)
      : String(LAYOUT[k]);
  }
  // derived: collision-floor = tile + pad
  const cR = document.getElementById("v-collisionR");
  if (cR) cR.textContent = String(LAYOUT.tileSize + LAYOUT.collisionPad);
  // derived: effective rest base (after the floor+200 clamp)
  const rb = document.getElementById("v-restBaseEff");
  if (rb) rb.textContent = String(restBase());
}

function wireDebugPanel() {
  const panel = document.getElementById("debug-panel");
  if (!panel) return;

  // Each slider: input → coerce → write LAYOUT → re-relax → save.
  for (const k of Object.keys(LAYOUT_DEFAULTS)) {
    const input = document.getElementById(`t-${k}`);
    if (!input) continue;
    input.addEventListener("input", () => {
      LAYOUT[k] = (k === "scoreK") ? parseFloat(input.value) : parseInt(input.value, 10);
      refreshPanelDisplay();
      relayout();
      savePanelState();
    });
  }

  document.getElementById("debug-reset")?.addEventListener("click", () => {
    Object.assign(LAYOUT, LAYOUT_DEFAULTS);
    refreshPanelDisplay();
    relayout();
    savePanelState();
  });

  // Server-side wipe. POST /admin/clear is localhost-only on the backend,
  // so this works from the wall machine but not from any phone on the
  // LAN — exactly what we want during a multi-person test session.
  // SSE 'clear' event then re-broadcasts to all connected clients
  // (including this one) so the UI empties via the existing path.
  document.getElementById("debug-clear-data")?.addEventListener("click", async () => {
    const n = users.length;
    if (n === 0) return;
    if (!confirm(`clear all ${n} profile(s)?`)) return;
    try {
      const res = await fetch("/admin/clear", { method: "POST" });
      if (!res.ok) throw new Error(`server ${res.status}`);
      // SSE 'clear' handler in startEventStream() empties users/nodes/STAGE.
    } catch (err) {
      alert(`clear failed: ${err.message}`);
    }
  });

  // ----- Mock section wires --------------------------------------

  // Per-channel show/hide checkboxes. Three peers: hardware (A) /
  // scanned (B) / mock (C). Each toggle flips a body class; CSS does
  // the actual hiding via `body.hide-X .avatar-tile[data-source=Y]`.
  const wireChannelCb = (id, key) => {
    const cb = document.getElementById(id);
    if (!cb) return;
    cb.checked = MOCK[key];
    cb.addEventListener("change", () => {
      MOCK[key] = cb.checked;
      applyChannelFilters();
      savePanelState();
    });
  };
  wireChannelCb("t-showHardware", "showHardware");
  wireChannelCb("t-showScanned",  "showScanned");
  wireChannelCb("t-showMock",     "showMock");

  // Per-channel clear buttons — wipe one source, leave others intact.
  // POST /admin/clear?source=X. SSE 'remove' events come back and the
  // UI drops those tiles via the existing handler.
  const wireChannelClear = (id, source, predicate) => {
    document.getElementById(id)?.addEventListener("click", async () => {
      const n = users.filter(predicate).length;
      if (n === 0) return;
      if (!confirm(`clear ${n} ${source} tile(s)?`)) return;
      try {
        const res = await fetch(`/admin/clear?source=${source}`, { method: "POST" });
        if (!res.ok) throw new Error(`server ${res.status}`);
      } catch (err) {
        alert(`clear ${source} failed: ${err.message}`);
      }
    });
  };
  wireChannelClear("debug-clear-hardware", "hardware",
    u => !u.is_mock && u.profile?.sticker !== undefined && u.profile?.sticker !== null);
  wireChannelClear("debug-clear-scanned", "scanned",
    u => !u.is_mock && (u.profile?.sticker === undefined || u.profile?.sticker === null));
  wireChannelClear("debug-clear-mock", "mock",
    u => !!u.is_mock);

  // Mock count input — persist on each edit so the next page load
  // remembers the demoer's preferred crowd size.
  const mockCountInput = document.getElementById("t-mockCount");
  if (mockCountInput) {
    mockCountInput.value = String(MOCK.count);
    mockCountInput.addEventListener("input", () => {
      const n = parseInt(mockCountInput.value, 10);
      if (Number.isFinite(n)) MOCK.count = Math.max(1, Math.min(100, n));
      savePanelState();
    });
  }

  // Seed-mock button — POSTs N synthetic profiles. SSE 'user' events
  // come back through the existing broadcast path so the wall fills
  // up tile-by-tile rather than all at once (visible "loading" feel).
  document.getElementById("debug-seed-mock")?.addEventListener("click", () => {
    seedMockProfiles(MOCK.count);
  });

  // Show-QR button — opens the center-screen QR modal. Three dismiss
  // paths (× button, backdrop click, ESC key) wired below so a stuck
  // operator can always close the overlay.
  document.getElementById("debug-show-qr")?.addEventListener("click", () => {
    setQROverlay(true);
  });
  document.getElementById("qr-close")?.addEventListener("click", (e) => {
    e.stopPropagation();
    setQROverlay(false);
  });
  document.getElementById("qr-overlay")?.addEventListener("click", (e) => {
    // close only when the click landed on the backdrop, not the card itself
    const card = document.querySelector(".qr-card-large");
    if (card && card.contains(e.target)) return;
    setQROverlay(false);
  });
  document.addEventListener("keydown", (e) => {
    if (e.key !== "Escape") return;
    const overlay = document.getElementById("qr-overlay");
    if (overlay && !overlay.hidden) setQROverlay(false);
  });

  // Apply initial channel-filter state from restored panel state. Has to
  // come after the checkboxes have been wired so their .checked attribute
  // is in sync with MOCK.* on first paint.
  applyChannelFilters();

  // Header click toggles collapsed state.
  panel.querySelector(".debug-header")?.addEventListener("click", (e) => {
    // ignore if the click was on the toggle button itself (it'd toggle twice)
    if (e.target.id === "debug-toggle") return;
    panel.classList.toggle("collapsed");
    savePanelState();
  });
  document.getElementById("debug-toggle")?.addEventListener("click", () => {
    panel.classList.toggle("collapsed");
    savePanelState();
  });

  refreshPanelDisplay();
}

// ----- Sway (per-tile small breathing offset) -----------------

function swayLoop(now) {
  for (let i = 0; i < users.length; i++) {
    const el = document.getElementById(`tile-${users[i].id}`);
    if (!el || el._baseX == null) continue;
    // unique phase per tile, independent x/y frequencies → organic feel
    const seed = i * 1.7;
    const sx = Math.sin(now / 1800 + seed)       * 3;
    const sy = Math.cos(now / 2200 + seed * 1.3) * 3;
    el.style.transform = `translate(${el._baseX + sx}px, ${el._baseY + sy}px)`;
  }
  requestAnimationFrame(swayLoop);
}

// ----- Animation tick (drives sprite frame breathing) ---------

const ANIM_INTERVAL_MS = 200;
function startSpriteAnimation() {
  setInterval(() => {
    animTick = (animTick + 1) >>> 0;
    paintAll();
  }, ANIM_INTERVAL_MS);
}

// ----- SSE (live-add new submissions) -------------------------

function startEventStream() {
  const es = new EventSource("./events");
  es.addEventListener("user", (e) => {
    try {
      const entry = JSON.parse(e.data);
      // Dedup against id in case we already loaded it from /api/users.
      if (users.some(u => u.id === entry.id)) return;
      users.push(entry);
      relayout();
    } catch (err) {
      console.warn("[collective] bad user event:", err);
    }
  });
  es.addEventListener("clear", () => {
    users = [];
    nodes = [];
    STAGE.innerHTML = "";
    COUNT_EL.textContent = "0";
  });
  // A `remove` event fires when /submit dedups on sticker number — the
  // server has already removed the prior entry from users.json and is
  // telling us to drop its tile. Payload: {id}. We splice both arrays
  // and remove the DOM tile; the new entry arrives in a follow-up
  // `user` event and gets laid out normally via relayout().
  es.addEventListener("remove", (e) => {
    try {
      const { id } = JSON.parse(e.data);
      const idx = users.findIndex(u => u.id === id);
      if (idx === -1) return;
      users.splice(idx, 1);
      nodes.splice(idx, 1);
      const el = document.getElementById(`tile-${id}`);
      if (el) el.remove();
      COUNT_EL.textContent = String(users.length);
    } catch (err) {
      console.warn("[collective] bad remove event:", err);
    }
  });
  es.onerror = () => {
    // browser auto-retries; nothing to do
  };
}

// ----- Resize handling ----------------------------------------

let resizeTimer = null;
window.addEventListener("resize", () => {
  clearTimeout(resizeTimer);
  resizeTimer = setTimeout(() => {
    settle(60);
    applyPositions();
  }, 120);
});

// ----- Boot ---------------------------------------------------

async function main() {
  try {
    // Show the mock-mode QR card if the URL says ?mock=1. Two wall modes
    // share one HTML file: real demo (no QR; physical-board paper QRs
    // own the entry) vs mock demo (wall QR visible for the tutor to
    // scan without overlaying any sticker 1..5 slot). Bare URL = wall-
    // only profile, intentionally.
    if (new URLSearchParams(location.search).get("mock") === "1") {
      document.getElementById("mock-qr-card")?.removeAttribute("hidden");
    }

    // Restore previously-tweaked LAYOUT values BEFORE first relayout, so
    // the wall opens with the user's last-known good settings.
    loadPanelState();
    await loadData();
    wireDebugPanel();   // also reflects restored LAYOUT into slider DOM
    relayout();
    startSpriteAnimation();
    requestAnimationFrame(swayLoop);
    startEventStream();
  } catch (err) {
    console.error("[collective] boot failed:", err);
    document.body.innerHTML =
      `<pre style="color:#e87b7b;padding:24px">collective view boot failed:\n${err.message}</pre>`;
  }
}

main();
