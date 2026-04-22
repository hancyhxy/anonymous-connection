// smoke_test.ino
// Target: Waveshare ESP32-C6-LCD-1.3 (ST7789V2, 240x240)
// Purpose: verify Arduino environment + flashing + Arduino_GFX drawing + main loop
//
// Expected result after flashing:
//   - Line 1: "Hello, I'm XinYi"
//   - Center: a counter that ticks up every second
//   - Serial Monitor (115200): "tick: 0, 1, 2, ..."
//
// If display looks wrong (blank / wrong color / wrong orientation), adjust the
// #defines below. Source of truth for correct values: Waveshare demo zip
// Arduino/examples/01_LVGL_Arduino/*.ino. See firmware/README.md §2.2.

#include <Arduino_GFX_Library.h>

// ---- LCD pin config (initial guesses for ESP32-C6-LCD-1.3) -----------------
// If display is wrong, replace these with values from Waveshare demo source.
// Verified against Waveshare demo zip:
// Arduino/examples/LVGL_Arduino/Display_ST7789.h — EXAMPLE_PIN_NUM_*
#define TFT_SCK   7
#define TFT_MOSI  6
#define TFT_CS    14
#define TFT_DC    15
#define TFT_RST   21
#define TFT_BL    22

// ---- LCD panel config ------------------------------------------------------
#define LCD_W     240
#define LCD_H     240
#define ROTATION  0      // 0/1/2/3 — if display is upside down, try another
#define IPS_MODE  true   // ST7789V2 on this board is IPS

// ---- Driver stack ----------------------------------------------------------
Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, GFX_NOT_DEFINED /* MISO */);
Arduino_GFX *gfx = new Arduino_ST7789(bus, TFT_RST, ROTATION, IPS_MODE, LCD_W, LCD_H, 0, 0, 0, 0);

// ---- App state -------------------------------------------------------------
uint32_t tick = 0;
uint32_t lastTickMs = 0;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[smoke_test] booting");

  // Backlight on
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  // Display init
  if (!gfx->begin()) {
    Serial.println("[smoke_test] gfx->begin() FAILED — check pin config");
    while (1) delay(1000);
  }
  gfx->fillScreen(RGB565_BLACK);

  // Static header
  gfx->setTextColor(RGB565_WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(10, 20);
  gfx->println("Hello, I'm XinYi");

  gfx->setTextSize(1);
  gfx->setCursor(10, 60);
  gfx->println("smoke_test: counter below");

  Serial.println("[smoke_test] setup done");
}

void loop() {
  uint32_t now = millis();
  if (now - lastTickMs >= 1000) {
    lastTickMs = now;

    // Erase previous counter area
    gfx->fillRect(40, 110, 160, 60, RGB565_BLACK);

    // Draw new counter
    gfx->setTextColor(RGB565_GREEN);
    gfx->setTextSize(6);
    gfx->setCursor(60, 115);
    gfx->print(tick);

    Serial.printf("tick: %u\n", tick);
    tick++;
  }
}
