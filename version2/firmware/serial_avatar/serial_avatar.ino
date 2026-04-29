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
#include <Wire.h>
#include <Adafruit_PN532.h>

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

// Heart-rate sensor support was removed 2026-04-27 — sensor was never
// wired up, and the 5 Hz [hr] debug noise was drowning Stage 3 match
// logging in the Web Serial console. The web side (app.js:67+) still
// listens for {"hr":N} messages; without firmware sending them, the
// energy slider falls back to manual mode (which is what we always used
// anyway). Re-add HR by reverting this commit if a sensor ever comes back.

#define IPS_MODE  true

Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *gfx = new Arduino_ST7789(bus, TFT_RST, ROTATION, IPS_MODE, LCD_W, LCD_H, 0, 0, 0, 0);


// ---- NFC config (PN532 over I2C, bring-up validated 2026-04-27) ------------
// Bus is independent from the LCD SPI (pins 6/7/14/15) so they coexist without
// arbitration. IRQ/RST aren't wired in I2C mode for this build — pass -1 and
// the library will skip optional features that need them.
#define NFC_I2C_SDA       1
#define NFC_I2C_SCL       2
// Poll timeout per loop iteration. Short enough that ANIM_INTERVAL_MS (166 ms)
// sprite breathing keeps its cadence — at 50 ms we get up to 3 idle polls per
// frame, never blocking long enough to skip a tick.
#define NFC_POLL_TIMEOUT  50
// Cool-down after a successful tap. Match animation runs ~21 s end-to-end and
// the firmware refuses to read while matchState != MATCH_IDLE anyway, so this
// only matters for the small window between tap and animation start. 1500 ms
// is comfortably longer than handleMatch + first ROLLING tick (~50 ms).
#define NFC_DEBOUNCE_MS   1500

Adafruit_PN532 nfc(-1, -1, &Wire);
uint32_t nfcLastTapMs = 0;
bool     nfcReady     = false;


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

// Linearly interpolate two RGB565 colors at a given mix point (0-255).
// Used for fade in/out — LCD has no alpha channel, so we re-paint each frame
// with a color that's already pre-blended toward the background.
uint16_t mix565(uint16_t fg, uint16_t bg, uint8_t t) {
  uint8_t fr = (fg >> 11) & 0x1F, fg5 = (fg >> 5) & 0x3F, fb = fg & 0x1F;
  uint8_t br = (bg >> 11) & 0x1F, bg5 = (bg >> 5) & 0x3F, bb = bg & 0x1F;
  uint8_t r = ((uint16_t)fr * t + (uint16_t)br * (255 - t)) / 255;
  uint8_t g = ((uint16_t)fg5 * t + (uint16_t)bg5 * (255 - t)) / 255;
  uint8_t b = ((uint16_t)fb * t + (uint16_t)bb * (255 - t)) / 255;
  return (r << 11) | (g << 5) | b;
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
// Latest-fully-received frame captured during a render-time drain. Set by
// drainSerialIntoBuf when it encounters '\n'; consumed by loop() once it's
// safe (matchState==IDLE) to handleLine() outside the drain path. Stays
// empty during normal idle operation — only used as a hand-off slot when
// a frame arrives while the screen is busy. Newer-wins: each '\n' overwrites
// any prior pendingLine, so only the most recent frame survives.
String pendingLine;
uint32_t animFrame = 0;
uint32_t lastAnimMs = 0;

// Outer-layer change flag — set when sex/color/num/smile or mood/energy or
// quote changes. Forces a full redraw (background + sprite + quote).
// Inner-frame ticks only redraw the sprite glyphs.
bool outerDirty = true;

// Stage 3 match-screen state machine. While in any non-IDLE state, the
// regular sprite redraw is suspended — the screen belongs to the match
// animation. Returns to IDLE after hint display, which re-arms outerDirty
// to repaint the avatar.
enum MatchAnimState {
  MATCH_IDLE,
  MATCH_NUMBER_ROLLING,    // big number ticking up toward `target`
  MATCH_NUMBER_STATIC,     // landed on target, holding for ~3 s
  MATCH_NUMBER_FADING,     // fade number out toward bg
  MATCH_HINT_FADING_IN,    // wrapped hint text fading in
  MATCH_HINT_STATIC,       // hint visible for ~3 s
};
MatchAnimState matchState = MATCH_IDLE;
uint32_t matchPhaseStartMs = 0;   // when the current phase started
int      matchTarget = 0;         // final score 0..100
int      matchCurrent = 0;        // currently displayed number
uint32_t matchLastTickMs = 0;     // last time we incremented matchCurrent
String   matchHint;               // server-supplied hint string (UTF-8)
uint16_t matchBg565 = 0;          // captured bg color for the entire animation
uint16_t matchFg565 = 0;          // captured fg color (matches mainCol)
bool     matchClearPending = false;  // set by handleMatch, consumed by tickMatchAnimation
                                     // to defer the bg fillScreen out of the drain path

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

// 4× scaled text: 32×32 per cell. Used by the match-score rolling animation
// where the number must be readable from across a small classroom. Each set
// glyph pixel becomes a 4×4 block; clear pixels are filled with bg.
size_t drawString4x(int16_t x, int16_t y, const char* s, size_t n_cells,
                    uint16_t fg, uint16_t bg) {
  size_t i = 0;
  int16_t cx = x;
  size_t drawn = 0;
  while (s[i] != '\0' && drawn < n_cells) {
    size_t n = 0;
    uint32_t cp = decodeUtf8(s + i, &n);
    if (n == 0) break;
    int idx = glyphIndex(cp);
    gfx->fillRect(cx, y, 32, 32, bg);
    if (idx >= 0) {
      uint8_t buf[GLYPH_BYTES];
      memcpy_P(buf, &GLYPH_BITMAPS[idx][0], GLYPH_BYTES);
      gfx->startWrite();
      // Non-uniform scale: horizontal 4×, vertical 2×. Glyph source is 8×16
      // (1:2 nat ratio) → painted as 32×32 cell. Earlier version used 4×4
      // and clipped to top 8 rows for "chunky 32 px tall" — that produced
      // half-digits because Unifont numerals split their stroke mass across
      // all 16 rows (top half of "6"/"8" alone reads as a tiny arc).
      for (int r = 0; r < GLYPH_H; r++) {
        uint8_t row_bits = buf[r];
        for (int c = 0; c < GLYPH_W; c++) {
          if (row_bits & (0x80 >> c)) {
            gfx->writeFillRect(cx + c * 4, y + r * 2, 4, 2, fg);
          }
        }
      }
      gfx->endWrite();
    }
    cx += 32;
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
  // Same FIFO-overflow trap renderFull() defends against: each gfx call here
  // is synchronous SPI that blocks ~tens of ms. Without intermediate drains,
  // a host frame arriving mid-render (e.g. animation just ended, st.valid is
  // still false → caller falls into this branch, and the next NFC tap's match
  // packet lands on the wire while we're filling the screen) overflows the
  // ~256 B USB-CDC RX FIFO and silently loses the '\n' that closes the JSON.
  gfx->fillScreen(RGB565_BLACK);
  drainSerialIntoBuf();
  drawString(30, 96,  "waiting for",    RGB565_WHITE, RGB565_BLACK);
  drainSerialIntoBuf();
  drawString(30, 120, "web connect...", RGB565_WHITE, RGB565_BLACK);
  drainSerialIntoBuf();
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
// Pull bytes from the USB-CDC RX FIFO into lineBuf without parsing. Called
// between SPI ops in renderFull() / inside tickMatchAnimation() so the FIFO
// (~256 B) cannot fill up while the screen is being repainted.
//
// Newer-wins semantics: when '\n' is encountered, the just-completed frame
// is moved into `pendingLine` (overwriting any earlier one). The loop()
// will dispatch pendingLine once it's safe — i.e. after render/anim finishes
// and we're back at the top of loop. This lets us preserve the *latest*
// frame that arrived during the render window without invoking handleLine
// (which is a blocking SPI op) inside the drain path.
//
// Mirrors the host's pendingPacket pattern — only the freshest frame seen
// during a busy window ends up dispatched.
void drainSerialIntoBuf() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (lineBuf.length() > 0) {
        pendingLine = lineBuf;   // newer-wins; overwrite any prior pending
        lineBuf = "";
      }
      continue;
    }
    lineBuf += c;
    if (lineBuf.length() > 1024) {
      Serial.printf("[serial_avatar] line buffer overflow at %u bytes — dropping (mid-render)\n",
                    lineBuf.length());
      lineBuf = "";
    }
  }
}

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
  drainSerialIntoBuf();

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
  drainSerialIntoBuf();

  drawQuoteBubble(bgSoft565, mainCol565);
  drainSerialIntoBuf();

  drawInterestRow(bgSoft565, mainCol565);
  drainSerialIntoBuf();
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


// ---- Stage 3 match handler -------------------------------------------------
// Step 1 (this commit): just acknowledge the message in serial. No drawing.
// Step 3 will draw the match panel; Step 4 will animate it.
//
// Why split? Verifying the data path independently from rendering means if
// the screen stays blank in Step 3 we already know the JSON arrived. Same
// "verify the data layer first" lesson burned into hardware.md §4.2.
void handleMatch(const JsonDocument &doc) {
  int score = doc["score"] | -1;
  const char* hint = doc["hint"] | "";
  Serial.printf("[serial_avatar] MATCH RECEIVED  score=%d  hint=\"%s\"\n",
                score, hint);

  // Dump the peer avatar fields too so we can see if the full payload
  // survived the web → serial transport. If any of these come back as
  // sentinels (-1 / empty), the bridge is mangling the JSON, not us.
  JsonObjectConst peer = doc["peer_avatar"].as<JsonObjectConst>();
  if (!peer.isNull()) {
    Serial.printf("[serial_avatar]   peer.sex=%s color=%d num=%d smile=%d mood=%d bg=%d\n",
                  (const char*)(peer["sex"] | ""),
                  (int)(peer["color"]    | -1),
                  (int)(peer["num"]      | -1),
                  (bool)(peer["smile"]   | false),
                  (int)(peer["mood"]     | -1),
                  (int)(peer["bg_level"] | -1));
  }

  // Common interests array — print as comma-joined list.
  JsonArrayConst common = doc["common"].as<JsonArrayConst>();
  if (!common.isNull()) {
    Serial.print("[serial_avatar]   common=[");
    bool first = true;
    for (JsonVariantConst v : common) {
      if (!first) Serial.print(",");
      Serial.print((const char*)(v | ""));
      first = false;
    }
    Serial.println("]");
  }

  // Kick off the on-screen match animation. Use the *current* avatar's
  // bg/main palette so the match screen feels visually continuous with the
  // sprite that was just there. Captured into matchBg565/matchFg565 so the
  // animation doesn't need to re-derive colors each frame.
  int m = constrain(st.mood,   0, 4);
  int e = constrain(st.energy, 0, 5);
  uint32_t bg      = pgm_read_dword(&PALETTE[m][e]);
  matchBg565 = hexToRgb565(lightenHex(bg));
  matchFg565 = hexToRgb565(darkenHex(bg));

  matchTarget = constrain(score, 0, 100);
  matchHint   = String(hint);

  // Score=0 (no overlap): skip the rolling animation — going from 1 down to
  // 0 reads as deflating. Show a brief static "0" then transition to hint.
  // For non-zero scores, start counting from 1 with an easing curve toward
  // matchTarget (loop() does the actual stepping).
  //
  // Critical: do NOT call gfx->fillScreen here. This handler runs inside the
  // loop()'s drain path — any blocking SPI op here means lineBuf for the
  // *next* incoming frame can't be advanced and the USB-CDC RX FIFO can
  // overflow (which is what just bit us: a 1025 B "merged" line reappeared
  // because fillScreen blocked while two stage3 frames arrived in quick
  // succession). The screen clear is deferred to the first ROLLING frame
  // (which already runs in tickMatchAnimation, outside the drain path).
  matchClearPending = true;
  if (matchTarget == 0) {
    matchCurrent = 0;
    matchState = MATCH_NUMBER_STATIC;     // skip rolling, hold "0" briefly
    matchPhaseStartMs = millis();
  } else {
    matchCurrent = 1;
    matchState = MATCH_NUMBER_ROLLING;
    matchPhaseStartMs = millis();
    matchLastTickMs = matchPhaseStartMs;
  }
}


// ---- Match-screen rendering helpers ---------------------------------------

// Center a score number (0-100) in 4× scale. Each glyph is 32 px wide,
// so 1-digit = 32 px, 2-digit = 64 px, 3-digit = 96 px. Vertically anchored
// at y=100 so the number sits roughly mid-screen.
void drawMatchNumber(int n, uint16_t fg) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", constrain(n, 0, 100));
  size_t cells = countCells(buf);
  int16_t total_w = (int16_t)(cells * 32);
  int16_t x = (240 - total_w) / 2;
  int16_t y = 100;
  drawString4x(x, y, buf, cells, fg, matchBg565);
  drainSerialIntoBuf();   // keep RX FIFO clear during rapid roll updates
}

// Wrap `s` to lines of at most max_chars cells each, breaking on the last
// space at or before the limit. Falls back to hard-cut if no space found.
// Writes line slices to `out` (max `max_lines` entries) and returns count.
int wrapHint(const char* s, int max_chars, String* out, int max_lines) {
  int line_count = 0;
  size_t total_len = strlen(s);
  size_t i = 0;
  while (i < total_len && line_count < max_lines) {
    // Count cells until we exceed max_chars or hit end.
    size_t bytes_for_max = 0;
    size_t cells_so_far  = 0;
    size_t last_space_byte = 0;
    size_t last_space_cells = 0;
    while (i + bytes_for_max < total_len && cells_so_far < (size_t)max_chars) {
      size_t n = 0;
      uint32_t cp = decodeUtf8(s + i + bytes_for_max, &n);
      if (n == 0) break;
      if (cp == ' ') {
        last_space_byte  = bytes_for_max;
        last_space_cells = cells_so_far;
      }
      bytes_for_max += n;
      cells_so_far++;
    }
    size_t take_bytes;
    size_t take_cells;
    bool   skip_trailing_space = false;
    if (i + bytes_for_max >= total_len) {
      take_bytes = bytes_for_max;
      take_cells = cells_so_far;
    } else if (last_space_cells > 0) {
      take_bytes = last_space_byte;
      take_cells = last_space_cells;
      skip_trailing_space = true;
    } else {
      take_bytes = bytes_for_max;
      take_cells = cells_so_far;
    }
    out[line_count++] = String(s + i).substring(0, take_bytes);
    i += take_bytes + (skip_trailing_space ? 1 : 0);
  }
  return line_count;
}

// Draw the wrapped hint centered vertically in a single color (fade can be
// achieved by re-calling with a mix565 result at the call site).
void drawMatchHint(uint16_t fg) {
  const int LINE_H    = 18;
  const int MAX_CHARS = 30;
  const int MAX_LINES = 6;
  String lines[MAX_LINES];
  int n = wrapHint(matchHint.c_str(), MAX_CHARS, lines, MAX_LINES);
  if (n == 0) return;
  int16_t total_h = (int16_t)(n * LINE_H);
  int16_t y = (240 - total_h) / 2;
  for (int li = 0; li < n; li++) {
    size_t cells = countCells(lines[li].c_str());
    int16_t line_w = (int16_t)(cells * GLYPH_W);
    int16_t x = (240 - line_w) / 2;
    drawString(x, y + li * LINE_H, lines[li].c_str(), fg, matchBg565, /*bold=*/true);
    // Drain between lines so a long hint (4+ wrapped lines × ~10 ms each)
    // doesn't keep the RX FIFO blocked long enough to merge two host frames.
    drainSerialIntoBuf();
  }
}

// Tick the match-screen state machine. Called from loop(); does its own
// timing via millis() so it never sleeps. Each phase paints what it owns
// and advances state when its timer expires.
void tickMatchAnimation() {
  if (matchState == MATCH_IDLE) return;

  // Deferred clear — handleMatch can't run fillScreen safely (it executes
  // inside the drain path), so it sets a flag and we clear here on the first
  // tick after entering an animation state.
  if (matchClearPending) {
    matchClearPending = false;
    gfx->fillScreen(matchBg565);
    if (matchTarget == 0) drawMatchNumber(0, matchFg565);
  }

  uint32_t now = millis();

  switch (matchState) {

    case MATCH_NUMBER_ROLLING: {
      // Eased step: faster at start, slower as we approach matchTarget.
      // Step interval = 10ms + (matchCurrent / matchTarget) * 100ms,
      // so e.g. at 90% of target each step is ~100 ms (老虎机 stop感).
      // Halved from the original 20+200 curve — original total felt ~2× too
      // long for the demo's pacing; same easing shape, just compressed.
      int target = matchTarget;
      uint32_t progress_pct = (matchCurrent * 100) / max(target, 1);
      uint32_t step_ms = 10 + (progress_pct * 100) / 100;
      if (now - matchLastTickMs >= step_ms) {
        matchLastTickMs = now;
        matchCurrent++;
        if (matchCurrent >= target) {
          matchCurrent = target;
          drawMatchNumber(matchCurrent, matchFg565);
          matchState = MATCH_NUMBER_STATIC;
          matchPhaseStartMs = now;
        } else {
          drawMatchNumber(matchCurrent, matchFg565);
        }
      }
      break;
    }

    case MATCH_NUMBER_STATIC: {
      // Hold for 3 s. The number is already on screen (drawn either by the
      // ROLLING tail or by the deferred-clear branch when score=0).
      if (now - matchPhaseStartMs >= 3000) {
        matchState = MATCH_NUMBER_FADING;
        matchPhaseStartMs = now;
      }
      break;
    }

    case MATCH_NUMBER_FADING: {
      // 500 ms fade — 10 frames at 50 ms each.
      uint32_t elapsed = now - matchPhaseStartMs;
      if (elapsed >= 500) {
        // Fully faded; clear and move to hint phase.
        gfx->fillScreen(matchBg565);
        matchState = MATCH_HINT_FADING_IN;
        matchPhaseStartMs = now;
      } else {
        // Re-draw with progressively bg-mixed color.
        uint8_t t = 255 - (elapsed * 255 / 500);  // 255 → 0 over 500ms
        uint16_t faded = mix565(matchFg565, matchBg565, t);
        drawMatchNumber(matchCurrent, faded);
      }
      break;
    }

    case MATCH_HINT_FADING_IN: {
      // 600 ms fade-in for the hint text.
      uint32_t elapsed = now - matchPhaseStartMs;
      if (elapsed >= 600) {
        drawMatchHint(matchFg565);
        matchState = MATCH_HINT_STATIC;
        matchPhaseStartMs = now;
      } else {
        uint8_t t = (elapsed * 255) / 600;
        uint16_t fading = mix565(matchFg565, matchBg565, t);
        drawMatchHint(fading);
      }
      break;
    }

    case MATCH_HINT_STATIC: {
      if (now - matchPhaseStartMs >= 5000) {
        matchState = MATCH_IDLE;
        outerDirty = true;   // re-arm avatar repaint
      }
      break;
    }

    default: break;
  }
}


// ---- Serial line parsing ---------------------------------------------------
void handleLine(const String &line) {
  // 512 B leaves headroom for the interests array (up to 3 × {icon,label}
  // on top of the base ~130 B payload). Bump if new fields are added.
  // Stage 3 match payloads are larger (peer_avatar object + common array
  // + hint string up to 80 chars) so we use a separate 1024 B doc inline.
  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, line);
  if (err) {
    Serial.printf("[serial_avatar] JSON parse err: %s\n", err.c_str());
    return;
  }

  // Stage 3 match messages carry type="match". Anything without a type
  // (or with type="avatar") is the legacy avatar-state path. This keeps
  // stage 1 + stage 2 working unchanged.
  const char* msgType = doc["type"] | "avatar";
  if (strcmp(msgType, "match") == 0) {
    handleMatch(doc);
    return;
  }

  // Defensive check: a real avatar packet always has at least one of the
  // core identity fields (sex / color / num). If a truncated frame (e.g.
  // tail end of a coalesced match payload) sneaks past deserializeJson but
  // has no avatar fields, the legacy path below would silently apply
  // default-zero values to st and flip outerDirty — turning the screen
  // into a blank "default avatar" without the user doing anything.
  // Reject those frames here so the screen stays on whatever it was.
  bool hasAvatarField = doc["sex"].is<const char*>()
                     || doc["color"].is<int>()
                     || doc["num"].is<int>()
                     || doc["mood"].is<int>();
  if (!hasAvatarField) {
    Serial.println("[serial_avatar] dropped: no type=match and no avatar fields (likely truncated frame)");
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


// ---- NFC polling -----------------------------------------------------------
// Read one tap per call (or fast-return if cooling down / animation playing /
// no card in field). On success, emit a JSON line on Serial — device.js's
// startReadLoop already forwards `{...}` lines as `device:message` window
// events, so no new wire protocol is needed. The web side (stage3.html) picks
// these up and routes them to doTap(uid).
//
// UID format intentionally matches STAGE3_USERS dict keys in server.py:
// uppercase hex, colon-separated (e.g. "75:91:49:A7"). Any drift here causes
// silent 404s from /api/tap.
void pollNFC() {
  if (!nfcReady) return;
  uint32_t now = millis();
  if (now - nfcLastTapMs < NFC_DEBOUNCE_MS) return;   // cool-down
  if (matchState != MATCH_IDLE) return;                // animation owns the screen

  uint8_t uid[7] = {0};
  uint8_t uidLen = 0;
  if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A,
                                uid, &uidLen, NFC_POLL_TIMEOUT)) {
    return;   // no card this poll
  }
  nfcLastTapMs = now;

  // Build "AA:BB:CC:DD" string. snprintf "%02X" is uppercase by default.
  char uidStr[24] = {0};
  size_t off = 0;
  for (uint8_t i = 0; i < uidLen && off + 4 < sizeof(uidStr); i++) {
    off += snprintf(uidStr + off, sizeof(uidStr) - off,
                    i == 0 ? "%02X" : ":%02X", uid[i]);
  }

  Serial.printf("{\"type\":\"tap\",\"uid\":\"%s\"}\n", uidStr);
}


// ---- Arduino entry points --------------------------------------------------
void setup() {
  // Stage 3 match packets are ~354 B in one write; the default USB-CDC RX
  // FIFO is 256 B, so a single match line silently drops bytes before
  // loop()'s drain ever sees them — handleLine never fires, no parse err,
  // no overflow log, just a screen that doesn't animate. Must be called
  // before Serial.begin() (ESP32 Arduino core requirement).
  Serial.setRxBufferSize(2048);
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

  // NFC bring-up. Failure is non-fatal — the firmware still works as a pure
  // serial-driven avatar (the original stage1/2 code path) so a busted PN532
  // shouldn't brick the demo. We just won't auto-emit tap events.
  Wire.begin(NFC_I2C_SDA, NFC_I2C_SCL);
  nfc.begin();
  uint32_t v = nfc.getFirmwareVersion();
  if (v) {
    Serial.printf("[serial_avatar] NFC ready, PN532 fw %d.%d (chip 0x%02X)\n",
                  (uint8_t)(v >> 16) & 0xFF,
                  (uint8_t)(v >>  8) & 0xFF,
                  (uint8_t)(v >> 24) & 0xFF);
    nfc.SAMConfig();
    nfcReady = true;
  } else {
    Serial.println("[serial_avatar] NFC init FAILED — check DIP (1=ON,2=OFF) and wiring; running without NFC");
  }
}

void loop() {
  // NFC polling first — internal guards (debounce + animation-busy check)
  // make this a sub-millisecond no-op when there's nothing to do, so it's
  // safe to run every loop iteration. A successful poll emits a JSON line
  // on Serial; the web side (stage3.html) handles routing via /api/tap.
  pollNFC();

  // Pick up any frame that finished arriving during a render-time drain.
  // pendingLine is the newest-wins hand-off slot from drainSerialIntoBuf;
  // dispatching here means handleLine runs outside the drain path (safe to
  // do blocking SPI work like fillScreen for the next match animation).
  // Only do this when the screen isn't currently busy — otherwise we'd
  // re-enter handleMatch mid-animation and corrupt state.
  if (pendingLine.length() > 0 && matchState == MATCH_IDLE) {
    String frame = pendingLine;
    pendingLine = "";
    handleLine(frame);
  }

  // Drain Serial
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (lineBuf.length() > 0) { handleLine(lineBuf); lineBuf = ""; }
    } else {
      lineBuf += c;
      // Buffer ceiling matches StaticJsonDocument (1024 B). Stage 3 match
      // payloads measure ~390 B; stage 1 avatar packets ~130 B. If a line
      // ever exceeds this, log loudly so the truncation is visible — the
      // old silent 320 B drop produced a "waiting" screen with no clue why.
      if (lineBuf.length() > 1024) {
        Serial.printf("[serial_avatar] line buffer overflow at %u bytes — dropping\n",
                      lineBuf.length());
        lineBuf = "";
      }
    }
  }

  // Match-screen animation owns the display while non-IDLE — suspend the
  // regular avatar redraw path so the rolling number / hint text aren't
  // overwritten between frames.
  if (matchState != MATCH_IDLE) {
    tickMatchAnimation();
    return;
  }

  // Outer redraw if state changed
  if (outerDirty) {
    outerDirty = false;
    renderFull();
  }

  uint32_t now = millis();

  // Inner-frame animation
  if (st.valid && (now - lastAnimMs >= ANIM_INTERVAL_MS)) {
    lastAnimMs = now;
    animFrame++;
    renderSpriteOnly();
  }
}
