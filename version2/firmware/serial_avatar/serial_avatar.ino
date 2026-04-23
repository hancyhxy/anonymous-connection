// serial_avatar.ino  (v3 — precomputed glyph bitmap rendering)
// Target: Waveshare ESP32-C6-LCD-1.3 (ST7789V2, 240x240)
//
// Receives avatar state from the web UI over USB Serial and renders:
//   1. background color derived from palette[mood][energy]
//   2. the full 18x18 sprite from sprites.json (8x16 Unifont bitmaps, per-cell)
//   3. the quote bubble near the top
//   4. a subtle in-sprite micro-animation (166ms/frame) identical to Web
//
// Protocol (one JSON object per line, '\n' terminated):
//   {"mood":2,"energy":4,"sex":"male","color":0,"num":1,"smile":false,"quote":"new here"}
//
// Key architectural notes:
//  - sprites.h is auto-generated from web/sprites.json by
//    scripts/sprites_to_header.py. Regenerate after any sprite change.
//  - glyphs.h is auto-generated from GNU Unifont by
//    scripts/glyphs_to_bitmap_header.py. Regenerate if sprite characters
//    change. ~3 KB of PROGMEM data (vs ~317 KB for u8g2_font_unifont_*).
//  - Rendering uses drawBitmap() from a precomputed codepoint→8x16 table —
//    no U8g2 runtime dependency, no "font missing glyph" failure modes.
//  - Colors are computed identically to web/renderer.js (darken / lighten /
//    darkest), so device and Web always agree.

#include <Arduino_GFX_Library.h>
#include <ArduinoJson.h>

#include "sprites.h"
#include "glyphs.h"


// ---- Types -----------------------------------------------------------------
// Declared BEFORE any function that references them, to sidestep Arduino
// IDE's auto-prototype generator. (The preprocessor inserts forward decls
// for every top-level function before any user code, and those decls would
// reference AvatarState before this struct is visible if we put it later.)
// Interest tag — one row of "icon label" badges renders below the sprite.
// Web caps Q_INTERESTS at 3 selections, so a fixed 3-slot array is enough
// (no heap, no growth). Both fields are UTF-8: icon is a single Unicode
// glyph (e.g. "♪"), label is a short ASCII word (e.g. "music").
#define MAX_INTERESTS 3
struct InterestTag {
  String icon;
  String label;
};

struct AvatarState {
  int          mood;
  int          energy;
  String       sex;
  int          color;
  int          num;
  bool         smile;
  String       quote;
  InterestTag  interests[MAX_INTERESTS];
  int          interest_count;
  bool         valid;
};


// ---- LCD pin config (verified in smoke_test.ino) ---------------------------
#define TFT_SCK   7
#define TFT_MOSI  6
#define TFT_CS    14
#define TFT_DC    15
#define TFT_RST   21
#define TFT_BL    22

#define LCD_W     240
#define LCD_H     240
#define ROTATION  0

// ---- Heart rate sensor (3-wire analog pulse sensor) ------------------------
// Wired:  VCC→3V3, GND→GND, S→GPIO3.
// GPIO3 is ADC1_CH3 on ESP32-C6 — clean ADC pin (1/2 are strapping-adjacent).
#define HR_PIN          3
#define HR_SAMPLE_MS    20      // 50 Hz sampling
#define HR_REPORT_MS    1000    // upstream report cadence
#define HR_BASELINE_N   100     // ~2 s window for DC-offset estimate
#define HR_RISE_THRESH  40      // ADC counts above baseline → peak candidate
                                // (tuned down from 80 — 80 was too strict for
                                // a finger pressed firmly, where pulse amplitude
                                // can drop below 60 counts)
#define HR_DEBUG        1       // 1 = also log raw ADC + baseline at 5 Hz so
                                // the threshold can be tuned by eyeball
#define IPS_MODE  true

Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *gfx = new Arduino_ST7789(bus, TFT_RST, ROTATION, IPS_MODE, LCD_W, LCD_H, 0, 0, 0, 0);


// ---- Sprite layout ---------------------------------------------------------
// Layout: top quote bubble (single line) → sprite → bottom interest tag row.
// Mirrors the web preview composition (quote above, avatar middle, tags below).
//
// Source 18×18 → cropped to 13×16 (cols 2..14, rows 2..17). The bottom
// two rows (16, 17) are preserved because for many poses (e.g.
// female_1_0) they carry the densely-drawn legs/ground — cropping them
// leaves the avatar looking like a floating head. Top row 0–1 stay
// cropped since most sprites barely use them.
//
// Per-cell: 11 px wide × 11 px tall. Unifont's native 8×16 glyph is
// clipped to 11 rows — bottom 5 rows of the glyph are descender/padding,
// safe to drop.
//
// Grid: 13×11 = 143 wide, 16×11 = 176 tall → centered, top at y=32.
#define SPRITE_COL_OFFSET 2
#define SPRITE_ROW_OFFSET 2
#define SPRITE_COLS 13
#define SPRITE_ROWS 16

#define CELL_W      11
#define CELL_H      11

#define SPRITE_PX_W (CELL_W * SPRITE_COLS)    // 143
#define SPRITE_PX_H (CELL_H * SPRITE_ROWS)    // 176

// Vertical layout (240 px LCD):
//   y=  6..28   quote bubble (single line, 22 px) + tail to y=32
//   y= 32..208  sprite (176 px)
//   y=212..232  interest tag row (~20 px)
// Quote is capped to a single line on web (maxlength=14), which freed
// the headroom that previously held a 2-line bubble (BUBBLE_BOTTOM=48).
#define SPRITE_ORIGIN_X ((LCD_W - SPRITE_PX_W) / 2)           // 48
#define SPRITE_ORIGIN_Y 32                                    // sprite top

// Animation cadence — matches Web ANIM_INTERVAL_MS.
#define ANIM_INTERVAL_MS 166


// ---- Palette (mirror of web/colors.json palette[mood][energy]) -------------
const uint32_t PALETTE[5][6] PROGMEM = {
  { 0x3d4a2a, 0x5a6b38, 0x7a8a48, 0x8aa84a, 0x9ec040, 0xa8c828 }, // chill
  { 0x2e6670, 0x3e8a98, 0x4faec0, 0x42a8d8, 0x2f95e5, 0x1a7ff0 }, // curious
  { 0x6e3b78, 0x9b4aa5, 0xc865c8, 0xe870c0, 0xf95ca8, 0xff3d8f }, // playful
  { 0x4a4a4a, 0x666663, 0x85837e, 0x9a968d, 0xa8a498, 0xb5b0a0 }, // tired
  { 0x8e6420, 0xb48438, 0xd8a04a, 0xecb548, 0xf8c93d, 0xffd92a }  // glowing
};


// ---- Color helpers (ported from hello-stranger renderer.js) ----------------
uint16_t hexToRgb565(uint32_t hex) {
  uint8_t r = (hex >> 16) & 0xFF;
  uint8_t g = (hex >>  8) & 0xFF;
  uint8_t b =  hex        & 0xFF;
  return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

uint32_t darkenHex(uint32_t hex) {
  int32_t r = (hex >> 16) & 0xFF;
  int32_t g = (hex >>  8) & 0xFF;
  int32_t b =  hex        & 0xFF;
  int32_t mx = max(r, max(g, b));
  int32_t dr = (r * (30 + (r == mx ? 20 : 0))) / 100;
  int32_t dg = (g * (30 + (g == mx ? 20 : 0))) / 100;
  int32_t db = (b * (30 + (b == mx ? 20 : 0))) / 100;
  return ((uint32_t)dr << 16) | ((uint32_t)dg << 8) | (uint32_t)db;
}

uint32_t lightenHex(uint32_t hex) {
  int32_t r = (hex >> 16) & 0xFF;
  int32_t g = (hex >>  8) & 0xFF;
  int32_t b =  hex        & 0xFF;
  int32_t lr = min((int32_t)255, (r * 130) / 100);
  int32_t lg = min((int32_t)255, (g * 130) / 100);
  int32_t lb = min((int32_t)255, (b * 130) / 100);
  return ((uint32_t)lr << 16) | ((uint32_t)lg << 8) | (uint32_t)lb;
}


// ---- App state -------------------------------------------------------------
// Struct type defined near the top of the file (before Arduino IDE's
// auto-prototype injection point). Here we only instantiate the singleton.
AvatarState st = { 2, 2, "male", 0, 0, false, "", {}, 0, false };

String lineBuf;
uint32_t animFrame = 0;
uint32_t lastAnimMs = 0;

// Outer-layer change flag — set when sex/color/num/smile or mood/energy or
// quote changes. Forces a full redraw (background + sprite + quote).
// Inner-frame ticks only redraw the sprite glyphs.
bool outerDirty = true;

// ---- Heart rate state -------------------------------------------------------
// Simple peak-detect over the analog pulse waveform. We track a rolling
// baseline (DC offset, drifts with finger pressure / ambient light) and
// declare a beat each time the signal crosses (baseline + HR_RISE_THRESH)
// from below. The last 3 beat intervals are averaged → BPM.
//
// Why this and not a library: the 3-wire pulse sensor outputs raw analog;
// libraries like PulseSensor expect a specific Arduino timer ISR setup that
// doesn't translate cleanly to ESP32-C6. A 50 Hz polled detector is good
// enough for 40-180 BPM range.
uint32_t hr_lastSampleMs = 0;
uint32_t hr_lastReportMs = 0;
int32_t  hr_baselineSum  = 0;       // running sum for baseline window
int      hr_baselineN    = 0;
int      hr_baseline     = 2048;    // mid-rail until we have data
bool     hr_above        = false;   // currently above threshold?
uint32_t hr_lastBeatMs   = 0;
uint32_t hr_intervals[3] = {0, 0, 0};
int      hr_intervalIdx  = 0;
int      hr_intervalCount = 0;
int      hr_currentBpm   = 0;


// ---- Sprite lookup ---------------------------------------------------------
// Build the key "sex_color_num[smile]" and linear-search SPRITES table.
// With 36 entries this is ~microseconds — no need for a hash map.
const SpriteEntry* findSprite(const AvatarState& s) {
  char key[24];
  snprintf(key, sizeof(key), "%s_%d_%d%s",
           s.sex.c_str(), s.color, s.num, s.smile ? "smile" : "");
  for (uint16_t i = 0; i < SPRITES_COUNT; i++) {
    if (strcmp(key, SPRITES[i].key) == 0) return &SPRITES[i];
  }
  // Smile fallback — drop the suffix if the smile variant is missing.
  if (s.smile) {
    snprintf(key, sizeof(key), "%s_%d_%d", s.sex.c_str(), s.color, s.num);
    for (uint16_t i = 0; i < SPRITES_COUNT; i++) {
      if (strcmp(key, SPRITES[i].key) == 0) return &SPRITES[i];
    }
  }
  return nullptr;
}


// ---- Glyph lookup (the heart of the new rendering path) --------------------
// Decode one UTF-8 codepoint starting at `s`, writing the number of bytes
// consumed into *consumed. Malformed sequences return U+FFFD and skip 1 byte
// so rendering never stalls on bad data.
uint32_t decodeUtf8(const char* s, size_t* consumed) {
  const uint8_t c0 = (uint8_t)s[0];
  if (c0 == 0) { *consumed = 0; return 0; }
  if (c0 < 0x80)               { *consumed = 1; return c0; }
  if ((c0 & 0xE0) == 0xC0 && s[1]) {
    *consumed = 2;
    return ((uint32_t)(c0 & 0x1F) << 6) | ((uint8_t)s[1] & 0x3F);
  }
  if ((c0 & 0xF0) == 0xE0 && s[1] && s[2]) {
    *consumed = 3;
    return ((uint32_t)(c0 & 0x0F) << 12)
         | ((uint32_t)((uint8_t)s[1] & 0x3F) << 6)
         | ((uint8_t)s[2] & 0x3F);
  }
  if ((c0 & 0xF8) == 0xF0 && s[1] && s[2] && s[3]) {
    *consumed = 4;
    return ((uint32_t)(c0 & 0x07) << 18)
         | ((uint32_t)((uint8_t)s[1] & 0x3F) << 12)
         | ((uint32_t)((uint8_t)s[2] & 0x3F) << 6)
         | ((uint8_t)s[3] & 0x3F);
  }
  *consumed = 1;
  return 0xFFFD;
}

// Binary search GLYPH_CODEPOINTS[] — ascending-sorted, so O(log N).
// Returns the index into GLYPH_BITMAPS, or -1 if the codepoint is absent.
int glyphIndex(uint32_t cp) {
  int lo = 0, hi = (int)GLYPH_COUNT - 1;
  while (lo <= hi) {
    int mid = (lo + hi) >> 1;
    uint32_t mid_cp = pgm_read_dword(&GLYPH_CODEPOINTS[mid]);
    if (mid_cp == cp) return mid;
    if (mid_cp < cp) lo = mid + 1;
    else             hi = mid - 1;
  }
  return -1;
}

// Draw one glyph cell at (x, y) inside a (cell_w × cell_h) rect.
//
// cell_w / cell_h are the cell's *footprint on screen* — the glyph is
// always read from GLYPH_BITMAPS at its native 8 px width and clipped
// to cell_h rows (so we can squeeze Unifont's 16 px glyph into a tighter
// vertical pitch without distortion).
//
// `bold=true` gives pixel-art chunkiness: each set pixel is painted at
// (x+c, y+r) AND (x+c+1, y+r). Strokes visibly thicken from 1 px to 2 px
// with no change to the source bitmap, no 2x scale artefacts, no extra
// memory. Descender/ascender rows that fall past cell_h are simply not
// drawn (Unifont's bottom 2-3 rows are almost always blank padding).
//
// Pass idx = -1 to just erase (space / missing-glyph cell).
void drawGlyphCell(int16_t x, int16_t y, int idx, uint16_t fg, uint16_t bg,
                   int cell_w, int cell_h, bool bold) {
  gfx->fillRect(x, y, cell_w, cell_h, bg);
  if (idx < 0) return;

  uint8_t buf[GLYPH_BYTES];
  memcpy_P(buf, &GLYPH_BITMAPS[idx][0], GLYPH_BYTES);

  // Vertical alignment: Unifont glyphs have their baseline around row 12,
  // with ascenders in rows 0–3 and descenders (comma, semicolon, etc.) in
  // rows 12–15. A naïve "rows 0..cell_h-1" clip would eat the bottom 4+
  // rows → commas and periods render as blank cells.
  //
  // Instead we shift the glyph up so its descender rows land inside the
  // cell. For cell_h=11 with a 16-row source: shift so glyph rows 3..13
  // map to cell rows 0..10. Ascender-heavy chars (A, quotes) lose their
  // top 3 rows — an acceptable trade since Unifont keeps the top 3 rows
  // mostly padding; the main glyph mass lives in rows 3..13.
  int shift = (GLYPH_H > cell_h) ? ((GLYPH_H - cell_h) / 2 + 1) : 0;

  gfx->startWrite();
  for (int r = 0; r < GLYPH_H; r++) {
    int dst_row = r - shift;
    if (dst_row < 0 || dst_row >= cell_h) continue;
    uint8_t row_bits = buf[r];
    for (int c = 0; c < GLYPH_W; c++) {
      if (row_bits & (0x80 >> c)) {
        gfx->writePixel(x + c, y + dst_row, fg);
        if (bold && (x + c + 1) < (x + cell_w)) {
          gfx->writePixel(x + c + 1, y + dst_row, fg);
        }
      }
    }
  }
  gfx->endWrite();
}


// ---- Rendering -------------------------------------------------------------
// Single-line text: natural 8×16 cells. `bold` doubles each lit pixel
// rightward (drawGlyphCell handles it) — handy for small UI labels that
// need more weight against a tinted background.
void drawString(int16_t x, int16_t y, const char* s, uint16_t fg, uint16_t bg, bool bold = false) {
  size_t i = 0;
  int16_t cx = x;
  while (s[i] != '\0') {
    size_t n = 0;
    uint32_t cp = decodeUtf8(s + i, &n);
    if (n == 0) break;
    drawGlyphCell(cx, y, glyphIndex(cp), fg, bg, GLYPH_W, GLYPH_H, bold);
    cx += GLYPH_W;
    i += n;
  }
}

// Larger single-line text: scale 2 (16×16 per cell) with horizontal bold
// repaint for chunky retro feel. Used by the quote bubble where legibility
// at arm's length matters more than compactness.
// `n_cells` caps how many cells (not bytes) will be drawn — useful when
// rendering a pre-measured line segment from a longer quote string.
// Returns the byte offset consumed from `s`.
size_t drawString2x(int16_t x, int16_t y, const char* s, size_t n_cells,
                    uint16_t fg, uint16_t bg) {
  size_t i = 0;
  int16_t cx = x;
  size_t drawn = 0;
  while (s[i] != '\0' && drawn < n_cells) {
    size_t n = 0;
    uint32_t cp = decodeUtf8(s + i, &n);
    if (n == 0) break;
    // 2× horizontal scale: cell is 16 px wide. Each set glyph pixel is
    // painted at (px, y) AND (px+1, y) to get true pixel-doubling.
    // Bold is already visually baked in by the 2× scale, so we pass
    // bold=false here — doubling + bold would make strokes 3 px wide
    // and the text would look smeared.
    int idx = glyphIndex(cp);
    gfx->fillRect(cx, y, 16, 16, bg);
    if (idx >= 0) {
      uint8_t buf[GLYPH_BYTES];
      memcpy_P(buf, &GLYPH_BITMAPS[idx][0], GLYPH_BYTES);
      gfx->startWrite();
      for (int r = 0; r < GLYPH_H; r++) {
        uint8_t row_bits = buf[r];
        for (int c = 0; c < GLYPH_W; c++) {
          if (row_bits & (0x80 >> c)) {
            int px = cx + c * 2;
            gfx->writePixel(px,     y + r, fg);
            gfx->writePixel(px + 1, y + r, fg);
          }
        }
      }
      gfx->endWrite();
    }
    cx += 16;
    i += n;
    drawn++;
  }
  return i;
}

// Count how many cells (codepoints, including ASCII) a UTF-8 string has.
size_t countCells(const char* s) {
  size_t i = 0, cells = 0;
  while (s[i] != '\0') {
    size_t n = 0;
    decodeUtf8(s + i, &n);
    if (n == 0) break;
    cells++;
    i += n;
  }
  return cells;
}

void renderWaiting() {
  gfx->fillScreen(RGB565_BLACK);
  drawString(30, 96,  "waiting for",    RGB565_WHITE, RGB565_BLACK);
  drawString(30, 120, "web connect...", RGB565_WHITE, RGB565_BLACK);
}

// Render the sprite — source is 18×18 cells of UTF-8; we render rows
// [SPRITE_ROW_OFFSET .. SPRITE_ROW_OFFSET+SPRITE_ROWS-1] and within each
// row cells [SPRITE_COL_OFFSET .. SPRITE_COL_OFFSET+SPRITE_COLS-1], each
// drawn at 2x horizontal scale in a CELL_W × CELL_H pixel tile.
void drawSprite(const char* frame_pgm, uint16_t bg_color, uint16_t fg_color) {
  // Pull the full frame into RAM once. The largest sprite frame in sprites.h
  // is ~675 bytes (female_4_2); 800 is plenty. `static` keeps it off the
  // loop() stack at 166 ms cadence — not a leak, just a safer budget.
  static char frame[800];
  size_t i = 0;
  while (i + 1 < sizeof(frame)) {
    char c = (char)pgm_read_byte(frame_pgm + i);
    frame[i] = c;
    if (c == '\0') break;
    i++;
  }
  frame[sizeof(frame) - 1] = '\0';

  size_t p = 0;

  // Walk all 18 source rows; draw rendered rows 0..SPRITE_ROWS-1.
  int rendered_row = 0;
  for (int src_row = 0; src_row < 18; src_row++) {
    bool should_render = (src_row >= SPRITE_ROW_OFFSET) &&
                         (rendered_row < SPRITE_ROWS);
    int16_t y = SPRITE_ORIGIN_Y + rendered_row * CELL_H;

    int src_col = 0;
    while (frame[p] != '\0' && frame[p] != '\n' && src_col < 18) {
      size_t n = 0;
      uint32_t cp = decodeUtf8(frame + p, &n);
      if (n == 0) break;

      if (should_render &&
          src_col >= SPRITE_COL_OFFSET &&
          src_col <  SPRITE_COL_OFFSET + SPRITE_COLS) {
        int16_t x = SPRITE_ORIGIN_X + (src_col - SPRITE_COL_OFFSET) * CELL_W;
        drawGlyphCell(x, y, glyphIndex(cp), fg_color, bg_color, CELL_W, CELL_H, true);
      }

      p += n;
      src_col++;
    }
    if (should_render) rendered_row++;
    while (frame[p] != '\0' && frame[p] != '\n') p++;
    if (frame[p] == '\n') p++;
  }
}

// Quote bubble — rendered with 16×16 glyphs (2× scale, chunky pixel-art feel).
// Single-line only: web caps input at 14 chars (matches MAX_PER_LINE), so
// any longer string is hard-truncated at the cell count cap.
// BOTTOM-anchored to y=28 — leaves y=32 onwards for the sprite.
void drawQuoteBubble(uint16_t bg_color, uint16_t fg_color) {
  if (st.quote.length() == 0) return;

  const char* quote = st.quote.c_str();

  // Sizing. 16 px per 2× glyph cell + 3 px padding on each side fits
  // 14 cells (14×16 + 2×3 = 230 px bubble, 5 px margin to each screen edge).
  const int16_t GLYPH_2X_W   = 16;
  const int16_t GLYPH_2X_H   = 16;
  const int16_t PAD          = 3;
  const size_t  MAX_PER_LINE = 14;
  // Bubble floor + tail must stay above SPRITE_ORIGIN_Y (= 32):
  // BUBBLE_BOTTOM (28) + TAIL_H (4) = 32 → tail tip touches sprite top edge.
  const int16_t BUBBLE_BOTTOM = 28;
  const int16_t TAIL_H        = 4;

  size_t total_cells = countCells(quote);
  if (total_cells > MAX_PER_LINE) total_cells = MAX_PER_LINE;

  int16_t inner_h = GLYPH_2X_H;
  int16_t bh      = inner_h + PAD * 2;
  int16_t inner_w = (int16_t)total_cells * GLYPH_2X_W;
  int16_t bw      = inner_w + PAD * 2;

  int16_t bx = (LCD_W - bw) / 2;
  int16_t by = BUBBLE_BOTTOM - bh;    // anchor to bottom

  // Round-rect bubble.
  gfx->fillRoundRect(bx, by, bw, bh, 4, bg_color);
  gfx->drawRoundRect(bx, by, bw, bh, 4, fg_color);

  // Speech-bubble tail: downward triangle pointing at the sprite head.
  int16_t tipX    = LCD_W / 2;
  int16_t tailTop = by + bh - 1;
  gfx->fillTriangle(tipX - 4, tailTop, tipX + 4, tailTop, tipX, tailTop + TAIL_H, bg_color);
  gfx->drawLine(tipX - 4, tailTop, tipX, tailTop + TAIL_H, fg_color);
  gfx->drawLine(tipX + 4, tailTop, tipX, tailTop + TAIL_H, fg_color);
  gfx->drawLine(tipX - 3, tailTop, tipX + 3, tailTop, bg_color);

  int16_t text_x = bx + PAD;
  int16_t text_y = by + PAD;
  drawString2x(text_x, text_y, quote, total_cells, fg_color, bg_color);
}

// Interest tag row — bottom of screen, mirrors web's #preview-interests row.
// Format: "<icon> <label>  <icon> <label>  <icon> <label>" (double-space
// separator between tags). U+00B7 middot isn't in glyphs.h so we keep it
// ASCII-only.
//
// Width budget: 240 px / 8 px per cell = 30 cells. Worst-case 3 tags
// (`♠ gaming  ✈ travel  ◉ photo`) ≈ 27 cells. Web caps at 3 selections
// so we don't have to handle overflow gracefully — just clip if needed.
void drawInterestRow(uint16_t bg_color, uint16_t fg_color) {
  if (st.interest_count == 0) return;

  // Build the row into a buffer so we can compute total width once and
  // center the whole line. Buffer sized for worst case (3 tags × ~10 cells
  // × 4 bytes/UTF-8 + separators) with comfortable margin.
  char buf[96];
  size_t bi = 0;
  for (int i = 0; i < st.interest_count && bi + 24 < sizeof(buf); i++) {
    if (i > 0) {
      buf[bi++] = ' ';
      buf[bi++] = ' ';
    }
    const char* icon  = st.interests[i].icon.c_str();
    const char* label = st.interests[i].label.c_str();
    while (*icon  && bi + 1 < sizeof(buf)) buf[bi++] = *icon++;
    if (bi + 1 < sizeof(buf)) buf[bi++] = ' ';
    while (*label && bi + 1 < sizeof(buf)) buf[bi++] = *label++;
  }
  buf[bi] = '\0';

  size_t cells = countCells(buf);
  int16_t row_w = (int16_t)cells * GLYPH_W;
  int16_t x = (LCD_W - row_w) / 2;
  if (x < 0) x = 0;                    // clip-left if it overflows
  int16_t y = 212;                     // sprite ends at 208; 4 px gap

  drawString(x, y, buf, fg_color, bg_color, /*bold=*/true);
}

// Full redraw — used when outer state (mood/energy/sprite identity/quote) changes.
void renderFull() {
  if (!st.valid) { renderWaiting(); return; }

  int m = constrain(st.mood,   0, 4);
  int e = constrain(st.energy, 0, 5);

  uint32_t bg      = pgm_read_dword(&PALETTE[m][e]);
  uint32_t bgSoft  = lightenHex(bg);
  uint32_t mainCol = darkenHex(bg);

  uint16_t bgSoft565  = hexToRgb565(bgSoft);
  uint16_t mainCol565 = hexToRgb565(mainCol);

  gfx->fillScreen(bgSoft565);

  const SpriteEntry* sp = findSprite(st);
  if (sp) {
    // Read the current frame pointer out of PROGMEM
    const char* frame_pgm = (const char*)pgm_read_ptr(
      &sp->frames[animFrame % sp->frame_count]
    );
    drawSprite(frame_pgm, bgSoft565, mainCol565);
  } else {
    // No sprite for this key — show the key in the center for debugging.
    char key[32];
    snprintf(key, sizeof(key), "missing: %s_%d_%d%s",
             st.sex.c_str(), st.color, st.num, st.smile ? "smile" : "");
    drawString(12, 112, key, mainCol565, bgSoft565);
  }

  drawQuoteBubble(bgSoft565, mainCol565);
  drawInterestRow(bgSoft565, mainCol565);
}

// Inner-frame redraw — only the sprite area, bubble untouched.
// Sprite region is y=32..240; bubble lives in y≈3..28. No overlap → the
// bubble can be drawn once (from renderFull) and stays stable without
// flicker. This is the WHY behind starting the sprite at y=32.
void renderSpriteOnly() {
  if (!st.valid) return;
  int m = constrain(st.mood,   0, 4);
  int e = constrain(st.energy, 0, 5);
  uint32_t bg      = pgm_read_dword(&PALETTE[m][e]);
  uint32_t bgSoft  = lightenHex(bg);
  uint32_t mainCol = darkenHex(bg);
  uint16_t bgSoft565  = hexToRgb565(bgSoft);
  uint16_t mainCol565 = hexToRgb565(mainCol);

  const SpriteEntry* sp = findSprite(st);
  if (!sp) return;
  const char* frame_pgm = (const char*)pgm_read_ptr(
    &sp->frames[animFrame % sp->frame_count]
  );
  drawSprite(frame_pgm, bgSoft565, mainCol565);
}


// ---- Serial line parsing ---------------------------------------------------
void handleLine(const String &line) {
  // 512 B leaves headroom for the interests array (up to 3 × {icon,label}
  // on top of the base ~130 B payload). Bump if new fields are added.
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, line);
  if (err) {
    Serial.printf("[serial_avatar] JSON parse err: %s\n", err.c_str());
    return;
  }

  if (doc["mood"].is<int>())                st.mood   = doc["mood"].as<int>();
  if (doc["energy"].is<int>())              st.energy = doc["energy"].as<int>();
  if (doc["color"].is<int>())               st.color  = doc["color"].as<int>();
  if (doc["num"].is<int>())                 st.num    = doc["num"].as<int>();
  if (doc["smile"].is<bool>())              st.smile  = doc["smile"].as<bool>();
  if (doc["sex"].is<const char*>())         st.sex    = String((const char*)doc["sex"]);
  if (doc["quote"].is<const char*>())       st.quote  = String((const char*)doc["quote"]);

  // Interests: optional array of {icon, label}. Absent key → clear.
  // Missing/non-object entries silently skipped; max MAX_INTERESTS kept.
  st.interest_count = 0;
  if (doc["interests"].is<JsonArray>()) {
    for (JsonObject tag : doc["interests"].as<JsonArray>()) {
      if (st.interest_count >= MAX_INTERESTS) break;
      const char* icon  = tag["icon"]  | "";
      const char* label = tag["label"] | "";
      if (icon[0] == '\0' && label[0] == '\0') continue;
      st.interests[st.interest_count].icon  = String(icon);
      st.interests[st.interest_count].label = String(label);
      st.interest_count++;
    }
  }

  st.valid = true;
  outerDirty = true;

  Serial.printf("[serial_avatar] OK mood=%d energy=%d sex=%s color=%d num=%d smile=%d quote=\"%s\" interests=%d\n",
                st.mood, st.energy, st.sex.c_str(), st.color, st.num, st.smile, st.quote.c_str(), st.interest_count);
}


// ---- Heart rate detection --------------------------------------------------
// Polled at 50 Hz. Updates hr_currentBpm in-place. Call from loop().
void hrTick(uint32_t now) {
  if (now - hr_lastSampleMs < HR_SAMPLE_MS) return;
  hr_lastSampleMs = now;

  int v = analogRead(HR_PIN);   // ESP32 default: 0..4095, 12-bit

#if HR_DEBUG
  // Throttled raw dump for threshold tuning. Format: line per 200 ms.
  // Watch the spread (max-min) over a few seconds — peak amplitude
  // tells you what HR_RISE_THRESH should be.
  static uint32_t hr_dbgLastMs = 0;
  if (now - hr_dbgLastMs >= 200) {
    hr_dbgLastMs = now;
    Serial.printf("[hr] raw=%4d  base=%4d  diff=%+4d\n", v, hr_baseline, v - hr_baseline);
  }
#endif

  // Baseline tracking: rolling mean over HR_BASELINE_N samples.
  hr_baselineSum += v;
  hr_baselineN++;
  if (hr_baselineN >= HR_BASELINE_N) {
    hr_baseline = hr_baselineSum / hr_baselineN;
    hr_baselineSum = 0;
    hr_baselineN = 0;
  }

  // Edge detect: signal crossing baseline+threshold from below = beat.
  bool above = (v > hr_baseline + HR_RISE_THRESH);
  if (above && !hr_above) {
    // Rising edge → candidate beat. Reject if too soon (would imply >200 BPM).
    if (hr_lastBeatMs != 0 && (now - hr_lastBeatMs) > 300) {
      uint32_t interval = now - hr_lastBeatMs;
      hr_intervals[hr_intervalIdx] = interval;
      hr_intervalIdx = (hr_intervalIdx + 1) % 3;
      if (hr_intervalCount < 3) hr_intervalCount++;

      // Compute BPM from the average of available intervals.
      uint32_t sum = 0;
      for (int i = 0; i < hr_intervalCount; i++) sum += hr_intervals[i];
      uint32_t avg = sum / hr_intervalCount;
      if (avg > 0) hr_currentBpm = (int)(60000UL / avg);
    }
    hr_lastBeatMs = now;
  }
  hr_above = above;

  // If no beat for 3 s, declare "no signal" (finger lifted).
  if (hr_lastBeatMs != 0 && (now - hr_lastBeatMs) > 3000) {
    hr_currentBpm = 0;
    hr_intervalCount = 0;
  }
}

// Periodically push current BPM to web. Cheap one-line JSON; web ignores
// it if it has no listener. Always emit (even bpm=0) so the web side knows
// we're alive and can show "no finger detected".
void hrReport(uint32_t now) {
  if (now - hr_lastReportMs < HR_REPORT_MS) return;
  hr_lastReportMs = now;
  Serial.printf("{\"hr\":%d}\n", hr_currentBpm);
}


// ---- Arduino entry points --------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[serial_avatar] booting");

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  if (!gfx->begin()) {
    Serial.println("[serial_avatar] gfx->begin() FAILED");
    while (1) delay(1000);
  }

  // Heart rate ADC. analogReadResolution sets 12-bit (default on ESP32-C6
  // anyway, but explicit avoids surprises if Arduino-ESP32 changes default).
  pinMode(HR_PIN, INPUT);
  analogReadResolution(12);

  renderWaiting();
  Serial.printf("[serial_avatar] ready — %d sprites loaded\n", SPRITES_COUNT);
}

void loop() {
  // Drain Serial
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (lineBuf.length() > 0) { handleLine(lineBuf); lineBuf = ""; }
    } else {
      lineBuf += c;
      if (lineBuf.length() > 320) lineBuf = "";
    }
  }

  // Outer redraw if state changed
  if (outerDirty) {
    outerDirty = false;
    renderFull();
  }

  uint32_t now = millis();

  // Heart rate sampling + periodic report. Independent of sprite state —
  // runs even before web has connected, so the first connect can pick up
  // a valid BPM immediately.
  hrTick(now);
  hrReport(now);

  // Inner-frame animation
  if (st.valid && (now - lastAnimMs >= ANIM_INTERVAL_MS)) {
    lastAnimMs = now;
    animFrame++;
    renderSpriteOnly();
  }
}
