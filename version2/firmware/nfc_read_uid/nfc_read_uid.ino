// nfc_read_uid.ino
// Target: Waveshare ESP32-C6-LCD-1.3 + Elechouse PN532 V3 (I2C mode)
// Purpose: verify the PN532 protocol layer end-to-end — handshake the chip,
//          then continuously poll for ISO14443A tags (the type used in most
//          NFC stickers, hotel keycards, transit cards) and print the UID.
//
// PRECONDITION: nfc_smoke_test/ must pass first (0x24 visible on the I2C bus).
// If you skip that and run this directly, a "Didn't find PN5xx board" error
// could mean either wiring OR protocol — much harder to diagnose.
//
// Expected output after flashing:
//
//   [nfc_read_uid] starting
//   [nfc_read_uid] PN532 firmware version: 1.6
//   [nfc_read_uid] waiting for tag...
//
// Then on each tap:
//
//   [nfc_read_uid] tap! UID=04:A3:B1:5C:D2:E8:91 (7 bytes)
//
// What this sketch is FOR (Stage 3 plan):
//   - Phase 2 of anonymous-connection needs each device to know "who did I
//     just touch?" The UID is the cheapest stable identifier we can read
//     without writing to the tag. Each user gets an NFC sticker pre-registered
//     to their device-side user_id; tapping someone reads their sticker's UID,
//     server looks it up, and pushes the match payload to both devices.
//
// What this sketch is NOT:
//   - No web/server connection — that's the next layer.
//   - No screen output — keeping serial-only so failures in this layer can't
//     be confused with renderer bugs (we already burned a week on that lesson
//     with the \xHH greediness bug; see hardware.md §4.2).
//
// Library: Adafruit-PN532 (install via Arduino Library Manager).
//   - In Library Manager search "PN532", install "Adafruit PN532" by Adafruit.
//   - DO NOT install "PN532" by Seeed Studio or others — API differs.

#include <Wire.h>
#include <Adafruit_PN532.h>

// ---- Pin config (per hardware.md §3.2) -------------------------------------
#define I2C_SDA  1
#define I2C_SCL  2

// IRQ / RESET on the PN532 V3 module are NOT wired up in I2C mode for this
// project. Adafruit_PN532's I2C constructor still requires an IRQ pin number;
// pass an unused GPIO. The library uses it for optional interrupt-driven
// reads, but our polling loop doesn't depend on it.
#define PN532_IRQ_UNUSED  -1
#define PN532_RST_UNUSED  -1

Adafruit_PN532 nfc(PN532_IRQ_UNUSED, PN532_RST_UNUSED, &Wire);

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n[nfc_read_uid] starting");

  Wire.begin(I2C_SDA, I2C_SCL);
  nfc.begin();

  uint32_t version = nfc.getFirmwareVersion();
  if (!version) {
    Serial.println("[nfc_read_uid] FAIL: didn't find PN5xx board.");
    Serial.println("[nfc_read_uid] check nfc_smoke_test first — 0x24 must be visible on I2C.");
    while (1) delay(1000);
  }

  // Decode firmware version per PN532 datasheet GetFirmwareVersion response:
  //   byte 2 (0xFF & version >> 16) = chip type (should be 0x32 for PN532)
  //   byte 3 (0xFF & version >>  8) = major version
  //   byte 4 (0xFF & version      ) = minor version
  Serial.printf("[nfc_read_uid] PN532 firmware version: %d.%d (chip 0x%02X)\n",
                (uint8_t)(version >> 16) & 0xFF,
                (uint8_t)(version >>  8) & 0xFF,
                (uint8_t)(version >> 24) & 0xFF);

  // SAMConfig switches the chip out of low-power mode and into "ready to read
  // ISO14443A tags". Required before readPassiveTargetID will return anything.
  nfc.SAMConfig();

  Serial.println("[nfc_read_uid] waiting for tag...");
}

void loop() {
  uint8_t uid[7] = {0};        // up to 7 bytes (ISO14443A 4-byte or 7-byte UID)
  uint8_t uidLen = 0;

  // 100 ms timeout per poll. Returns true when a tag is in the field.
  bool ok = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 100);
  if (!ok) return;  // no card this iteration; loop again

  Serial.print("[nfc_read_uid] tap! UID=");
  for (uint8_t i = 0; i < uidLen; i++) {
    if (i > 0) Serial.print(":");
    if (uid[i] < 0x10) Serial.print('0');
    Serial.print(uid[i], HEX);
  }
  Serial.printf(" (%u bytes)\n", uidLen);

  // Debounce: after a successful read, wait so the same card doesn't fire
  // 5 times while held against the antenna. 600 ms feels right for "tap and
  // pull away"; tune later when integrating with the match flow.
  delay(600);
}
