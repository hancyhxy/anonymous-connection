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
struct AvatarState {
  int    mood;
  int    energy;
  String sex;
  int    color;
  int    num;
  bool   smile;
  String quote;
  bool   valid;
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
#define IPS_MODE  true

Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *gfx = new Arduino_ST7789(bus, TFT_RST, ROTATION, IPS_MODE, LCD_W, LCD_H, 0, 0, 0, 0);


// ---- Sprite layout ---------------------------------------------------------
// Layout tuned to match the hello-stranger web reference: sprite occupies
// ~60% of the frame width, bottom-anchored (figure "stands on" the screen's
// bottom edge), with the quote bubble floating above in the top ~25%.
//
// Source 18×18 → cropped to 13×16 (cols 2..14, rows 2..17). The bottom
// two rows (16, 17) are preserved because for many poses (e.g.
// female_1_0) they carry the densely-drawn legs/ground — cropping them
// leaves the avatar looking like a floating head. Top row 0–1 stay
// cropped since most sprites barely use them.
//
// Per-cell: 11 px wide × 11 px tall. 11×16 = 176 px tall sprite fits
// under the bubble region with comfortable headroom. Unifont's native
// 8×16 glyph is clipped to 11 rows — bottom 5 rows of the glyph are
// descender/padding, safe to drop.
//
// Grid: 13×11 = 143 wide, 16×11 = 176 tall → centered, bottom-aligned.
#define SPRITE_COL_OFFSET 2
#define SPRITE_ROW_OFFSET 2
#define SPRITE_COLS 13
#define SPRITE_ROWS 16

#define CELL_W      11
#define CELL_H      11

#define SPRITE_PX_W (CELL_W * SPRITE_COLS)    // 143
#define SPRITE_PX_H (CELL_H * SPRITE_ROWS)    // 176

// Bottom-anchor sprite to y=236 (4 px from screen bottom). Top of sprite
// = 236 - 176 = 60 → 60 px of headroom for the quote bubble above.
#define SPRITE_ORIGIN_X ((LCD_W - SPRITE_PX_W) / 2)           // 48
#define SPRITE_ORIGIN_Y (LCD_H - SPRITE_PX_H - 4)             // 60

// Animation cadence — matches Web ANIM_INTERVAL_MS.
#define ANIM_INTERVAL_MS 166


// ---- Palette (mirror of web/colors.json palette[mood][energy]) -------------
const uint32_t PALETTE[5][6] PROGMEM = {
  { 0x2a4a52, 0x3a6670, 0x4a8089, 0x3d7bb5, 0x2c5db8, 0x1e3fa8 }, // chill
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
AvatarState st = { 2, 2, "male", 0, 0, false, "", false };

String lineBuf;
uint32_t animFrame = 0;
uint32_t lastAnimMs = 0;

// Outer-layer change flag — set when sex/color/num/smile or mood/energy or
// quote changes. Forces a full redraw (background + sprite + quote).
// Inner-frame ticks only redraw the sprite glyphs.
bool outerDirty = true;


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
// Single-line text: natural 8×16 cells, no bold.
// Used by boot screen (needs minimum size so full messages fit at all).
void drawString(int16_t x, int16_t y, const char* s, uint16_t fg, uint16_t bg) {
  size_t i = 0;
  int16_t cx = x;
  while (s[i] != '\0') {
    size_t n = 0;
    uint32_t cp = decodeUtf8(s + i, &n);
    if (n == 0) break;
    drawGlyphCell(cx, y, glyphIndex(cp), fg, bg, GLYPH_W, GLYPH_H, false);
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

// Find a "nice" break point in the quote.
//
// Goal: fill line 1 as much as possible. A space is a prettier break than
// a hard cut, but only if it's in the rightmost ~third of the line —
// otherwise we'd strand the first line with way fewer chars than needed
// (e.g. "want quietwant..." would break at cells=4 after "want", leaving
// line 1 = 4 cells and stuffing the rest into line 2).
//
// Rule: walk to cell `max_cells`; if the last space we saw is in
// cells [max_cells*2/3 .. max_cells], break there. Otherwise hard-cut.
size_t findLineBreak(const char* s, size_t total_cells, size_t max_cells) {
  if (total_cells <= max_cells) return total_cells;
  size_t i = 0, cells = 0, last_space_cells = 0;
  bool saw_space = false;
  while (s[i] != '\0' && cells < max_cells) {
    size_t n = 0;
    uint32_t cp = decodeUtf8(s + i, &n);
    if (n == 0) break;
    if (cp == ' ') {
      saw_space = true;
      last_space_cells = cells;
    }
    cells++;
    i += n;
  }
  size_t min_space_pos = (max_cells * 2) / 3;  // only trust spaces in last 1/3
  if (saw_space && last_space_cells >= min_space_pos) return last_space_cells;
  return max_cells;  // hard cut — line 1 is full
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
// Layout: BOTTOM-anchored to a fixed y, so single-line quotes always sit in
// the same spot; longer quotes wrap to 2 lines and grow upward instead.
//
// Word-wrap: splits on the last space before the max-width if possible,
// otherwise hard-cuts. Lines past 2 are silently truncated (we never
// have more than ~28 chars of useful quote anyway).
void drawQuoteBubble(uint16_t bg_color, uint16_t fg_color) {
  if (st.quote.length() == 0) return;

  const char* quote = st.quote.c_str();

  // Sizing constants. 16 px per 2× glyph cell + 3 px padding on each side
  // fits 14 cells per line (14×16 + 2×3 = 230 px bubble, 5 px margin to
  // each screen edge). Going to 15 cells (15×16 + 2×3 = 246) overflows.
  const int16_t GLYPH_2X_W   = 16;
  const int16_t GLYPH_2X_H   = 16;
  const int16_t PAD          = 3;
  const size_t  MAX_PER_LINE = 14;
  // Bubble floor y: leave BUBBLE_BOTTOM + TAIL_H < SPRITE_ORIGIN_Y (= 60)
  // so the tail tip never visually collides with the sprite's head row.
  const int16_t BUBBLE_BOTTOM = 48;
  const int16_t TAIL_H        = 6;   // tip at y = 48 + 6 - 1 = 53, 7 px from sprite top

  size_t total_cells = countCells(quote);

  // Decide 1-line vs 2-line layout.
  size_t line1_cells;
  size_t line2_cells;
  const char* line2_start = nullptr;  // points into `quote`

  if (total_cells <= MAX_PER_LINE) {
    line1_cells = total_cells;
    line2_cells = 0;
  } else {
    // Find a break point — prefer last space at or before MAX_PER_LINE.
    line1_cells = findLineBreak(quote, total_cells, MAX_PER_LINE);
    // Walk forward to the byte after line1_cells codepoints.
    size_t byte_i = 0, cells_i = 0;
    while (quote[byte_i] != '\0' && cells_i < line1_cells) {
      size_t n = 0;
      decodeUtf8(quote + byte_i, &n);
      if (n == 0) break;
      byte_i += n;
      cells_i++;
    }
    // Skip a single space after the break so the second line doesn't
    // start with a leading space.
    if (quote[byte_i] == ' ') byte_i++;
    line2_start = quote + byte_i;
    line2_cells = countCells(line2_start);
    if (line2_cells > MAX_PER_LINE) line2_cells = MAX_PER_LINE;  // hard truncate
  }

  // Bubble dimensions — grows UPWARD from BUBBLE_BOTTOM when 2 lines.
  int  n_lines      = (line2_cells > 0) ? 2 : 1;
  int16_t inner_h   = n_lines * GLYPH_2X_H;
  int16_t bh        = inner_h + PAD * 2;
  int16_t max_cells = (line1_cells > line2_cells) ? line1_cells : line2_cells;
  int16_t inner_w   = (int16_t)max_cells * GLYPH_2X_W;
  int16_t bw        = inner_w + PAD * 2;

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

  // Left-align both lines against the bubble's inner left edge. When the
  // second line is shorter than the first, it stays hugged to the left
  // (not centered), matching the feel of a typed-out message rather than
  // a logo/title block.
  int16_t text_x = bx + PAD;
  int16_t line1_y = by + PAD;
  drawString2x(text_x, line1_y, quote, line1_cells, fg_color, bg_color);

  if (line2_cells > 0) {
    int16_t line2_y = by + PAD + GLYPH_2X_H;
    drawString2x(text_x, line2_y, line2_start, line2_cells, fg_color, bg_color);
  }
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
  StaticJsonDocument<384> doc;
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

  st.valid = true;
  outerDirty = true;

  Serial.printf("[serial_avatar] OK mood=%d energy=%d sex=%s color=%d num=%d smile=%d quote=\"%s\"\n",
                st.mood, st.energy, st.sex.c_str(), st.color, st.num, st.smile, st.quote.c_str());
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

  // Inner-frame animation
  uint32_t now = millis();
  if (st.valid && (now - lastAnimMs >= ANIM_INTERVAL_MS)) {
    lastAnimMs = now;
    animFrame++;
    renderSpriteOnly();
  }
}
