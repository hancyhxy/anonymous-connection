// lcd_smoke.ino — 一次性 LCD 诊断 sketch。
//
// 目的：判断破损的 LCD 还能显示什么。不连 Wi-Fi、不读 NFC、不写
// HTTP——只对 ST7789 整屏画大色块 + 大字，3 秒切一次。
//
// 用法：用 Arduino IDE 把这个 .ino 烧到 B 板（裂屏那台）。烧好后
// 屏幕会循环 BLACK → WHITE → RED → GREEN → BLUE → "OK"，同时
// Serial Monitor 也会同步打印当前应该是什么颜色，方便对照"应该看到
// 的 vs 实际看到的"。
//
// 验完想恢复 B 板正常工作，重烧 serial_avatar/ 即可。

#include <Arduino_GFX_Library.h>

// ---- LCD pin config (同 serial_avatar.ino) ---------------------------------
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

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[lcd_smoke] booting");

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  if (!gfx->begin()) {
    Serial.println("[lcd_smoke] gfx->begin() FAILED");
    while (1) delay(1000);
  }

  Serial.println("[lcd_smoke] gfx ready — entering cycle");
}

// Helper：用 16 px 大字写一行居中文本，方便远距离辨认。
void drawBigText(const char* msg, uint16_t fg, uint16_t bg) {
  gfx->fillScreen(bg);
  gfx->setTextColor(fg);
  gfx->setTextSize(6);   // 内置 5x7 字体 × 6 ≈ 30x42 px 一个字符
  // 居中位置：6 字 × 30 px = 180 px 宽，从 (30, 100) 起画
  gfx->setCursor(30, 100);
  gfx->print(msg);
}

void loop() {
  Serial.println("[lcd_smoke] --- BLACK (should look completely dark)");
  gfx->fillScreen(0x0000);
  delay(3000);

  Serial.println("[lcd_smoke] --- WHITE (should look completely bright)");
  gfx->fillScreen(0xFFFF);
  delay(3000);

  Serial.println("[lcd_smoke] --- RED");
  gfx->fillScreen(0xF800);
  delay(3000);

  Serial.println("[lcd_smoke] --- GREEN");
  gfx->fillScreen(0x07E0);
  delay(3000);

  Serial.println("[lcd_smoke] --- BLUE");
  gfx->fillScreen(0x001F);
  delay(3000);

  Serial.println("[lcd_smoke] --- big text 'OK' (white on black)");
  drawBigText("OK", 0xFFFF, 0x0000);
  delay(3000);

  Serial.println("[lcd_smoke] cycle done — restarting\n");
}
