/***************************************************
  最简单的 ST7789 屏幕测试
  XIAO ESP32-C3 接线：
  GND  → GND
  VCC  → 3.3V
  SCL  → D8 (GPIO8)
  SDA  → D10 (GPIO10)
  RES  → D5 (GPIO7)
  DC   → D4 (GPIO6)
  BLK  → 3.3V
 ****************************************************/

#include <Adafruit_GFX.h>
#include <Arduino_ST7789.h>
#include <SPI.h>

#define TFT_DC    6
#define TFT_RST   7
#define TFT_MOSI  10
#define TFT_SCLK  8

// 软件 SPI
Arduino_ST7789 tft = Arduino_ST7789(TFT_DC, TFT_RST, TFT_MOSI, TFT_SCLK);

void setup() {
  Serial.begin(115200);
  Serial.println("Starting...");

  tft.init(240, 240);
  Serial.println("Init done");

  // 红屏测试
  tft.fillScreen(RED);
  Serial.println("RED");
}

void loop() {
  delay(2000);
  tft.fillScreen(RED);
  Serial.println("RED");
  delay(2000);
  tft.fillScreen(GREEN);
  Serial.println("GREEN");
  delay(2000);
  tft.fillScreen(BLUE);
  Serial.println("BLUE");
  delay(2000);
  tft.fillScreen(WHITE);
  Serial.println("WHITE");
}
