// nfc_smoke_test.ino
// Target: Waveshare ESP32-C6-LCD-1.3 + Elechouse PN532 V3 (I2C mode)
// Purpose: verify physical wiring + I2C bus + DIP switch mode are all correct,
//          BEFORE bringing in the Adafruit-PN532 library.
//
// This sketch does NOT touch the PN532 protocol. It only scans the I2C bus and
// prints any addresses that ACK. Expected output:
//
//   [nfc_smoke_test] scanning...
//   [nfc_smoke_test]   found device at 0x24    <-- this is the PN532
//   [nfc_smoke_test] scan done (1 device)
//
// If 0x24 never appears, the problem is one of these (in order of likelihood):
//   1. DIP switch on the PN532 board is NOT in I2C mode.
//      Correct setting: switch 1 = ON, switch 2 = OFF.
//      (HSU = both OFF; SPI = switch 2 ON.)
//   2. SDA/SCL swapped. SDA must go to IO1, SCL must go to IO2 (per
//      hardware.md §3.2).
//   3. VCC not connected, or connected to GND. PN532 V3 board has an onboard
//      LDO so 3V3 or 5V both work; 5V recommended for headroom.
//   4. Bad jumper wire (try a different one — breadboard contacts are flaky).
//
// Why scan instead of using the PN532 library directly?
//   The Adafruit-PN532 library bundles SAM config + firmware version handshake
//   into begin(). When wiring is wrong it fails with a misleading "Didn't find
//   PN5xx board" message that hides whether the failure is electrical or
//   protocol-level. Stripping back to a raw I2C probe isolates layer 1.
//
// Once 0x24 appears here, move on to nfc_read_uid/ for the next layer.

#include <Wire.h>

// ---- Pin config (per hardware.md §3.2) -------------------------------------
#define I2C_SDA  1   // PN532 SDA -> ESP32-C6 IO1
#define I2C_SCL  2   // PN532 SCL -> ESP32-C6 IO2

// ---- PN532 fixed I2C address -----------------------------------------------
// PN532 datasheet §6.2.4: 7-bit slave address is 0x24 (0x48 when shifted left
// for the R/W bit). Wire library uses the 7-bit form, so we expect 0x24.
#define PN532_I2C_ADDR  0x24

void setup() {
  Serial.begin(115200);
  delay(300);  // give USB CDC time to enumerate
  Serial.println("\n[nfc_smoke_test] starting");
  Serial.printf("[nfc_smoke_test] I2C pins: SDA=IO%d  SCL=IO%d\n", I2C_SDA, I2C_SCL);

  Wire.begin(I2C_SDA, I2C_SCL);
  // Default speed is 100 kHz, which is fine for PN532 in I2C mode.
}

void loop() {
  Serial.println("[nfc_smoke_test] scanning...");

  uint8_t found = 0;
  bool sawPN532 = false;

  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      Serial.printf("[nfc_smoke_test]   found device at 0x%02X", addr);
      if (addr == PN532_I2C_ADDR) {
        Serial.print("    <-- PN532");
        sawPN532 = true;
      }
      Serial.println();
      found++;
    }
  }

  if (found == 0) {
    Serial.println("[nfc_smoke_test] NO devices found.");
    Serial.println("[nfc_smoke_test] check: DIP switch in I2C mode? SDA/SCL right pins? VCC connected?");
  } else {
    Serial.printf("[nfc_smoke_test] scan done (%u device%s)%s\n",
                  found,
                  found == 1 ? "" : "s",
                  sawPN532 ? " — PN532 OK" : " — PN532 NOT seen at 0x24");
  }

  delay(3000);
}
