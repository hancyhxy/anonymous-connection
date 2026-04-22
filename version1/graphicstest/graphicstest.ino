  // XIAO ESP32-C3 板载测试
  // 不需要接任何外设

  #define LED_PIN D10  // XIAO ESP32-C3 板载 LED

  void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("XIAO ESP32-C3 is alive!");
    Serial.println("LED will blink every 500ms");
    pinMode(LED_PIN, OUTPUT);
  }

  void loop() {
    digitalWrite(LED_PIN, HIGH);
    Serial.println("LED ON");
    delay(500);
    digitalWrite(LED_PIN, LOW);
    Serial.println("LED OFF");
    delay(500);
  }